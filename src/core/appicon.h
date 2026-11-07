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

#ifndef APPICON_H
#define APPICON_H

#include <QObject>
#include <QList>
#include <QString>
#include <QIcon>
#include <QImage>

// The application icon (Settings > Appearance > Application icon): one of the pomegranate styles in data/icons/akousma/.
// The icons come from each style's .ico (16 to 256 px, with the drawings made for the small sizes), larger pictures from its 512 px
// PNG; both are rendered from the style's SVG, which Qt SVG can't draw right (clip paths, filters). The grey ones are made from them.
// With "Use the icon set's Akousma icon" it is the pomegranate the chosen icon set has in its own style (the akousma icon of the
// bundled sets in data/iconsets/), or the chosen style when the set has none.
class AppIcon : public QObject {
  Q_OBJECT

 public:
  struct Style {
    QString key;   // Stored in the settings.
    QString name;  // Shown in the dropdown.
  };

  static constexpr char kDefaultStyle[] = "win11";
  // The names IconLoader::Load() gives the application icon for.
  static constexpr char kIconName[] = "akousma";
  static constexpr char kGreyIconName[] = "akousma-grey";

  // The styles in the order of the dropdown, the default first.
  static QList<Style> Styles();
  // The key itself when it names a style, else the default style.
  static QString ValidStyle(const QString &key);
  // The style chosen in the settings.
  static QString StyleFromSettings();
  // The style in use, also the one used when the icon set has no pomegranate.
  static QString CurrentStyle();

  // Whether the icon set's pomegranate is chosen in the settings.
  static bool FromIconSetFromSettings();
  // The bundled set whose pomegranate stands for an icon set: the set itself, or for the desktop's icon theme, an installed theme or
  // one of the user's themes the bundled set made from that theme. Empty when there is none, as for the built-in icons.
  static QString IconSetWithIcon(const QString &iconset_id);
  // The bundled set whose pomegranate is in use, empty when the style's icon is.
  static QString CurrentIconSet();
  // The pomegranate of a bundled set, colored or grey, 16 to 256 px; null when the set has none.
  static QIcon IconSetIcon(const QString &bundled_id, const bool grey = false);
  // A picture of a bundled set's pomegranate at the given size, colored or grey.
  static QImage IconSetPicture(const QString &bundled_id, const int size, const bool grey = false);

  // The icon of a style, colored or grey.
  static QIcon StyleIcon(const QString &key, const bool grey = false);
  // A picture of a style at the given size, colored or grey.
  static QImage StylePicture(const QString &key, const int size, const bool grey = false);

  // The application icon: it shows the style in use, also after the style changed.
  static QIcon Icon(const bool grey = false);
  static QImage Picture(const int size, const bool grey = false);
  // Whether an icon is one Icon() made.
  static bool IsAppIcon(const QIcon &icon);

  // Uses the style or icon set chosen in the settings when it is another one: sets the application and window icons and emits
  // Changed(). Also called when the desktop's icon theme changes.
  static void ReloadSettings();

  // Tells when the style changed. Things showing the icon as a pixmap or picture update themselves then.
  static AppIcon *Instance();

  // A desaturated copy that keeps the transparency, like ImageMagick's "-colorspace Gray".
  static QImage Grey(const QImage &image);

 Q_SIGNALS:
  void Changed();

 private:
  explicit AppIcon(QObject *parent = nullptr) : QObject(parent) {}
  // Reads the style and the icon set from the settings the first time they are needed.
  static void Resolve();
  static void SetIcon(const QString &style, const QString &iconset);
  static void WatchIconTheme();
  static QString current_style_;
  static QString current_iconset_;
  static bool resolved_;
};

#endif  // APPICON_H
