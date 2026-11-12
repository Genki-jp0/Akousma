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

#include <QByteArray>
#include <QVariant>

#include "core/settings.h"
#include "constants/backendsettings.h"
#include "streamtlsverification.h"

namespace StreamTls {

StreamTlsVerification FromVerifyCertificate(const bool verify_certificate) {

  return verify_certificate ? StreamTlsVerification::Verify : StreamTlsVerification::NoVerify;

}

bool StrictSSL(const bool global_strict_ssl, const StreamTlsVerification verification) {

  switch (verification) {
    case StreamTlsVerification::Verify:
      return true;
    case StreamTlsVerification::NoVerify:
      return false;
    case StreamTlsVerification::Default:
      break;
  }

  return global_strict_ssl;

}

StreamTlsVerification ForSourceUri(const QByteArray &source_uri, const QByteArray &current_uri, const StreamTlsVerification current_verification, const QByteArray &next_uri, const StreamTlsVerification next_verification) {

  // The next stream is checked first: while it is set up, the current stream is still the one playing.
  if (!source_uri.isEmpty()) {
    if (!next_uri.isEmpty() && source_uri == next_uri) {
      return next_verification;
    }
    if (!current_uri.isEmpty() && source_uri == current_uri) {
      return current_verification;
    }
  }

  // Never use an exception (NoVerify) for a source that can't be matched to the stream it was given for.
  if (current_verification == StreamTlsVerification::Verify || next_verification == StreamTlsVerification::Verify) {
    return StreamTlsVerification::Verify;
  }

  return StreamTlsVerification::Default;

}

bool GlobalStrictSSL() {

  Settings s;
  s.beginGroup(BackendSettings::kSettingsGroup);
  const bool strict_ssl = s.value(BackendSettings::kStrictSSL, BackendSettings::kDefaultStrictSSL).toBool();
  s.endGroup();

  return strict_ssl;

}

bool MigrateStrictSSLDefault() {

  Settings s;
  s.beginGroup(BackendSettings::kSettingsGroup);

  bool reset = false;
  if (!s.value(BackendSettings::kStrictSSLDefaultMigrated, false).toBool()) {
    if (s.contains(BackendSettings::kStrictSSL) && !s.value(BackendSettings::kStrictSSL).toBool()) {
      s.setValue(BackendSettings::kStrictSSL, true);
      reset = true;
    }
    s.setValue(BackendSettings::kStrictSSLDefaultMigrated, true);
  }

  s.endGroup();

  return reset;

}

}  // namespace StreamTls
