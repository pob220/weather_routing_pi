// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from xGRIB 0.4.5 Android touch generator (a5606ce).
// Uses Weather Routing storage, credentials and shared in-process engine.
#include "android/PassageWeatherGenerator.h"
#include "android/ForecastCredentials.h"
#include "android/ForecastDialogBack.h"
#include "WeatherRoutingFileDialog.h"
#include "android/ForecastTimeValidation.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <map>
#include <mutex>
#include <QCheckBox>
#include <QApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QCalendarWidget>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QListWidget>
#include <QGuiApplication>
#include <QInputMethod>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include "version.h"
#include "AreaPreset.h"
#include "android/ForecastServerDownload.h"
#include "environmental_grib/environment.h"
#include "environmental_grib/estimate.h"
#include "environmental_grib/preflight.h"
#include "environmental_grib/area_validation.h"
#include "environmental_grib/copernicus_auth.h"

namespace eg = environmental_grib;
namespace {
std::string Text(const QString& value) { return value.toUtf8().toStdString(); }
QString Text(const std::string& value) { return QString::fromUtf8(value.c_str()); }
wxString Wx(const QString& value) { return wxString::FromUTF8(Text(value).c_str()); }
QString QtText(const wxString& value) { return QString::fromUtf8(value.ToUTF8()); }

class TouchChoiceDelegate : public QStyledItemDelegate {
 public:
  explicit TouchChoiceDelegate(QObject* parent) : QStyledItemDelegate(parent) {}
  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    const auto size = QStyledItemDelegate::sizeHint(option, index);
    return QSize(size.width(), std::max(72, size.height()));
  }
};

class ResizeFollower : public QObject {
 public:
  ResizeFollower(QWidget* canvas, QWidget* dialog)
      : QObject(dialog), canvas_(canvas), dialog_(dialog) {
    canvas_->installEventFilter(this);
    auto* input = QGuiApplication::inputMethod();
    connect(input, &QInputMethod::keyboardRectangleChanged, this, [this] { Fit(); });
    connect(input, &QInputMethod::visibleChanged, this, [this] { Fit(); });
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget*) {
      RevealEditor();
    });
  }
  void Fit() {
    const auto available = canvas_->size();
    const int width = std::max(240, std::min(1000, available.width() - 24));
    const auto* input = QGuiApplication::inputMethod();
    const int keyboard = input->isVisible() ? int(input->keyboardRectangle().height()) : 0;
    dialog_->setGeometry((available.width() - width) / 2, 12,
                         width, std::max(200, available.height() - keyboard - 24));
    // Reflow happens after the keyboard resize event. Scroll only after the
    // layout has updated, and again once Android's keyboard animation settles.
    QTimer::singleShot(0, this, [this] { RevealEditor(); });
    QTimer::singleShot(250, this, [this] { RevealEditor(); });
  }
  void RevealEditor() {
    auto* editor = QApplication::focusWidget();
    if (!editor || !dialog_->isAncestorOf(editor) || !QGuiApplication::inputMethod()->isVisible()) return;
    for (auto* ancestor = editor->parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
      if (auto* scroll = qobject_cast<QScrollArea*>(ancestor)) {
        scroll->ensureWidgetVisible(editor, 12, 30);
        break;
      }
    }
  }
 protected:
  bool eventFilter(QObject* object, QEvent* event) override {
    if (object == canvas_ && event->type() == QEvent::Resize) Fit();
    return false;
  }
 private:
  QWidget *canvas_, *dialog_;
};
}

struct AndroidPassageWeatherDialog::Impl {
  std::unique_ptr<WR_ForecastBackFilter> backFilter;
  AndroidPassageWeatherDialog* owner;
  GribReadyCallback ready;
  QWidget* root;
  QTabWidget* tabs;
  QPushButton *generate, *cancel, *close;
  QLabel *status, *estimate, *heading;
  QPlainTextEdit* log;
  QTimer* timer;
  ResizeFollower* resize;
  std::map<std::string, QLineEdit*> fields;
  std::map<std::string, QComboBox*> choices;
  std::map<std::string, QWidget*> rows;
  QCheckBox *extend, *minor, *keep, *open, *rememberUsername, *rememberPassword;
  QPushButton* startPicker;
  QDateTime startUtc;
  enum class CredentialAction { None, Load, Save, Forget };
  CredentialAction credentialAction{CredentialAction::None};
  std::future<QString> credentialJob;
  QString credentialUsername;
  bool restoredPassword{false};
  QLabel* accountNote;
  QLabel* timeNote;
  bool serverDownload{false};
  PlugIn_ViewPort viewport{};
  std::future<eg::EnvironmentResult> job;
  std::shared_ptr<std::atomic<bool>> cancelled;
  std::mutex logMutex;
  std::string latestProgress;
  bool running{false};
  eg::EnvironmentRequest lastRequest;
  bool hasLog{false};
  bool closeWhenFinished{false};
  QCheckBox* showPassword;

  QVBoxLayout* Page(const char* title) {
    auto* scroll = new QScrollArea(tabs);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* page = new QWidget(scroll);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);
    layout->setAlignment(Qt::AlignTop);
    scroll->setWidget(page);
    QScroller::grabGesture(scroll->viewport(), QScroller::TouchGesture);
    tabs->addTab(scroll, title);
    return layout;
  }
  void Note(QVBoxLayout* layout, const QString& text) {
    auto* label = new QLabel(text); label->setWordWrap(true); layout->addWidget(label);
  }
  QLineEdit* Field(QVBoxLayout* layout, const char* key, const char* label, const QString& value) {
    auto* row = new QWidget; rows[key] = row;
    auto* rowLayout = new QVBoxLayout(row); rowLayout->setContentsMargins(0, 0, 0, 6);
    Note(rowLayout, label); layout->addWidget(row);
    auto* field = new QLineEdit(value);
    field->setObjectName(QString("wr_forecast_") + key);
    fields[key] = field; rowLayout->addWidget(field); return field;
  }
  QComboBox* Choice(QVBoxLayout* layout, const char* key, const char* label,
                    std::initializer_list<std::pair<const char*, const char*>> values) {
    auto* row = new QWidget; rows[key] = row;
    auto* rowLayout = new QVBoxLayout(row); rowLayout->setContentsMargins(0, 0, 0, 6);
    Note(rowLayout, label); layout->addWidget(row);
    auto* choice = new QComboBox;
    choice->setObjectName(QString("wr_forecast_") + key);
    choice->setItemDelegate(new TouchChoiceDelegate(choice));
    choice->view()->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    QScroller::grabGesture(choice->view()->viewport(), QScroller::TouchGesture);
    choice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    choice->setMinimumContentsLength(10);
    for (const auto& value : values) choice->addItem(value.second, value.first);
    choices[key] = choice; rowLayout->addWidget(choice); return choice;
  }
  void File(QVBoxLayout* layout, const char* key, const char* label) {
    auto* field = Field(layout, key, label, ""); field->setReadOnly(true);
    auto* button = new QPushButton(QString("Choose ") + label); rows[key]->layout()->addWidget(button);
    QObject::connect(button, &QPushButton::clicked, root, [this, field] {
      WR_FileDialog picker(owner, _("Choose data file"),
          wxEmptyString, wxEmptyString, "*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
      if (picker.ShowModal() == wxID_OK)
        field->setText(QtText(picker.GetPath()));
    });
  }
  std::string Value(const char* key) const { return Text(fields.at(key)->text().trimmed()); }
  std::string Selected(const char* key) const { return Text(choices.at(key)->currentData().toString()); }
  double Number(const char* key) const {
    bool ok; const double value = fields.at(key)->text().toDouble(&ok);
    if (!ok || !std::isfinite(value)) throw std::runtime_error(std::string("Enter a number for ") + key);
    return value;
  }
  void UpdateStartLabel() {
    startPicker->setText(startUtc.toString("ddd d MMM yyyy  ·  HH:mm 'UTC'"));
  }
  void ChooseStart() {
    QGuiApplication::inputMethod()->hide();
    wxDialog picker(owner, wxID_ANY, "Forecast start (UTC)", wxDefaultPosition,
        wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto* panel = static_cast<QWidget*>(picker.GetHandle());
    panel->setStyleSheet(root->styleSheet() +
        "QPushButton, QComboBox {min-height: 72px;}"
        "QCalendarWidget QToolButton {min-height: 64px; font-size: 20px;}"
        "QCalendarWidget QAbstractItemView {font-size: 20px;}");
    auto* layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel("Choose forecast start — UTC"));
    auto* calendar = new QCalendarWidget(panel);
    calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    calendar->setSelectedDate(startUtc.date());
    calendar->setMinimumHeight(300);
    layout->addWidget(calendar, 1);
    auto* time = new QHBoxLayout;
    time->addWidget(new QLabel("Hour"));
    auto* hour = new QComboBox(panel);
    for (int i = 0; i < 24; ++i) hour->addItem(QString("%1").arg(i, 2, 10, QChar('0')));
    hour->setCurrentIndex(startUtc.time().hour()); time->addWidget(hour, 1);
    time->addWidget(new QLabel("Minute"));
    auto* minute = new QComboBox(panel);
    for (int i = 0; i < 60; ++i) minute->addItem(QString("%1").arg(i, 2, 10, QChar('0')));
    minute->setCurrentIndex(startUtc.time().minute()); time->addWidget(minute, 1);
    for (auto* choice : {hour, minute}) {
      choice->setItemDelegate(new TouchChoiceDelegate(choice));
      choice->setMaxVisibleItems(10);
      for (int i = 0; i < choice->count(); ++i)
        choice->setItemData(i, QSize(0, 52), Qt::SizeHintRole);
      choice->view()->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
      // This Qt Android build delivers synthesized mouse events to combo
      // popups. Recognize their drag before the combo treats release as a tap.
      QScroller::grabGesture(choice->view()->viewport(), QScroller::LeftMouseButtonGesture);
    }
    time->addWidget(new QLabel("UTC")); layout->addLayout(time);
    auto* now = new QPushButton("Use current UTC hour", panel); layout->addWidget(now);
    QObject::connect(now, &QPushButton::clicked, panel, [calendar, hour, minute] {
      const auto utc = QDateTime::currentDateTimeUtc();
      calendar->setSelectedDate(utc.date()); hour->setCurrentIndex(utc.time().hour());
      minute->setCurrentIndex(0);
    });
    auto* actions = new QHBoxLayout;
    auto* cancel = new QPushButton("Cancel", panel);
    auto* use = new QPushButton("Use date and time", panel);
    actions->addWidget(cancel, 1); actions->addWidget(use, 1); layout->addLayout(actions);
    bool accepted = false;
    QObject::connect(cancel, &QPushButton::clicked, panel, [&] { picker.EndModal(wxID_CANCEL); });
    QObject::connect(use, &QPushButton::clicked, panel, [&] {
      accepted = true; picker.EndModal(wxID_OK);
    });
    picker.Bind(wxEVT_CLOSE_WINDOW, [&](wxCloseEvent&) { picker.EndModal(wxID_CANCEL); });
    WR_ForecastBackFilter back(&picker, [&] { picker.EndModal(wxID_CANCEL); });
    ResizeFollower resize(static_cast<QWidget*>(owner->GetParent()->GetHandle()), panel);
    resize.Fit(); picker.ShowModal();
    if (accepted) {
      startUtc = QDateTime(calendar->selectedDate(), QTime(hour->currentIndex(), minute->currentIndex()), Qt::UTC);
      UpdateStartLabel(); UpdateOptions();
    }
  }
  Impl(AndroidPassageWeatherDialog* dialog, GribReadyCallback callback)
      : owner(dialog), ready(std::move(callback)), root(static_cast<QWidget*>(dialog->GetHandle())) {
    root->setStyleSheet("QWidget {font-size: 17pt;}"
        "QLineEdit, QComboBox, QPushButton {min-height: 64px;}"
        "QComboBox QAbstractItemView::item {min-height: 64px;}"
        "QPushButton {padding-left: 10px; padding-right: 10px;}"
        "QTabBar::tab {min-height: 64px; padding: 4px 12px;}"
        "QCheckBox {min-height: 64px;} QCheckBox::indicator {width: 30px; height: 30px;}");
    auto* layout = new QVBoxLayout(root); layout->setContentsMargins(10, 10, 10, 10);
    heading = new QLabel("Weather Routing — Generate forecast"); layout->addWidget(heading);
    tabs = new QTabWidget(root); tabs->setUsesScrollButtons(true); layout->addWidget(tabs, 1);
    auto* area = Page("Area / time");
    Note(area, "Coordinates are decimal degrees. A west longitude greater than east crosses the date line. All times are UTC.");
    auto* useChart = new QPushButton("Use chart area"); area->addWidget(useChart);
    auto* areas = new QComboBox;
    areas->addItem("Choose a saved area...");
    const auto presets = xgrib::LoadAreaPresets(GetOCPNConfigObject());
    for (const auto& preset : presets) areas->addItem(QtText(preset.name));
    area->addWidget(areas);
    Note(area, "Area presets set the boundaries only; choose weather and current sources on their tabs.");
    Field(area, "west", "West longitude", "-8"); Field(area, "east", "East longitude", "-2");
    Field(area, "south", "South latitude", "50"); Field(area, "north", "North latitude", "56");
    for (const char* key : {"west", "east", "south", "north"}) fields[key]->setInputMethodHints(Qt::ImhFormattedNumbersOnly);
    QObject::connect(areas, static_cast<void(QComboBox::*)(int)>(&QComboBox::activated), root, [this, presets](int index) {
      if (index < 1 || index > int(presets.size())) return;
      const auto& preset = presets[index - 1];
      fields["west"]->setText(QString::number(preset.west));
      fields["east"]->setText(QString::number(preset.east));
      fields["south"]->setText(QString::number(preset.south));
      fields["north"]->setText(QString::number(preset.north));
    });
    QObject::connect(useChart, &QPushButton::clicked, root, [this] {
      double west = viewport.lon_min, east = viewport.lon_max;
      if (!xgrib::NormalizeAreaLongitudes(&west, &east)) {
        log->appendPlainText("The chart longitude bounds are invalid."); return;
      }
      fields["west"]->setText(QString::number(west, 'f', 3));
      fields["east"]->setText(QString::number(east, 'f', 3));
      fields["south"]->setText(QString::number(viewport.lat_min, 'f', 3));
      fields["north"]->setText(QString::number(viewport.lat_max, 'f', 3));
    });
    auto* startRow = new QWidget; rows["start"] = startRow;
    auto* startLayout = new QVBoxLayout(startRow); startLayout->setContentsMargins(0, 0, 0, 6);
    startLayout->addWidget(new QLabel("Start date and time (UTC)"));
    startUtc = QDateTime::currentDateTimeUtc();
    startUtc.setTime(QTime(startUtc.time().hour(), 0));
    startPicker = new QPushButton(startRow); UpdateStartLabel();
    startLayout->addWidget(startPicker); area->addWidget(startRow);
    QObject::connect(startPicker, &QPushButton::clicked, root, [this] { ChooseStart(); });
    timeNote = new QLabel("For ordinary forecast downloads, duration starts at the model cycle. Start is used for tidal/current predictions and forecast extension. Check the generated coverage below.");
    timeNote->setWordWrap(true); area->addWidget(timeNote);
    Field(area, "hours", "Duration (hours)", "24")->setInputMethodHints(Qt::ImhDigitsOnly);
    Choice(area, "step", "Forecast interval (hours)", {{"1","1"},{"3","3"},{"6","6"},{"12","12"}})->setCurrentIndex(1);
    auto* weather = Page("Weather / waves");
    Choice(area, "serverDuration", "Server forecast duration", {{"24","24 hours"},{"72","3 days"},{"999","Full available forecast"}});
    Choice(weather, "serverModel", "OpenCPN forecast server", {{"ecmwf0p25","ECMWF IFS 0.25 degrees"},{"ecmwfaifs0p25","ECMWF AIFS 0.25 degrees"}});
    Choice(weather, "weather", "Weather source", {{"gfs","NOAA GFS"}, {"noaa_hrrr","NOAA HRRR 3 km"},
      {"ukmo_ukv","Met Office UKV 2 km"}, {"metno_nordic","MET Norway Nordic"}, {"dwd_icon_eu","DWD ICON-EU"},
      {"ecmwf_ifs_open","ECMWF IFS Open Data"}, {"ecmwf_aifs_open","ECMWF AIFS Open Data"},
      {"existing-file","Existing weather GRIB"}, {"none","None"}});
    Choice(weather, "preset", "Weather fields", {{"minimal","Wind only"}, {"routing","Routing: wind, gusts where available, pressure, temperature"},
      {"marine","Marine"}, {"all","All supported fields"}})->setCurrentIndex(1);
    Choice(weather, "wave", "Waves", {{"none","None"},{"gfs_wave","NOAA GFS Wave"},{"copernicus_global_waves","Copernicus Global Waves"}});
    File(weather, "weatherFile", "weather GRIB");
    Field(weather, "weatherGrid", "Weather output grid spacing (degrees)", "0.025");
    Note(weather, "Regional sources must cover the area and forecast period. The shared generator validates their actual coverage.");
    auto* currents = Page("Currents");
    Choice(currents, "current", "Current source", {{"none","None"}, {"existing-file","Existing current GRIB"},
      {"offline-tidal","Offline tidal package (.xtd)"},
      {"marine_ie_irish_sea","Marine.ie Irish Sea"}, {"copernicus_nws","Copernicus NWS"}, {"copernicus_ibi","Copernicus IBI"},
      {"copernicus_mediterranean","Copernicus Mediterranean"}, {"copernicus_global","Copernicus Global"},
      {"noaa_rtofs_global","NOAA RTOFS"}, {"noaa_ofs_s111","NOAA OFS / S-111"}, {"auto","Automatic model provider"}, {"netcdf","Local NetCDF"}});
    File(currents, "currentFile", "current GRIB"); File(currents, "tidalFile", "offline tidal package (.xtd)");
    Choice(currents, "offlineMode", "Offline current calculation", {{"tide-only","Astronomical tide only"},{"tide-expected-seasonal","Tide + seasonal circulation"}});
    File(currents, "netcdfFile", "current NetCDF");
    Field(currents, "currentGrid", "Current grid spacing (degrees)", "0.05");
    minor = new QCheckBox("Infer minor tidal constituents"); minor->setChecked(true); currents->addWidget(minor);
    accountNote = new QLabel("Copernicus login is on the Account tab."); accountNote->setWordWrap(true); currents->addWidget(accountNote);
    auto* options = Page("Options");
    extend = new QCheckBox("Extend using fallback models"); options->addWidget(extend);
    Choice(options, "fallbackWeather", "Weather fallback", {{"none","None"},{"gfs","NOAA GFS"},{"ecmwf_ifs_open","ECMWF IFS"}});
    Choice(options, "fallbackWave", "Wave fallback", {{"none","None"},{"gfs_wave","NOAA GFS Wave"}});
    Choice(options, "fallbackCurrent", "Current fallback", {{"none","None"},{"offline-tidal","Offline tides"},{"copernicus_global","Copernicus Global"}});
    Field(options, "filename", "Output filename", "wr_forecast_" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss") + ".grb2");
    keep = new QCheckBox("Keep intermediate downloads"); options->addWidget(keep);
    open = new QCheckBox("Load completed GRIB for routing"); open->setChecked(true); options->addWidget(open);
    auto* credentials = Page("Account");
    Field(credentials, "username", "Copernicus username", "");
    Field(credentials, "password", "Copernicus password", "")->setEchoMode(QLineEdit::Password);
    fields["username"]->setInputMethodHints(Qt::ImhNoAutoUppercase | Qt::ImhNoPredictiveText);
    fields["password"]->setInputMethodHints(Qt::ImhSensitiveData | Qt::ImhNoAutoUppercase | Qt::ImhNoPredictiveText);
    rememberUsername = new QCheckBox("Remember username on this tablet"); credentials->addWidget(rememberUsername);
    rememberPassword = new QCheckBox("Remember password on this tablet"); credentials->addWidget(rememberPassword);
    QObject::connect(rememberPassword, &QCheckBox::toggled, root, [this](bool remember) {
      if (remember) rememberUsername->setChecked(true);
    });
    QObject::connect(rememberUsername, &QCheckBox::toggled, root, [this](bool remember) {
      if (!remember) rememberPassword->setChecked(false);
    });
    showPassword = new QCheckBox("Show password"); credentials->addWidget(showPassword);
    QObject::connect(showPassword, &QCheckBox::toggled, root, [this](bool show) { fields["password"]->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password); });
    Note(credentials, "Copernicus Marine account — for selected ocean-current and wave products. Other providers and offline .xtd tides do not need this login.");
    Note(credentials, "Remember password is optional. It saves an encrypted password protected by Android Keystore for this OpenCPN installation. Uncheck it and Close to forget the saved password. Passwords are sent over HTTPS to Copernicus; Show password resets on Close. A valid Copernicus Marine account and acceptance of the provider's terms are required.");
    estimate = new QLabel; estimate->setWordWrap(true); layout->addWidget(estimate);
    Note(options, "Output is stored in Weather Routing’s generated folder. Processing runs on the tablet; online providers need an internet connection.");
    status = new QLabel("Ready. Choose an area and sources, then use the action below."); status->setWordWrap(true); layout->addWidget(status);
    log = new QPlainTextEdit; log->setReadOnly(true); log->setMaximumBlockCount(150); log->setMaximumHeight(110); log->hide(); layout->addWidget(log);
    auto* actions = new QHBoxLayout;
    generate = new QPushButton("Generate"); cancel = new QPushButton("Cancel job"); close = new QPushButton("Close"); cancel->setEnabled(false);
    actions->addWidget(generate, 1); actions->addWidget(cancel, 1); actions->addWidget(close, 1); layout->addLayout(actions);
    QObject::connect(generate, &QPushButton::clicked, root, [this] { Start(); });
    QObject::connect(cancel, &QPushButton::clicked, root, [this] { Cancel(); });
    QObject::connect(close, &QPushButton::clicked, root, [this] { Close(); });
    timer = new QTimer(root); QObject::connect(timer, &QTimer::timeout, root, [this] { Poll(); });
    resize = new ResizeFollower(static_cast<QWidget*>(owner->GetParent()->GetHandle()), root);
    backFilter = std::make_unique<WR_ForecastBackFilter>(owner, [this] { Close(); });
    QObject::connect(QGuiApplication::inputMethod(), &QInputMethod::visibleChanged, root, [this] {
      const bool editing = QGuiApplication::inputMethod()->isVisible();
      estimate->setVisible(!editing); status->setVisible(!editing);
      log->setVisible(!editing && hasLog);
      resize->Fit();
    });
    for (auto& [key, field] : fields) {
      if (key == "password" || key == "start" || key == "filename" || key == "username") continue;
      wxString saved;
      if (auto* config = GetOCPNConfigObject(); config && config->Read("/PlugIns/xWeatherRouting2/AndroidGenerator/" + wxString(key), &saved)) field->setText(QtText(saved));
    }
    if (auto* config = GetOCPNConfigObject()) {
      const bool remember = config->ReadBool("/PlugIns/xWeatherRouting2/AndroidGenerator/rememberUsername", false);
      rememberUsername->setChecked(remember);
      wxString username;
      if (remember && config->Read("/PlugIns/xWeatherRouting2/AndroidGenerator/username", &username)) fields["username"]->setText(QtText(username));
      rememberPassword->setChecked(config->ReadBool("/PlugIns/xWeatherRouting2/AndroidGenerator/rememberPassword", false));
    }
    for (auto& [key, choice] : choices) {
      wxString saved;
      if (auto* config = GetOCPNConfigObject(); config && config->Read("/PlugIns/xWeatherRouting2/AndroidGenerator/" + wxString(key), &saved)) {
        const int index = choice->findData(QtText(saved)); if (index >= 0) choice->setCurrentIndex(index);
      }
    }
    for (const auto& entry : fields)
      QObject::connect(entry.second, &QLineEdit::textChanged, root, [this] { UpdateOptions(); });
    for (const auto& entry : choices)
      QObject::connect(entry.second, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                       root, [this] { UpdateOptions(); });
    QObject::connect(extend, &QCheckBox::toggled, root, [this] { UpdateOptions(); });
    QObject::connect(fields["username"], &QLineEdit::textChanged, root, [this] {
      if (restoredPassword && fields["username"]->text().trimmed() != credentialUsername) {
        fields["password"]->clear(); restoredPassword = false;
      }
    });
    UpdateOptions();
  }
  ~Impl() { if (cancelled) cancelled->store(true); if (job.valid()) job.wait(); if (credentialJob.valid()) credentialJob.wait(); }
  void CredentialTask(CredentialAction action, const QString& value = {}) {
    credentialUsername = fields["username"]->text().trimmed();
    const auto username = credentialUsername;
    credentialJob = std::async(std::launch::async, [action, username, value] {
      if (action == CredentialAction::Forget) return weather_routing::android_credentials::Forget();
      return weather_routing::android_credentials::Transform(username, value, action == CredentialAction::Save);
    });
    credentialAction = action;
    tabs->setEnabled(false); generate->setEnabled(false); close->setEnabled(false);
    status->setText(action == CredentialAction::Load ? "Unlocking saved password…" : "Updating secure password storage…");
    timer->start(150);
  }
  void LoadCredentials() {
    if (credentialAction != CredentialAction::None || !rememberPassword->isChecked()) return;
    wxString encrypted, username;
    auto* config = GetOCPNConfigObject();
    if (config && config->Read("/PlugIns/xWeatherRouting2/AndroidGenerator/passwordCiphertext", &encrypted) &&
        config->Read("/PlugIns/xWeatherRouting2/AndroidGenerator/passwordAccount", &username) &&
        QtText(username) == fields["username"]->text().trimmed())
      CredentialTask(CredentialAction::Load, QtText(encrypted));
  }
  void FinishClose() {
    fields["password"]->clear(); restoredPassword = false;
    showPassword->setChecked(false);
    if (!rememberUsername->isChecked()) fields["username"]->clear();
    status->setText("Ready. Choose an area and sources, then use the action below.");
    owner->EndModal(wxID_CLOSE);
  }
  void Cancel() {
    if (cancelled) cancelled->store(true);
    status->setText("Cancelling… waiting for the current operation to stop."); cancel->setEnabled(false);
  }
  void Close() {
    if (credentialAction != CredentialAction::None) return;
    if (running) { closeWhenFinished = true; Cancel(); return; }
    QGuiApplication::inputMethod()->hide();
    if (auto* config = GetOCPNConfigObject()) {
      for (const auto& [key, field] : fields)
        if (key != "password" && key != "start" && key != "filename" && key != "username") config->Write("/PlugIns/xWeatherRouting2/AndroidGenerator/" + wxString(key), Wx(field->text()));
      config->Write("/PlugIns/xWeatherRouting2/AndroidGenerator/rememberUsername", rememberUsername->isChecked());
      if (rememberUsername->isChecked()) config->Write("/PlugIns/xWeatherRouting2/AndroidGenerator/username", Wx(fields["username"]->text().trimmed()));
      else config->DeleteEntry("/PlugIns/xWeatherRouting2/AndroidGenerator/username");
      for (const auto& [key, choice] : choices) config->Write("/PlugIns/xWeatherRouting2/AndroidGenerator/" + wxString(key), Wx(choice->currentData().toString()));
      // Android can terminate the host without a desktop-style clean exit.
      // Make the closed generator's preferences durable now.
      config->Flush();
    }
    if (rememberPassword->isChecked()) {
      if (fields["username"]->text().trimmed().isEmpty() || fields["password"]->text().isEmpty()) {
        tabs->setCurrentIndex(4);
        status->setText("Enter a username and password to remember, or uncheck Remember password.");
        return;
      }
      CredentialTask(CredentialAction::Save, fields["password"]->text());
    } else {
      CredentialTask(CredentialAction::Forget);
    }
  }
  eg::EnvironmentRequest Request(bool preparing = true) {
    eg::EnvironmentRequest request;
    request.bbox = {Number("west"), Number("south"), Number("east"), Number("north")}; request.bbox.Validate();
    request.start = eg::ParseUtcDateTime(Text((serverDownload ? QDateTime::currentDateTimeUtc() : startUtc).toString("yyyy-MM-ddTHH:mm:ssZ")));
    const double hours = serverDownload ? 24 : Number("hours");
    if (hours < 1 || hours > 384 || hours != std::floor(hours)) throw std::runtime_error("Duration must be a whole number from 1 to 384 hours");
    request.hours = static_cast<int>(hours); request.step_hours = std::stoi(Selected("step")); request.wave_step_hours = 3;
    request.time_policy = "review";
    request.weather_provider = Selected("weather"); request.weather_preset = Selected("preset");
    request.include_waves = Selected("wave") != "none"; request.wave_provider = Selected("wave"); request.current_source = Selected("current");
    if (!serverDownload && xgrib::NonHourlyNwsStart(request.current_source,
            std::chrono::duration_cast<std::chrono::seconds>(
                request.start.time_since_epoch()).count()))
      throw std::runtime_error(xgrib::kNwsHourlyStartError);
    request.weather_grid_spacing_deg = serverDownload ? 0.25 : Number("weatherGrid");
    request.current_grid_spacing_deg = serverDownload ? 0.05 : Number("currentGrid");
    if (request.weather_grid_spacing_deg <= 0 || request.current_grid_spacing_deg <= 0) throw std::runtime_error("Grid spacing must be positive");
    auto path = [&](const char* key, std::optional<std::filesystem::path>& target) { const auto value = Value(key); if (!value.empty()) target = value; };
    path("weatherFile", request.weather_file); path("currentFile", request.current_file); path("tidalFile", request.offline_tidal_file);
    path("netcdfFile", request.input_netcdf);
    request.offline_current_mode = Selected("offlineMode"); request.infer_minor_tides = minor->isChecked();
    request.copernicus_username = Value("username");
    // Password whitespace may be significant: do not trim it like numeric fields.
    request.copernicus_password = Text(fields["password"]->text()); request.extend_forecast = extend->isChecked();
    request.fallback_weather_provider = Selected("fallbackWeather"); request.fallback_wave_provider = Selected("fallbackWave"); request.fallback_current_source = Selected("fallbackCurrent");
    request.parallel_components = false; request.keep_intermediate = keep->isChecked();
    const std::string filename = Value("filename");
    if (filename.empty() || filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos || filename == "." || filename == "..") throw std::runtime_error("Enter an output filename without folders");
    const auto directory = std::filesystem::path(GetPluginDataDir(PLUGIN_PACKAGE_NAME).ToStdString()) / "generated";
    if (preparing) std::filesystem::create_directories(directory);
    request.output = directory / filename;
    if (preparing && std::filesystem::exists(request.output)) throw std::runtime_error("That output already exists. Choose another filename in Options.");
    request.download_directory = directory / "downloads"; request.execution.cancelled = cancelled;
    request.execution.ca_bundle = ((GetPluginDataDir(PLUGIN_PACKAGE_NAME) + "/data/") + "android-ca-bundle.pem").ToStdString();
    return request;
  }
  void UpdateOptions() {
    const auto weather = Selected("weather"), current = Selected("current");
    for (const char* key : {"start", "hours", "step", "weather", "wave"}) rows[key]->setVisible(!serverDownload);
    timeNote->setVisible(!serverDownload);
    rows["serverModel"]->setVisible(serverDownload); rows["serverDuration"]->setVisible(serverDownload);
    tabs->setTabEnabled(2, !serverDownload); tabs->setTabEnabled(4, !serverDownload);
    extend->setVisible(!serverDownload); keep->setVisible(!serverDownload);
    rows["weatherFile"]->setVisible(!serverDownload && weather == "existing-file");
    rows["preset"]->setVisible(!serverDownload && weather != "none" && weather != "existing-file");
    rows["weatherGrid"]->setVisible(!serverDownload && (weather == "metno_nordic" || weather == "ukmo_ukv"));
    rows["currentFile"]->setVisible(current == "existing-file");
    rows["netcdfFile"]->setVisible(current == "netcdf");
    const bool offline = current == "offline-tidal" || (extend->isChecked() && Selected("fallbackCurrent") == "offline-tidal");
    rows["tidalFile"]->setVisible(offline); rows["offlineMode"]->setVisible(offline);
    minor->setVisible(offline);
    rows["currentGrid"]->setVisible(current != "none" && current != "existing-file");
    const bool credentials = current.find("copernicus") == 0 || current == "auto" ||
        Selected("wave").find("copernicus") == 0 ||
        (current != "none" && extend->isChecked() && Selected("fallbackCurrent").find("copernicus") == 0);
    accountNote->setVisible(credentials);
    tabs->setTabText(4, credentials && !serverDownload ? "Account *" : "Account");
    for (const char* key : {"fallbackWeather", "fallbackWave", "fallbackCurrent"}) rows[key]->setVisible(!serverDownload && extend->isChecked());
    if (running) return;
    if (serverDownload) {
      estimate->setText("Downloads from grib.bosun.io, the same volunteer-run ECMWF service as desktop xGRIB. Model fields/timesteps are set by the server. No currents are added. Maximum download: 128 MiB.");
      return;
    }
    try {
      const auto size = eg::EstimateEnvironment(Request(false));
      const auto numeric = size.isMember("decodedBytes") ? "Decoded numeric data: " : "Known numeric data (partial): ";
      QString message = QString(numeric) + QString::number(size["knownDecodedBytes"].asUInt64() / 1048576.0, 'f', 1) + " MiB.";
      if (size.isMember("fileUpperBytes")) message += " File planning upper estimate: " + QString::number(size["fileUpperBytes"].asUInt64() / 1048576.0, 'f', 1) + " MiB.";
      message += " Generation and routing need additional RAM.";
      estimate->setText(message);
    } catch (const std::exception&) { estimate->setText("Size estimate available when the area, time and source inputs are valid."); }
  }
  bool ChooseTimeOptions(const Json::Value& issue, eg::EnvironmentRequest& request) {
    QStringList labels;
    for (const auto& action : issue["actions"]) labels.append(Text(action["label"].asString()));
    labels.append("Change settings");
    QString message = Text(issue["message"].asString());
    for (const auto& entry : issue["coverage"])
      log->appendPlainText(Text(entry["component"].asString() + " " + entry["field"].asString() +
          ": " + entry["fromUtc"].asString() + " to " + entry["throughUtc"].asString()));
    if (issue.isMember("coverageSummary"))
      message += Text("\n\nField coverage (UTC):\n" + issue["coverageSummary"].asString());
    wxDialog dialog(owner, wxID_ANY, "Forecast time options", wxDefaultPosition,
                    wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto* panel = static_cast<QWidget*>(dialog.GetHandle());
    panel->setStyleSheet("QWidget {font-size: 22px;} QPushButton {min-height: 56px;}");
    auto* layout = new QVBoxLayout(panel);
    auto* explanation = new QLabel(message);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto* list = new QListWidget(panel);
    list->setWordWrap(true);
    list->setTextElideMode(Qt::ElideNone);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for (const auto& label : labels) {
      auto* item = new QListWidgetItem(label, list);
      item->setSizeHint(QSize(0, label.size() > 65 ? 112 : 72));
    }
    list->setCurrentRow(labels.size() - 1);
    QScroller::grabGesture(list->viewport(), QScroller::TouchGesture);
    layout->addWidget(list, 1);
    auto* buttons = new QHBoxLayout;
    auto* use = new QPushButton("Use selected option");
    auto* cancelOption = new QPushButton("Cancel");
    buttons->addWidget(use); buttons->addWidget(cancelOption); layout->addLayout(buttons);
    QObject::connect(use, &QPushButton::clicked, panel, [&] { dialog.EndModal(wxID_OK); });
    QObject::connect(cancelOption, &QPushButton::clicked, panel, [&] { dialog.EndModal(wxID_CANCEL); });
    dialog.Bind(wxEVT_CLOSE_WINDOW, [&](wxCloseEvent&) { dialog.EndModal(wxID_CANCEL); });
    auto* fit = new ResizeFollower(root, panel);
    fit->Fit();
    WR_ForecastBackFilter back(&dialog, [&dialog] { dialog.EndModal(wxID_CANCEL); });
    if (dialog.ShowModal() != wxID_OK) return false;
    const int index = list->currentRow();
    if (index < 0 || index >= static_cast<int>(issue["actions"].size())) return false;
    const auto selected = labels[index];
    eg::ApplyPreflightAction(request, issue["actions"][index]);
    fields["hours"]->setText(QString::number(request.hours));
    auto select = [&](const char* key, const std::string& value) {
      const int found = choices[key]->findData(Text(value));
      if (found >= 0) choices[key]->setCurrentIndex(found);
    };
    select("step", std::to_string(request.step_hours));
    select("weather", request.weather_provider);
    select("current", request.current_source);
    select("wave", request.include_waves ? request.wave_provider : "none");
    log->appendPlainText("Time option selected: " + selected);
    return true;
  }
  bool ValidateAreaInput() {
    const char* keys[] = {"west", "south", "east", "north"};
    double values[4]{};
    QString message;
    int invalid = 0;
    for (int i = 0; i < 4; ++i) {
      const auto text = fields[keys[i]]->text().trimmed();
      bool ok = false;
      values[i] = text.toDouble(&ok);
      const auto label = QString::fromUtf8(eg::AreaCoordinateLabel(static_cast<eg::AreaCoordinate>(i)));
      if (text.isEmpty() || !ok || !std::isfinite(values[i])) {
        message = text.isEmpty() ? QString("Enter ") + label + "."
                                 : label + " must be a finite number.";
        invalid = i; break;
      }
    }
    if (message.isEmpty()) {
      const auto issue = eg::ValidateDownloadArea(values[0], values[1], values[2], values[3]);
      if (!issue.message) return true;
      message = QString::fromUtf8(issue.message); invalid = static_cast<int>(issue.field);
    }
    tabs->setCurrentIndex(0);
    status->setText(message);
    QMessageBox::warning(root, "Invalid download area", message +
        "\n\nEnter the four area coordinates, use the current chart area, or select an area preset.");
    fields[keys[invalid]]->setFocus(); fields[keys[invalid]]->selectAll();
    return false;
  }
  void Start(std::optional<eg::EnvironmentRequest> retry = std::nullopt) {
    if (running || credentialAction != CredentialAction::None) return;
    QGuiApplication::inputMethod()->hide();
    if (!retry && !ValidateAreaInput()) return;
    try {
      cancelled = std::make_shared<std::atomic<bool>>(false);
      auto request = retry ? *retry : Request();
      request.execution.cancelled = cancelled;
      if (!serverDownload) {
        auto check = eg::PreflightEnvironment(request);
        while (!check["ready"].asBool()) {
          if (!ChooseTimeOptions(check["issues"][0], request)) return;
          check = eg::PreflightEnvironment(request);
        }
      }
      if (!serverDownload &&
          (request.current_source.find("copernicus") == 0 ||
           (request.include_waves && request.wave_provider.find("copernicus") == 0) ||
           (request.current_source != "none" && request.extend_forecast &&
            request.fallback_current_source.find("copernicus") == 0)) &&
          (request.copernicus_username.empty() || request.copernicus_password.empty())) {
        tabs->setCurrentIndex(4);
        throw std::runtime_error("Enter your Copernicus username and password on the Account tab.");
      }
      QGuiApplication::inputMethod()->hide();
      latestProgress.clear(); log->clear(); hasLog = true; log->show();
      const auto model = Selected("serverModel"); const int duration = std::stoi(Selected("serverDuration"));
      lastRequest = request;
      job = std::async(std::launch::async, [this, request = std::move(request), model, duration] {
        if (serverDownload) return xgrib::DownloadServerGrib(request, model, duration);
        auto result = eg::GenerateEnvironment(request, {}, std::nullopt,
            [this, flag = request.execution.cancelled](const std::string& stage, const Json::Value&) {
              if (flag->load()) throw std::runtime_error("Generation cancelled");
              std::lock_guard<std::mutex> lock(logMutex); latestProgress = stage;
            });
        if (request.execution.cancelled->load()) throw std::runtime_error("Generation cancelled");
        return result;
      });
      running = true; closeWhenFinished = false;
      tabs->setEnabled(false); generate->setEnabled(false); cancel->setEnabled(true);
      close->setText("Cancel and close");
      status->setText(serverDownload ? "Preparing / downloading forecast from server…" : "Generating forecast…"); timer->start(150);
    } catch (const std::exception& error) { status->setText(Text(std::string(error.what()))); }
  }
  void Poll() {
    if (credentialAction != CredentialAction::None) {
      if (credentialJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
      const auto action = credentialAction; credentialAction = CredentialAction::None;
      timer->stop(); tabs->setEnabled(true); generate->setEnabled(true); close->setEnabled(true);
      try {
        const auto value = credentialJob.get();
        if (action == CredentialAction::Load) {
          fields["password"]->setText(value); restoredPassword = true;
          status->setText("Saved password ready. Choose an area and sources, then Generate.");
        } else {
          auto* config = GetOCPNConfigObject();
          if (!config) throw std::runtime_error("OpenCPN settings are unavailable. Please retry.");
          {
            config->Write("/PlugIns/xWeatherRouting2/AndroidGenerator/rememberPassword", action == CredentialAction::Save);
            if (action == CredentialAction::Save) {
              config->Write("/PlugIns/xWeatherRouting2/AndroidGenerator/passwordCiphertext", Wx(value));
              config->Write("/PlugIns/xWeatherRouting2/AndroidGenerator/passwordAccount", Wx(credentialUsername));
            } else {
              config->DeleteEntry("/PlugIns/xWeatherRouting2/AndroidGenerator/passwordCiphertext");
              config->DeleteEntry("/PlugIns/xWeatherRouting2/AndroidGenerator/passwordAccount");
            }
            config->Flush();
          }
          FinishClose();
        }
      } catch (const std::exception& error) {
        if (action == CredentialAction::Load) {
          rememberPassword->setChecked(false); fields["password"]->clear();
          status->setText("Saved password could not be unlocked. Please enter it again on Account.");
        } else status->setText(Text(std::string(error.what())) + " Retry Close to try again. No plaintext password was saved.");
      }
      return;
    }
    if (!running) return;
    { std::lock_guard<std::mutex> lock(logMutex); if (!latestProgress.empty()) { log->appendPlainText(Text(latestProgress)); latestProgress.clear(); } }
    if (job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    timer->stop(); running = false; tabs->setEnabled(true); generate->setEnabled(true); cancel->setEnabled(false); close->setText("Close");
    try {
      const auto result = job.get();
      status->setText(QString("Ready: %1 messages, %2 MiB. %3").arg(static_cast<qulonglong>(result.message_count))
          .arg(result.byte_count / 1048576.0, 0, 'f', 2).arg(Text(result.output.filename().string())));
      log->appendPlainText(Text(result.output.string()));
      for (const auto& warning : result.diagnostics["warnings"])
        log->appendPlainText("Forecast note: " + Text(warning.asString()));
      const auto& coverage = result.diagnostics["time_coverage"];
      if (coverage["severity"] == "advisory" &&
          (coverage["policy"] == "review" || coverage["policy"] == "keep-all"))
        status->setText(status->text() + Text("\nCoverage note: " + coverage["message"].asString()));
      if (result.inspection.isMember("first_valid_time") &&
          result.inspection.isMember("last_valid_time"))
        log->appendPlainText(Text("File coverage (UTC): " +
            result.inspection["first_valid_time"].asString() + " to " +
            result.inspection["last_valid_time"].asString() +
            ". Individual fields can have shorter coverage."));
      if (!closeWhenFinished && open->isChecked() && ready) ready(wxString::FromUTF8(result.output.string().c_str()));
      // Keep repeated jobs convenient without ever overwriting a previous GRIB.
      fields["filename"]->setText("wr_forecast_" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss") + ".grb2");
    } catch (const eg::CopernicusAuthenticationError& error) {
      const auto message = Text(std::string(error.what()));
      status->setText(message); log->appendPlainText(message);
      if (!cancelled->load() && !closeWhenFinished)
        QMessageBox::warning(root, "Copernicus sign-in failed", message);
    } catch (const eg::PreflightError& error) {
      status->setText(Text(std::string(error.what())));
      if (!cancelled->load() && !closeWhenFinished && ChooseTimeOptions(error.issue(), lastRequest)) {
        Start(lastRequest);
        return;
      }
    } catch (const std::exception& error) { status->setText(cancelled->load() ? "Generation cancelled." : Text(std::string(error.what()))); }
    if (closeWhenFinished) { closeWhenFinished = false; Close(); }
  }
};

AndroidPassageWeatherDialog::AndroidPassageWeatherDialog(wxWindow* parent, GribReadyCallback ready)
    : wxDialog(parent, wxID_ANY, "Weather Routing", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      impl_(std::make_unique<Impl>(this, std::move(ready))) {
  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { impl_->Close(); });
}
AndroidPassageWeatherDialog::~AndroidPassageWeatherDialog() = default;
void AndroidPassageWeatherDialog::ShowMobile(const PlugIn_ViewPort& viewport, bool serverDownload) {
  impl_->viewport = viewport; impl_->serverDownload = serverDownload;
  impl_->generate->setText(serverDownload ? "Download" : "Generate");
  impl_->heading->setText(serverDownload ? "Weather Routing — Download from server" : "Weather Routing — Generate forecast");
  impl_->tabs->setCurrentIndex(0); impl_->UpdateOptions();
  impl_->resize->Fit(); impl_->LoadCredentials(); ShowModal();
}

void WR_FitForecastDialog(wxWindow* parent, wxDialog& dialog) {
  auto* resize = new ResizeFollower(static_cast<QWidget*>(parent->GetHandle()),
                                    static_cast<QWidget*>(dialog.GetHandle()));
  resize->Fit();
}

int WR_ChooseForecastTime(wxWindow* parent, const wxArrayString& times, int selected) {
  if (times.empty()) return wxNOT_FOUND;
  bool accepted = false;
  wxDialog dialog(parent, wxID_ANY, "Forecast time", wxDefaultPosition,
      wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  auto* root = static_cast<QWidget*>(dialog.GetHandle());
  root->setStyleSheet("QWidget {font-size: 22px;} QPushButton {min-height: 52px;}");
  auto* layout = new QVBoxLayout(root);
  layout->addWidget(new QLabel("Select forecast time"));
  auto* list = new QListWidget(root);
  for (const auto& time : times) {
    auto* item = new QListWidgetItem(QtText(time), list);
    item->setSizeHint(QSize(0, 56));
  }
  list->setCurrentRow(selected);
  QScroller::grabGesture(list->viewport(), QScroller::TouchGesture);
  layout->addWidget(list, 1);
  auto* actions = new QHBoxLayout;
  auto* use = new QPushButton("Use selected time");
  auto* cancel = new QPushButton("Cancel");
  actions->addWidget(use); actions->addWidget(cancel); layout->addLayout(actions);
  QObject::connect(use, &QPushButton::clicked, root, [&] {
    if (list->currentRow() >= 0) { accepted = true; dialog.EndModal(wxID_OK); }
  });
  QObject::connect(cancel, &QPushButton::clicked, root, [&] { dialog.EndModal(wxID_CANCEL); });
  dialog.Bind(wxEVT_CLOSE_WINDOW, [&](wxCloseEvent&) { dialog.EndModal(wxID_CANCEL); });
  auto* resize = new ResizeFollower(static_cast<QWidget*>(parent->GetHandle()), root);
  resize->Fit();
  WR_ForecastBackFilter back(&dialog, [&dialog] { dialog.EndModal(wxID_CANCEL); });
  dialog.ShowModal();
  return accepted ? list->currentRow() : wxNOT_FOUND;
}
