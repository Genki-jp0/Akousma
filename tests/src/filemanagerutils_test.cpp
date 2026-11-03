/*
 * Akousma
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

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>

using namespace Qt::Literals::StringLiterals;

namespace Utilities {
// Defined in utilities/filemanagerutils.cpp, not declared in its header.
void OpenInFileManager(const QString &path, const QUrl &url);
}  // namespace Utilities

namespace {

class EnvironmentOverride {
 public:
  EnvironmentOverride(const char *name, const QByteArray &value) : name_(name), had_value_(qEnvironmentVariableIsSet(name)), old_value_(qgetenv(name)) {
    qputenv(name, value);
  }
  ~EnvironmentOverride() {
    if (had_value_) qputenv(name_, old_value_);
    else qunsetenv(name_);
  }
  Q_DISABLE_COPY_MOVE(EnvironmentOverride)

 private:
  const char *name_;
  const bool had_value_;
  const QByteArray old_value_;
};

bool WriteFile(const QString &filename, const QByteArray &data, const bool executable = false) {

  QFile file(filename);
  if (!file.open(QIODevice::WriteOnly)) return false;
  if (file.write(data) != data.size()) return false;
  file.close();
  if (executable) {
    return file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
  }
  return true;

}

// Regression test: the Exec line of the default file manager's desktop file was split into the command and its parameters,
// and the first part was taken without checking that there was one, so an Exec line with only field codes like %U crashed.
TEST(FileManagerUtilsTest, ExecLineWithOnlyFieldCodesDoesNotCrash) {

  QTemporaryDir temp_dir;
  ASSERT_TRUE(temp_dir.isValid());
  const QString bin_dir = temp_dir.filePath(u"bin"_s);
  const QString data_dir = temp_dir.filePath(u"share"_s);
  ASSERT_TRUE(QDir().mkpath(bin_dir));
  ASSERT_TRUE(QDir().mkpath(data_dir + u"/applications"_s));

  // xdg-mime names the default file manager, xdg-open is what the fallback runs, it does nothing here.
  ASSERT_TRUE(WriteFile(bin_dir + u"/xdg-mime"_s, "#!/bin/sh\necho akousma-test-filemanager.desktop\n", true));
  ASSERT_TRUE(WriteFile(bin_dir + u"/xdg-open"_s, "#!/bin/sh\nexit 0\n", true));
  ASSERT_TRUE(WriteFile(data_dir + u"/applications/akousma-test-filemanager.desktop"_s, "[Desktop Entry]\nType=Application\nName=Test\nExec=%U\n"));

  const EnvironmentOverride path_override("PATH", QFile::encodeName(bin_dir) + ':' + qgetenv("PATH"));
  const EnvironmentOverride data_home_override("XDG_DATA_HOME", QFile::encodeName(data_dir));
  const EnvironmentOverride data_dirs_override("XDG_DATA_DIRS", QFile::encodeName(data_dir));

  const QString music_file = temp_dir.filePath(u"song.mp3"_s);
  ASSERT_TRUE(WriteFile(music_file, QByteArray()));

  Utilities::OpenInFileManager(temp_dir.path(), QUrl::fromLocalFile(music_file));

}

}  // namespace
