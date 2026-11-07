/*
 * Strawberry Music Player
 * Copyright 2013, 2017-2021, Jonas Kvinge <jonas@jkvinge.net>
 * Copyright 2026, Genki (genki_jp0) <genki_jp0@users.noreply.github.com>
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

#ifndef ICONLOADER_H
#define ICONLOADER_H

#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QIcon>
#include <QPixmap>
#include <QObject>

class QEvent;

// Repaints the windows when the desktop's icon theme changes while Akousma follows it (Linux).
class IconThemeWatcher : public QObject {
  Q_OBJECT

 public:
  explicit IconThemeWatcher(QObject *parent = nullptr);

 Q_SIGNALS:
  void IconThemeChanged(const QString &theme_name);

 protected:
  bool eventFilter(QObject *object, QEvent *event) override;

 private:
  QString theme_name_;
  bool pending_ = false;
};

class IconLoader {
 public:
  // Icon sets (Settings > Appearance > Icon set).
  static constexpr char kIconSetBuiltIn[] = "akousma";
  static constexpr char kIconSetSystem[] = "system";
  static constexpr char kIconSetSystemThemePrefix[] = "systemtheme:";
  static constexpr char kIconSetUserThemePrefix[] = "theme:";

  enum class IconSetType {
    BuiltIn,    // Akousma's own icons.
    System,     // Follows the desktop's icon theme, also when it changes (Linux).
    SystemTheme,  // One of the icon themes installed on the system (Linux).
    Bundled,    // An icon set shipped with Akousma as iconsets/<id>.rcc.
    UserTheme   // A freedesktop icon theme folder in <data dir>/icons.
  };

  struct IconSet {
    QString id;
    QString name;
    IconSetType type = IconSetType::BuiltIn;
    QString path;      // The .rcc file of a bundled set, the folder of an installed or user theme.
    QString licence;
    QString source;
    QString theme;     // A bundled set: the folder name of the icon theme it was made from.
    int icons = -1;    // How many of the themable icons a bundled set has.
    bool needs_svg = false;  // A bundled set with icons only in SVG, which need the Qt SVG module.
  };

  static void Init();
  static QIcon Load(const QString &name, const bool system_icon = true, const int fixed_size = 0, const int min_size = 0, const int max_size = 0);

  // The icon set in use since the start, and the one chosen in the settings (used after a restart).
  static QString CurrentIconSet() { return icon_set_; }
  static QString IconSetFromSettings();

  // All icon sets that can be chosen, built-in first.
  static QList<IconSet> IconSets();
  static IconSet FindIconSet(const QString &id);
  // The bundled sets (iconsets/<id>.rcc), sorted by name.
  static QList<IconSet> BundledIconSets();
  // The bundled set made from the icon theme with that folder name (iconset.ini's theme), empty when there is none.
  static QString BundledIconSetForTheme(const QString &theme_name);
  // The files of an icon in a bundled set as (size, file), size 0 for a scalable SVG, also of icons the set has besides the
  // themable ones (its akousma icon). Empty when the set or the icon isn't there; SVG files only with the Qt SVG module.
  static QList<QPair<int, QString>> BundledIconFiles(const QString &id, const QString &name);
  // A pixmap of the icon in the given set, falling back to the built-in icon (previews in the settings).
  static QPixmap IconSetPixmap(const QString &id, const QString &name, const int size);
  static QPixmap IconSetPixmap(const IconSet &iconset, const QString &name, const int size);

  // The desktop's icon theme now, and the watcher telling when it changes.
  static QString SystemIconThemeName();
  static IconThemeWatcher *ThemeWatcher();
  // The icon themes installed on the system (Linux): every folder with an index.theme listing icon directories in the
  // icon theme search paths, except hicolor and cursor themes.
  static QList<IconSet> SystemIconThemes();

  // Where bundled .rcc sets are looked for, and the folder for the user's own icon themes and custom icons.
  static QStringList IconSetDirs();
  static QString UserIconsDir();

  // Akousma icon name -> freedesktop names standing in for it; empty for icons that always come from the built-in set.
  static const QMap<QString, QStringList> &IconNames();
  // How many icons an icon set can replace.
  static int ThemableIconCount();

 private:
  explicit IconLoader() {}
  static bool RegisterIconSet(const IconSet &iconset);
  static QIcon LoadThemeIcon(const QString &name, const int fixed_size, const int min_size, const int max_size, const bool user_theme);
  // Points QIcon's theme lookup at an installed or user theme, or back at the desktop's theme (empty name).
  static void SetThemeOverride(const QString &theme_name, const QStringList &search_paths);
  static QIcon LoadIconSetIcon(const IconSet &iconset, const QString &name, const int fixed_size);
  static QIcon LoadBuiltInIcon(const QString &name, const int fixed_size);
  friend class FollowThemeIconEngine;

  static bool system_icons_;
  static bool custom_icons_;
  static bool svg_supported_;
  static QString icon_set_;
  static IconSet active_set_;
  static QString system_theme_name_;
  static QStringList system_theme_search_paths_;
  // The theme name Akousma set for an installed or user theme, empty while following the desktop's theme.
  static QString theme_name_override_;
  static QStringList theme_search_paths_override_;
};

#endif  // ICONLOADER_H
