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

#ifndef SETTINGS_H
#define SETTINGS_H

#include <QSettings>
#include <QString>

class Settings : public QSettings {
  Q_OBJECT

 public:
  explicit Settings(QObject *parent = nullptr);
  explicit Settings(const QSettings::Scope scope, QObject *parent = nullptr);
  explicit Settings(const QString &filename, const Format format, QObject *parent = nullptr);

 private:
  // On Unix, except macOS, creates the settings file readable and writable by the owner only (0600) and removes group and other permissions from an existing file.
  void RestrictFilePermissions();
};

#endif  // SETTINGS_H
