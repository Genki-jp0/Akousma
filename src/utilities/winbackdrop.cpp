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

#include <windows.h>
#include <dwmapi.h>

#include <cwchar>

#include "winbackdrop.h"

namespace {

// Not in older SDK headers, so defined here with the documented values.
constexpr DWORD kDwmwaUseImmersiveDarkModeBefore20H1 = 19;
constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
constexpr DWORD kDwmwaSystemBackdropType = 38;

constexpr DWORD kDwmsbtAuto = 0;
constexpr DWORD kDwmsbtMainWindow = 2;       // Mica
constexpr DWORD kDwmsbtTransientWindow = 3;  // Acrylic
constexpr DWORD kDwmsbtTabbedWindow = 4;     // Mica Alt

constexpr unsigned long kBuildWindows10_1803 = 17134;
constexpr unsigned long kBuildWindows10_1809 = 17763;
constexpr unsigned long kBuildWindows10_20H1 = 18985;
constexpr unsigned long kBuildWindows11_22H2 = 22621;

// Undocumented, user32.dll (Windows 10+).
enum AccentState : DWORD {
  kAccentDisabled = 0,
  kAccentEnableBlurBehind = 3,
  kAccentEnableAcrylicBlurBehind = 4
};

// Use the GradientColor as the acrylic tint.
constexpr DWORD kAccentFlagGradientColor = 2;

constexpr DWORD kWcaAccentPolicy = 19;

struct AccentPolicy {
  DWORD accent_state;
  DWORD accent_flags;
  DWORD gradient_color;  // 0xAABBGGRR
  DWORD animation_id;
};

struct WindowCompositionAttribData {
  DWORD attrib;
  PVOID data;
  SIZE_T data_size;
};

using SetWindowCompositionAttributeFunc = BOOL (WINAPI*)(HWND, WindowCompositionAttribData*);
using RtlGetVersionFunc = LONG (WINAPI*)(PRTL_OSVERSIONINFOW);
using WTSRegisterSessionNotificationFunc = BOOL (WINAPI*)(HWND, DWORD);
using WTSUnRegisterSessionNotificationFunc = BOOL (WINAPI*)(HWND);

// Window messages, defined here as some are missing from older SDK headers.
constexpr UINT kWmDestroy = 0x0002;
constexpr UINT kWmDisplayChange = 0x007E;
constexpr UINT kWmSettingChange = 0x001A;
constexpr UINT kWmEnterSizeMove = 0x0231;
constexpr UINT kWmExitSizeMove = 0x0232;
constexpr UINT kWmWtsSessionChange = 0x02B1;
constexpr UINT kWmDpiChanged = 0x02E0;
constexpr UINT kWmThemeChanged = 0x031A;
constexpr UINT kWmDwmCompositionChanged = 0x031E;

constexpr DWORD kNotifyForThisSession = 0;
constexpr DWORD kLoadLibrarySearchSystem32 = 0x00000800;

// Casting through void (*)() avoids -Wcast-function-type with MinGW.
template<typename T>
T GetProc(const wchar_t *module_name, const char *proc_name) {

  HMODULE module = GetModuleHandleW(module_name);
  if (!module) return nullptr;
  return reinterpret_cast<T>(reinterpret_cast<void (*)()>(GetProcAddress(module, proc_name)));

}

// For DLLs that are not loaded by Qt or Akousma already, only from the system directory. Never unloaded.
template<typename T>
T GetSystemProc(const wchar_t *module_name, const char *proc_name) {

  HMODULE module = GetModuleHandleW(module_name);
  if (!module) module = LoadLibraryExW(module_name, nullptr, kLoadLibrarySearchSystem32);
  if (!module) return nullptr;
  return reinterpret_cast<T>(reinterpret_cast<void (*)()>(GetProcAddress(module, proc_name)));

}

SetWindowCompositionAttributeFunc SetWindowCompositionAttributeProc() {

  static const SetWindowCompositionAttributeFunc func = GetProc<SetWindowCompositionAttributeFunc>(L"user32.dll", "SetWindowCompositionAttribute");
  return func;

}

bool SetAccent(HWND hwnd, const DWORD accent_state, const DWORD accent_flags, const DWORD gradient_color_abgr) {

  const SetWindowCompositionAttributeFunc set_window_composition_attribute = SetWindowCompositionAttributeProc();
  if (!set_window_composition_attribute) return false;

  AccentPolicy policy{};
  policy.accent_state = accent_state;
  policy.accent_flags = accent_flags;
  policy.gradient_color = gradient_color_abgr;
  policy.animation_id = 0;

  WindowCompositionAttribData data{};
  data.attrib = kWcaAccentPolicy;
  data.data = &policy;
  data.data_size = sizeof(policy);

  return set_window_composition_attribute(hwnd, &data) != FALSE;

}

void ExtendFrame(HWND hwnd, const bool whole_client_area) {

  const int margin = whole_client_area ? -1 : 0;
  const MARGINS margins = { margin, margin, margin, margin };
  DwmExtendFrameIntoClientArea(hwnd, &margins);

}

DWORD ArgbToAbgr(const unsigned int argb) {

  const DWORD a = (argb >> 24) & 0xFF;
  const DWORD r = (argb >> 16) & 0xFF;
  const DWORD g = (argb >> 8) & 0xFF;
  const DWORD b = argb & 0xFF;
  return (a << 24) | (b << 16) | (g << 8) | r;

}

}  // namespace

namespace WinBackdrop {

unsigned long WindowsBuildNumber() {

  static const unsigned long build_number = []() -> unsigned long {
    const RtlGetVersionFunc rtl_get_version = GetProc<RtlGetVersionFunc>(L"ntdll.dll", "RtlGetVersion");
    if (!rtl_get_version) return 0;
    RTL_OSVERSIONINFOW version_info{};
    version_info.dwOSVersionInfoSize = sizeof(version_info);
    if (rtl_get_version(&version_info) != 0) return 0;  // STATUS_SUCCESS
    return version_info.dwBuildNumber;
  }();

  return build_number;

}

bool SystemBackdropAvailable() {

  return WindowsBuildNumber() >= kBuildWindows11_22H2;

}

bool AccentPolicyAvailable() {

  return SetWindowCompositionAttributeProc() != nullptr;

}

bool IsTypeAvailable(const Type type) {

  switch (type) {
    case Type::None:
      return true;
    case Type::Mica:
    case Type::MicaAlt:
      return SystemBackdropAvailable();
    case Type::Acrylic:
      return SystemBackdropAvailable() || AccentPolicyAvailable();
  }

  return false;

}

void SetDarkMode(void *window, const bool dark) {

  HWND hwnd = static_cast<HWND>(window);
  if (!hwnd) return;

  const unsigned long build_number = WindowsBuildNumber();
  if (build_number < kBuildWindows10_1809) return;

  const BOOL value = dark ? TRUE : FALSE;
  const DWORD attribute = build_number >= kBuildWindows10_20H1 ? kDwmwaUseImmersiveDarkMode : kDwmwaUseImmersiveDarkModeBefore20H1;
  DwmSetWindowAttribute(hwnd, attribute, &value, sizeof(value));

}

bool CompositionEnabled() {

  // Always enabled since Windows 8, but a driver or a Windows 7 basic theme can turn it off.
  BOOL enabled = FALSE;
  if (FAILED(DwmIsCompositionEnabled(&enabled))) return false;
  return enabled != FALSE;

}

bool IsRemoteSession() {

  return GetSystemMetrics(SM_REMOTESESSION) != 0;

}

bool TransparencyEffectsEnabled() {

  DWORD value = 1;
  DWORD size = sizeof(value);
  // A missing value means the default, which is on.
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
    return true;
  }
  return value != 0;

}

OpaqueReason CurrentOpaqueReason() {

  if (!CompositionEnabled()) return OpaqueReason::CompositionDisabled;
  if (IsRemoteSession()) return OpaqueReason::RemoteSession;
  if (!TransparencyEffectsEnabled()) return OpaqueReason::TransparencyEffectsOff;
  return OpaqueReason::None;

}

Method Apply(void *window, const Type type, const bool dark, const unsigned int tint_argb, const bool moving) {

  HWND hwnd = static_cast<HWND>(window);
  if (!hwnd || !IsWindow(hwnd)) return Method::None;

  SetDarkMode(hwnd, dark);

  if (type == Type::None) {
    Remove(hwnd);
    return Method::None;
  }

  if (SystemBackdropAvailable()) {
    // An accent policy left over from the fallback would be drawn on top of the system backdrop.
    if (AccentPolicyAvailable()) SetAccent(hwnd, kAccentDisabled, 0, 0);
    DWORD backdrop_type = kDwmsbtMainWindow;
    switch (type) {
      case Type::Mica:
        backdrop_type = kDwmsbtMainWindow;
        break;
      case Type::MicaAlt:
        backdrop_type = kDwmsbtTabbedWindow;
        break;
      case Type::Acrylic:
      case Type::None:
        backdrop_type = kDwmsbtTransientWindow;
        break;
    }
    // The backdrop is drawn behind the frame only, so extend the frame into the whole client area ("sheet of glass").
    ExtendFrame(hwnd, true);
    if (SUCCEEDED(DwmSetWindowAttribute(hwnd, kDwmwaSystemBackdropType, &backdrop_type, sizeof(backdrop_type)))) {
      return Method::SystemBackdrop;
    }
    ExtendFrame(hwnd, false);
  }

  // Windows 10 and Windows 11 21H2: only Acrylic is offered, a stored Mica setting gets the same fallback.
  // The accent policy uses the window's own alpha, extending the frame would break it.
  if (!AccentPolicyAvailable()) return Method::None;
  ExtendFrame(hwnd, false);
  if (WindowsBuildNumber() >= kBuildWindows10_1803) {
    // The acrylic accent lags behind the mouse while the window is moved or resized, a plain blur doesn't (well-known workaround).
    if (moving && SetAccent(hwnd, kAccentEnableBlurBehind, 0, 0)) {
      return Method::AccentAcrylic;
    }
    // A fully transparent tint makes the acrylic disappear on some builds.
    unsigned int tint = tint_argb;
    if (((tint >> 24) & 0xFF) < 0x10) tint = (tint & 0x00FFFFFFU) | 0x10000000U;
    if (SetAccent(hwnd, kAccentEnableAcrylicBlurBehind, kAccentFlagGradientColor, ArgbToAbgr(tint))) {
      return Method::AccentAcrylic;
    }
  }
  if (SetAccent(hwnd, kAccentEnableBlurBehind, 0, 0)) {
    return Method::AccentBlur;
  }

  return Method::None;

}

void Remove(void *window) {

  HWND hwnd = static_cast<HWND>(window);
  if (!hwnd || !IsWindow(hwnd)) return;

  if (SystemBackdropAvailable()) {
    const DWORD backdrop_type = kDwmsbtAuto;
    DwmSetWindowAttribute(hwnd, kDwmwaSystemBackdropType, &backdrop_type, sizeof(backdrop_type));
  }
  if (AccentPolicyAvailable()) {
    SetAccent(hwnd, kAccentDisabled, 0, 0);
  }
  ExtendFrame(hwnd, false);

}

MessageAction ClassifyMessage(const void *message, void *window) {

  const MSG *msg = static_cast<const MSG*>(message);
  if (!msg || !window || msg->hwnd != static_cast<HWND>(window)) return MessageAction::None;

  switch (msg->message) {
    case kWmDwmCompositionChanged:
    case kWmThemeChanged:
    case kWmDpiChanged:
    case kWmDisplayChange:
      return MessageAction::Reapply;
    case kWmSettingChange: {
      // "ImmersiveColorSet" is sent when the light/dark mode, the accent color or the transparency effects setting change.
      const wchar_t *area = reinterpret_cast<const wchar_t*>(msg->lParam);
      if (area && std::wcscmp(area, L"ImmersiveColorSet") == 0) return MessageAction::Reapply;
      return MessageAction::Recheck;
    }
    case kWmWtsSessionChange:
      return MessageAction::Recheck;
    case kWmEnterSizeMove:
      return MessageAction::EnterSizeMove;
    case kWmExitSizeMove:
      return MessageAction::ExitSizeMove;
    case kWmDestroy:
      return MessageAction::Destroyed;
    default:
      break;
  }

  return MessageAction::None;

}

bool RegisterSessionNotification(void *window) {

  HWND hwnd = static_cast<HWND>(window);
  if (!hwnd || !IsWindow(hwnd)) return false;

  static const WTSRegisterSessionNotificationFunc func = GetSystemProc<WTSRegisterSessionNotificationFunc>(L"wtsapi32.dll", "WTSRegisterSessionNotification");
  if (!func) return false;
  return func(hwnd, kNotifyForThisSession) != FALSE;

}

void UnregisterSessionNotification(void *window) {

  HWND hwnd = static_cast<HWND>(window);
  if (!hwnd) return;

  static const WTSUnRegisterSessionNotificationFunc func = GetSystemProc<WTSUnRegisterSessionNotificationFunc>(L"wtsapi32.dll", "WTSUnRegisterSessionNotification");
  if (!func) return;
  func(hwnd);

}

}  // namespace WinBackdrop
