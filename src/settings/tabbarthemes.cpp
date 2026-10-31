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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QCryptographicHash>
#include <QDateTime>

#include "core/settings.h"
#include "core/standardpaths.h"
#include "constants/appearancesettings.h"
#include "tabbarthemes.h"

using namespace Qt::Literals::StringLiterals;

namespace TabBarThemes {

QList<Theme> Load() {

  QList<Theme> themes;

  Settings s;
  s.beginGroup(QLatin1String(kSettingsGroup));
  const int count = s.beginReadArray(QLatin1String(kThemesArray));
  for (int i = 0; i < count; ++i) {
    s.setArrayIndex(i);
    Theme theme;
    theme.name = s.value(u"name"_s).toString().trimmed();
    if (theme.name.isEmpty() || IndexOf(themes, theme.name) >= 0) continue;
    theme.background = static_cast<int>(AppearanceSettings::TabBarBackgroundFromSetting(s.value(u"background"_s, theme.background).toInt()));
    theme.system_color = s.value(u"system_color"_s, theme.system_color).toBool();
    theme.color = s.value(u"color"_s).value<QColor>();
    theme.gradient = s.value(u"gradient"_s, theme.gradient).toBool();
    theme.gradient_custom_colors = s.value(u"gradient_custom_colors"_s, theme.gradient_custom_colors).toBool();
    theme.gradient_start_color = s.value(u"gradient_start_color"_s).value<QColor>();
    theme.gradient_end_color = s.value(u"gradient_end_color"_s).value<QColor>();
    theme.gradient_direction = s.value(u"gradient_direction"_s, theme.gradient_direction).toInt();
    theme.image = s.value(QLatin1String(kImageKey)).toString();
    theme.image_position = s.value(u"image_position"_s, theme.image_position).toInt();
    theme.image_max_size = s.value(u"image_max_size"_s, theme.image_max_size).toInt();
    theme.image_stretch = s.value(u"image_stretch"_s, theme.image_stretch).toBool();
    theme.image_keep_aspect_ratio = s.value(u"image_keep_aspect_ratio"_s, theme.image_keep_aspect_ratio).toBool();
    theme.image_do_not_cut = s.value(u"image_do_not_cut"_s, theme.image_do_not_cut).toBool();
    theme.image_tile = s.value(u"image_tile"_s, theme.image_tile).toBool();
    theme.image_blur_radius = s.value(u"image_blur_radius"_s, theme.image_blur_radius).toInt();
    theme.image_opacity = s.value(u"image_opacity"_s, theme.image_opacity).toInt();
    theme.image_tint = s.value(u"image_tint"_s, theme.image_tint).toBool();
    themes << theme;
  }
  s.endArray();
  s.endGroup();

  // A theme saved under a name the dropdown now uses for a built-in background ("Gradient", "Clementine", "Strawberry",
  // "Custom") is renamed to "<name> (2)" (or the next free number), here and in the setting naming the chosen theme.
  bool renamed = false;
  for (Theme &theme : themes) {
    if (!IsBuiltInName(theme.name)) continue;
    QString new_name;
    for (int n = 2; new_name.isEmpty() || IndexOf(themes, new_name) >= 0; ++n) {
      new_name = theme.name + u" ("_s + QString::number(n) + u')';
    }
    s.beginGroup(AppearanceSettings::kSettingsGroup);
    if (s.value(AppearanceSettings::kTabBarTheme).toString().compare(theme.name, Qt::CaseInsensitive) == 0) {
      s.setValue(AppearanceSettings::kTabBarTheme, new_name);
    }
    s.endGroup();
    theme.name = new_name;
    renamed = true;
  }
  if (renamed) Save(themes);

  return themes;

}

void Save(const QList<Theme> &themes) {

  Settings s;
  s.beginGroup(QLatin1String(kSettingsGroup));
  s.remove(QLatin1String(kThemesArray));
  s.beginWriteArray(QLatin1String(kThemesArray), static_cast<int>(themes.count()));
  for (int i = 0; i < themes.count(); ++i) {
    const Theme &theme = themes[i];
    s.setArrayIndex(i);
    s.setValue(u"name"_s, theme.name);
    s.setValue(u"background"_s, theme.background);
    s.setValue(u"system_color"_s, theme.system_color);
    s.setValue(u"color"_s, theme.color);
    s.setValue(u"gradient"_s, theme.gradient);
    s.setValue(u"gradient_custom_colors"_s, theme.gradient_custom_colors);
    s.setValue(u"gradient_start_color"_s, theme.gradient_start_color);
    s.setValue(u"gradient_end_color"_s, theme.gradient_end_color);
    s.setValue(u"gradient_direction"_s, theme.gradient_direction);
    s.setValue(QLatin1String(kImageKey), theme.image);
    s.setValue(u"image_position"_s, theme.image_position);
    s.setValue(u"image_max_size"_s, theme.image_max_size);
    s.setValue(u"image_stretch"_s, theme.image_stretch);
    s.setValue(u"image_keep_aspect_ratio"_s, theme.image_keep_aspect_ratio);
    s.setValue(u"image_do_not_cut"_s, theme.image_do_not_cut);
    s.setValue(u"image_tile"_s, theme.image_tile);
    s.setValue(u"image_blur_radius"_s, theme.image_blur_radius);
    s.setValue(u"image_opacity"_s, theme.image_opacity);
    s.setValue(u"image_tint"_s, theme.image_tint);
  }
  s.endArray();
  s.endGroup();
  s.sync();

}

qsizetype IndexOf(const QList<Theme> &themes, const QString &name) {

  const QString trimmed = name.trimmed();
  for (qsizetype i = 0; i < themes.count(); ++i) {
    if (themes[i].name.compare(trimmed, Qt::CaseInsensitive) == 0) return i;
  }
  return -1;

}

bool IsBuiltInName(const QString &name) {

  // The entries of the dropdown and their short forms, in English; the translated entries are checked by the settings page.
  static const QStringList built_in_names = QStringList() << u"Plain gradient, no graphics"_s << u"Plain gradient"_s << u"Gradient"_s
                                                          << u"Clementine background"_s << u"Clementine"_s
                                                          << u"Default background (strawberry graphics)"_s << u"Default background"_s << u"Default"_s << u"Strawberry"_s
                                                          << u"Custom image"_s << u"Custom"_s;
  return built_in_names.contains(name.trimmed(), Qt::CaseInsensitive);

}

QString ImageDir() {
  return StandardPaths::WritableLocation(StandardPaths::StandardLocation::AppLocalDataLocation) + u"/tabbar-themes"_s;
}

QString CopyImage(const QString &name, const QString &image) {

  if (image.isEmpty() || !QFileInfo(image).isFile()) return image;
  const QString dir = ImageDir();
  // Already a copy.
  if (QFileInfo(image).absolutePath() == QFileInfo(dir).absoluteFilePath()) return image;
  if (!QDir().mkpath(dir)) return image;

  // A file name from the theme name, made unique, the original suffix kept.
  QString base;
  for (const QChar c : name.trimmed()) {
    base += c.isLetterOrNumber() ? c.toLower() : u'-';
  }
  const QByteArray hash = QCryptographicHash::hash(QByteArray(name.toUtf8() + QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toUtf8()), QCryptographicHash::Sha1).toHex().left(8);
  const QString suffix = QFileInfo(image).suffix().isEmpty() ? u"img"_s : QFileInfo(image).suffix().toLower();
  const QString copy = dir + u'/' + base.left(40) + u'-' + QString::fromLatin1(hash) + u'.' + suffix;
  if (!QFile::copy(image, copy)) return image;
  return copy;

}

void RemoveImage(const QString &image) {

  if (image.isEmpty()) return;
  if (QFileInfo(image).absolutePath() != QFileInfo(ImageDir()).absoluteFilePath()) return;
  QFile::remove(image);

}

}  // namespace TabBarThemes
