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

#include "gtest_include.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QSize>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "core/settings.h"
#include "core/iconloader.h"
#include "constants/appearancesettings.h"

using namespace Qt::Literals::StringLiterals;

namespace {

bool CopyFile(const QString &source, const QString &destination) {
  return QDir().mkpath(QFileInfo(destination).absolutePath()) && QFile::copy(source, destination);
}

bool WriteFile(const QString &filename, const QByteArray &data) {
  if (!QDir().mkpath(QFileInfo(filename).absolutePath())) return false;
  QFile file(filename);
  if (!file.open(QIODevice::WriteOnly)) return false;
  return file.write(data) == data.size();
}

class IconLoaderTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    QCoreApplication::setOrganizationName(u"Akousma"_s);
    QStandardPaths::setTestModeEnabled(true);
    // Installed icon themes: only the folders of this test, the desktop's search paths are remembered at the first Init().
    themes_dir_ = new QTemporaryDir;
    const QString dir = themes_dir_->path();
    WriteFile(dir + u"/Good/index.theme"_s, "[Icon Theme]\nName=Good Theme\nDirectories=22x22/actions\n\n[22x22/actions]\nSize=22\nType=Fixed\n");
    CopyFile(u":/icons/22x22/media-playback-stop.png"_s, dir + u"/Good/22x22/actions/media-playback-stop.png"_s);
    WriteFile(dir + u"/Pointers/index.theme"_s, "[Icon Theme]\nName=Pointers\nComment=A cursor theme\nInherits=Good\n");
    WriteFile(dir + u"/hicolor/index.theme"_s, "[Icon Theme]\nName=Hicolor\nDirectories=22x22/actions\n");
    WriteFile(dir + u"/Secret/index.theme"_s, "[Icon Theme]\nName=Secret\nHidden=true\nDirectories=22x22/actions\n");
    QIcon::setThemeSearchPaths(QStringList() << dir);
  }
  static void TearDownTestSuite() {
    delete themes_dir_;
    themes_dir_ = nullptr;
  }
  void SetUp() override {
    Settings s;
    s.clear();
    s.sync();
  }
  void TearDown() override {
    Settings s;
    s.clear();
    s.sync();
    IconLoader::Init();
  }
  static void SetIconSet(const QString &id) {
    Settings s;
    s.setValue(u"Appearance/icon_set"_s, id);
    s.sync();
    IconLoader::Init();
  }
  static bool HasSize(const QIcon &icon, const int size) {
    return icon.availableSizes().contains(QSize(size, size));
  }
  static QTemporaryDir *themes_dir_;
};

QTemporaryDir *IconLoaderTest::themes_dir_ = nullptr;

}  // namespace

TEST_F(IconLoaderTest, IconNames) {

  const QMap<QString, QStringList> &names = IconLoader::IconNames();
  EXPECT_EQ(QStringList() << u"media-playback-start"_s, names.value(u"media-playback-start"_s));
  EXPECT_EQ(QStringList() << u"system-search"_s << u"edit-find"_s, names.value(u"search"_s));
  EXPECT_TRUE(names.contains(u"spotify"_s));
  EXPECT_TRUE(names.value(u"spotify"_s).isEmpty());
  EXPECT_TRUE(names.contains(u"document-edit"_s));
  // Every built-in icon has a line, and every line a built-in icon.
  const QStringList builtin = QDir(u":/icons/22x22"_s).entryList(QStringList() << u"*.png"_s);
  EXPECT_EQ(builtin.count(), names.count());
  for (const QString &filename : builtin) {
    EXPECT_TRUE(names.contains(QFileInfo(filename).completeBaseName())) << filename.toStdString();
  }
  EXPECT_GT(IconLoader::ThemableIconCount(), 70);
  EXPECT_LT(IconLoader::ThemableIconCount(), names.count());

}

TEST_F(IconLoaderTest, DefaultAndMigration) {

  IconLoader::Init();
  EXPECT_EQ(u"akousma"_s, IconLoader::CurrentIconSet());

  // Strawberry's "System icons" checkbox becomes "Follow system icon theme" on Linux.
  {
    Settings s;
    s.setValue(u"Appearance/system_icons"_s, true);
    s.sync();
  }
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN32)
  EXPECT_EQ(u"akousma"_s, IconLoader::IconSetFromSettings());
#else
  EXPECT_EQ(u"system"_s, IconLoader::IconSetFromSettings());
#endif
  {
    Settings s;
    s.setValue(u"Appearance/icon_set"_s, u"akousma"_s);
    s.sync();
  }
  EXPECT_EQ(u"akousma"_s, IconLoader::IconSetFromSettings());

  // The removed Clementine set means the built-in set, and is written back as that.
  SetIconSet(u"clementine"_s);
  EXPECT_EQ(u"akousma"_s, IconLoader::CurrentIconSet());
  EXPECT_EQ(u"akousma"_s, Settings().value(u"Appearance/icon_set"_s).toString());
  EXPECT_FALSE(IconLoader::FindIconSet(u"clementine"_s).type == IconLoader::IconSetType::Bundled);

  // A set that isn't there: the built-in icons.
  SetIconSet(u"theme:Missing"_s);
  EXPECT_EQ(u"akousma"_s, IconLoader::CurrentIconSet());
  EXPECT_FALSE(IconLoader::Load(u"media-playback-start"_s).isNull());

}

TEST_F(IconLoaderTest, InstalledThemes) {

  IconLoader::Init();
  const QList<IconLoader::IconSet> themes = IconLoader::SystemIconThemes();
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN32)
  EXPECT_TRUE(themes.isEmpty());
#else
  // No cursor themes, no hicolor, no hidden themes.
  ASSERT_EQ(1, themes.count());
  EXPECT_EQ(u"systemtheme:Good"_s, themes[0].id);
  EXPECT_EQ(u"Good Theme"_s, themes[0].name);

  SetIconSet(u"systemtheme:Good"_s);
  EXPECT_EQ(u"systemtheme:Good"_s, IconLoader::CurrentIconSet());
  EXPECT_EQ(u"Good"_s, QIcon::themeName());
  EXPECT_EQ(u"media-playback-stop"_s, IconLoader::Load(u"media-playback-stop"_s).name());
  // Not in the theme: the built-in icon.
  const QIcon builtin = IconLoader::Load(u"media-playback-start"_s);
  EXPECT_TRUE(builtin.name().isEmpty());
  EXPECT_TRUE(HasSize(builtin, 48));

  // Back to the built-in set: the desktop's theme again.
  SetIconSet(u"akousma"_s);
  EXPECT_NE(u"Good"_s, QIcon::themeName());
#endif

}

TEST_F(IconLoaderTest, UserTheme) {

  const QString theme_dir = IconLoader::UserIconsDir() + u"/MyIcons"_s;
  ASSERT_TRUE(WriteFile(theme_dir + u"/index.theme"_s, "[Icon Theme]\nName=My Icons\nDirectories=22x22/actions\n\n[22x22/actions]\nSize=22\nType=Fixed\n"));
  ASSERT_TRUE(CopyFile(u":/icons/22x22/media-playback-start.png"_s, theme_dir + u"/22x22/actions/media-playback-start.png"_s));

  IconLoader::Init();
  const IconLoader::IconSet iconset = IconLoader::FindIconSet(u"theme:MyIcons"_s);
  EXPECT_EQ(IconLoader::IconSetType::UserTheme, iconset.type);
  EXPECT_EQ(u"My Icons"_s, iconset.name);

  SetIconSet(u"theme:MyIcons"_s);
  EXPECT_EQ(u"theme:MyIcons"_s, IconLoader::CurrentIconSet());
  EXPECT_EQ(u"media-playback-start"_s, IconLoader::Load(u"media-playback-start"_s).name());
  EXPECT_TRUE(IconLoader::Load(u"spotify"_s).name().isEmpty());
  EXPECT_FALSE(IconLoader::Load(u"spotify"_s).isNull());

  // The theme folder is not taken for the old flat custom icons folder.
  EXPECT_TRUE(HasSize(IconLoader::Load(u"media-playback-stop"_s), 48));

  QDir(IconLoader::UserIconsDir()).removeRecursively();

}

TEST_F(IconLoaderTest, BundledSet) {

  // A bundled set from the build, as in share/akousma/iconsets.
  const QString iconsets_dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/akousma/iconsets"_s;
  ASSERT_TRUE(QDir().mkpath(iconsets_dir));
  QFile::remove(iconsets_dir + u"/oxygen.rcc"_s);
  ASSERT_TRUE(QFile::copy(QStringLiteral(AKOUSMA_TEST_ICONSETS_DIR "/oxygen.rcc"), iconsets_dir + u"/oxygen.rcc"_s));

  IconLoader::Init();
  const IconLoader::IconSet iconset = IconLoader::FindIconSet(u"oxygen"_s);
  ASSERT_EQ(IconLoader::IconSetType::Bundled, iconset.type);
  EXPECT_EQ(u"Oxygen"_s, iconset.name);
  EXPECT_EQ(u"LGPL-3+"_s, iconset.licence);
  EXPECT_GT(iconset.icons, 70);
  EXPECT_LE(iconset.icons, IconLoader::ThemableIconCount());

  SetIconSet(u"oxygen"_s);
  EXPECT_EQ(u"oxygen"_s, IconLoader::CurrentIconSet());
  // Oxygen's own 16 pixel icon, which the built-in set doesn't have.
  EXPECT_TRUE(HasSize(IconLoader::Load(u"media-playback-start"_s), 16));
  EXPECT_TRUE(HasSize(IconLoader::Load(u"document-edit"_s), 16));
  // Logos always come from the built-in set.
  EXPECT_FALSE(HasSize(IconLoader::Load(u"spotify"_s), 16));
  EXPECT_FALSE(IconLoader::Load(u"spotify"_s).isNull());
  // A fixed size the set doesn't have: the built-in icon.
  EXPECT_FALSE(IconLoader::Load(u"media-playback-start"_s, true, 128).isNull());
  // Not chosen for icons that must stay Akousma's own.
  EXPECT_FALSE(HasSize(IconLoader::Load(u"media-playback-start"_s, false), 16));

  EXPECT_FALSE(IconLoader::IconSetPixmap(u"oxygen"_s, u"document-edit"_s, 20).isNull());
  EXPECT_FALSE(IconLoader::IconSetPixmap(u"akousma"_s, u"document-edit"_s, 20).isNull());

  QFile::remove(iconsets_dir + u"/oxygen.rcc"_s);

}
