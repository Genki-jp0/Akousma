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

#ifndef CREDENTIALSBACKENDLIBSECRET_H
#define CREDENTIALSBACKENDLIBSECRET_H

#include <memory>

#include <libsecret/secret.h>

#include <QObject>
#include <QByteArray>
#include <QString>

#include "credentialsbackendinterface.h"
#include "credentialsidentity.h"

struct _GCancellable;
struct CredentialsBackendLibSecretGuard;

// Stores passwords in the Secret Service (GNOME Keyring, KeePassXC, etc.) using the asynchronous libsecret API.
// libsecret calls the callbacks in the thread iterating the default GLib main context, the results are passed back to the thread of this object.
class CredentialsBackendLibSecret : public CredentialsBackendInterface {
  Q_OBJECT

 public:
  explicit CredentialsBackendLibSecret(const CredentialsIdentity &identity = CredentialsIdentity::Akousma(), QObject *parent = nullptr);
  ~CredentialsBackendLibSecret() override;

  QString name() const override;
  void CheckAvailable(const AvailableCallback &available_callback) override;
  void ReadPassword(const QString &service, const ResultCallback &callback) override;
  void SavePassword(const QString &service, const QString &password, const ResultCallback &callback) override;
  void DeletePassword(const QString &service, const ResultCallback &callback) override;

 private:
  const CredentialsIdentity identity_;
  const QByteArray schema_name_;
  const QByteArray application_;
  SecretSchema schema_;
  std::shared_ptr<CredentialsBackendLibSecretGuard> guard_;
  _GCancellable *cancellable_;
};

#endif  // CREDENTIALSBACKENDLIBSECRET_H
