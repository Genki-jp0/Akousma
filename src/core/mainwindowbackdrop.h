/*
 * Strawberry Music Player
 * This file is part of Strawberry.
 *
 * Strawberry is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Strawberry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef MAINWINDOWBACKDROP_H
#define MAINWINDOWBACKDROP_H

#include "config.h"

#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QPointer>
#include <QString>

#include "constants/appearancesettings.h"
#include "utilities/winbackdrop.h"

class QWidget;
class QWindow;
class QEvent;
class FancyTabWidget;

// Windows only: Settings > Appearance > Window backdrop for the main window.
// The window is made translucent (Qt::WA_TranslucentBackground) before its native window is created, so the backdrop can only be
// switched on with a restart. Switching between backdrops, or back to None, is applied live.
// While a backdrop is shown, the main window's palette gets a translucent Window color (and translucent Base / AlternateBase colors
// for the lists when "Translucent content areas" is on), the tabbar draws its default background translucent, and the central widget
// gets a style sheet that tints the headers, toolbars, player controls and search fields (MainWindowBackdropStyleSheet).
// The backdrop is applied again when Windows may have reset it (DWM composition, theme, DPI, display, window state and screen changes),
// and the window is drawn opaque instead while translucency can't be used (no composition, Remote Desktop, transparency effects off).
class MainWindowBackdrop : public QObject, public QAbstractNativeEventFilter {
  Q_OBJECT

 public:
  explicit MainWindowBackdrop(QWidget *window, FancyTabWidget *tabs, QWidget *central_widget);
  ~MainWindowBackdrop() override;

  // Reads the setting and makes the window translucent if a backdrop is set. Call before the native window is created.
  void Init();

  // Applies the current setting (Apply / OK in the settings dialog).
  void ReloadSettings();

  static AppearanceSettings::WindowBackdrop LoadSetting();
  static bool LoadTranslucentContentSetting();
  static WinBackdrop::Type ToWinBackdropType(const AppearanceSettings::WindowBackdrop backdrop);
  static bool IsAvailable(const AppearanceSettings::WindowBackdrop backdrop);

  bool nativeEventFilter(const QByteArray &event_type, void *message, qintptr *result) override;

 protected:
  bool eventFilter(QObject *obj, QEvent *e) override;

 private:
  void Update();
  void UpdatePalette();
  void UpdateStyleSheet();
  void ApplyNative();
  void ScheduleUpdate();
  void ScheduleRecheck();
  void Recheck();
  void UpdateSessionNotification();
  void ConnectWindowHandle();
  bool IsActive() const;
  bool IsDarkPalette() const;

 private:
  QWidget *window_;
  FancyTabWidget *tabs_;
  QWidget *central_widget_;
  AppearanceSettings::WindowBackdrop backdrop_;
  bool translucent_content_;
  bool translucent_;
  bool palette_set_;
  bool updating_;
  bool update_scheduled_;
  bool recheck_scheduled_;
  bool moving_;
  WinBackdrop::Method method_;
  WinBackdrop::OpaqueReason opaque_reason_;
  void *session_hwnd_;
  QPointer<QWindow> window_handle_;
  QString central_widget_stylesheet_;
  QString backdrop_stylesheet_;
};

#endif  // MAINWINDOWBACKDROP_H
