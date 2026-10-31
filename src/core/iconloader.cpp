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

#include "config.h"

#include <algorithm>
#include <utility>

#include <QtGlobal>
#include <QCoreApplication>
#include <QApplication>
#include <QWidget>
#include <QDir>
#include <QDirIterator>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QSet>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QIcon>
#include <QIconEngine>
#include <QImageReader>
#include <QPainter>
#include <QPixmap>
#include <QResource>
#include <QSize>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QTimer>

#if defined(HAVE_QPA_QPLATFORMNATIVEINTERFACE) && !defined(Q_OS_MACOS) && !defined(Q_OS_WIN32)
#  include <private/qiconloader_p.h>
#  define HAVE_QICONLOADER_UPDATESYSTEMTHEME
#endif

#include "logging.h"
#include "standardpaths.h"
#include "settings.h"
#include "includes/iconmapper.h"
#include "iconloader.h"
#include "constants/appearancesettings.h"

using namespace Qt::Literals::StringLiterals;

bool IconLoader::system_icons_ = false;
bool IconLoader::custom_icons_ = false;
bool IconLoader::svg_supported_ = false;
QString IconLoader::icon_set_ = QLatin1String(IconLoader::kIconSetBuiltIn);
IconLoader::IconSet IconLoader::active_set_;
QString IconLoader::system_theme_name_;
QStringList IconLoader::system_theme_search_paths_;
QString IconLoader::theme_name_override_;
QStringList IconLoader::theme_search_paths_override_;

namespace {

// Makes QIcon read the desktop's icon theme again. Qt 6.4 doesn't do this itself when the desktop's settings change.
void UpdateSystemIconTheme() {
#ifdef HAVE_QICONLOADER_UPDATESYSTEMTHEME
  QIconLoader::instance()->updateSystemTheme();
#endif
}

QList<int> IconSizes(const int fixed_size) {
  if (fixed_size != 0) return QList<int>() << fixed_size;
  return QList<int>() << 22 << 32 << 48 << 64 << 128;
}

QString IconSetRoot(const QString &id) {
  return u":/iconsets/"_s + id;
}

// The files of each registered bundled set: set id -> icon name -> (size, resource path); size 0 is a scalable SVG.
QMap<QString, QMap<QString, QList<QPair<int, QString>>>> &IconSetFiles() {
  static QMap<QString, QMap<QString, QList<QPair<int, QString>>>> files;
  return files;
}

// The old "custom icons" folder: <data dir>/icons/scalable or <data dir>/icons/22x22 etc. with icons named like Akousma's.
bool HasCustomIcons(const QString &path) {

  const QDir dir(path);
  if (!dir.exists()) return false;
  if (dir.exists(u"scalable"_s)) return true;
  static const QRegularExpression size_dir(u"^\\d+x\\d+$"_s);
  const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString &subdir : subdirs) {
    if (size_dir.match(subdir).hasMatch()) return true;
  }
  return false;

}

// A small reader for iconset.ini and index.theme: QSettings would split values with commas into lists.
QMap<QString, QString> ReadIniFile(const QString &filename) {

  QMap<QString, QString> values;
  QFile file(filename);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return values;
  QString section;
  const QStringList lines = QString::fromUtf8(file.readAll()).split(u'\n');
  file.close();
  for (const QString &raw_line : lines) {
    const QString line = raw_line.trimmed();
    if (line.isEmpty() || line.startsWith(u'#') || line.startsWith(u';')) continue;
    if (line.startsWith(u'[') && line.endsWith(u']')) {
      section = line.mid(1, line.length() - 2);
      continue;
    }
    const qsizetype equals = line.indexOf(u'=');
    if (equals <= 0) continue;
    values.insert(section + u'/' + line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
  }
  return values;

}

}  // namespace

// An icon that follows the desktop's icon theme: it looks itself up again when the theme changed, and uses the
// built-in icon when the theme doesn't have it.
class FollowThemeIconEngine : public QIconEngine {
 public:
  FollowThemeIconEngine(const QString &name, const int fixed_size, const int min_size, const int max_size)
      : name_(name), fixed_size_(fixed_size), min_size_(min_size), max_size_(max_size) {}

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
  QString iconName() override { return name_; }
  QString key() const override { return u"FollowThemeIconEngine"_s; }
  QIconEngine *clone() const override { return new FollowThemeIconEngine(name_, fixed_size_, min_size_, max_size_); }

 private:
  const QIcon &Icon() {
    const QString theme_name = QIcon::themeName();
    if (!resolved_ || theme_name != theme_name_) {
      theme_name_ = theme_name;
      resolved_ = true;
      icon_ = IconLoader::LoadThemeIcon(name_, fixed_size_, min_size_, max_size_, false);
      if (icon_.isNull()) icon_ = IconLoader::LoadBuiltInIcon(name_, fixed_size_);
    }
    return icon_;
  }

  const QString name_;
  const int fixed_size_;
  const int min_size_;
  const int max_size_;
  bool resolved_ = false;
  QString theme_name_;
  QIcon icon_;
};

IconThemeWatcher::IconThemeWatcher(QObject *parent) : QObject(parent), theme_name_(QIcon::themeName()) {}

bool IconThemeWatcher::eventFilter(QObject *object, QEvent *event) {

  // The platform theme sends ThemeChange to every window when the desktop's settings change, after Qt read the new icon theme.
  if (event->type() == QEvent::ThemeChange && !pending_) {
    pending_ = true;
    QTimer::singleShot(0, this, [this]() {
      pending_ = false;
      const QString theme_name = IconLoader::SystemIconThemeName();
      if (theme_name == theme_name_) return;
      qLog(Info) << "Desktop icon theme changed from" << theme_name_ << "to" << theme_name;
      theme_name_ = theme_name;
      if (IconLoader::CurrentIconSet() == QLatin1String(IconLoader::kIconSetSystem)) {
        const QWidgetList widgets = QApplication::allWidgets();
        for (QWidget *widget : widgets) widget->update();
      }
      Q_EMIT IconThemeChanged(theme_name);
    });
  }

  return QObject::eventFilter(object, event);

}

IconThemeWatcher *IconLoader::ThemeWatcher() {

  static IconThemeWatcher *watcher = nullptr;
  if (!watcher && qobject_cast<QApplication*>(QCoreApplication::instance())) {
    watcher = new IconThemeWatcher(QCoreApplication::instance());
    QCoreApplication::instance()->installEventFilter(watcher);
  }
  return watcher;

}

QString IconLoader::IconSetFromSettings() {

  Settings s;
  s.beginGroup(AppearanceSettings::kSettingsGroup);
  QString id = s.value(AppearanceSettings::kIconSet).toString();
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN32)
  // Strawberry's "System icons" checkbox: follow the desktop's icon theme.
  if (id.isEmpty() && s.value(AppearanceSettings::kSystemThemeIcons, AppearanceSettings::kDefaultSystemIcons).toBool()) {
    id = QLatin1String(kIconSetSystem);
  }
#endif
  s.endGroup();

  if (id.isEmpty()) id = QLatin1String(kIconSetBuiltIn);
  return id;

}

void IconLoader::SetThemeOverride(const QString &theme_name, const QStringList &search_paths) {

  if (QIcon::themeSearchPaths() != search_paths) QIcon::setThemeSearchPaths(search_paths);
  // An empty name makes QIcon use the desktop's theme again, and follow it.
  QIcon::setThemeName(theme_name);

}

void IconLoader::Init() {

  svg_supported_ = QImageReader::supportedImageFormats().contains("svg");

  // Remember the desktop's icon theme before an installed or user theme replaces it.
  if (system_theme_search_paths_.isEmpty()) {
    system_theme_name_ = QIcon::themeName();
    system_theme_search_paths_ = QIcon::themeSearchPaths();
  }

  // Back to the desktop's theme first, for an installed or user theme chosen before.
  if (!theme_name_override_.isEmpty()) {
    theme_name_override_.clear();
    SetThemeOverride(QString(), system_theme_search_paths_);
  }

  icon_set_ = IconSetFromSettings();
  active_set_ = IconSet();
  active_set_.id = QLatin1String(kIconSetBuiltIn);
  system_icons_ = false;
  theme_search_paths_override_ = system_theme_search_paths_;

  if (icon_set_ == QLatin1String(kIconSetSystem)) {
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN32)
    system_icons_ = true;
    active_set_.id = icon_set_;
    active_set_.type = IconSetType::System;
#else
    icon_set_ = QLatin1String(kIconSetBuiltIn);
#endif
  }
  else if (icon_set_ != QLatin1String(kIconSetBuiltIn)) {
    const IconSet iconset = FindIconSet(icon_set_);
    if (iconset.id.isEmpty() || (iconset.type == IconSetType::Bundled && !RegisterIconSet(iconset))) {
      qLog(Warning) << "Icon set" << icon_set_ << "was not found, using the built-in icons.";
      icon_set_ = QLatin1String(kIconSetBuiltIn);
    }
    else {
      active_set_ = iconset;
      if (iconset.type == IconSetType::SystemTheme || iconset.type == IconSetType::UserTheme) {
        system_icons_ = true;
        theme_name_override_ = QFileInfo(iconset.path).fileName();
        if (iconset.type == IconSetType::UserTheme) theme_search_paths_override_.prepend(UserIconsDir());
        SetThemeOverride(theme_name_override_, theme_search_paths_override_);
      }
    }
  }

  qLog(Debug) << "Icon set:" << icon_set_;

  ThemeWatcher();

  custom_icons_ = HasCustomIcons(UserIconsDir());

}

QString IconLoader::SystemIconThemeName() {

  if (theme_name_override_.isEmpty()) {
    UpdateSystemIconTheme();
    return QIcon::themeName();
  }
  SetThemeOverride(QString(), system_theme_search_paths_);
  UpdateSystemIconTheme();
  const QString theme_name = QIcon::themeName();
  SetThemeOverride(theme_name_override_, theme_search_paths_override_);
  return theme_name;

}

QString IconLoader::UserIconsDir() {
  return StandardPaths::WritableLocation(StandardPaths::StandardLocation::AppLocalDataLocation) + u"/icons"_s;
}

QStringList IconLoader::IconSetDirs() {

  QStringList candidates;
  const QString app_dir = QCoreApplication::applicationDirPath();
  if (!app_dir.isEmpty()) {
    candidates << app_dir + u"/iconsets"_s;
#ifdef Q_OS_MACOS
    candidates << app_dir + u"/../Resources/iconsets"_s;
#endif
    candidates << app_dir + u"/../share/akousma/iconsets"_s;
  }
  candidates << QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, u"akousma/iconsets"_s, QStandardPaths::LocateDirectory);

  QStringList dirs;
  for (const QString &candidate : std::as_const(candidates)) {
    const QFileInfo fileinfo(candidate);
    if (!fileinfo.isDir()) continue;
    const QString dir = fileinfo.canonicalFilePath();
    if (!dir.isEmpty() && !dirs.contains(dir)) dirs << dir;
  }
  return dirs;

}

const QMap<QString, QStringList> &IconLoader::IconNames() {

  static QMap<QString, QStringList> names;
  static bool loaded = false;
  if (loaded) return names;

  QFile file(u":/iconsets/names.txt"_s);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qLog(Error) << "Could not open" << file.fileName();
    return names;
  }
  loaded = true;
  const QStringList lines = QString::fromUtf8(file.readAll()).split(u'\n');
  file.close();
  for (const QString &raw_line : lines) {
    const QString line = raw_line.trimmed();
    if (line.isEmpty() || line.startsWith(u'#')) continue;
    const qsizetype colon = line.indexOf(u':');
    if (colon <= 0) continue;
    const QString name = line.left(colon).trimmed();
    QStringList candidates = line.mid(colon + 1).split(u' ', Qt::SkipEmptyParts);
    if (candidates == QStringList() << u"="_s) candidates.clear();
    else if (candidates.isEmpty()) candidates << name;
    names.insert(name, candidates);
  }
  return names;

}

int IconLoader::ThemableIconCount() {

  int count = 0;
  const QMap<QString, QStringList> &names = IconNames();
  for (const QStringList &candidates : names) {
    if (!candidates.isEmpty()) ++count;
  }
  return count;

}

bool IconLoader::RegisterIconSet(const IconSet &iconset) {

  static QSet<QString> registered;
  if (registered.contains(iconset.path)) return true;
  if (!QResource::registerResource(iconset.path)) {
    qLog(Error) << "Could not load icon set" << iconset.path;
    return false;
  }
  const QString root = IconSetRoot(iconset.id);
  if (!QFile::exists(root + u"/iconset.ini"_s)) {
    qLog(Error) << "Icon set" << iconset.path << "does not contain" << root;
    QResource::unregisterResource(iconset.path);
    return false;
  }
  registered.insert(iconset.path);

  // <size>/<name>.png|svg, or scalable/<name>.svg: the theme's original files.
  QMap<QString, QList<QPair<int, QString>>> &files = IconSetFiles()[iconset.id];
  files.clear();
  QDirIterator it(root, QStringList() << u"*.png"_s << u"*.svg"_s, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QFileInfo fileinfo(it.next());
    const QString size_dir = fileinfo.dir().dirName();
    const int size = size_dir == "scalable"_L1 ? 0 : size_dir.toInt();
    files[fileinfo.completeBaseName()] << qMakePair(size, fileinfo.filePath());
  }
  return true;

}

QList<IconLoader::IconSet> IconLoader::SystemIconThemes() {

  QList<IconSet> themes;
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN32)
  QSet<QString> folders_seen;
  const QString user_icons_dir = QFileInfo(UserIconsDir()).absoluteFilePath();
  for (const QString &search_path : std::as_const(system_theme_search_paths_)) {
    if (search_path.startsWith(u':') || QFileInfo(search_path).absoluteFilePath() == user_icons_dir) continue;
    const QFileInfoList folders = QDir(search_path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &folder : folders) {
      const QString folder_name = folder.fileName();
      if (folders_seen.contains(folder_name) || folder_name == "hicolor"_L1) continue;
      const QMap<QString, QString> ini = ReadIniFile(folder.absoluteFilePath() + u"/index.theme"_s);
      // Cursor themes have an index.theme without icon directories.
      if (ini.value(u"Icon Theme/Directories"_s).isEmpty()) continue;
      if (ini.value(u"Icon Theme/Hidden"_s).compare("true"_L1, Qt::CaseInsensitive) == 0) continue;
      folders_seen.insert(folder_name);
      IconSet iconset;
      iconset.id = QLatin1String(kIconSetSystemThemePrefix) + folder_name;
      iconset.name = ini.value(u"Icon Theme/Name"_s, folder_name);
      iconset.type = IconSetType::SystemTheme;
      iconset.path = folder.absoluteFilePath();
      themes << iconset;
    }
  }
  std::sort(themes.begin(), themes.end(), [](const IconSet &a, const IconSet &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
#endif
  return themes;

}

QList<IconLoader::IconSet> IconLoader::IconSets() {

  QList<IconSet> iconsets;

  IconSet builtin;
  builtin.id = QLatin1String(kIconSetBuiltIn);
  builtin.name = QCoreApplication::translate("IconLoader", "Akousma (built-in)");
  builtin.type = IconSetType::BuiltIn;
  builtin.licence = u"GPL-3+"_s;
  builtin.icons = ThemableIconCount();
  iconsets << builtin;

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN32)
  IconSet system;
  system.id = QLatin1String(kIconSetSystem);
  const QString system_theme_name = SystemIconThemeName();
  system.name = system_theme_name.isEmpty() ? QCoreApplication::translate("IconLoader", "Follow system icon theme") : QCoreApplication::translate("IconLoader", "Follow system icon theme (now %1)").arg(system_theme_name);
  system.type = IconSetType::System;
  iconsets << system;
#endif

  // Bundled sets, sorted by name. The first folder with a set wins. Sets with SVG icons need the Qt SVG module.
  QList<IconSet> bundled;
  QSet<QString> ids;
  const QStringList dirs = IconSetDirs();
  for (const QString &dir : dirs) {
    const QFileInfoList files = QDir(dir).entryInfoList(QStringList() << u"*.rcc"_s, QDir::Files, QDir::Name);
    for (const QFileInfo &fileinfo : files) {
      IconSet iconset;
      iconset.id = fileinfo.completeBaseName().toLower();
      iconset.type = IconSetType::Bundled;
      iconset.path = fileinfo.absoluteFilePath();
      if (ids.contains(iconset.id) || iconset.id == QLatin1String(kIconSetBuiltIn) || iconset.id == QLatin1String(kIconSetSystem)) continue;
      if (!RegisterIconSet(iconset)) continue;
      const QMap<QString, QString> ini = ReadIniFile(IconSetRoot(iconset.id) + u"/iconset.ini"_s);
      iconset.name = ini.value(u"IconSet/name"_s, iconset.id);
      iconset.licence = ini.value(u"IconSet/licence"_s);
      iconset.source = ini.value(u"IconSet/source"_s);
      const QMap<QString, QList<QPair<int, QString>>> set_files = IconSetFiles().value(iconset.id);
      iconset.icons = static_cast<int>(set_files.count());
      // Needs Qt SVG when an icon has only SVG files; PNG files of an icon are enough without it.
      for (const QList<QPair<int, QString>> &icon_files : set_files) {
        const bool has_png = std::any_of(icon_files.cbegin(), icon_files.cend(), [](const QPair<int, QString> &icon_file) { return icon_file.second.endsWith(".png"_L1); });
        if (!has_png) iconset.needs_svg = true;
      }
      if (iconset.needs_svg && !svg_supported_) {
        qLog(Warning) << "Icon set" << iconset.name << "needs the Qt SVG module.";
        continue;
      }
      ids.insert(iconset.id);
      bundled << iconset;
    }
  }
  std::sort(bundled.begin(), bundled.end(), [](const IconSet &a, const IconSet &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
  iconsets << bundled;

  iconsets << SystemIconThemes();

  // Icon themes the user put into <data dir>/icons/<folder>/index.theme.
  const QFileInfoList folders = QDir(UserIconsDir()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  for (const QFileInfo &folder : folders) {
    const QString index_theme = folder.absoluteFilePath() + u"/index.theme"_s;
    if (!QFile::exists(index_theme)) continue;
    const QMap<QString, QString> ini = ReadIniFile(index_theme);
    IconSet iconset;
    iconset.id = QLatin1String(kIconSetUserThemePrefix) + folder.fileName();
    iconset.name = ini.value(u"Icon Theme/Name"_s, folder.fileName());
    iconset.type = IconSetType::UserTheme;
    iconset.path = folder.absoluteFilePath();
    iconsets << iconset;
  }

  return iconsets;

}

IconLoader::IconSet IconLoader::FindIconSet(const QString &id) {

  const QList<IconSet> iconsets = IconSets();
  for (const IconSet &iconset : iconsets) {
    if (iconset.id == id) return iconset;
  }
  return IconSet();

}

QIcon IconLoader::LoadIconSetIcon(const IconSet &iconset, const QString &name, const int fixed_size) {

  QIcon ret;
  const QList<QPair<int, QString>> files = IconSetFiles().value(iconset.id).value(name);
  for (const QPair<int, QString> &file : files) {
    if (file.second.endsWith(".svg"_L1) && !svg_supported_) continue;
    if (fixed_size != 0 && file.first != 0 && file.first != fixed_size) continue;
    if (file.first == 0) {
      ret.addFile(file.second);
    }
    else {
      ret.addFile(file.second, QSize(file.first, file.first));
    }
  }
  return ret;

}

QIcon IconLoader::LoadBuiltInIcon(const QString &name, const int fixed_size) {

  QIcon ret;
  const QString path(u":/icons/%1x%1/%2.png"_s);
  const QList<int> sizes = IconSizes(fixed_size);
  for (const int size : sizes) {
    const QString filename(path.arg(size).arg(name));
    if (QFile::exists(filename)) ret.addFile(filename, QSize(size, size));
  }
  return ret;

}

QIcon IconLoader::LoadThemeIcon(const QString &name, const int fixed_size, const int min_size, const int max_size, const bool user_theme) {

  QIcon ret;

  IconMapper::IconProperties icon_prop;
  if (IconMapper::iconmapper_.contains(name)) {
    icon_prop = IconMapper::iconmapper_[name];
  }
  if (min_size != 0) icon_prop.min_size = min_size;
  if (max_size != 0) icon_prop.max_size = max_size;
  if (!icon_prop.allow_system_icon) return ret;

  // The icon's own name, then the alternative names from IconMapper and data/iconsets/names.txt. A user theme
  // replaces logos and the other Akousma artwork only when it has an icon with exactly that name.
  QStringList names = QStringList() << name;
  const QMap<QString, QStringList> &icon_names = IconNames();
  const bool akousma_artwork = icon_names.contains(name) && icon_names.value(name).isEmpty();
  if (!(user_theme && akousma_artwork)) {
    names << icon_prop.names << icon_names.value(name);
  }
  names.removeAll(QString());
  names.removeDuplicates();

  for (const QString &theme_name : std::as_const(names)) {
    ret = QIcon::fromTheme(theme_name);
    if (!ret.isNull()) break;
  }
  if (ret.isNull()) {
    qLog(Debug) << "Couldn't load icon" << name << "from the icon theme.";
    return ret;
  }

  if (fixed_size != 0 && !ret.availableSizes().contains(QSize(fixed_size, fixed_size))) {
    qLog(Warning) << "Can't use theme icon for" << name << "icon does not have fixed size." << fixed_size;
    return QIcon();
  }

  int size_smallest = 0;
  int size_largest = 0;
  const QList<QSize> available_sizes = ret.availableSizes();
  for (const QSize &s : available_sizes) {
    if (s.width() != s.height()) {
      qLog(Warning) << "Can't use theme icon for" << name << "icon is not proportional.";
      return QIcon();
    }
    if (size_smallest == 0 || s.width() < size_smallest) size_smallest = s.width();
    if (s.width() > size_largest) size_largest = s.width();
  }
  if (size_smallest != 0 && icon_prop.min_size != 0 && size_smallest < icon_prop.min_size) {
    qLog(Warning) << "Can't use theme icon for" << name << "icon too small." << size_smallest;
    return QIcon();
  }
  if (size_largest != 0 && icon_prop.max_size != 0 && size_largest > icon_prop.max_size) {
    qLog(Warning) << "Can't use theme icon for" << name << "icon too large." << size_largest;
    return QIcon();
  }

  return ret;

}

QPixmap IconLoader::IconSetPixmap(const QString &id, const QString &name, const int size) {

  return IconSetPixmap(id == icon_set_ ? active_set_ : FindIconSet(id), name, size);

}

QPixmap IconLoader::IconSetPixmap(const IconSet &iconset, const QString &name, const int size) {

  QIcon icon;
  switch (iconset.type) {
    case IconSetType::Bundled:
      if (RegisterIconSet(iconset)) icon = LoadIconSetIcon(iconset, name, 0);
      break;
    case IconSetType::System:
    case IconSetType::SystemTheme:
    case IconSetType::UserTheme:{
      // Look the icon up in that theme for a moment, then go back to the theme in use.
      if (iconset.type == IconSetType::System) {
        SetThemeOverride(QString(), system_theme_search_paths_);
      }
      else {
        QStringList search_paths = system_theme_search_paths_;
        if (iconset.type == IconSetType::UserTheme) search_paths.prepend(UserIconsDir());
        SetThemeOverride(QFileInfo(iconset.path).fileName(), search_paths);
      }
      const QIcon theme_icon = LoadThemeIcon(name, 0, 0, 0, iconset.type == IconSetType::UserTheme);
      const QPixmap pixmap = theme_icon.isNull() ? QPixmap() : theme_icon.pixmap(size, size);
      SetThemeOverride(theme_name_override_, theme_search_paths_override_);
      if (!pixmap.isNull()) return pixmap;
      break;
    }
    case IconSetType::BuiltIn:
      break;
  }
  if (icon.isNull()) icon = LoadBuiltInIcon(name, 0);
  return icon.isNull() ? QPixmap() : icon.pixmap(size, size);

}

QIcon IconLoader::Load(const QString &name, const bool system_icon, const int fixed_size, const int min_size, const int max_size) {

  QIcon ret;

  if (name.isEmpty()) {
    qLog(Error) << "Icon name is empty!";
    return ret;
  }

  const QList<int> sizes = IconSizes(fixed_size);

  // Custom icons take precedence over system theme icons: a user-provided icon set is an explicit choice and should be honored everywhere, with the system theme only used as a fallback for icons the user did not provide.
  if (custom_icons_) {
    const QString custom_icons_path = UserIconsDir();

    // A single scalable SVG in icons/scalable is preferred when SVG is supported, since QIcon can render it at any requested size.
    if (svg_supported_) {
      const QString scalable_svg_filename = custom_icons_path + u"/scalable/"_s + name + u".svg"_s;
      if (QFile::exists(scalable_svg_filename)) {
        ret.addFile(scalable_svg_filename);
        if (!ret.isNull()) return ret;
      }
    }

    const QString custom_icon_path = custom_icons_path + u"/%1x%1/%2.%3"_s;
    for (int s : std::as_const(sizes)) {
      const QString png_filename = custom_icon_path.arg(s).arg(name, u"png"_s);
      if (QFile::exists(png_filename)) {
        ret.addFile(png_filename, QSize(s, s));
        continue;
      }
      if (svg_supported_) {
        const QString svg_filename = custom_icon_path.arg(s).arg(name, u"svg"_s);
        if (QFile::exists(svg_filename)) {
          ret.addFile(svg_filename, QSize(s, s));
        }
      }
    }
    if (!ret.isNull()) return ret;
    qLog(Warning) << "Couldn't load icon" << name << "from custom icons.";
  }

  // The chosen icon set; icons it doesn't have come from the built-in set.
  if (system_icon) {
    switch (active_set_.type) {
      case IconSetType::System:
        // Looked up again when the desktop's icon theme changes.
        if (!LoadThemeIcon(name, fixed_size, min_size, max_size, false).isNull()) {
          return QIcon(new FollowThemeIconEngine(name, fixed_size, min_size, max_size));
        }
        {
          // Not in the theme now: built-in, but still following the theme in case the next one has it.
          const QIcon builtin = LoadBuiltInIcon(name, fixed_size);
          if (!builtin.isNull()) return QIcon(new FollowThemeIconEngine(name, fixed_size, min_size, max_size));
        }
        break;
      case IconSetType::SystemTheme:
      case IconSetType::UserTheme:
        ret = LoadThemeIcon(name, fixed_size, min_size, max_size, active_set_.type == IconSetType::UserTheme);
        break;
      case IconSetType::Bundled:
        ret = LoadIconSetIcon(active_set_, name, fixed_size);
        break;
      case IconSetType::BuiltIn:
        break;
    }
    if (!ret.isNull()) return ret;
  }

  ret = LoadBuiltInIcon(name, fixed_size);

  if (ret.isNull() && !system_icons_ && !custom_icons_) {
    qLog(Error) << "Couldn't load icon" << name;
  }

  return ret;

}
