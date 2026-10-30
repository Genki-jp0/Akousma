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

#include <QtGlobal>
#include <QPalette>
#include <QColor>
#include <QString>

#include "mainwindowbackdropstylesheet.h"

using namespace Qt::Literals::StringLiterals;

namespace {

// Alpha of the text color drawn over the tint for the interactive states and the separators.
constexpr int kAlphaSeparator = 46;
constexpr int kAlphaHover = 26;
constexpr int kAlphaPressed = 46;
constexpr int kAlphaChecked = 36;
constexpr int kAlphaFieldBorder = 64;
constexpr int kAlphaFieldBorderHover = 96;
constexpr int kAlphaCheckedBorder = 170;

QString Rgba(const QColor &color, const int alpha) {

  // Qt reads an alpha of 0 or 1 as a fraction (0.0 - 1.0), so 1 would be opaque.
  int a = qBound(0, alpha, 255);
  if (a == 1) a = 2;
  return QStringLiteral("rgba(%1, %2, %3, %4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(a);

}

}  // namespace

namespace MainWindowBackdropStyleSheet {

QString Build(const QPalette &palette, const int chrome_alpha, const int field_alpha) {

  const QColor window = palette.color(QPalette::Active, QPalette::Window);
  const QColor window_text = palette.color(QPalette::Active, QPalette::WindowText);
  const QColor base = palette.color(QPalette::Active, QPalette::Base);
  const QColor text = palette.color(QPalette::Active, QPalette::Text);
  const QColor highlight = palette.color(QPalette::Active, QPalette::Highlight);
  const QColor highlighted_text = palette.color(QPalette::Active, QPalette::HighlightedText);

  const QString chrome = Rgba(window, chrome_alpha);
  const QString field = Rgba(base, field_alpha);
  const QString separator = Rgba(window_text, kAlphaSeparator);
  const QString hover = Rgba(window_text, kAlphaHover);
  const QString pressed = Rgba(window_text, kAlphaPressed);
  const QString checked = Rgba(window_text, kAlphaChecked);
  const QString checked_border = Rgba(highlight, kAlphaCheckedBorder);
  const QString field_border = Rgba(text, kAlphaFieldBorder);
  const QString field_border_hover = Rgba(text, kAlphaFieldBorderHover);

  // The tool buttons of the player controls, the status bar and the playlist toolbar.
  const QString buttons = u"QWidget#player_controls_container QToolButton, PlaylistContainer QFrame#toolbar QToolButton"_s;
  auto buttons_state = [&buttons](const QString &state) {
    QString selectors = buttons;
    return selectors.replace(u"QToolButton"_s, u"QToolButton"_s + state);
  };

  QString css;

  // Bars: one tint for the whole bottom area (controls, track slider, status bar), and the playlist toolbar.
  css += u"QWidget#player_controls_container, PlaylistContainer QFrame#toolbar { background-color: "_s + chrome + u"; }\n"_s;

  // List headers (playlist columns...).
  css += u"QHeaderView { background-color: "_s + chrome + u"; }\n"_s;
  css += u"QHeaderView::section { background-color: transparent; color: "_s + window_text.name() + u"; border: none; border-right: 1px solid "_s + separator + u"; border-bottom: 1px solid "_s + separator + u"; padding: 2px 4px; }\n"_s;
  css += u"QHeaderView::section:hover { background-color: "_s + hover + u"; }\n"_s;
  css += u"QHeaderView::section:pressed { background-color: "_s + pressed + u"; }\n"_s;

  // Search fields (playlist, collection, radios, streaming). The left padding for the clear button comes from SearchField itself.
  css += u"SearchField QLineEdit { background-color: "_s + field + u"; color: "_s + text.name() + u"; border: 1px solid "_s + field_border + u"; border-radius: 4px; padding-top: 2px; padding-bottom: 2px; padding-right: 2px; selection-background-color: "_s + highlight.name() + u"; selection-color: "_s + highlighted_text.name() + u"; }\n"_s;
  css += u"SearchField QLineEdit:hover { border: 1px solid "_s + field_border_hover + u"; }\n"_s;
  css += u"SearchField QLineEdit:focus { border: 1px solid "_s + highlight.name() + u"; }\n"_s;

  // Tool buttons: transparent until hovered, the native hover and pressed backgrounds are opaque.
  css += buttons + u" { background-color: transparent; border: 1px solid transparent; border-radius: 4px; padding: 2px; }\n"_s;
  css += buttons_state(u":hover"_s) + u" { background-color: "_s + hover + u"; border: 1px solid "_s + separator + u"; }\n"_s;
  css += buttons_state(u":pressed"_s) + u" { background-color: "_s + pressed + u"; }\n"_s;
  css += buttons_state(u":checked"_s) + u" { background-color: "_s + checked + u"; border: 1px solid "_s + checked_border + u"; }\n"_s;
  // The stop button has a menu (MenuButtonPopup).
  css += buttons_state(u"[popupMode=\"MenuButtonPopup\"]"_s) + u" { padding-right: 14px; }\n"_s;
  css += buttons_state(u"::menu-button"_s) + u" { border: none; width: 14px; }\n"_s;

  return css;

}

}  // namespace MainWindowBackdropStyleSheet
