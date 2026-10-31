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

#include "config.h"

#include <QApplication>
#include <QCoreApplication>
#include <QWidget>
#include <QWindow>
#include <QScreen>
#include <QEvent>
#include <QTimer>
#include <QPalette>
#include <QColor>

#include "core/logging.h"
#include "core/settings.h"
#include "constants/appearancesettings.h"
#include "utilities/winbackdrop.h"
#include "widgets/fancytabwidget.h"
#include "mainwindowbackdrop.h"
#include "mainwindowbackdropstylesheet.h"

using namespace AppearanceSettings;

namespace {

// Alpha (0-255) of the colors drawn over the backdrop.
// window: the main window's Window color, what the style and Akousma's own widgets (analyzer, moodbar...) fill with.
// base: Base and AlternateBase, the lists (playlist, collection, files, context...) when "Translucent content areas" is on, else 255.
// tabbar: the default strawberry/gradient sidebar.
// chrome: the playlist toolbar, list headers and the player controls / status bar (MainWindowBackdropStyleSheet).
// field: the search fields.
struct Tint {
  int window;
  int base;
  int tabbar;
  int chrome;
  int field;
};

// Mica, Mica Alt and Acrylic are tinted by DWM (light or dark, following DWMWA_USE_IMMERSIVE_DARK_MODE), so only a light tint is added.
constexpr Tint kTintSystemBackdrop = { 48, 150, 110, 64, 120 };
// The Windows 10 acrylic accent is tinted with the window color (kAccentAcrylicTintAlpha below), the lists get a bit more.
constexpr Tint kTintAccentAcrylic = { 48, 170, 110, 64, 130 };
// The plain blur fallback has no tint at all.
constexpr Tint kTintAccentBlur = { 170, 215, 190, 170, 200 };

// Alpha of the window color used as the Windows 10 acrylic tint (GradientColor).
constexpr int kAccentAcrylicTintAlpha = 0x99;

Tint TintForMethod(const WinBackdrop::Method method) {

  switch (method) {
    case WinBackdrop::Method::AccentAcrylic:
      return kTintAccentAcrylic;
    case WinBackdrop::Method::AccentBlur:
      return kTintAccentBlur;
    case WinBackdrop::Method::SystemBackdrop:
    case WinBackdrop::Method::None:
      break;
  }

  return kTintSystemBackdrop;

}

void SetColorAlpha(QPalette *palette, const QPalette &source, const QPalette::ColorGroup group, const QPalette::ColorRole role, const int alpha) {

  QColor color = source.color(group, role);
  color.setAlpha(qMin(color.alpha(), alpha));
  palette->setColor(group, role, color);

}

const char *OpaqueReasonText(const WinBackdrop::OpaqueReason reason) {

  switch (reason) {
    case WinBackdrop::OpaqueReason::CompositionDisabled:
      return "desktop composition is disabled";
    case WinBackdrop::OpaqueReason::RemoteSession:
      return "Remote Desktop session";
    case WinBackdrop::OpaqueReason::TransparencyEffectsOff:
      return "Windows transparency effects are off";
    case WinBackdrop::OpaqueReason::None:
      break;
  }

  return "none";

}

}  // namespace

MainWindowBackdrop::MainWindowBackdrop(QWidget *window, FancyTabWidget *tabs, QWidget *central_widget)
    : QObject(window),
      window_(window),
      tabs_(tabs),
      central_widget_(central_widget),
      backdrop_(MainWindowBackdrop::LoadSetting()),
      translucent_content_(MainWindowBackdrop::LoadTranslucentContentSetting()),
      translucent_(false),
      palette_set_(false),
      updating_(false),
      update_scheduled_(false),
      recheck_scheduled_(false),
      moving_(false),
      method_(WinBackdrop::Method::None),
      opaque_reason_(WinBackdrop::OpaqueReason::None),
      session_hwnd_(nullptr) {}

MainWindowBackdrop::~MainWindowBackdrop() {

  if (translucent_ && QCoreApplication::instance()) {
    QCoreApplication::instance()->removeNativeEventFilter(this);
  }
  // Normally done on WM_DESTROY already.
  if (session_hwnd_) {
    WinBackdrop::UnregisterSessionNotification(session_hwnd_);
    session_hwnd_ = nullptr;
  }

}

AppearanceSettings::WindowBackdrop MainWindowBackdrop::LoadSetting() {

  Settings s;
  s.beginGroup(kSettingsGroup);
  const int value = s.value(kWindowBackdrop, static_cast<int>(kDefaultWindowBackdrop)).toInt();
  s.endGroup();

  switch (value) {
    case static_cast<int>(AppearanceSettings::WindowBackdrop::Mica):
      return AppearanceSettings::WindowBackdrop::Mica;
    case static_cast<int>(AppearanceSettings::WindowBackdrop::MicaAlt):
      return AppearanceSettings::WindowBackdrop::MicaAlt;
    case static_cast<int>(AppearanceSettings::WindowBackdrop::Acrylic):
      return AppearanceSettings::WindowBackdrop::Acrylic;
    default:
      return AppearanceSettings::WindowBackdrop::None;
  }

}

bool MainWindowBackdrop::LoadTranslucentContentSetting() {

  Settings s;
  s.beginGroup(kSettingsGroup);
  const bool translucent_content = s.value(kWindowBackdropTranslucentContent, kDefaultWindowBackdropTranslucentContent).toBool();
  s.endGroup();

  return translucent_content;

}

WinBackdrop::Type MainWindowBackdrop::ToWinBackdropType(const AppearanceSettings::WindowBackdrop backdrop) {

  switch (backdrop) {
    case AppearanceSettings::WindowBackdrop::Mica:
      return WinBackdrop::Type::Mica;
    case AppearanceSettings::WindowBackdrop::MicaAlt:
      return WinBackdrop::Type::MicaAlt;
    case AppearanceSettings::WindowBackdrop::Acrylic:
      return WinBackdrop::Type::Acrylic;
    case AppearanceSettings::WindowBackdrop::None:
      break;
  }

  return WinBackdrop::Type::None;

}

bool MainWindowBackdrop::IsAvailable(const AppearanceSettings::WindowBackdrop backdrop) {

  return WinBackdrop::IsTypeAvailable(ToWinBackdropType(backdrop));

}

void MainWindowBackdrop::Init() {

  backdrop_ = LoadSetting();
  translucent_content_ = LoadTranslucentContentSetting();
  window_->installEventFilter(this);

  if (backdrop_ == AppearanceSettings::WindowBackdrop::None) return;

  if (!IsAvailable(backdrop_)) {
    qLog(Warning) << "Window backdrop" << static_cast<int>(backdrop_) << "is not available on Windows build" << WinBackdrop::WindowsBuildNumber();
    return;
  }

  // The alpha channel of the backing store is chosen when the native window is created.
  if (window_->testAttribute(Qt::WA_WState_Created)) {
    qLog(Warning) << "The main window already has a native window, the window backdrop is used after a restart";
    return;
  }

  // The window stays translucent even if translucency can't be used right now (Remote Desktop...), it is then drawn opaque
  // until it can, see Update().
  window_->setAttribute(Qt::WA_TranslucentBackground, true);
  translucent_ = true;

  if (central_widget_) central_widget_stylesheet_ = central_widget_->styleSheet();

  // DWM, theme, DPI, setting, session and move/resize messages of the main window.
  QCoreApplication::instance()->installNativeEventFilter(this);

  qLog(Debug) << "Window backdrop" << static_cast<int>(backdrop_) << "on Windows build" << WinBackdrop::WindowsBuildNumber();

}

void MainWindowBackdrop::ReloadSettings() {

  const AppearanceSettings::WindowBackdrop backdrop = LoadSetting();
  if (!translucent_ && backdrop != AppearanceSettings::WindowBackdrop::None) {
    qLog(Debug) << "The window backdrop is used after a restart";
  }
  backdrop_ = backdrop;
  translucent_content_ = LoadTranslucentContentSetting();
  Update();

}

bool MainWindowBackdrop::IsActive() const {

  return translucent_ && backdrop_ != AppearanceSettings::WindowBackdrop::None && IsAvailable(backdrop_) && opaque_reason_ == WinBackdrop::OpaqueReason::None;

}

bool MainWindowBackdrop::IsDarkPalette() const {

  const QPalette palette = QApplication::palette(window_);
  return palette.color(QPalette::Active, QPalette::Window).lightness() < palette.color(QPalette::Active, QPalette::WindowText).lightness();

}

void MainWindowBackdrop::ScheduleUpdate() {

  if (update_scheduled_) return;
  update_scheduled_ = true;
  // Queued, so it runs after Qt itself has handled the message or event (Qt may reset the frame margins).
  QTimer::singleShot(0, this, &MainWindowBackdrop::Update);

}

void MainWindowBackdrop::ScheduleRecheck() {

  if (recheck_scheduled_) return;
  recheck_scheduled_ = true;
  QTimer::singleShot(0, this, &MainWindowBackdrop::Recheck);

}

void MainWindowBackdrop::Recheck() {

  recheck_scheduled_ = false;
  if (!translucent_) return;

  if (WinBackdrop::CurrentOpaqueReason() != opaque_reason_) {
    Update();
  }

}

void MainWindowBackdrop::Update() {

  update_scheduled_ = false;

  // A window started without a backdrop stays exactly as it is, the backdrop is used after a restart.
  if (!translucent_ || updating_) return;

  updating_ = true;

  const WinBackdrop::OpaqueReason opaque_reason = WinBackdrop::CurrentOpaqueReason();
  if (opaque_reason != opaque_reason_) {
    if (opaque_reason == WinBackdrop::OpaqueReason::None) {
      qLog(Info) << "Window backdrop: translucency can be used again";
    }
    else {
      qLog(Info) << "Window backdrop: drawing the window opaque," << OpaqueReasonText(opaque_reason);
    }
    opaque_reason_ = opaque_reason;
  }

  ConnectWindowHandle();
  UpdateSessionNotification();
  ApplyNative();
  UpdatePalette();
  UpdateStyleSheet();

  updating_ = false;

}

void MainWindowBackdrop::ConnectWindowHandle() {

  QWindow *window_handle = window_->windowHandle();
  if (!window_handle || window_handle == window_handle_) return;

  if (window_handle_) QObject::disconnect(window_handle_, nullptr, this, nullptr);
  window_handle_ = window_handle;
  // Moving the window to another screen (other DPI, other display) can reset the frame margins.
  QObject::connect(window_handle, &QWindow::screenChanged, this, &MainWindowBackdrop::ScheduleUpdate);

}

void MainWindowBackdrop::UpdateSessionNotification() {

  void *hwnd = reinterpret_cast<void*>(window_->internalWinId());
  if (hwnd == session_hwnd_) return;

  if (session_hwnd_) WinBackdrop::UnregisterSessionNotification(session_hwnd_);
  session_hwnd_ = nullptr;
  // WM_WTSSESSION_CHANGE: Remote Desktop connects or disconnects.
  if (hwnd && WinBackdrop::RegisterSessionNotification(hwnd)) session_hwnd_ = hwnd;

}

void MainWindowBackdrop::ApplyNative() {

  void *hwnd = reinterpret_cast<void*>(window_->winId());

  if (IsActive()) {
    QColor tint = QApplication::palette(window_).color(QPalette::Active, QPalette::Window);
    tint.setAlpha(kAccentAcrylicTintAlpha);
    const WinBackdrop::Method method = WinBackdrop::Apply(hwnd, ToWinBackdropType(backdrop_), IsDarkPalette(), tint.rgba(), moving_);
    if (method != method_) {
      qLog(Debug) << "Window backdrop method" << static_cast<int>(method);
    }
    if (method == WinBackdrop::Method::None) {
      qLog(Warning) << "Could not apply the window backdrop";
    }
    method_ = method;
  }
  else {
    WinBackdrop::Remove(hwnd);
    method_ = WinBackdrop::Method::None;
  }

}

void MainWindowBackdrop::UpdatePalette() {

  if (method_ == WinBackdrop::Method::None) {
    // The window has an alpha channel: without a backdrop it is filled like a normal window, so it looks as without the backdrop.
    if (palette_set_) {
      window_->setPalette(QPalette());
      palette_set_ = false;
    }
    window_->setAutoFillBackground(true);
    if (tabs_) tabs_->SetBackdropAlpha(255);
    return;
  }

  const Tint tint = TintForMethod(method_);
  // Text on translucent pixels can't use ClearType (Qt falls back to grayscale antialiasing there), so the lists stay opaque
  // unless "Translucent content areas" is on.
  const int base_alpha = translucent_content_ ? tint.base : 255;

  const QPalette app_palette = QApplication::palette(window_);
  QPalette palette;
  for (const QPalette::ColorGroup group : { QPalette::Active, QPalette::Inactive, QPalette::Disabled }) {
    SetColorAlpha(&palette, app_palette, group, QPalette::Window, tint.window);
    SetColorAlpha(&palette, app_palette, group, QPalette::Base, base_alpha);
    SetColorAlpha(&palette, app_palette, group, QPalette::AlternateBase, base_alpha);
  }

  window_->setAutoFillBackground(false);
  if (!palette_set_ || window_->palette() != palette) {
    window_->setPalette(palette);
    palette_set_ = true;
  }
  if (tabs_) tabs_->SetBackdropAlpha(tint.tabbar);

}

void MainWindowBackdrop::UpdateStyleSheet() {

  if (!central_widget_) return;

  QString stylesheet;
  if (method_ != WinBackdrop::Method::None) {
    const Tint tint = TintForMethod(method_);
    stylesheet = MainWindowBackdropStyleSheet::Build(QApplication::palette(window_), tint.chrome, tint.field);
  }

  if (stylesheet == backdrop_stylesheet_) return;
  backdrop_stylesheet_ = stylesheet;

  if (stylesheet.isEmpty()) {
    central_widget_->setStyleSheet(central_widget_stylesheet_);
  }
  else {
    central_widget_->setStyleSheet(central_widget_stylesheet_ + QLatin1Char('\n') + stylesheet);
  }

}

bool MainWindowBackdrop::nativeEventFilter(const QByteArray &event_type, void *message, qintptr *result) {

  Q_UNUSED(result)

  if (!translucent_ || event_type != QByteArrayLiteral("windows_generic_MSG")) return false;

  switch (WinBackdrop::ClassifyMessage(message, reinterpret_cast<void*>(window_->internalWinId()))) {
    case WinBackdrop::MessageAction::Reapply:
      ScheduleUpdate();
      break;
    case WinBackdrop::MessageAction::Recheck:
      ScheduleRecheck();
      break;
    case WinBackdrop::MessageAction::EnterSizeMove:
      // Windows 10 acrylic accent: a plain blur while moving or resizing.
      moving_ = true;
      if (method_ == WinBackdrop::Method::AccentAcrylic && !updating_) ApplyNative();
      break;
    case WinBackdrop::MessageAction::ExitSizeMove:
      if (moving_) {
        moving_ = false;
        if (method_ == WinBackdrop::Method::AccentAcrylic && !updating_) ApplyNative();
      }
      break;
    case WinBackdrop::MessageAction::Destroyed:
      if (session_hwnd_) {
        WinBackdrop::UnregisterSessionNotification(session_hwnd_);
        session_hwnd_ = nullptr;
      }
      break;
    case WinBackdrop::MessageAction::None:
      break;
  }

  // Never consumed, Qt handles the message as usual.
  return false;

}

bool MainWindowBackdrop::eventFilter(QObject *obj, QEvent *e) {

  if (obj == window_ && translucent_ && !updating_) {
    switch (e->type()) {
      case QEvent::WinIdChange:
      case QEvent::Show:
        // Qt resets the frame margins when the window flags change, and a new native window has none of the attributes.
        Update();
        break;
      case QEvent::WindowStateChange:
        // Maximize, restore, minimize and full screen can reset the frame margins.
      case QEvent::ApplicationPaletteChange:
      case QEvent::StyleChange:
      case QEvent::ThemeChange:
        // Dark mode and the tint follow the palette.
        ScheduleUpdate();
        break;
      default:
        break;
    }
  }

  return QObject::eventFilter(obj, e);

}
