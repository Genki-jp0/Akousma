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

#include <QString>

#include "credentialsidentity.h"

using namespace Qt::Literals::StringLiterals;

CredentialsIdentity CredentialsIdentity::Akousma() {

  CredentialsIdentity identity;
  identity.secret_service_schema = u"io.github.genki_jp0.Akousma.Password"_s;
  identity.secret_service_application = u"akousma"_s;
  identity.label = u"Akousma"_s;
  identity.kwallet_app_id = u"Akousma"_s;
  identity.kwallet_folder = u"Akousma"_s;
  identity.name_prefix = u"Akousma/"_s;
  identity.wincred_user_name = u"akousma"_s;
  return identity;

}

CredentialsIdentity CredentialsIdentity::Strawberry() {

  // The names in Strawberry's credentials manager backends.
  CredentialsIdentity identity;
  identity.secret_service_schema = u"org.strawberrymusicplayer.Password"_s;
  identity.secret_service_application = u"strawberry"_s;
  identity.label = u"Strawberry"_s;
  identity.kwallet_app_id = u"Strawberry"_s;
  identity.kwallet_folder = u"Strawberry"_s;
  identity.name_prefix = u"Strawberry/"_s;
  identity.wincred_user_name = u"strawberry"_s;
  return identity;

}
