/*
 * Akousma
 * Copyright 2026, Genki (genki_jp0) <genki_jp0@users.noreply.github.com>
 *
 * Akousma is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Akousma is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Akousma.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef TABBARTHEMES_H
#define TABBARTHEMES_H

#include <QList>
#include <QString>
#include <QColor>

// The user's own sidebar (tabbar) themes, saved by name in Settings > Appearance > Tabbar background.
// A theme is everything that defines the look of the sidebar: the background type, the colors and gradient and the custom image
// with its options. The image is copied to <data dir>/tabbar-themes, so the theme keeps working when the original file is moved.
// The themes are kept in the settings group "TabBarThemes" (exported and imported with the settings, kept by a reset).
namespace TabBarThemes {

constexpr char kSettingsGroup[] = "TabBarThemes";
constexpr char kThemesArray[] = "themes";
// Key with the image file of a theme, the settings export adds the image itself under kImageDataKey.
constexpr char kImageKey[] = "image";
constexpr char kImageDataKey[] = "image_data";

struct Theme {
  QString name;
  int background = 1;  // AppearanceSettings::TabBarBackground
  bool system_color = true;
  QColor color;
  bool gradient = true;
  bool gradient_custom_colors = false;
  QColor gradient_start_color;
  QColor gradient_end_color;
  int gradient_direction = 0;
  QString image;
  int image_position = 3;
  int image_max_size = 0;
  bool image_stretch = true;
  bool image_keep_aspect_ratio = true;
  bool image_do_not_cut = false;
  bool image_tile = false;
  int image_blur_radius = 0;
  int image_opacity = 100;
  bool image_tint = false;
};

QList<Theme> Load();
void Save(const QList<Theme> &themes);

// Index of the theme with this name (case insensitive), -1 if there is none.
qsizetype IndexOf(const QList<Theme> &themes, const QString &name);

// The names of the built-in backgrounds, a theme can't have one of them.
bool IsBuiltInName(const QString &name);

// Where the copies of the theme images are.
QString ImageDir();
// Copies image to ImageDir() for the theme name, returns the copy, or image when it can't be copied.
QString CopyImage(const QString &name, const QString &image);
// Removes a copy made by CopyImage() (files outside ImageDir() are left alone).
void RemoveImage(const QString &image);

}  // namespace TabBarThemes

#endif  // TABBARTHEMES_H
