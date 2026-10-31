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

#ifndef PASSWORDCOPIER_H
#define PASSWORDCOPIER_H

#include <functional>

#include <QString>
#include <QStringList>

#include "includes/scoped_ptr.h"
#include "credentialsmanager/credentialsmanager.h"
#include "credentialsmanager/credentialsresult.h"

class CredentialsBackendInterface;
struct CredentialsIdentity;

// Copies passwords from another application's keyring entries to Akousma's, for the import from Strawberry and Clementine.
// The credentials backends are asynchronous, these functions wait for them in a local event loop, so they need a Q(Core)Application.
// Passwords are only passed from the reading to the saving function, they are never logged or kept.
class PasswordCopier {
 public:
  using ReadFunction = std::function<CredentialsResult(const QString &service)>;
  using SaveFunction = std::function<CredentialsResult(const QString &service, const QString &password)>;

  struct Result {
    QStringList copied;     // Services whose password was copied.
    QStringList not_found;  // Services without a stored password.
    QStringList failed;     // "service: error" for passwords that could not be read or saved.
  };

  // Reads the password of each service and saves the ones found.
  static Result Copy(const QStringList &services, const ReadFunction &read, const SaveFunction &save);

  // The secure backend another application with the given keyring names uses, chosen the way CredentialsManager chooses Akousma's:
  // the selected backend if it is available, otherwise the first available backend in the automatic order.
  // Returns nullptr when that application uses its settings file (the selected backend is "settings", or no keyring is available).
  static ScopedPtr<CredentialsBackendInterface> FindBackend(const CredentialsManager::BackendType selected_backend, const CredentialsIdentity &identity);

  // Blocking wrappers for the asynchronous backend and CredentialsManager requests.
  static bool CheckAvailable(CredentialsBackendInterface *backend);
  static CredentialsResult ReadPassword(CredentialsBackendInterface *backend, const QString &service);
  static CredentialsResult SavePassword(CredentialsManager *credentials_manager, const QString &service, const QString &password);
};

#endif  // PASSWORDCOPIER_H
