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

#ifndef CREDENTIALSIDENTITY_H
#define CREDENTIALSIDENTITY_H

#include <QString>

// The names an application uses for its entries in the system keyrings.
// Akousma uses its own names, the import from Strawberry reads the entries Strawberry made with Strawberry's names.
struct CredentialsIdentity {
  QString secret_service_schema;       // libsecret and GNOME Keyring (Secret Service): the xdg:schema attribute.
  QString secret_service_application;  // libsecret and GNOME Keyring: the "application" attribute.
  QString label;                       // Item label, "<label> <service> password".
  QString kwallet_app_id;              // KWallet: application ID passed to kwalletd.
  QString kwallet_folder;              // KWallet: folder holding the passwords.
  QString name_prefix;                 // Windows Credential Manager target name and macOS Keychain service prefix, for example "Akousma/".
  QString wincred_user_name;           // Windows Credential Manager: user name stored with the credential.

  QString ItemLabel(const QString &service) const { return QStringLiteral("%1 %2 password").arg(label, service); }

  // The names Akousma uses.
  static CredentialsIdentity Akousma();
  // The names Strawberry uses, for importing Strawberry's passwords.
  static CredentialsIdentity Strawberry();
};

#endif  // CREDENTIALSIDENTITY_H
