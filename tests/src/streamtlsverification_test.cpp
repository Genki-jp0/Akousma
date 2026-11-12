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

#include <QByteArray>
#include <QString>

#include "core/settings.h"
#include "core/urlhandler.h"
#include "core/streamtlsverification.h"
#include "constants/backendsettings.h"

using namespace Qt::Literals::StringLiterals;

namespace {

// A URL handler which does not override tls_verification(), like the handlers for services without a "Verify server certificate" setting.
class PlainUrlHandler : public UrlHandler {
 public:
  QString scheme() const override { return u"plain"_s; }
};

void SetStoredStrictSSL(const bool strict_ssl) {
  Settings s;
  s.beginGroup(BackendSettings::kSettingsGroup);
  s.setValue(BackendSettings::kStrictSSL, strict_ssl);
  s.endGroup();
}

void RemoveStoredStrictSSL() {
  Settings s;
  s.beginGroup(BackendSettings::kSettingsGroup);
  s.remove(BackendSettings::kStrictSSL);
  s.endGroup();
}

TEST(StreamTlsVerificationTest, StrictSSLIsTheDefault) {

  EXPECT_TRUE(BackendSettings::kDefaultStrictSSL);

  RemoveStoredStrictSSL();
  EXPECT_TRUE(StreamTls::GlobalStrictSSL());

}

TEST(StreamTlsVerificationTest, GlobalStrictSSLReadsTheSetting) {

  SetStoredStrictSSL(false);
  EXPECT_FALSE(StreamTls::GlobalStrictSSL());

  SetStoredStrictSSL(true);
  EXPECT_TRUE(StreamTls::GlobalStrictSSL());

  RemoveStoredStrictSSL();

}

void RemoveStrictSSLDefaultMigrated() {
  Settings s;
  s.beginGroup(BackendSettings::kSettingsGroup);
  s.remove(BackendSettings::kStrictSSLDefaultMigrated);
  s.endGroup();
}

bool StrictSSLDefaultMigrated() {
  Settings s;
  s.beginGroup(BackendSettings::kSettingsGroup);
  const bool migrated = s.value(BackendSettings::kStrictSSLDefaultMigrated, false).toBool();
  s.endGroup();
  return migrated;
}

bool StoredStrictSSLContained() {
  Settings s;
  s.beginGroup(BackendSettings::kSettingsGroup);
  const bool contains = s.contains(BackendSettings::kStrictSSL);
  s.endGroup();
  return contains;
}

TEST(StreamTlsVerificationTest, MigrateStrictSSLDefaultResetsStoredFalseOnce) {

  RemoveStrictSSLDefaultMigrated();
  SetStoredStrictSSL(false);

  EXPECT_TRUE(StreamTls::MigrateStrictSSLDefault());
  EXPECT_TRUE(StreamTls::GlobalStrictSSL());
  EXPECT_TRUE(StrictSSLDefaultMigrated());

  // The user turns it off again, which is kept.
  SetStoredStrictSSL(false);
  EXPECT_FALSE(StreamTls::MigrateStrictSSLDefault());
  EXPECT_FALSE(StreamTls::GlobalStrictSSL());

  RemoveStrictSSLDefaultMigrated();
  RemoveStoredStrictSSL();

}

TEST(StreamTlsVerificationTest, MigrateStrictSSLDefaultKeepsOtherValues) {

  // Already on.
  RemoveStrictSSLDefaultMigrated();
  SetStoredStrictSSL(true);
  EXPECT_FALSE(StreamTls::MigrateStrictSSLDefault());
  EXPECT_TRUE(StreamTls::GlobalStrictSSL());
  EXPECT_TRUE(StrictSSLDefaultMigrated());

  // Not stored, the default applies, and nothing is stored for it.
  RemoveStrictSSLDefaultMigrated();
  RemoveStoredStrictSSL();
  EXPECT_FALSE(StreamTls::MigrateStrictSSLDefault());
  EXPECT_FALSE(StoredStrictSSLContained());
  EXPECT_TRUE(StreamTls::GlobalStrictSSL());
  EXPECT_TRUE(StrictSSLDefaultMigrated());

  RemoveStrictSSLDefaultMigrated();

}

TEST(StreamTlsVerificationTest, StrictSSL) {

  // Default follows the global setting.
  EXPECT_TRUE(StreamTls::StrictSSL(true, StreamTlsVerification::Default));
  EXPECT_FALSE(StreamTls::StrictSSL(false, StreamTlsVerification::Default));

  // The server's own setting wins over the global setting, in both directions.
  EXPECT_TRUE(StreamTls::StrictSSL(false, StreamTlsVerification::Verify));
  EXPECT_TRUE(StreamTls::StrictSSL(true, StreamTlsVerification::Verify));
  EXPECT_FALSE(StreamTls::StrictSSL(true, StreamTlsVerification::NoVerify));
  EXPECT_FALSE(StreamTls::StrictSSL(false, StreamTlsVerification::NoVerify));

}

TEST(StreamTlsVerificationTest, FromVerifyCertificate) {

  EXPECT_EQ(StreamTlsVerification::Verify, StreamTls::FromVerifyCertificate(true));
  EXPECT_EQ(StreamTlsVerification::NoVerify, StreamTls::FromVerifyCertificate(false));

}

TEST(StreamTlsVerificationTest, ForSourceUriMatchesCurrentAndNextStream) {

  const QByteArray current_uri = "https://subsonic.example.test/rest/stream?id=1";
  const QByteArray next_uri = "https://radio.example.test/stream";

  EXPECT_EQ(StreamTlsVerification::NoVerify, StreamTls::ForSourceUri(current_uri, current_uri, StreamTlsVerification::NoVerify, next_uri, StreamTlsVerification::Default));
  EXPECT_EQ(StreamTlsVerification::Default, StreamTls::ForSourceUri(next_uri, current_uri, StreamTlsVerification::NoVerify, next_uri, StreamTlsVerification::Default));
  EXPECT_EQ(StreamTlsVerification::Verify, StreamTls::ForSourceUri(next_uri, current_uri, StreamTlsVerification::NoVerify, next_uri, StreamTlsVerification::Verify));

  // Without a next stream.
  EXPECT_EQ(StreamTlsVerification::NoVerify, StreamTls::ForSourceUri(current_uri, current_uri, StreamTlsVerification::NoVerify, QByteArray(), StreamTlsVerification::Default));

  // The same URI as current and next stream, like the next segment of a CUE sheet or a track that repeats, uses the next stream's.
  EXPECT_EQ(StreamTlsVerification::Verify, StreamTls::ForSourceUri(current_uri, current_uri, StreamTlsVerification::NoVerify, current_uri, StreamTlsVerification::Verify));

}

TEST(StreamTlsVerificationTest, ForSourceUriNeverGivesAnExceptionToAnUnknownSource) {

  const QByteArray current_uri = "https://subsonic.example.test/rest/stream?id=1";
  const QByteArray next_uri = "https://jellyfin.example.test/Audio/2/stream";
  const QByteArray other_uri = "https://other.example.test/stream";

  EXPECT_EQ(StreamTlsVerification::Default, StreamTls::ForSourceUri(other_uri, current_uri, StreamTlsVerification::NoVerify, next_uri, StreamTlsVerification::NoVerify));
  EXPECT_EQ(StreamTlsVerification::Default, StreamTls::ForSourceUri(QByteArray(), current_uri, StreamTlsVerification::NoVerify, QByteArray(), StreamTlsVerification::Default));
  EXPECT_EQ(StreamTlsVerification::Default, StreamTls::ForSourceUri(other_uri, QByteArray(), StreamTlsVerification::Default, QByteArray(), StreamTlsVerification::Default));

  // When a stream needs verification, an unknown source is verified, also when the global setting is off.
  EXPECT_EQ(StreamTlsVerification::Verify, StreamTls::ForSourceUri(other_uri, current_uri, StreamTlsVerification::Verify, next_uri, StreamTlsVerification::NoVerify));
  EXPECT_EQ(StreamTlsVerification::Verify, StreamTls::ForSourceUri(other_uri, current_uri, StreamTlsVerification::NoVerify, next_uri, StreamTlsVerification::Verify));

  // An empty source URI does not match an empty stream URI.
  EXPECT_EQ(StreamTlsVerification::Default, StreamTls::ForSourceUri(QByteArray(), QByteArray(), StreamTlsVerification::NoVerify, QByteArray(), StreamTlsVerification::NoVerify));

}

TEST(StreamTlsVerificationTest, UrlHandlerDefaultFollowsGlobalSetting) {

  PlainUrlHandler url_handler;
  EXPECT_EQ(StreamTlsVerification::Default, url_handler.tls_verification());

}

}  // namespace
