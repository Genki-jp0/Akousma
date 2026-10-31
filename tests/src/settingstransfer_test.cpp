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
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QColor>
#include <QByteArray>

#include "core/settings.h"
#include "core/settingstransfer.h"
#include "settings/tabbarthemes.h"
#include "core/iconloader.h"
#include <QImage>

using namespace Qt::Literals::StringLiterals;

namespace {

class SettingsTransferTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Settings uses the organization name for the file name, the test main only sets the application name. The settings are in a
    // temporary directory, the backups and the pending marker in the test mode data directory.
    QCoreApplication::setOrganizationName(u"Akousma"_s);
    QStandardPaths::setTestModeEnabled(true);
    Settings s;
    s.clear();
    s.setValue(u"Appearance/player_layout"_s, 1);
    s.setValue(u"Appearance/icon_size_play_control_buttons"_s, 32);
    s.setValue(u"Appearance/tab_color"_s, QColor(10, 20, 30));
    s.setValue(u"MainWindow/geometry"_s, QByteArray("\x01\x02\x00\x03", 4));
    s.setValue(u"Subsonic/username"_s, u"genki"_s);
    s.setValue(u"Subsonic/password"_s, u"dummy-password"_s);
    s.setValue(u"Tidal/access_token"_s, u"dummy-token"_s);
    s.setValue(u"Credentials/subsonic"_s, u"dummy-stored"_s);
    s.setValue(u"SerializedSmartPlaylists/songs_version"_s, 3);
    s.sync();
  }
  void TearDown() override {
    Settings s;
    s.clear();
    s.sync();
  }
  QTemporaryDir dir_;
};

}  // namespace

TEST_F(SettingsTransferTest, ExportLeavesOutSecrets) {

  const QString filename = dir_.filePath(u"export.ini"_s);
  const SettingsTransfer::Result result = SettingsTransfer::Export(filename);
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_EQ(3, result.secrets);

  QSettings in(filename, QSettings::IniFormat);
  EXPECT_EQ(u"Akousma"_s, in.value(u"AkousmaSettingsExport/application"_s).toString());
  EXPECT_FALSE(in.value(u"AkousmaSettingsExport/version"_s).toString().isEmpty());
  EXPECT_EQ(1, in.value(u"Appearance/player_layout"_s).toInt());
  EXPECT_EQ(u"genki"_s, in.value(u"Subsonic/username"_s).toString());
  EXPECT_FALSE(in.contains(u"Subsonic/password"_s));
  EXPECT_FALSE(in.contains(u"Tidal/access_token"_s));
  EXPECT_FALSE(in.contains(u"Credentials/subsonic"_s));

  QFile file(filename);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_FALSE(file.readAll().contains("dummy"));

}

TEST_F(SettingsTransferTest, RejectsOtherFiles) {

  const QString filename = dir_.filePath(u"other.ini"_s);
  {
    QSettings other(filename, QSettings::IniFormat);
    other.setValue(u"General/foo"_s, 1);
  }
  QString version;
  QString error;
  EXPECT_FALSE(SettingsTransfer::CheckExportFile(filename, &version, &error));
  EXPECT_FALSE(error.isEmpty());
  EXPECT_FALSE(SettingsTransfer::CheckExportFile(dir_.filePath(u"missing.ini"_s), &version, &error));

  const SettingsTransfer::Result result = SettingsTransfer::Import(filename);
  EXPECT_FALSE(result.success);
  EXPECT_EQ(1, Settings().value(u"Appearance/player_layout"_s).toInt());

}

TEST_F(SettingsTransferTest, ResetKeepsSecretsAndSmartPlaylists) {

  const SettingsTransfer::Result result = SettingsTransfer::Reset();
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_TRUE(QFileInfo(result.backup_dir + u"/settings.ini"_s).isFile());

  Settings s;
  EXPECT_FALSE(s.contains(u"Appearance/player_layout"_s));
  EXPECT_FALSE(s.contains(u"MainWindow/geometry"_s));
  EXPECT_FALSE(s.contains(u"Subsonic/username"_s));
  EXPECT_EQ(u"dummy-password"_s, s.value(u"Subsonic/password"_s).toString());
  EXPECT_EQ(u"dummy-token"_s, s.value(u"Tidal/access_token"_s).toString());
  EXPECT_EQ(u"dummy-stored"_s, s.value(u"Credentials/subsonic"_s).toString());
  EXPECT_EQ(3, s.value(u"SerializedSmartPlaylists/songs_version"_s).toInt());

  QSettings backup(result.backup_dir + u"/settings.ini"_s, QSettings::IniFormat);
  EXPECT_EQ(1, backup.value(u"Appearance/player_layout"_s).toInt());

}

TEST_F(SettingsTransferTest, ExportResetImportRoundTrip) {

  const QString filename = dir_.filePath(u"roundtrip.ini"_s);
  ASSERT_TRUE(SettingsTransfer::Export(filename).success);
  ASSERT_TRUE(SettingsTransfer::Reset().success);

  {
    Settings s;
    s.setValue(u"Appearance/player_layout"_s, 2);
    s.setValue(u"Analyzer/type"_s, u"BlockAnalyzer"_s);
  }

  const SettingsTransfer::Result result = SettingsTransfer::Import(filename);
  ASSERT_TRUE(result.success) << result.error.toStdString();
  EXPECT_FALSE(result.version.isEmpty());

  Settings s;
  EXPECT_EQ(1, s.value(u"Appearance/player_layout"_s).toInt());
  EXPECT_EQ(32, s.value(u"Appearance/icon_size_play_control_buttons"_s).toInt());
  EXPECT_EQ(QColor(10, 20, 30), s.value(u"Appearance/tab_color"_s).value<QColor>());
  EXPECT_EQ(QByteArray("\x01\x02\x00\x03", 4), s.value(u"MainWindow/geometry"_s).toByteArray());
  EXPECT_EQ(u"genki"_s, s.value(u"Subsonic/username"_s).toString());
  // Not in the export: set after it, removed by the import.
  EXPECT_FALSE(s.contains(u"Analyzer/type"_s));
  // Kept, the export has no secrets.
  EXPECT_EQ(u"dummy-password"_s, s.value(u"Subsonic/password"_s).toString());
  EXPECT_EQ(u"dummy-token"_s, s.value(u"Tidal/access_token"_s).toString());
  EXPECT_FALSE(s.contains(u"AkousmaSettingsExport/application"_s));

}

TEST_F(SettingsTransferTest, PendingAction) {

  EXPECT_EQ(SettingsTransfer::Action::None, SettingsTransfer::PendingAction());
  SettingsTransfer::SetPendingAction(SettingsTransfer::Action::Import, u"/some/file.ini"_s);
  QString filename;
  EXPECT_EQ(SettingsTransfer::Action::Import, SettingsTransfer::PendingAction(&filename));
  EXPECT_EQ(u"/some/file.ini"_s, filename);
  SettingsTransfer::SetPendingAction(SettingsTransfer::Action::Reset);
  EXPECT_EQ(SettingsTransfer::Action::Reset, SettingsTransfer::PendingAction());
  SettingsTransfer::SetPendingAction(SettingsTransfer::Action::None);
  EXPECT_EQ(SettingsTransfer::Action::None, SettingsTransfer::PendingAction());

}

// The icon set goes into an export, a reset goes back to the built-in icons and an import brings the set back.
TEST_F(SettingsTransferTest, IconSet) {

  {
    Settings s;
    s.setValue(u"Appearance/icon_set"_s, u"breeze"_s);
    s.sync();
  }
  EXPECT_EQ(u"breeze"_s, IconLoader::IconSetFromSettings());

  const QString filename = dir_.filePath(u"iconset.ini"_s);
  ASSERT_TRUE(SettingsTransfer::Export(filename).success);
  EXPECT_EQ(u"breeze"_s, QSettings(filename, QSettings::IniFormat).value(u"Appearance/icon_set"_s).toString());

  ASSERT_TRUE(SettingsTransfer::Reset().success);
  EXPECT_EQ(u"akousma"_s, IconLoader::IconSetFromSettings());

  ASSERT_TRUE(SettingsTransfer::Import(filename).success);
  EXPECT_EQ(u"breeze"_s, IconLoader::IconSetFromSettings());

}

// The album cover sidebar background was removed: an export that still has it is imported as the plain gradient.
TEST_F(SettingsTransferTest, ImportMapsRemovedAlbumCoverBackground) {

  const QString filename = dir_.filePath(u"albumcover.ini"_s);
  ASSERT_TRUE(SettingsTransfer::Export(filename).success);
  {
    QSettings file(filename, QSettings::IniFormat);
    file.setValue(u"Appearance/tab_background"_s, 3);
  }
  ASSERT_TRUE(SettingsTransfer::Import(filename).success);
  EXPECT_EQ(1, Settings().value(u"Appearance/tab_background"_s).toInt());

}

// Saved sidebar themes: stored by name, with a copy of the image; kept by a reset; exported with the image and imported again.
TEST_F(SettingsTransferTest, TabBarThemes) {

  EXPECT_TRUE(TabBarThemes::IsBuiltInName(u" plain gradient "_s));
  EXPECT_TRUE(TabBarThemes::IsBuiltInName(u"Custom image"_s));
  EXPECT_FALSE(TabBarThemes::IsBuiltInName(u"Night"_s));

  const QString original = dir_.filePath(u"night.png"_s);
  QImage image(20, 40, QImage::Format_RGB32);
  image.fill(QColor(10, 20, 200));
  ASSERT_TRUE(image.save(original));

  TabBarThemes::Theme theme;
  theme.name = u"Night"_s;
  theme.background = 2;
  theme.system_color = false;
  theme.color = QColor(1, 2, 3);
  theme.image = TabBarThemes::CopyImage(theme.name, original);
  ASSERT_NE(original, theme.image);
  EXPECT_TRUE(theme.image.startsWith(TabBarThemes::ImageDir()));
  EXPECT_TRUE(QFileInfo::exists(theme.image));
  TabBarThemes::Theme theme2;
  theme2.name = u"Day"_s;
  TabBarThemes::Save(QList<TabBarThemes::Theme>() << theme << theme2);

  QList<TabBarThemes::Theme> themes = TabBarThemes::Load();
  ASSERT_EQ(2, themes.count());
  EXPECT_EQ(0, TabBarThemes::IndexOf(themes, u"night"_s));
  EXPECT_EQ(1, TabBarThemes::IndexOf(themes, u"Day"_s));
  EXPECT_EQ(-1, TabBarThemes::IndexOf(themes, u"Dusk"_s));
  EXPECT_EQ(QColor(1, 2, 3), themes[0].color);
  EXPECT_EQ(2, themes[0].background);

  // Kept by a reset.
  ASSERT_TRUE(SettingsTransfer::Reset().success);
  EXPECT_EQ(2, TabBarThemes::Load().count());

  // The export carries the image, the import writes it back when the copy is gone.
  const QString filename = dir_.filePath(u"themes.ini"_s);
  ASSERT_TRUE(SettingsTransfer::Export(filename).success);
  QSettings exported(filename, QSettings::IniFormat);
  EXPECT_FALSE(exported.value(u"TabBarThemes/themes/1/image_data"_s).toByteArray().isEmpty());
  TabBarThemes::RemoveImage(themes[0].image);
  EXPECT_FALSE(QFileInfo::exists(themes[0].image));
  TabBarThemes::Save(QList<TabBarThemes::Theme>());
  ASSERT_TRUE(SettingsTransfer::Import(filename).success);
  themes = TabBarThemes::Load();
  ASSERT_EQ(2, themes.count());
  EXPECT_TRUE(QFileInfo::exists(themes[0].image));
  EXPECT_EQ(QColor(10, 20, 200), QImage(themes[0].image).pixelColor(5, 5));
  EXPECT_FALSE(Settings().contains(u"TabBarThemes/themes/1/image_data"_s));

  TabBarThemes::RemoveImage(themes[0].image);
  TabBarThemes::Save(QList<TabBarThemes::Theme>());

}
