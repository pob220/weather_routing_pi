#pragma once

#include "WeatherRoutingMessageDialog.h"
#include <wx/filedlg.h>

#ifdef __OCPN__ANDROID__
#include "AndroidDialogHeader.h"
#include <wx/filename.h>
#include <QDir>
#include <QFileInfo>
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QStandardPaths>
#include <QAbstractItemView>
#include <QScreen>
#include <QScrollBar>
#include "ocpn_plugin.h"

// Consume a drag before QListWidget turns its release into itemClicked.
// This host can deliver synthesized mouse input instead of QScroller touch
// events. Handle both paths and activate only a stationary tap.
class WR_PickerDragFilter : public QObject {
 public:
  explicit WR_PickerDragFilter(QListWidget* list) : QObject(list), list_(list) {
    list_->viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    list_->viewport()->installEventFilter(this);
  }
 protected:
  bool eventFilter(QObject*, QEvent* event) override {
    const bool touch = event->type() == QEvent::TouchBegin ||
        event->type() == QEvent::TouchUpdate || event->type() == QEvent::TouchEnd ||
        event->type() == QEvent::TouchCancel;
    const bool mouse = event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonRelease;
    if (!touch && !mouse) return false;
    QPoint point;
    bool begin = false, end = false;
    const bool cancel = event->type() == QEvent::TouchCancel;
    if (touch) {
      auto* input = static_cast<QTouchEvent*>(event);
      if (input->touchPoints().empty()) return false;
      point = input->touchPoints().first().screenPos().toPoint();
      begin = event->type() == QEvent::TouchBegin;
      end = event->type() == QEvent::TouchEnd || cancel;
    } else {
      if (touchActive_ || (suppressMouse_ && clock_.elapsed() < 500)) return true;
      auto* input = static_cast<QMouseEvent*>(event);
      if (event->type() != QEvent::MouseMove && input->button() != Qt::LeftButton) return false;
      point = input->globalPos();
      begin = event->type() == QEvent::MouseButtonPress;
      end = event->type() == QEvent::MouseButtonRelease;
    }
    if (begin) {
      active_ = true; touchActive_ = touch; suppressMouse_ = touch;
      moved_ = false; origin_ = point; scroll_ = list_->verticalScrollBar()->value();
      clock_.start();
    }
    if (!active_) return false;
    if ((point - origin_).manhattanLength() > qMax(12, QApplication::startDragDistance())) moved_ = true;
    if (moved_) list_->verticalScrollBar()->setValue(scroll_ - (point.y() - origin_.y()));
    if (end) {
      active_ = touchActive_ = false;
      if (!cancel && !moved_) {
        if (auto* item = list_->itemAt(list_->viewport()->mapFromGlobal(point))) {
          list_->setCurrentItem(item);
          emit list_->itemClicked(item);
        }
      }
    }
    event->accept();
    return true;
  }
 private:
  QListWidget* list_;
  QPoint origin_;
  QElapsedTimer clock_;
  int scroll_{0};
  bool active_{false}, touchActive_{false}, suppressMouse_{false}, moved_{false};
};

// A touch browser stays in Qt's event loop and surface stack. The host's
// Java chooser can be obscured by a visible wxQt plugin sheet even while it
// owns Android input. Use app-accessible folders without that native overlay.
class WR_FileDialog {
public:
  WR_FileDialog(wxWindow* parent, const wxString& title,
                const wxString& directory, const wxString& name,
                const wxString& wildcard, long flags)
      : m_parent(parent), m_title(title), m_directory(directory),
        m_name(name), m_wildcard(wildcard), m_flags(flags) {}
  int ShowModal() {
    m_paths.Clear();
    const bool multiple = (m_flags & wxFD_MULTIPLE) && !(m_flags & wxFD_SAVE);
    WR_MessageSheet browser(m_parent ? static_cast<QWidget*>(m_parent->GetHandle())
                                      : nullptr, wxID_CANCEL);
    browser.setStyleSheet("QLabel, QLineEdit, QComboBox, QListWidget { font-size: 17pt; } "
        "QPushButton { font-size: 17pt; min-height: 64px; padding: 8px; } "
        "QLineEdit, QComboBox { min-height: 64px; } "
        "QListWidget::item { min-height: 72px; padding: 6px; }");
    auto* layout = new QVBoxLayout(&browser);
    auto* title = new QLabel(QString::fromUtf8(m_title.ToUTF8().data()));
    title->setStyleSheet("color: white; background: #193b4c; font-weight: bold; padding: 14px;");
    layout->addWidget(title);
    if (multiple) {
      auto* hint = new QLabel(QObject::tr("Tap files to select one or more polars, or enter a name to create a new polar."));
      hint->setWordWrap(true);
      layout->addWidget(hint);
    }
    // Keep the editor above the file list: Android can pan an entire native
    // dialog before Qt receives the IME resize if its focused field is low.
    auto* name = new QLineEdit;
    name->setPlaceholderText(QObject::tr("File name"));
    if (m_flags & wxFD_SAVE) name->setText(QString::fromUtf8(m_name.ToUTF8().data()));
    layout->addWidget(name);
    auto* locations = new QComboBox;
    locations->setItemDelegate(new WR_AndroidChoiceDelegate(locations));
    locations->addItem(QObject::tr("Current folder"), QString::fromUtf8(m_directory.ToUTF8().data()));
    locations->addItem(QObject::tr("OpenCPN files"), QString::fromUtf8(GetpPrivateApplicationDataLocation()->ToUTF8().data()));
    locations->addItem(QObject::tr("Downloads"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    layout->addWidget(locations);
    auto* navigation = new QHBoxLayout;
    auto* up = new QPushButton(QObject::tr("Up"));
    auto* folder = new QLabel;
    folder->setWordWrap(true);
    navigation->addWidget(up);
    navigation->addWidget(folder, 1);
    layout->addLayout(navigation);
    auto* files = new QListWidget;
    files->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    new WR_PickerDragFilter(files);
    layout->addWidget(files, 1);
    auto* error = new QLabel;
    error->setWordWrap(true);
    error->setStyleSheet("color: #9e2525;");
    layout->insertWidget(layout->indexOf(name) + 1, error);
    auto* actions = new QHBoxLayout;
    auto* newFolder = new QPushButton(QObject::tr("New folder"));
    auto* cancel = new QPushButton(QObject::tr("Cancel"));
    auto* accept = new QPushButton(m_flags & wxFD_SAVE ? QObject::tr("Save") : QObject::tr("Open"));
    accept->setStyleSheet("color: white; background: #176b84;");
    actions->addWidget(newFolder);
    actions->addWidget(cancel);
    actions->addWidget(accept, 1);
    // Native Android windows may keep their full height above the IME. Put
    // actions with the filename rather than below the expanding file list.
    layout->insertLayout(layout->indexOf(error) + 1, actions);
    QDir directory(QString::fromUtf8(m_directory.ToUTF8().data()));
    if (m_directory.empty() || !directory.exists())
      directory = QDir(locations->itemData(1).toString());
    QStringList masks;
    const QStringList parts = QString::fromUtf8(m_wildcard.ToUTF8().data()).split('|');
    for (int i = 1; i < parts.size(); i += 2) masks.append(parts[i].split(';', QString::SkipEmptyParts));
    if (masks.isEmpty()) masks << "*";
    auto refresh = [&]() {
      files->clear();
      folder->setText(directory.dirName().isEmpty() ? directory.absolutePath() : directory.dirName());
      folder->setToolTip(directory.absolutePath());
      up->setEnabled(!directory.isRoot());
      const auto entries = directory.entryInfoList(masks,
          QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
      for (const auto& entry : entries) {
        auto* item = new QListWidgetItem((entry.isDir() ? QObject::tr("Folder: ") : multiple ? QString::fromUtf8("\u25a1 ") : QString()) + entry.fileName(), files);
        item->setData(Qt::UserRole, entry.absoluteFilePath());
        item->setData(Qt::UserRole + 1, entry.isDir());
      }
      error->clear();
      accept->setText(m_flags & wxFD_SAVE ? QObject::tr("Save") : QObject::tr("Open"));
      if (entries.isEmpty()) {
        auto* item = new QListWidgetItem(QObject::tr("No matching files in this folder"), files);
        item->setFlags(Qt::NoItemFlags);
      }
    };
    QObject::connect(up, &QPushButton::clicked, &browser, [&]() { directory.cdUp(); refresh(); });
    QObject::connect(locations, QOverload<int>::of(&QComboBox::activated), &browser, [&](int index) {
      const QDir next(locations->itemData(index).toString());
      if (next.exists()) { directory = next; refresh(); }
      else error->setText(QObject::tr("This folder is unavailable. Choose another location."));
    });
    QObject::connect(files, &QListWidget::itemClicked, &browser, [&](QListWidgetItem* item) {
      if (item->data(Qt::UserRole + 1).toBool()) {
        directory = QDir(item->data(Qt::UserRole).toString()); refresh();
      } else if (item->data(Qt::UserRole).isValid()) {
        name->setText(QFileInfo(item->data(Qt::UserRole).toString()).fileName());
        if (multiple) {
          item->setData(Qt::UserRole + 2, !item->data(Qt::UserRole + 2).toBool());
          int count = 0;
          for (int i = 0; i < files->count(); ++i) {
            auto* row = files->item(i);
            if (row->data(Qt::UserRole + 1).toBool()) continue;
            const bool selected = row->data(Qt::UserRole + 2).toBool();
            count += selected;
            row->setText(QString::fromUtf8(selected ? "\u2713 " : "\u25a1 ") +
                QFileInfo(row->data(Qt::UserRole).toString()).fileName());
          }
          accept->setText(count ? QObject::tr("Open (%1)").arg(count) : QObject::tr("Open"));
        }
      }
    });
    QObject::connect(name, &QLineEdit::textEdited, &browser, [&](const QString&) {
      if (!multiple) return;
      for (int i = 0; i < files->count(); ++i) {
        auto* row = files->item(i);
        if (!row->data(Qt::UserRole + 2).toBool()) continue;
        row->setData(Qt::UserRole + 2, false);
        row->setText(QString::fromUtf8("\u25a1 ") + QFileInfo(row->data(Qt::UserRole).toString()).fileName());
      }
      accept->setText(QObject::tr("Open"));
    });
    auto choose = [&]() {
      if (multiple) {
        m_paths.Clear();
        for (int i = 0; i < files->count(); ++i) {
          auto* row = files->item(i);
          if (row->data(Qt::UserRole + 2).toBool())
            m_paths.Add(wxString::FromUTF8(row->data(Qt::UserRole).toString().toUtf8().constData()));
        }
        if (!m_paths.IsEmpty()) {
          m_path = m_paths.front(); browser.done(wxID_OK); return;
        }
      }
      const QString filename = name->text().trimmed();
      if (filename.isEmpty() || filename == "." || filename == ".." || filename.contains('/')) {
        error->setText(QObject::tr("Enter a file name without a folder separator."));
        name->setFocus(); return;
      }
      const QString path = directory.absoluteFilePath(filename);
      if (!(m_flags & wxFD_SAVE) && !QFileInfo(path).isFile()) {
        if (!multiple || (m_flags & wxFD_FILE_MUST_EXIST)) {
          error->setText(QObject::tr("Select an existing file.")); return;
        }
        if (WR_MessageBox(_("Create a new polar file with this name?"), m_title,
            wxYES_NO | wxNO_DEFAULT, m_parent) != wxYES) return;
      }
      m_path = wxString::FromUTF8(path.toUtf8().constData());
      browser.done(wxID_OK);
    };
    QObject::connect(accept, &QPushButton::clicked, &browser, choose);
    QObject::connect(name, &QLineEdit::returnPressed, &browser, choose);
    QObject::connect(cancel, &QPushButton::clicked, &browser, [&]() { browser.done(wxID_CANCEL); });
    QObject::connect(newFolder, &QPushButton::clicked, &browser, [&]() {
      WR_MessageSheet create(&browser, wxID_CANCEL);
      create.setStyleSheet(browser.styleSheet());
      auto* content = new QVBoxLayout(&create);
      content->addWidget(new QLabel(QObject::tr("New folder name")));
      auto* input = new QLineEdit;
      content->addWidget(input);
      auto* folderError = new QLabel;
      folderError->setWordWrap(true);
      content->addWidget(folderError);
      auto* createButton = new QPushButton(QObject::tr("Create folder"));
      auto* cancelButton = new QPushButton(QObject::tr("Cancel"));
      content->addWidget(createButton); content->addWidget(cancelButton);
      QObject::connect(cancelButton, &QPushButton::clicked, &create, [&]() { create.done(wxID_CANCEL); });
      QObject::connect(createButton, &QPushButton::clicked, &create, [&]() {
        const QString value = input->text().trimmed();
        if (!value.isEmpty() && value != "." && value != ".." && !value.contains('/') && directory.mkdir(value)) {
          directory.cd(value); refresh(); create.done(wxID_OK);
        } else folderError->setText(QObject::tr("Choose a new folder name. The folder must be writable."));
      });
      create.resize(qMin(700, browser.width() - 32), 330);
      create.move(browser.geometry().center() - QPoint(create.width()/2, create.height()/2));
      create.exec();
    });
    refresh();
    const auto fit = [&browser, locations, up, folder]() {
      QRect bounds = QApplication::primaryScreen()->availableGeometry().adjusted(12, 12, -12, -12);
      auto* input = QApplication::inputMethod();
      // On a landscape tablet the keyboard leaves too little height for both
      // folder navigation and the action row. Restore navigation on dismissal.
      locations->setVisible(!input->isVisible());
      up->setVisible(!input->isVisible());
      folder->setVisible(!input->isVisible());
      browser.layout()->invalidate();
      browser.layout()->activate();
      if (input->isVisible()) {
        // Some Android hosts already remove the IME from availableGeometry.
        // Cap at the keyboard edge instead of subtracting its height twice.
        const int keyboardTop = QApplication::primaryScreen()->geometry().bottom() + 1 -
            int(input->keyboardRectangle().height());
        bounds.setBottom(qMax(bounds.top() + 300, qMin(bounds.bottom(), keyboardTop - 12)));
      }
      browser.setGeometry(bounds);
    };
    QObject::connect(QApplication::inputMethod(), &QInputMethod::keyboardRectangleChanged, &browser, fit);
    QObject::connect(QApplication::inputMethod(), &QInputMethod::visibleChanged, &browser, fit);
    fit();
    const int result = browser.exec();
    if (result != wxID_OK || m_path.IsEmpty()) return wxID_CANCEL;
    if (!(m_flags & wxFD_SAVE) && !multiple && !wxFileName::FileExists(m_path)) {
      WR_MessageBox(_("Select an existing file."), m_title,
                   wxOK | wxICON_INFORMATION, m_parent);
      return wxID_CANCEL;
    }
    // Only an existing file containing data needs overwrite confirmation.
    if ((m_flags & wxFD_SAVE) && (m_flags & wxFD_OVERWRITE_PROMPT) &&
        wxFileName::FileExists(m_path) && wxFileName(m_path).GetSize() > 0 &&
        WR_MessageBox(_("Replace the existing file?"), m_title,
                     wxYES_NO | wxNO_DEFAULT, m_parent) != wxYES)
      return wxID_CANCEL;
    return wxID_OK;
  }
  wxString GetPath() const { return m_path; }
  wxString GetDirectory() const { return wxFileName(m_path).GetPath(); }
  void GetPaths(wxArrayString& paths) const {
    paths.Clear();
    if (!m_paths.IsEmpty()) paths = m_paths;
    else if (!m_path.IsEmpty()) paths.Add(m_path);
  }
private:
  wxWindow* m_parent;
  wxString m_title, m_directory, m_name, m_wildcard, m_path;
  wxArrayString m_paths;
  long m_flags;
};
#else
using WR_FileDialog = wxFileDialog;
#endif
