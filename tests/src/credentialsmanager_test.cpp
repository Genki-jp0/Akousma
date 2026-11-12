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

#include <QCoreApplication>
#include <QString>
#include <QSignalSpy>

#include "core/settings.h"
#include "credentialsmanager/credentialsmanager.h"
#include "credentialsmanager/credentialsreply.h"
#include "credentialsmanager/credentialsresult.h"

using namespace Qt::Literals::StringLiterals;

namespace {

// The tests select the settings backend, so they don't touch a keyring and the result does not depend on the keyrings available on the machine.
class CredentialsManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Settings uses the organization name for the file name, the test main only sets the application name. The settings are in a temporary directory.
    QCoreApplication::setOrganizationName(u"Akousma"_s);
    Settings s;
    s.remove(u"Credentials"_s);
    s.remove(QLatin1String(CredentialsManager::kSettingsGroup));
    s.beginGroup(CredentialsManager::kSettingsGroup);
    s.setValue(CredentialsManager::kBackend, u"settings"_s);
    s.endGroup();
  }

  void TearDown() override {
    Settings s;
    s.remove(u"Credentials"_s);
    s.remove(QLatin1String(CredentialsManager::kSettingsGroup));
  }

  static CredentialsResult WaitForReply(CredentialsReplyPtr reply) {
    if (!reply->finished()) {
      QSignalSpy spy(&*reply, &CredentialsReply::Finished);
      EXPECT_TRUE(spy.wait(10000));
    }
    return reply->result();
  }
};

TEST_F(CredentialsManagerTest, InsecureBackendIsSignaled) {

  CredentialsManager credentials_manager;
  QSignalSpy spy_insecure(&credentials_manager, &CredentialsManager::InsecureBackend);

  EXPECT_FALSE(credentials_manager.insecure_backend());

  EXPECT_TRUE(WaitForReply(credentials_manager.SavePasswordAsync(u"test"_s, u"dummy-secret"_s)).success());

  EXPECT_EQ(1, spy_insecure.count());
  EXPECT_TRUE(credentials_manager.insecure_backend());
  EXPECT_TRUE(credentials_manager.settings_backend_selected());
  EXPECT_EQ(u"Settings"_s, credentials_manager.backend_name());

  // The backend is only initialized once.
  const CredentialsResult result = WaitForReply(credentials_manager.ReadPasswordAsync(u"test"_s));
  EXPECT_TRUE(result.success());
  EXPECT_EQ(u"dummy-secret"_s, result.password);
  EXPECT_EQ(1, spy_insecure.count());

}

TEST_F(CredentialsManagerTest, InsecureBackendNoticeIsGivenOnce) {

  EXPECT_TRUE(CredentialsManager::TakeInsecureBackendNotice());
  EXPECT_FALSE(CredentialsManager::TakeInsecureBackendNotice());

  // A new manager still signals the insecure backend, but the user was already told.
  CredentialsManager credentials_manager;
  QSignalSpy spy_insecure(&credentials_manager, &CredentialsManager::InsecureBackend);
  EXPECT_TRUE(WaitForReply(credentials_manager.ReadPasswordAsync(u"test"_s)).status == CredentialsResult::Status::NotFound);
  EXPECT_EQ(1, spy_insecure.count());
  EXPECT_FALSE(CredentialsManager::TakeInsecureBackendNotice());

  Settings s;
  s.beginGroup(CredentialsManager::kSettingsGroup);
  EXPECT_TRUE(s.value(CredentialsManager::kInsecureBackendNoticeShown).toBool());
  s.remove(CredentialsManager::kInsecureBackendNoticeShown);
  s.endGroup();

  EXPECT_TRUE(CredentialsManager::TakeInsecureBackendNotice());

}

}  // namespace
