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

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include "core/settingstransfer.h"

using namespace Qt::Literals::StringLiterals;

// SettingsTransfer::PlanImport() decides what a settings import writes, removes, refuses and holds back, without touching the settings.

namespace {

QStringList ChangedKeys(const QList<SettingsTransfer::ConnectionChange> &changes) {
  QStringList keys;
  for (const SettingsTransfer::ConnectionChange &change : changes) {
    keys << change.key;
  }
  return keys;
}

QStringList ChangeIds(const QList<SettingsTransfer::ConnectionChange> &changes) {
  QStringList ids;
  for (const SettingsTransfer::ConnectionChange &change : changes) {
    ids << change.id;
  }
  return ids;
}

}  // namespace

TEST(SettingsImportPlanTest, KnownGroups) {

  EXPECT_TRUE(SettingsTransfer::IsKnownSettingsGroup(u"Appearance/player_layout"_s));
  EXPECT_TRUE(SettingsTransfer::IsKnownSettingsGroup(u"Transcoder/lamemp3enc/bitrate"_s));
  EXPECT_TRUE(SettingsTransfer::IsKnownSettingsGroup(u"Jellyfin/url"_s));
  EXPECT_TRUE(SettingsTransfer::IsKnownSettingsGroup(u"SavedGroupings/1/name"_s));
  EXPECT_TRUE(SettingsTransfer::IsKnownSettingsGroup(u"SavedGroupings_Tidal/1/name"_s));
  EXPECT_FALSE(SettingsTransfer::IsKnownSettingsGroup(u"SavedGroupings_/1/name"_s));
  EXPECT_FALSE(SettingsTransfer::IsKnownSettingsGroup(u"Evil/url"_s));
  EXPECT_FALSE(SettingsTransfer::IsKnownSettingsGroup(u"appearance/player_layout"_s));
  EXPECT_FALSE(SettingsTransfer::IsKnownSettingsGroup(u"toplevel"_s));
  EXPECT_FALSE(SettingsTransfer::IsKnownSettingsGroup(u"/Appearance"_s));
  EXPECT_FALSE(SettingsTransfer::IsKnownSettingsGroup(u"Credentials/jellyfin"_s));
  EXPECT_FALSE(SettingsTransfer::IsKnownSettingsGroup(u"Import/pending_items"_s));

}

TEST(SettingsImportPlanTest, ConnectionKeys) {

  EXPECT_TRUE(SettingsTransfer::IsConnectionSettingKey(u"Jellyfin/url"_s));
  EXPECT_TRUE(SettingsTransfer::IsConnectionSettingKey(u"Jellyfin/username"_s));
  EXPECT_TRUE(SettingsTransfer::IsConnectionSettingKey(u"Jellyfin/verifycertificate"_s));
  // The spellings used since SettingsMigration renamed the keys.
  EXPECT_TRUE(SettingsTransfer::IsConnectionSettingKey(u"Jellyfin/verify_certificate"_s));
  EXPECT_TRUE(SettingsTransfer::IsConnectionSettingKey(u"Subsonic/verify_certificate"_s));
  EXPECT_TRUE(SettingsTransfer::IsConnectionSettingKey(u"Subsonic/auth_method"_s));
  EXPECT_TRUE(SettingsTransfer::IsConnectionSettingKey(u"Plex/verify_certificate"_s));
  EXPECT_TRUE(SettingsTransfer::IsConnectionSettingKey(u"Subsonic/url"_s));
  EXPECT_TRUE(SettingsTransfer::IsConnectionSettingKey(u"Subsonic/authmethod"_s));
  EXPECT_TRUE(SettingsTransfer::IsConnectionSettingKey(u"Plex/server_url"_s));
  EXPECT_TRUE(SettingsTransfer::IsConnectionSettingKey(u"Plex/server_machine_identifier"_s));
  EXPECT_TRUE(SettingsTransfer::IsConnectionSettingKey(u"NetworkProxy/hostname"_s));
  EXPECT_TRUE(SettingsTransfer::IsConnectionSettingKey(u"NetworkProxy/mode"_s));
  EXPECT_TRUE(SettingsTransfer::IsConnectionSettingKey(u"Backend/strict_ssl"_s));
  EXPECT_TRUE(SettingsTransfer::IsConnectionSettingKey(u"CredentialsManager/backend"_s));
  EXPECT_FALSE(SettingsTransfer::IsConnectionSettingKey(u"Jellyfin/enabled"_s));
  EXPECT_FALSE(SettingsTransfer::IsConnectionSettingKey(u"Appearance/player_layout"_s));
  EXPECT_FALSE(SettingsTransfer::IsConnectionSettingKey(u"Backend/bufferduration"_s));
  EXPECT_FALSE(SettingsTransfer::IsConnectionSettingKey(u"Backend/buffer_duration"_s));

}

// Groups this version doesn't know are refused, also settings outside any group. The export header is not a setting.
TEST(SettingsImportPlanTest, RefusesUnknownGroups) {

  const QVariantMap current { { u"Appearance/player_layout"_s, 2 } };
  const QVariantMap imported { { u"AkousmaSettingsExport/application"_s, u"Akousma"_s },
                               { u"Appearance/player_layout"_s, u"1"_s },
                               { u"Evil/url"_s, u"https://evil.example/"_s },
                               { u"Evil/other"_s, 1 },
                               { u"toplevel"_s, 1 } };

  const SettingsTransfer::ImportPlan plan = SettingsTransfer::PlanImport(current, imported);
  EXPECT_EQ(QStringList() << u"Appearance/player_layout"_s, plan.write.keys());
  EXPECT_EQ(u"1"_s, plan.write.value(u"Appearance/player_layout"_s).toString());
  QStringList refused_keys = plan.refused_keys;
  refused_keys.sort();
  EXPECT_EQ(QStringList() << u"Evil/other"_s << u"Evil/url"_s << u"toplevel"_s, refused_keys);
  EXPECT_EQ(QStringList() << u"Evil"_s << u"toplevel"_s, plan.refused_groups);
  EXPECT_TRUE(plan.connection_changes.isEmpty());

}

// Current settings in groups the import can't write are kept, they can't be replaced from the file.
TEST(SettingsImportPlanTest, KeepsCurrentUnknownGroups) {

  const QVariantMap current { { u"Appearance/player_layout"_s, 2 },
                              { u"Analyzer/type"_s, u"BlockAnalyzer"_s },
                              { u"SomeOldGroup/key"_s, 1 } };
  const QVariantMap imported { { u"Appearance/player_layout"_s, 1 } };

  const SettingsTransfer::ImportPlan plan = SettingsTransfer::PlanImport(current, imported);
  EXPECT_TRUE(plan.remove.contains(u"Analyzer/type"_s));
  EXPECT_TRUE(plan.remove.contains(u"Appearance/player_layout"_s));
  EXPECT_FALSE(plan.remove.contains(u"SomeOldGroup/key"_s));

}

// Secrets are never written by an import and never removed.
TEST(SettingsImportPlanTest, Secrets) {

  const QVariantMap current { { u"Subsonic/password"_s, u"current-password"_s },
                              { u"Credentials/jellyfin"_s, u"stored"_s },
                              { u"NetworkProxy/password"_s, u"proxy-password"_s },
                              { u"Import/pending_items"_s, u"settings"_s } };
  const QVariantMap imported { { u"Subsonic/password"_s, u"file-password"_s },
                               { u"Credentials/jellyfin"_s, u"file"_s },
                               { u"NetworkProxy/password"_s, u"file"_s },
                               { u"Import/pending_items"_s, u"collection"_s } };

  const SettingsTransfer::ImportPlan plan = SettingsTransfer::PlanImport(current, imported);
  EXPECT_TRUE(plan.write.isEmpty());
  EXPECT_TRUE(plan.remove.isEmpty());
  EXPECT_TRUE(plan.refused_keys.isEmpty());
  EXPECT_TRUE(plan.connection_changes.isEmpty());
  EXPECT_EQ(3, plan.secrets);

}

// A file that points Jellyfin at another server: the server address is held back until the user accepts it, the rest is imported.
TEST(SettingsImportPlanTest, HoldsBackServerChange) {

  const QVariantMap current { { u"Jellyfin/enabled"_s, false },
                              { u"Jellyfin/url"_s, u"https://home.example/"_s },
                              { u"Jellyfin/username"_s, u"genki"_s } };
  const QVariantMap imported { { u"Jellyfin/enabled"_s, u"true"_s },
                               { u"Jellyfin/url"_s, u"https://evil.example/"_s },
                               { u"Jellyfin/username"_s, u"genki"_s } };

  const SettingsTransfer::ImportPlan plan = SettingsTransfer::PlanImport(current, imported);
  ASSERT_EQ(1, plan.connection_changes.count());
  EXPECT_EQ(u"Jellyfin/url"_s, plan.connection_changes[0].key);
  EXPECT_EQ(u"https://home.example/"_s, plan.connection_changes[0].current.toString());
  EXPECT_EQ(u"https://evil.example/"_s, plan.connection_changes[0].imported.toString());
  EXPECT_FALSE(plan.connection_changes[0].id.isEmpty());
  EXPECT_EQ(QStringList() << u"Jellyfin/url"_s, ChangedKeys(plan.held_back));

  // Not written and not removed: the current server stays.
  EXPECT_FALSE(plan.write.contains(u"Jellyfin/url"_s));
  EXPECT_FALSE(plan.remove.contains(u"Jellyfin/url"_s));
  // The unchanged user name and the other settings are imported.
  EXPECT_TRUE(plan.write.contains(u"Jellyfin/username"_s));
  EXPECT_TRUE(plan.write.contains(u"Jellyfin/enabled"_s));

}

TEST(SettingsImportPlanTest, AcceptedChangeIsWritten) {

  const QVariantMap current { { u"Subsonic/url"_s, u"https://home.example/"_s },
                              { u"Subsonic/authmethod"_s, 1 } };
  const QVariantMap imported { { u"Subsonic/url"_s, u"https://new.example/"_s },
                               { u"Subsonic/authmethod"_s, u"0"_s } };

  const SettingsTransfer::ImportPlan asked = SettingsTransfer::PlanImport(current, imported);
  ASSERT_EQ(2, asked.connection_changes.count());
  EXPECT_EQ(2, asked.held_back.count());

  const SettingsTransfer::ImportPlan plan = SettingsTransfer::PlanImport(current, imported, ChangeIds(asked.connection_changes));
  EXPECT_EQ(2, plan.connection_changes.count());
  EXPECT_TRUE(plan.held_back.isEmpty());
  EXPECT_EQ(u"https://new.example/"_s, plan.write.value(u"Subsonic/url"_s).toString());
  EXPECT_EQ(u"0"_s, plan.write.value(u"Subsonic/authmethod"_s).toString());

  // Accepting one change only applies that one.
  QStringList only_url;
  for (const SettingsTransfer::ConnectionChange &change : asked.connection_changes) {
    if (change.key == u"Subsonic/url"_s) only_url << change.id;
  }
  const SettingsTransfer::ImportPlan partial = SettingsTransfer::PlanImport(current, imported, only_url);
  EXPECT_TRUE(partial.write.contains(u"Subsonic/url"_s));
  EXPECT_FALSE(partial.write.contains(u"Subsonic/authmethod"_s));
  EXPECT_EQ(QStringList() << u"Subsonic/authmethod"_s, ChangedKeys(partial.held_back));

}

// The acceptance is for the value the user saw: a file changed after it (the import is done at the next start) is held back again.
TEST(SettingsImportPlanTest, AcceptanceIsForTheValueShown) {

  const QVariantMap current { { u"Plex/server_url"_s, u"https://home.example:32400"_s } };
  const QVariantMap shown { { u"Plex/server_url"_s, u"https://new.example:32400"_s } };
  const QVariantMap changed { { u"Plex/server_url"_s, u"https://evil.example:32400"_s } };

  const QStringList accepted = ChangeIds(SettingsTransfer::PlanImport(current, shown).connection_changes);
  ASSERT_EQ(1, accepted.count());

  const SettingsTransfer::ImportPlan plan = SettingsTransfer::PlanImport(current, changed, accepted);
  EXPECT_EQ(QStringList() << u"Plex/server_url"_s, ChangedKeys(plan.held_back));
  EXPECT_FALSE(plan.write.contains(u"Plex/server_url"_s));

  // Not interchangeable between settings either.
  const QVariantMap other_key { { u"Jellyfin/url"_s, u"https://new.example:32400"_s } };
  EXPECT_EQ(1, SettingsTransfer::PlanImport(QVariantMap(), other_key, accepted).held_back.count());

}

// A file without a connection setting that is set now would remove it (back to the default): a change too.
TEST(SettingsImportPlanTest, RemovingAConnectionSettingIsAChange) {

  const QVariantMap current { { u"Backend/strict_ssl"_s, true },
                              { u"Backend/bufferduration"_s, 4000 } };
  const QVariantMap imported { { u"Appearance/player_layout"_s, 1 } };

  const SettingsTransfer::ImportPlan plan = SettingsTransfer::PlanImport(current, imported);
  ASSERT_EQ(1, plan.connection_changes.count());
  EXPECT_EQ(u"Backend/strict_ssl"_s, plan.connection_changes[0].key);
  EXPECT_FALSE(plan.connection_changes[0].imported.isValid());
  EXPECT_FALSE(plan.remove.contains(u"Backend/strict_ssl"_s));
  EXPECT_TRUE(plan.remove.contains(u"Backend/bufferduration"_s));

  const SettingsTransfer::ImportPlan accepted = SettingsTransfer::PlanImport(current, imported, ChangeIds(plan.connection_changes));
  EXPECT_TRUE(accepted.held_back.isEmpty());
  EXPECT_TRUE(accepted.remove.contains(u"Backend/strict_ssl"_s));

}

// Setting a server that isn't set now is a change too: a saved password can be there without a server (after a reset).
TEST(SettingsImportPlanTest, NewServerIsAChange) {

  const QVariantMap imported { { u"Jellyfin/url"_s, u"https://evil.example/"_s },
                               { u"Jellyfin/enabled"_s, u"true"_s } };

  const SettingsTransfer::ImportPlan plan = SettingsTransfer::PlanImport(QVariantMap(), imported);
  ASSERT_EQ(1, plan.held_back.count());
  EXPECT_FALSE(plan.held_back[0].current.isValid());
  EXPECT_FALSE(plan.write.contains(u"Jellyfin/url"_s));
  EXPECT_TRUE(plan.write.contains(u"Jellyfin/enabled"_s));

}

// The whole proxy group is connection settings, the proxy password stays a secret.
TEST(SettingsImportPlanTest, NetworkProxy) {

  const QVariantMap current { { u"NetworkProxy/mode"_s, 0 },
                              { u"NetworkProxy/password"_s, u"proxy-password"_s } };
  const QVariantMap imported { { u"NetworkProxy/mode"_s, u"2"_s },
                               { u"NetworkProxy/hostname"_s, u"proxy.evil.example"_s },
                               { u"NetworkProxy/port"_s, u"3128"_s },
                               { u"NetworkProxy/use_authentication"_s, u"true"_s } };

  const SettingsTransfer::ImportPlan plan = SettingsTransfer::PlanImport(current, imported);
  EXPECT_EQ(QStringList() << u"NetworkProxy/hostname"_s << u"NetworkProxy/mode"_s << u"NetworkProxy/port"_s << u"NetworkProxy/use_authentication"_s, ChangedKeys(plan.connection_changes));
  EXPECT_EQ(4, plan.held_back.count());
  EXPECT_TRUE(plan.write.isEmpty());
  EXPECT_TRUE(plan.remove.isEmpty());
  EXPECT_EQ(1, plan.secrets);

}

// The settings and an INI file give different types for the same value: not a change.
TEST(SettingsImportPlanTest, SameValueOtherType) {

  const QVariantMap current { { u"NetworkProxy/mode"_s, 2 },
                              { u"Jellyfin/verifycertificate"_s, true },
                              { u"Subsonic/username"_s, u"genki"_s } };
  const QVariantMap imported { { u"NetworkProxy/mode"_s, u"2"_s },
                               { u"Jellyfin/verifycertificate"_s, u"true"_s },
                               { u"Subsonic/username"_s, u"genki"_s } };

  const SettingsTransfer::ImportPlan plan = SettingsTransfer::PlanImport(current, imported);
  EXPECT_TRUE(plan.connection_changes.isEmpty());
  EXPECT_EQ(3, plan.write.count());

  // A changed certificate check is a change.
  QVariantMap no_check = imported;
  no_check.insert(u"Jellyfin/verifycertificate"_s, u"false"_s);
  EXPECT_EQ(QStringList() << u"Jellyfin/verifycertificate"_s, ChangedKeys(SettingsTransfer::PlanImport(current, no_check).held_back));

}
