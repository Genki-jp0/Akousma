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

#include "config.h"

#include <QtGlobal>
#include <QtEndian>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QApplication>
#include <QWidget>
#include <QFile>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QIcon>
#include <QIconEngine>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPixmap>
#include <QSize>
#include <QPair>

#include <iterator>

#include "logging.h"
#include "settings.h"
#include "appicon.h"
#include "iconloader.h"
#include "constants/appearancesettings.h"

using namespace Qt::Literals::StringLiterals;

QString AppIcon::current_style_;
QString AppIcon::current_iconset_;
bool AppIcon::resolved_ = false;

namespace {

// Style key -> base name of its files in data/icons/akousma/.
QString StyleFileName(const QString &key) {
  if (key == "win11"_L1) return u"akousma-win11"_s;
  if (key == "win10"_L1) return u"akousma-win10"_s;
  if (key == "win81"_L1) return u"akousma-win81-desktop"_s;
  if (key == "win8"_L1) return u"akousma-win8-tile"_s;
  if (key == "aero"_L1) return u"akousma-aero"_s;
  return QString();
}

// The largest image in each style's .ico.
constexpr int kLargestIconSize = 256;

QString StyleFile(const QString &key, const QString &suffix) {
  return u":/icons/akousma/"_s + StyleFileName(AppIcon::ValidStyle(key)) + suffix;
}

// The sizes of the icons made from an icon set's pomegranate.
constexpr int kIconSetIconSizes[] = { 16, 22, 24, 32, 48, 64, 128, 256 };

// The file of an icon set's icon to draw it at a size from: the one made for that size, else the scalable SVG, else the SVG made
// for the nearest size, else the smallest larger PNG or the largest PNG.
QString IconFileForSize(const QList<QPair<int, QString>> &files, const int size) {

  QString svg;
  int svg_distance = -1;
  QString larger_png;
  int larger_png_size = 0;
  QString largest_png;
  int largest_png_size = 0;
  for (const QPair<int, QString> &file : files) {
    if (file.first == size) return file.second;
  }
  for (const QPair<int, QString> &file : files) {
    if (file.second.endsWith(".svg"_L1)) {
      if (file.first == 0) return file.second;
      const int distance = qAbs(file.first - size);
      if (svg_distance < 0 || distance < svg_distance || (distance == svg_distance && file.first > size)) {
        svg = file.second;
        svg_distance = distance;
      }
    }
    else {
      if (file.first > size && (larger_png.isEmpty() || file.first < larger_png_size)) {
        larger_png = file.second;
        larger_png_size = file.first;
      }
      if (largest_png.isEmpty() || file.first > largest_png_size) {
        largest_png = file.second;
        largest_png_size = file.first;
      }
    }
  }
  if (!svg.isEmpty()) return svg;
  return larger_png.isEmpty() ? largest_png : larger_png;

}

// An icon file at a size: an SVG drawn at that size, a PNG scaled when it has another size.
QImage IconFileImage(const QString &filename, const int size) {

  if (filename.isEmpty()) return QImage();
  QImageReader reader(filename);
  if (filename.endsWith(".svg"_L1)) reader.setScaledSize(QSize(size, size));
  QImage image = reader.read();
  if (image.isNull()) {
    qLog(Error) << "Could not read" << filename << reader.errorString();
    return image;
  }
  if (image.size() != QSize(size, size)) {
    image = image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }
  return image.convertToFormat(QImage::Format_ARGB32);

}

// The images in a .ico file. When they are all PNG images they are read without Qt's ICO plugin, else with it.
QList<QImage> ReadIcoFile(const QString &filename) {

  QList<QImage> images;

  QFile file(filename);
  if (!file.open(QIODevice::ReadOnly)) {
    qLog(Error) << "Could not open" << filename;
    return images;
  }
  const QByteArray data = file.readAll();
  file.close();

  // ICONDIR: reserved, type (1 for icons), count, then 16 bytes for each image with the size and offset of its data at 8 and 12.
  if (data.size() >= 6 && qFromLittleEndian<quint16>(data.constData() + 2) == 1) {
    const int count = qFromLittleEndian<quint16>(data.constData() + 4);
    for (int i = 0; i < count; ++i) {
      const qsizetype entry = 6 + static_cast<qsizetype>(i) * 16;
      const qsizetype size = entry + 16 <= data.size() ? qFromLittleEndian<quint32>(data.constData() + entry + 8) : 0;
      const qsizetype offset = entry + 16 <= data.size() ? qFromLittleEndian<quint32>(data.constData() + entry + 12) : 0;
      QImage image;
      if (size == 0 || offset + size > data.size() || !image.loadFromData(data.mid(offset, size), "PNG")) {
        images.clear();
        break;
      }
      images << image;
    }
  }

  if (images.isEmpty()) {
    QImageReader reader(filename);
    QImage image;
    while (reader.read(&image)) {
      images << image;
      if (!reader.jumpToNextImage()) break;
    }
  }

  if (images.isEmpty()) qLog(Error) << "Could not read" << filename;

  return images;

}

// The application icon: draws the style in use, so it changes with it.
class AppIconEngine : public QIconEngine {
 public:
  explicit AppIconEngine(const bool grey) : grey_(grey) {}

  void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override {
    Icon().paint(painter, rect, Qt::AlignCenter, mode, state);
  }
  QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
    return Icon().pixmap(size, mode, state);
  }
  QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
    if (scale <= 0) scale = 1;
    return Icon().pixmap(QSize(qRound(size.width() / scale), qRound(size.height() / scale)), scale, mode, state);
  }
  QSize actualSize(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
    return Icon().actualSize(size, mode, state);
  }
  QList<QSize> availableSizes(QIcon::Mode mode, QIcon::State state) override {
    return Icon().availableSizes(mode, state);
  }
  bool isNull() override { return Icon().isNull(); }
  QString iconName() override { return QLatin1String(grey_ ? AppIcon::kGreyIconName : AppIcon::kIconName); }
  QString key() const override { return u"AppIconEngine"_s; }
  QIconEngine *clone() const override { return new AppIconEngine(grey_); }

 private:
  QIcon Icon() const {
    const QString iconset = AppIcon::CurrentIconSet();
    if (!iconset.isEmpty()) {
      const QIcon icon = AppIcon::IconSetIcon(iconset, grey_);
      if (!icon.isNull()) return icon;
    }
    return AppIcon::StyleIcon(AppIcon::CurrentStyle(), grey_);
  }

  const bool grey_;
};

}  // namespace

QList<AppIcon::Style> AppIcon::Styles() {

  return QList<Style>() << Style { u"win11"_s, tr("Fluent (Windows 11)") }
                        << Style { u"win10"_s, tr("MDL2 (Windows 10)") }
                        << Style { u"win81"_s, tr("Modern UI (Windows 8.1)") }
                        << Style { u"win8"_s, tr("Metro (Windows 8)") }
                        << Style { u"aero"_s, tr("Aero (Windows Vista / 7)") };

}

QString AppIcon::ValidStyle(const QString &key) {
  return StyleFileName(key).isEmpty() ? QLatin1String(kDefaultStyle) : key;
}

QString AppIcon::StyleFromSettings() {

  Settings s;
  s.beginGroup(AppearanceSettings::kSettingsGroup);
  const QString key = s.value(AppearanceSettings::kAppIcon, QLatin1String(kDefaultStyle)).toString();
  s.endGroup();

  return ValidStyle(key);

}

QString AppIcon::CurrentStyle() {

  Resolve();
  return current_style_;

}

bool AppIcon::FromIconSetFromSettings() {

  Settings s;
  s.beginGroup(AppearanceSettings::kSettingsGroup);
  const bool from_iconset = s.value(AppearanceSettings::kAppIconFromIconSet, AppearanceSettings::kDefaultAppIconFromIconSet).toBool();
  s.endGroup();

  return from_iconset;

}

QString AppIcon::IconSetWithIcon(const QString &iconset_id) {

  QString bundled_id;
  if (iconset_id.isEmpty() || iconset_id == QLatin1String(IconLoader::kIconSetBuiltIn)) {
    return QString();
  }
  if (iconset_id == QLatin1String(IconLoader::kIconSetSystem)) {
    bundled_id = IconLoader::BundledIconSetForTheme(IconLoader::SystemIconThemeName());
  }
  else if (iconset_id.startsWith(QLatin1String(IconLoader::kIconSetSystemThemePrefix))) {
    bundled_id = IconLoader::BundledIconSetForTheme(iconset_id.mid(static_cast<qsizetype>(qstrlen(IconLoader::kIconSetSystemThemePrefix))));
  }
  else if (iconset_id.startsWith(QLatin1String(IconLoader::kIconSetUserThemePrefix))) {
    bundled_id = IconLoader::BundledIconSetForTheme(iconset_id.mid(static_cast<qsizetype>(qstrlen(IconLoader::kIconSetUserThemePrefix))));
  }
  else {
    bundled_id = iconset_id;
  }

  if (bundled_id.isEmpty() || IconLoader::BundledIconFiles(bundled_id, QLatin1String(kIconName)).isEmpty()) return QString();
  return bundled_id;

}

QString AppIcon::CurrentIconSet() {

  Resolve();
  return current_iconset_;

}

void AppIcon::Resolve() {

  if (resolved_) return;
  resolved_ = true;
  current_style_ = StyleFromSettings();
  current_iconset_ = FromIconSetFromSettings() ? IconSetWithIcon(IconLoader::IconSetFromSettings()) : QString();
  WatchIconTheme();

}

void AppIcon::WatchIconTheme() {

  // The desktop's icon theme can change while Akousma follows it, and with it the icon set's pomegranate.
  static bool watching = false;
  if (watching) return;
  if (IconThemeWatcher *watcher = IconLoader::ThemeWatcher()) {
    watching = true;
    QObject::connect(watcher, &IconThemeWatcher::IconThemeChanged, Instance(), []() { AppIcon::ReloadSettings(); });
  }

}

QIcon AppIcon::IconSetIcon(const QString &bundled_id, const bool grey) {

  static QHash<QString, QIcon> icons;
  static QHash<QString, QIcon> grey_icons;

  QHash<QString, QIcon> &cache = grey ? grey_icons : icons;
  if (cache.contains(bundled_id)) return cache.value(bundled_id);

  const QList<QPair<int, QString>> files = IconLoader::BundledIconFiles(bundled_id, QLatin1String(kIconName));
  if (files.isEmpty()) return QIcon();

  QIcon icon;
  for (const int size : kIconSetIconSizes) {
    const QImage image = IconFileImage(IconFileForSize(files, size), size);
    if (!image.isNull()) icon.addPixmap(QPixmap::fromImage(grey ? Grey(image) : image));
  }
  if (!icon.isNull()) cache.insert(bundled_id, icon);
  return icon;

}

QImage AppIcon::IconSetPicture(const QString &bundled_id, const int size, const bool grey) {

  if (size <= 0) return QImage();

  // Up to 256 px the icon's image of that size or the next larger one, above it drawn from the set's SVG or its largest PNG.
  QImage image;
  if (size <= kIconSetIconSizes[std::size(kIconSetIconSizes) - 1]) {
    image = IconSetIcon(bundled_id).pixmap(QSize(size, size), 1.0).toImage();
  }
  if (image.isNull()) {
    image = IconFileImage(IconFileForSize(IconLoader::BundledIconFiles(bundled_id, QLatin1String(kIconName)), size), size);
  }
  if (image.isNull()) return QImage();
  if (image.size() != QSize(size, size)) {
    image = image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }

  image = image.convertToFormat(QImage::Format_ARGB32);
  return grey ? Grey(image) : image;

}

QImage AppIcon::Picture(const int size, const bool grey) {

  const QString iconset = CurrentIconSet();
  if (!iconset.isEmpty()) {
    const QImage image = IconSetPicture(iconset, size, grey);
    if (!image.isNull()) return image;
  }
  return StylePicture(CurrentStyle(), size, grey);

}

QIcon AppIcon::StyleIcon(const QString &key, const bool grey) {

  static QHash<QString, QIcon> icons;
  static QHash<QString, QIcon> grey_icons;

  const QString style = ValidStyle(key);
  QHash<QString, QIcon> &cache = grey ? grey_icons : icons;
  if (cache.contains(style)) return cache.value(style);

  QIcon icon;
  const QList<QImage> images = ReadIcoFile(StyleFile(style, u".ico"_s));
  for (const QImage &image : images) {
    icon.addPixmap(QPixmap::fromImage(grey ? Grey(image) : image));
  }
  cache.insert(style, icon);
  return icon;

}

QImage AppIcon::StylePicture(const QString &key, const int size, const bool grey) {

  if (size <= 0) return QImage();

  const QString style = ValidStyle(key);

  // Up to 256 px the icon's image of that size or the next larger one, above it the 512 px picture.
  QImage image;
  if (size <= kLargestIconSize) {
    image = StyleIcon(style).pixmap(QSize(size, size), 1.0).toImage();
  }
  if (image.isNull()) {
    image = QImage(StyleFile(style, u"-512.png"_s));
  }
  if (image.isNull()) return QImage();
  if (image.size() != QSize(size, size)) {
    image = image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }

  image = image.convertToFormat(QImage::Format_ARGB32);
  return grey ? Grey(image) : image;

}

QIcon AppIcon::Icon(const bool grey) {
  return QIcon(new AppIconEngine(grey));
}

bool AppIcon::IsAppIcon(const QIcon &icon) {

  const QString name = icon.name();
  return name == QLatin1String(kIconName) || name == QLatin1String(kGreyIconName);

}

AppIcon *AppIcon::Instance() {

  static AppIcon *instance = nullptr;
  if (!instance) instance = new AppIcon(QCoreApplication::instance());
  return instance;

}

void AppIcon::ReloadSettings() {

  Resolve();
  const QString style = StyleFromSettings();
  const QString iconset = FromIconSetFromSettings() ? IconSetWithIcon(IconLoader::IconSetFromSettings()) : QString();
  if (style == current_style_ && iconset == current_iconset_) return;
  SetIcon(style, iconset);

}

void AppIcon::SetIcon(const QString &style, const QString &iconset) {

  current_style_ = ValidStyle(style);
  current_iconset_ = iconset;
  if (current_iconset_.isEmpty()) {
    qLog(Debug) << "Application icon:" << current_style_;
  }
  else {
    qLog(Debug) << "Application icon: from the icon set" << current_iconset_;
  }

  if (qobject_cast<QApplication*>(QCoreApplication::instance())) {
    // A new icon, so that Qt gives the platform the icon again. Wayland has no window icons (the desktop file's icon is used).
    if (QGuiApplication::platformName() != "wayland"_L1) {
      QApplication::setWindowIcon(Icon());
    }
    // Windows without their own icon follow QApplication. Those given the application icon themselves are given it again.
    const QWidgetList windows = QApplication::topLevelWidgets();
    for (QWidget *window : windows) {
      if (window->testAttribute(Qt::WA_SetWindowIcon) && IsAppIcon(window->windowIcon())) {
        window->setWindowIcon(Icon());
      }
    }
    // The icons in tabs, menus and buttons draw the new style when painted again.
    const QWidgetList widgets = QApplication::allWidgets();
    for (QWidget *widget : widgets) widget->update();
  }

  Q_EMIT Instance()->Changed();

}

QImage AppIcon::Grey(const QImage &image) {

  QImage grey = image.convertToFormat(QImage::Format_ARGB32);
  for (int y = 0; y < grey.height(); ++y) {
    QRgb *line = reinterpret_cast<QRgb*>(grey.scanLine(y));
    for (int x = 0; x < grey.width(); ++x) {
      // Rec. 709 luma.
      const int value = (qRed(line[x]) * 2126 + qGreen(line[x]) * 7152 + qBlue(line[x]) * 722 + 5000) / 10000;
      line[x] = qRgba(value, value, value, qAlpha(line[x]));
    }
  }
  return grey;

}
