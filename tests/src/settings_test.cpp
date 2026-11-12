/*
 * Strawberry Music Player
 * Copyright 2026, Jonas Kvinge <jonas@jkvinge.net>
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

#include "gtest_include.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <QCoreApplication>
#include <QFile>
#include <QFileDevice>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include "core/settings.h"

using namespace Qt::Literals::StringLiterals;

namespace {

constexpr QFileDevice::Permissions kGroupOtherPermissions = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
constexpr QFileDevice::Permissions kWorldReadablePermissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser | QFileDevice::WriteUser | QFileDevice::ReadGroup | QFileDevice::ReadOther;

class SettingsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Settings uses the organization name for the file name, the test main only sets the application name. The settings are in a temporary directory.
    QCoreApplication::setOrganizationName(u"Akousma"_s);
    // QSettings creates new files with the permissions of the umask, use the usual umask so the tests show that Settings does not depend on it.
    old_umask_ = ::umask(022);
    settings_filename_ = Settings().fileName();
    QFile::remove(settings_filename_);
  }

  void TearDown() override {
    QFile::remove(settings_filename_);
    ::umask(old_umask_);
  }

  static bool OwnerOnly(const QString &filename) {
    return QFile::exists(filename) && !(QFile::permissions(filename) & kGroupOtherPermissions);
  }

  mode_t old_umask_ = 0;
  QString settings_filename_;
};

TEST_F(SettingsTest, NewSettingsFileIsOwnerOnly) {

  ASSERT_FALSE(settings_filename_.isEmpty());
  ASSERT_FALSE(QFile::exists(settings_filename_));

  {
    Settings s;
    s.setValue(u"LastFM/username"_s, u"user"_s);
    s.sync();
    EXPECT_EQ(QSettings::NoError, s.status());
  }

  EXPECT_TRUE(OwnerOnly(settings_filename_));
  EXPECT_TRUE(QFile::permissions(settings_filename_).testFlag(QFileDevice::ReadOwner));
  EXPECT_TRUE(QFile::permissions(settings_filename_).testFlag(QFileDevice::WriteOwner));

  // Writing the file again keeps the permissions.
  {
    Settings s;
    s.setValue(u"LastFM/subscriber"_s, true);
    s.sync();
  }

  EXPECT_TRUE(OwnerOnly(settings_filename_));
  EXPECT_EQ(u"user"_s, Settings().value(u"LastFM/username"_s).toString());

}

TEST_F(SettingsTest, ExistingWorldReadableSettingsFileIsRestricted) {

  {
    Settings s;
    s.setValue(u"Proxy/username"_s, u"user"_s);
    s.sync();
  }
  ASSERT_TRUE(QFile::setPermissions(settings_filename_, kWorldReadablePermissions));
  ASSERT_FALSE(OwnerOnly(settings_filename_));

  {
    Settings s;
    EXPECT_TRUE(OwnerOnly(settings_filename_));
    EXPECT_EQ(u"user"_s, s.value(u"Proxy/username"_s).toString());
  }

  EXPECT_TRUE(OwnerOnly(settings_filename_));

}

// Files opened by file name, like the settings of another player read by the importer, are not changed.
TEST_F(SettingsTest, SettingsOpenedByFileNameAreNotChanged) {

  QTemporaryDir temp_dir;
  ASSERT_TRUE(temp_dir.isValid());
  const QString filename = temp_dir.filePath(u"other.conf"_s);

  {
    QFile file(filename);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("[General]\nkey=value\n");
    file.close();
  }
  ASSERT_TRUE(QFile::setPermissions(filename, kWorldReadablePermissions));

  {
    Settings s(filename, QSettings::IniFormat);
    EXPECT_EQ(u"value"_s, s.value(u"key"_s).toString());
  }

  EXPECT_TRUE(QFile::permissions(filename).testFlag(QFileDevice::ReadGroup));
  EXPECT_TRUE(QFile::permissions(filename).testFlag(QFileDevice::ReadOther));

}

}  // namespace
