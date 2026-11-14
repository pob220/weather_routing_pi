// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from xGRIB 0.4.5, source a5606ce; scoped to Weather Routing.
#pragma once

#include <QApplication>
#include <QGuiApplication>
#include <QInputMethod>
#include <QKeyEvent>
#include <QPointer>
#include <QWidget>
#include <functional>
#include <wx/dialog.h>
#include <wx/toplevel.h>
#include <dlfcn.h>

// Handle both halves of Back before hiding a sheet, so OpenCPN cannot treat
// the same key-up as its own double-Back exit gesture. Newest modal wins.
class WR_ForecastBackFilter : public QObject {
 public:
  WR_ForecastBackFilter(wxDialog* dialog, std::function<void()> action)
      : QObject(dialog->GetHandle()), dialog_(dialog), action_(std::move(action)) {
    // xGRIB hides its static libraries to isolate its TLS stack. That also
    // gives wxQt a private top-level-window list. The Android host counts its
    // own list before forwarding Back key-up to Qt. Register just our owned
    // sheets with the host's matching wx ABI; keep all TLS symbols private.
    // Android loads the Qt application library into a local linker scope, so
    // RTLD_DEFAULT does not necessarily expose its wx symbols to a plugin.
    void* host = dlopen("libgorp.so", RTLD_NOW | RTLD_NOLOAD);
    if (host) {
      hostWindows_ = reinterpret_cast<wxWindowList*>(dlsym(host, "wxTopLevelWindows"));
      dlclose(host);
    }
    if (hostWindows_ && hostWindows_ != &wxTopLevelWindows && !hostWindows_->Find(dialog_)) {
      hostWindows_->Append(dialog_); registered_ = true;
    }
    qApp->installEventFilter(this);
  }
  ~WR_ForecastBackFilter() override {
    if (registered_) hostWindows_->DeleteObject(dialog_);
  }
 protected:
  bool eventFilter(QObject* target, QEvent* event) override {
    // This wxQt ShowModal implementation opens QDialog directly without
    // updating wxWindowBase's visibility bit. The host queries that bit.
    if (target == dialog_->GetHandle()) {
      if (event->type() == QEvent::Show) dialog_->wxWindowBase::Show(true);
      else if (event->type() == QEvent::Hide) dialog_->wxWindowBase::Show(false);
    }
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease) return false;
    const auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() != Qt::Key_Back && key->key() != Qt::Key_Escape) return false;
    if (event->type() == QEvent::KeyRelease && consumed_) {
      consumed_ = false;
      if (keyboard_) QGuiApplication::inputMethod()->hide();
      else if (popupGesture_) { if (popup_) popup_->hide(); }
      else action_();
      return true;
    }
    if (!dialog_->IsShown()) return false;
    auto* widget = qobject_cast<QWidget*>(target);
    if (!widget) widget = QApplication::activeWindow();
    auto* popup = QApplication::activePopupWidget();
    if (!popup && (!widget || widget->window() != dialog_->GetHandle()->window())) return false;
    if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
      consumed_ = true;
      keyboard_ = QGuiApplication::inputMethod()->isVisible();
      popupGesture_ = popup != nullptr; popup_ = popup;
    }
    return true;
  }
 private:
  wxDialog* dialog_;
  std::function<void()> action_;
  QPointer<QWidget> popup_;
  wxWindowList* hostWindows_{nullptr};
  bool registered_{false};
  bool consumed_{false}, keyboard_{false}, popupGesture_{false};
};
