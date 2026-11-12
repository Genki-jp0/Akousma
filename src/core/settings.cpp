/*
 * Strawberry Music Player
 * Copyright 2024-2025, Jonas Kvinge <jonas@jkvinge.net>
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

#include <atomic>

#include <QtGlobal>
#include <QSettings>
#include <QString>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDevice>

#include "core/logging.h"
#include "settings.h"

using namespace Qt::Literals::StringLiterals;

Settings::Settings(QObject *parent)
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    : QSettings(QCoreApplication::organizationName().toLower(), QCoreApplication::applicationName().toLower(), parent) {
#else
    : QSettings(parent) {
#endif

  RestrictFilePermissions();

}

Settings::Settings(const QSettings::Scope scope, QObject *parent)
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    : QSettings(scope, QCoreApplication::organizationName().toLower(), QCoreApplication::applicationName().toLower(), parent) {
#else
    : QSettings(scope, parent) {
#endif

  if (scope == QSettings::UserScope) {
    RestrictFilePermissions();
  }

}

Settings::Settings(const QString &filename, const Format format, QObject *parent)
    : QSettings(filename, format, parent) {}

void Settings::RestrictFilePermissions() {

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)

  // The settings file holds login tokens, and the passwords when no keyring is available, so only the user may read it.
  // QSettings creates a new file with the permissions of the umask, usually 0644, but keeps the permissions of an existing file when it writes it again.
  // So the file is created with owner only permissions before QSettings creates it, and group and other permissions are removed from an existing file.
  // Only Akousma's own settings file in the user scope is changed, files opened by file name belong to someone else, like the settings of another player.

  const QString filename = fileName();
  if (filename.isEmpty()) return;

  static std::atomic<bool> warned(false);

  QFile file(filename);
  if (!file.exists()) {
    if (!QDir().mkpath(QFileInfo(filename).absolutePath())) return;
    // NewOnly fails when the file was created in the meantime, its permissions are fixed below.
    if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
      file.close();
      return;
    }
    if (!file.exists()) {
      if (!warned.exchange(true)) {
        qLog(Warning) << "Could not create settings file" << filename << file.errorString();
      }
      return;
    }
  }

  constexpr QFileDevice::Permissions kGroupOtherPermissions = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;

  const QFileDevice::Permissions permissions = file.permissions();
  if (!(permissions & kGroupOtherPermissions)) return;

  if (!file.setPermissions(permissions & ~kGroupOtherPermissions) && !warned.exchange(true)) {
    qLog(Warning) << "Could not restrict the permissions of settings file" << filename << "to the owner" << file.errorString();
  }

#endif

}
