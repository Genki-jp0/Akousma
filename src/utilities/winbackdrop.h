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

#ifndef WINBACKDROP_H
#define WINBACKDROP_H

// Windows only, plain Win32 (no Qt): system backdrop (Mica, Mica Alt, Acrylic) behind a top-level window.
// Windows 11 22H2 and later: DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE) with the frame extended into the whole client area.
// Windows 10 and Windows 11 21H2: the undocumented SetWindowCompositionAttribute accent policy (acrylic, else blur).
// The window's client area must be drawn with per-pixel alpha (Qt::WA_TranslucentBackground) for the backdrop to show.

namespace WinBackdrop {

enum class Type {
  None,
  Mica,
  MicaAlt,
  Acrylic
};

enum class Method {
  None,            // Nothing applied (or removed)
  SystemBackdrop,  // DWMWA_SYSTEMBACKDROP_TYPE (Windows 11 22H2+), DWM draws its own tint
  AccentAcrylic,   // ACCENT_ENABLE_ACRYLICBLURBEHIND with a gradient tint color
  AccentBlur       // ACCENT_ENABLE_BLURBEHIND, no tint: the application must draw a stronger tint itself
};

// Why translucency can't be used right now, the window must then be drawn opaque.
enum class OpaqueReason {
  None,                   // Translucency can be used
  CompositionDisabled,    // DwmIsCompositionEnabled() is false (Windows 7 basic theme, some drivers)
  RemoteSession,          // GetSystemMetrics(SM_REMOTESESSION): Remote Desktop doesn't compose translucent windows reliably
  TransparencyEffectsOff  // Settings > Personalization > Colors > Transparency effects is off (EnableTransparency = 0)
};

// What a window message means for the backdrop, see ClassifyMessage().
enum class MessageAction {
  None,
  Reapply,        // DWM / theme / DPI / display changed: the frame margins and attributes may have been reset, apply again
  Recheck,        // A setting or the session changed: check OpaqueReason again
  EnterSizeMove,  // WM_ENTERSIZEMOVE: the user starts moving or resizing the window
  ExitSizeMove,   // WM_EXITSIZEMOVE
  Destroyed       // WM_DESTROY: unregister the session notification
};

// Windows build number from RtlGetVersion (not affected by the manifest), 0 if unknown.
unsigned long WindowsBuildNumber();

// Windows 11 22H2 (build 22621) or later.
bool SystemBackdropAvailable();

// SetWindowCompositionAttribute is exported by user32.dll.
bool AccentPolicyAvailable();

// Whether a backdrop type can be shown on this Windows version.
// Mica and Mica Alt need Windows 11 22H2, Acrylic falls back to the accent policy on older versions.
bool IsTypeAvailable(const Type type);

// Sets DWMWA_USE_IMMERSIVE_DARK_MODE (attribute 20, or 19 before Windows 10 20H1). Does nothing before Windows 10 1809.
void SetDarkMode(void *hwnd, const bool dark);

// Checked in this order: composition, Remote Desktop session, the user's transparency effects setting.
OpaqueReason CurrentOpaqueReason();
bool CompositionEnabled();
bool IsRemoteSession();
bool TransparencyEffectsEnabled();

// Applies the backdrop to the top-level window, tint_argb is 0xAARRGGBB and is only used by the accent acrylic fallback.
// Returns the method that was used, Method::None if the type is None or nothing could be applied.
// moving: the window is being moved or resized (between WM_ENTERSIZEMOVE and WM_EXITSIZEMOVE). The acrylic accent lags behind the
// mouse then, so a plain blur is used instead; the returned method is still Method::AccentAcrylic, call Apply() again when done.
Method Apply(void *hwnd, const Type type, const bool dark, const unsigned int tint_argb, const bool moving = false);

// Removes everything Apply() set, except the dark mode attribute.
void Remove(void *hwnd);

// msg is a MSG* from a native event filter. Returns MessageAction::None for messages to other windows than hwnd.
MessageAction ClassifyMessage(const void *msg, void *hwnd);

// WTSRegisterSessionNotification(NOTIFY_FOR_THIS_SESSION), so the window gets WM_WTSSESSION_CHANGE when a Remote Desktop session
// connects or disconnects. wtsapi32.dll is loaded on demand. Unregister before the window is destroyed.
bool RegisterSessionNotification(void *hwnd);
void UnregisterSessionNotification(void *hwnd);

}  // namespace WinBackdrop

#endif  // WINBACKDROP_H
