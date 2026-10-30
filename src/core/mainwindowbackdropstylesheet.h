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

#ifndef MAINWINDOWBACKDROPSTYLESHEET_H
#define MAINWINDOWBACKDROPSTYLESHEET_H

#include "config.h"

#include <QString>

class QPalette;

// Windows only (window backdrop), but plain Qt: the style sheet set on the main window's central widget while a backdrop is shown.
// The style draws headers, tool buttons and line edits with its own opaque (often native) colors, this replaces them with a
// translucent tint so the backdrop shows through. Hover, pressed and checked states are drawn with the text color at a low alpha,
// so they are visible on light and dark palettes. Only widgets inside the central widget are styled (not dialogs, not the menu).
namespace MainWindowBackdropStyleSheet {

// palette: the opaque application palette.
// chrome_alpha (0-255): tint of the playlist toolbar, the list headers and the player controls / status bar.
// field_alpha (0-255): tint of the search fields.
QString Build(const QPalette &palette, const int chrome_alpha, const int field_alpha);

}  // namespace MainWindowBackdropStyleSheet

#endif  // MAINWINDOWBACKDROPSTYLESHEET_H
