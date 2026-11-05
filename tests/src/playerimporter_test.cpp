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

#include <functional>

#include "gtest_include.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QMap>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include "includes/shared_ptr.h"
#include "constants/timeconstants.h"
#include "core/settings.h"
#include "core/song.h"
#include "core/database.h"
#include "core/memorydatabase.h"
#include "collection/collectionbackend.h"
#include "collection/collectionmanager.h"
#include "credentialsmanager/credentialsbackendinterface.h"
#include "credentialsmanager/credentialsidentity.h"
#include "credentialsmanager/credentialsmanager.h"
#include "credentialsmanager/credentialsresult.h"
#include "smartplaylists/playlistquerygenerator.h"
#include "smartplaylists/smartplaylistsearch.h"
#include "smartplaylists/smartplaylistsearchterm.h"
#include "import/clementinesmartplaylist.h"
#include "import/passwordcopier.h"
#include "import/playerimporter.h"

using namespace Qt::Literals::StringLiterals;
using std::make_shared;

// clazy:excludeall=non-pod-global-static,returning-void-expression

namespace {

using Field = SmartPlaylistSearchTerm::Field;
using Operator = SmartPlaylistSearchTerm::Operator;
using SearchType = SmartPlaylistSearch::SearchType;
using SortType = SmartPlaylistSearch::SortType;

// QueryGenerator::Save() of Clementine's default smart playlists and of a few made in Clementine's wizard.
// Written by a Qt 5 program with Clementine's own Search / SearchTerm / QueryGenerator serialization code, see CHANGES.txt.
struct ClementineBlob {
  const char *name;
  const char *hex;
};
constexpr ClementineBlob kClementineBlobs[] = {
  { "50 random tracks", "000000000000000000320200" },
  { "Ever played", "0000000112040000000200000000000000000001000000000000140000" },
  { "Never played", "0000000112060000000200000000000000000001000000000000140000" },
  { "Last played", "000000000214000000140200" },
  { "Most played", "000000000212000000140200" },
  { "Favourite tracks", "000000000211000000140200" },
  { "Newest tracks", "00000000020e000000140200" },
  { "All tracks", "000000000101ffffffff0200" },
  { "Least favourite tracks", "00000002100500000006003fe333333333333300000000010013040000000200000000040000000001000213000000140100" },
  { "Dynamic random mix", "000000000000000000140201" },
  { "Added in the last 30 days", "000000010e0700000002000000001e000000000101020effffffff0000" },
  { "Rock I like", "0000000309000000000a00000000080052006f0063006b0000000001001104000000020000000032000000000100100400000006003fe00000000000000000000001000211000000280000" },
  { "Long ones, not live", "0000000505040000000200000001a400000000010000010000000a0000000008004c00690076006500000000010016020000000a000000001e002f006d0075007300690063002f0041007200740069007300740020004100000000010008000000000a0000000004003100390000000001001408000000020000000002000000020000000006020102ffffffff0000" },
  { "Top score only", "0000000111040000000200000000500000000001000000000000140001" },
};

QByteArray ClementineData(const QString &name) {
  for (const ClementineBlob &blob : kClementineBlobs) {
    if (QLatin1String(blob.name) == name) return QByteArray::fromHex(blob.hex);
  }
  return QByteArray();
}

SmartPlaylistSearch LoadSearch(const QByteArray &data, bool *dynamic = nullptr) {
  PlaylistQueryGenerator generator;
  generator.Load(data);
  if (dynamic) *dynamic = generator.is_dynamic();
  return generator.search();
}

TEST(ClementineSmartPlaylistTest, MapsEveryClementineFieldButScore) {

  for (int field = 0; field < static_cast<int>(ClementineSmartPlaylist::ClementineField::FieldCount); ++field) {
    const std::optional<Field> mapped = ClementineSmartPlaylist::MapField(field);
    if (field == static_cast<int>(ClementineSmartPlaylist::ClementineField::Score)) {
      EXPECT_FALSE(mapped.has_value());
    }
    else {
      EXPECT_TRUE(mapped.has_value()) << field;
    }
  }
  EXPECT_EQ(Field::Title, ClementineSmartPlaylist::MapField(0));
  EXPECT_EQ(Field::AlbumArtist, ClementineSmartPlaylist::MapField(3));
  EXPECT_EQ(Field::Length, ClementineSmartPlaylist::MapField(5));
  EXPECT_EQ(Field::Rating, ClementineSmartPlaylist::MapField(16));
  EXPECT_EQ(Field::PlayCount, ClementineSmartPlaylist::MapField(18));
  EXPECT_EQ(Field::Filepath, ClementineSmartPlaylist::MapField(22));
  EXPECT_EQ(Field::OriginalYear, ClementineSmartPlaylist::MapField(25));
  EXPECT_FALSE(ClementineSmartPlaylist::MapField(26).has_value());

  EXPECT_EQ(static_cast<int>(PlaylistGenerator::Type::Query), ClementineSmartPlaylist::GeneratorType(u"Query"_s));
  EXPECT_FALSE(ClementineSmartPlaylist::GeneratorType(u"Jamendo"_s).has_value());

}

TEST(ClementineSmartPlaylistTest, ConvertsAllDefaultSmartPlaylists) {

  for (const char *name : { "50 random tracks", "Ever played", "Never played", "Last played", "Most played", "Favourite tracks", "Newest tracks", "All tracks", "Least favourite tracks", "Dynamic random mix", "Added in the last 30 days" }) {
    const ClementineSmartPlaylist::Result result = ClementineSmartPlaylist::Convert(QLatin1String(name), ClementineData(QLatin1String(name)));
    EXPECT_TRUE(result.success) << name << result.error.toStdString();
    EXPECT_TRUE(LoadSearch(result.data).is_valid()) << name;
  }

  EXPECT_EQ(SmartPlaylistSearch(SearchType::All, SmartPlaylistSearch::TermList(), SortType::Random, Field::Title, 50), LoadSearch(ClementineSmartPlaylist::Convert(u"50 random tracks"_s, ClementineData(u"50 random tracks"_s)).data));
  EXPECT_EQ(SmartPlaylistSearch(SearchType::And, SmartPlaylistSearch::TermList() << SmartPlaylistSearchTerm(Field::PlayCount, Operator::GreaterThan, 0), SortType::Random, Field::Title), LoadSearch(ClementineSmartPlaylist::Convert(u"Ever played"_s, ClementineData(u"Ever played"_s)).data));
  EXPECT_EQ(SmartPlaylistSearch(SearchType::All, SmartPlaylistSearch::TermList(), SortType::FieldDesc, Field::LastPlayed), LoadSearch(ClementineSmartPlaylist::Convert(u"Last played"_s, ClementineData(u"Last played"_s)).data));
  EXPECT_EQ(SmartPlaylistSearch(SearchType::All, SmartPlaylistSearch::TermList(), SortType::FieldAsc, Field::Artist, -1), LoadSearch(ClementineSmartPlaylist::Convert(u"All tracks"_s, ClementineData(u"All tracks"_s)).data));

  const SmartPlaylistSearch least = LoadSearch(ClementineSmartPlaylist::Convert(u"Least favourite tracks"_s, ClementineData(u"Least favourite tracks"_s)).data);
  EXPECT_EQ(SmartPlaylistSearch(SearchType::Or, SmartPlaylistSearch::TermList() << SmartPlaylistSearchTerm(Field::Rating, Operator::LessThan, 0.6) << SmartPlaylistSearchTerm(Field::SkipCount, Operator::GreaterThan, 4), SortType::FieldDesc, Field::SkipCount), least);

  SmartPlaylistSearchTerm added(Field::DateCreated, Operator::NumericDate, 30);
  added.datetype_ = SmartPlaylistSearchTerm::DateType::Day;
  EXPECT_EQ(SmartPlaylistSearch(SearchType::And, SmartPlaylistSearch::TermList() << added, SortType::FieldDesc, Field::DateCreated, -1), LoadSearch(ClementineSmartPlaylist::Convert(u"Added in the last 30 days"_s, ClementineData(u"Added in the last 30 days"_s)).data));

  bool dynamic = false;
  const ClementineSmartPlaylist::Result mix = ClementineSmartPlaylist::Convert(u"Dynamic random mix"_s, ClementineData(u"Dynamic random mix"_s));
  LoadSearch(mix.data, &dynamic);
  EXPECT_TRUE(mix.dynamic);
  EXPECT_TRUE(dynamic);

}

TEST(ClementineSmartPlaylistTest, SortByScoreBecomesRating) {

  const ClementineSmartPlaylist::Result result = ClementineSmartPlaylist::Convert(u"Favourite tracks"_s, ClementineData(u"Favourite tracks"_s));
  ASSERT_TRUE(result.success);
  EXPECT_EQ(SmartPlaylistSearch(SearchType::All, SmartPlaylistSearch::TermList(), SortType::FieldDesc, Field::Rating), LoadSearch(result.data));
  ASSERT_EQ(1, result.dropped.count());
  EXPECT_TRUE(result.dropped.first().contains(u"Score"_s));

}

TEST(ClementineSmartPlaylistTest, DropsScoreCondition) {

  const ClementineSmartPlaylist::Result result = ClementineSmartPlaylist::Convert(u"Rock I like"_s, ClementineData(u"Rock I like"_s));
  ASSERT_TRUE(result.success);
  EXPECT_EQ(SmartPlaylistSearch(SearchType::And, SmartPlaylistSearch::TermList() << SmartPlaylistSearchTerm(Field::Genre, Operator::Contains, u"Rock"_s) << SmartPlaylistSearchTerm(Field::Rating, Operator::GreaterThan, 0.5), SortType::FieldDesc, Field::Rating, 40), LoadSearch(result.data));
  // The Score condition, and the sort order by Score.
  ASSERT_EQ(2, result.dropped.count());
  EXPECT_TRUE(result.dropped.at(0).contains(u"Rock I like"_s));
  EXPECT_TRUE(result.dropped.at(0).contains(u"Score"_s));
  EXPECT_TRUE(result.dropped.at(0).contains(u"50"_s));

}

TEST(ClementineSmartPlaylistTest, DropsUnsupportedOperatorAndKeepsTheRest) {

  const ClementineSmartPlaylist::Result result = ClementineSmartPlaylist::Convert(u"Long ones, not live"_s, ClementineData(u"Long ones, not live"_s));
  ASSERT_TRUE(result.success);
  SmartPlaylistSearchTerm relative(Field::LastPlayed, Operator::RelativeDate, 2);
  relative.second_value_ = 6;
  relative.datetype_ = SmartPlaylistSearchTerm::DateType::Week;
  const SmartPlaylistSearch expected(SearchType::And, SmartPlaylistSearch::TermList()
                                     << SmartPlaylistSearchTerm(Field::Length, Operator::GreaterThan, 420)
                                     << SmartPlaylistSearchTerm(Field::Title, Operator::NotContains, u"Live"_s)
                                     << SmartPlaylistSearchTerm(Field::Filepath, Operator::StartsWith, u"/music/Artist A"_s)
                                     << relative,
                                     SortType::FieldAsc, Field::Album, -1);
  EXPECT_EQ(expected, LoadSearch(result.data));
  // "Year contains 19": Akousma has no "contains" for numbers.
  ASSERT_EQ(1, result.dropped.count());
  EXPECT_TRUE(result.dropped.first().contains(u"Year"_s));

}

TEST(ClementineSmartPlaylistTest, SkipsWhenNoConditionIsLeft) {

  const ClementineSmartPlaylist::Result result = ClementineSmartPlaylist::Convert(u"Top score only"_s, ClementineData(u"Top score only"_s));
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error.isEmpty());
  EXPECT_EQ(1, result.dropped.count());

}

TEST(ClementineSmartPlaylistTest, RejectsGarbage) {

  EXPECT_FALSE(ClementineSmartPlaylist::Convert(u"Empty"_s, QByteArray()).success);
  EXPECT_FALSE(ClementineSmartPlaylist::Convert(u"Garbage"_s, QByteArray::fromHex("ffffffff00")).success);

}

// The converted searches run on Akousma's collection.
TEST(ClementineSmartPlaylistTest, ConvertedSearchesRun) {

  SharedPtr<Database> database = make_shared<MemoryDatabase>(nullptr);
  SharedPtr<CollectionBackend> backend = make_shared<CollectionBackend>();
  backend->Init(database, nullptr, Song::Source::Collection, QLatin1String(CollectionManager::kSongsTable), QLatin1String(CollectionManager::kDirsTable), QLatin1String(CollectionManager::kSubdirsTable));
  backend->AddDirectory(u"/music"_s);

  SongList songs;
  for (int i = 0; i < 3; ++i) {
    Song song;
    song.set_directory_id(1);
    song.set_url(QUrl::fromLocalFile(u"/music/Artist A/%1.flac"_s.arg(i)));
    song.set_mtime(1);
    // Added to the collection now, for "Added in the last 30 days".
    song.set_ctime(QDateTime::currentSecsSinceEpoch());
    song.set_filesize(1);
    song.set_title(u"Song %1"_s.arg(i));
    song.set_genre(u"Rock"_s);
    song.set_rating(i == 0 ? 0.8F : 0.2F);
    song.set_length_nanosec((i == 2 ? 500LL : 100LL) * kNsecPerSec);
    songs << song;
  }
  backend->AddOrUpdateSongs(songs);

  const auto titles = [&backend](const QString &name) {
    const ClementineSmartPlaylist::Result result = ClementineSmartPlaylist::Convert(name, ClementineData(name));
    PlaylistQueryGenerator generator;
    generator.Load(result.data);
    generator.set_collection_backend(backend);
    QStringList ret;
    const PlaylistItemPtrList items = generator.Generate();
    for (const PlaylistItemPtr &item : items) ret << item->EffectiveMetadata().title();
    ret.sort();
    return ret;
  };

  EXPECT_EQ(QStringList() << u"Song 0"_s, titles(u"Rock I like"_s));
  EXPECT_EQ(QStringList() << u"Song 1"_s << u"Song 2"_s, titles(u"Least favourite tracks"_s));
  EXPECT_EQ(QStringList() << u"Song 0"_s << u"Song 1"_s << u"Song 2"_s, titles(u"Added in the last 30 days"_s));

}

TEST(PlayerImporterTest, SecretSettingKeys) {

  const QStringList secret_keys = QStringList() << u"Subsonic/password"_s << u"NetworkProxy/password"_s << u"Tidal/access_token"_s << u"Tidal/refresh_token"_s << u"Spotify/access_token"_s << u"Qobuz/user_auth_token"_s << u"Qobuz/app_secret"_s << u"Plex/token"_s << u"Plex/server_token"_s << u"LastFM/session_key"_s << u"ListenBrainz/user_token"_s << u"Genius/client_secret"_s << u"AcoustID/api_key"_s << u"Credentials/subsonic"_s;
  for (const QString &key : secret_keys) {
    EXPECT_TRUE(PlayerImporter::IsSecretSettingKey(key)) << key.toStdString();
  }
  const QStringList other_keys = QStringList() << u"Subsonic/username"_s << u"Subsonic/url"_s << u"Tidal/token_type"_s << u"Tidal/expires_in"_s << u"Plex/clientid"_s << u"CredentialsManager/backend"_s << u"SerializedSmartPlaylists/songs/1/data"_s << u"Behaviour/language"_s;
  for (const QString &key : other_keys) {
    EXPECT_FALSE(PlayerImporter::IsSecretSettingKey(key)) << key.toStdString();
  }

}

TEST(PlayerImporterTest, PasswordsItem) {

  bool ok = false;
  EXPECT_EQ(PlayerImporter::Items(PlayerImporter::Item::All), PlayerImporter::ItemsFromString(u"all"_s, &ok));
  EXPECT_TRUE(ok);
  EXPECT_FALSE(PlayerImporter::ItemsFromString(u"all"_s) & PlayerImporter::Item::Passwords);
  EXPECT_EQ(PlayerImporter::Item::Settings | PlayerImporter::Item::Passwords, PlayerImporter::ItemsFromString(u"settings,passwords"_s, &ok));
  EXPECT_TRUE(ok);
  EXPECT_EQ(u"settings,passwords"_s, PlayerImporter::ItemsToString(PlayerImporter::Item::Settings | PlayerImporter::Item::Passwords));

}

// The first start prompt is offered when Akousma has no settings and no collection.
// Keys from the fallback locations don't count: on macOS those are the global preferences every account has, here a system wide file stands in for them.
TEST(PlayerImporterTest, FirstStartIgnoresFallbackSettings) {

  // Settings uses the organization name for the file name, the test main only sets the application name. The settings are in a temporary directory.
  QCoreApplication::setOrganizationName(u"Akousma"_s);
  QStandardPaths::setTestModeEnabled(true);
  if (QFile::exists(PlayerImporter::AkousmaDatabaseFile())) {
    GTEST_SKIP() << "An Akousma collection exists in the test data directory";
  }

  QTemporaryDir system_dir;
  ASSERT_TRUE(system_dir.isValid());
  QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, system_dir.path());
  QSettings::setPath(QSettings::NativeFormat, QSettings::SystemScope, system_dir.path());

  Settings s;
  s.clear();
  s.sync();
  EXPECT_FALSE(PlayerImporter::AkousmaHasData());
  EXPECT_FALSE(PlayerImporter::FirstRunPromptDone());

  // The import's own bookkeeping is not Akousma data.
  PlayerImporter::SetFirstRunPromptDone();
  EXPECT_TRUE(PlayerImporter::FirstRunPromptDone());
  EXPECT_FALSE(PlayerImporter::AkousmaHasData());

  {
    Settings system_settings(QSettings::SystemScope);
    system_settings.setValue(u"Behaviour/language"_s, u"de"_s);
    system_settings.sync();
  }
  EXPECT_TRUE(s.allKeys().contains(u"Behaviour/language"_s));
  EXPECT_FALSE(PlayerImporter::AkousmaHasData());

  s.setValue(u"Behaviour/language"_s, u"en"_s);
  s.sync();
  EXPECT_TRUE(PlayerImporter::AkousmaHasData());

  s.clear();
  s.sync();
  Settings system_settings(QSettings::SystemScope);
  system_settings.clear();
  system_settings.sync();

}

// An in-memory keyring shared by all instances, where entries are stored under the application's names,
// the way Windows Credential Manager and the Keychain store them ("<prefix><service>").
QMap<QString, QString> sFakeKeyring;

class FakeKeyringBackend : public CredentialsBackendInterface {
 public:
  explicit FakeKeyringBackend(const CredentialsIdentity &identity) : identity_(identity) {}
  QString name() const override { return u"Fake keyring"_s; }
  void CheckAvailable(const AvailableCallback &available_callback) override {
    QTimer::singleShot(0, this, [available_callback]() { available_callback(true); });
  }
  void ReadPassword(const QString &service, const ResultCallback &callback) override {
    const QString key = identity_.name_prefix + service;
    // Answer asynchronously, like the real keyrings.
    QTimer::singleShot(0, this, [key, callback]() { callback(sFakeKeyring.contains(key) ? CredentialsResult::Success(sFakeKeyring.value(key)) : CredentialsResult::NotFound()); });
  }
  void SavePassword(const QString &service, const QString &password, const ResultCallback &callback) override {
    sFakeKeyring.insert(identity_.name_prefix + service, password);
    QTimer::singleShot(0, this, [callback]() { callback(CredentialsResult::Success()); });
  }
  void DeletePassword(const QString &service, const ResultCallback &callback) override {
    sFakeKeyring.remove(identity_.name_prefix + service);
    QTimer::singleShot(0, this, [callback]() { callback(CredentialsResult::Success()); });
  }

 private:
  const CredentialsIdentity identity_;
};

TEST(PasswordCopierTest, CopiesStrawberryEntriesToAkousmaNames) {

  // Dummy values, not real passwords.
  const QString dummy_subsonic = u"dummy-subsonic-secret"_s;
  const QString dummy_other = u"dummy-other-app-secret"_s;
  sFakeKeyring.clear();
  sFakeKeyring.insert(u"Strawberry/subsonic"_s, dummy_subsonic);
  sFakeKeyring.insert(u"OtherApp/jellyfin"_s, dummy_other);

  FakeKeyringBackend strawberry(CredentialsIdentity::Strawberry());
  FakeKeyringBackend akousma(CredentialsIdentity::Akousma());
  EXPECT_TRUE(PasswordCopier::CheckAvailable(&strawberry));

  const PasswordCopier::Result result = PasswordCopier::Copy(PlayerImporter::CredentialsServices(),
                                                             [&strawberry](const QString &service) { return PasswordCopier::ReadPassword(&strawberry, service); },
                                                             [&akousma](const QString &service, const QString &password) {
                                                               akousma.SavePassword(service, password, [](const CredentialsResult&) {});
                                                               return PasswordCopier::ReadPassword(&akousma, service).password == password ? CredentialsResult::Success() : CredentialsResult::Error(u"not saved"_s);
                                                             });

  EXPECT_EQ(QStringList() << u"subsonic"_s, result.copied);
  EXPECT_EQ(QStringList() << u"jellyfin"_s, result.not_found);
  EXPECT_TRUE(result.failed.isEmpty());

  // Copied under Akousma's names, Strawberry's entry and another application's entry are unchanged, nothing else was written.
  EXPECT_TRUE(sFakeKeyring.value(u"Akousma/subsonic"_s) == dummy_subsonic);
  EXPECT_TRUE(sFakeKeyring.value(u"Strawberry/subsonic"_s) == dummy_subsonic);
  EXPECT_TRUE(sFakeKeyring.value(u"OtherApp/jellyfin"_s) == dummy_other);
  EXPECT_FALSE(sFakeKeyring.contains(u"Akousma/jellyfin"_s));
  EXPECT_EQ(3, sFakeKeyring.count());

}

TEST(PasswordCopierTest, ReportsErrorsWithoutPasswords) {

  const PasswordCopier::Result result = PasswordCopier::Copy(QStringList() << u"subsonic"_s << u"jellyfin"_s,
                                                             [](const QString &service) { return service == "subsonic"_L1 ? CredentialsResult::Error(u"keyring locked"_s) : CredentialsResult::Success(u"dummy-value"_s); },
                                                             [](const QString&, const QString&) { return CredentialsResult::Error(u"cannot write"_s); });
  EXPECT_TRUE(result.copied.isEmpty());
  ASSERT_EQ(2, result.failed.count());
  EXPECT_EQ(u"subsonic: keyring locked"_s, result.failed.at(0));
  EXPECT_EQ(u"jellyfin: cannot write"_s, result.failed.at(1));
  EXPECT_FALSE(result.failed.join(u' ').contains(u"dummy-value"_s));

}

// Saving goes through Akousma's own CredentialsManager, here with the settings backend so the test doesn't touch a keyring.
TEST(PasswordCopierTest, SavesWithAkousmaCredentialsManager) {

  // Settings uses the organization name for the file name, the test main only sets the application name. The settings are in a temporary directory.
  QCoreApplication::setOrganizationName(u"Akousma"_s);
  {
    Settings s;
    s.beginGroup(CredentialsManager::kSettingsGroup);
    s.setValue(CredentialsManager::kBackend, u"settings"_s);
    s.endGroup();
  }

  const QString dummy = u"dummy-jellyfin-secret"_s;
  CredentialsManager credentials_manager;
  EXPECT_TRUE(PasswordCopier::SavePassword(&credentials_manager, u"jellyfin"_s, dummy).success());
  EXPECT_EQ(u"Settings"_s, credentials_manager.backend_name());

  Settings s;
  EXPECT_TRUE(QString::fromUtf8(QByteArray::fromBase64(s.value(u"Credentials/jellyfin"_s).toByteArray())) == dummy);
  s.remove(u"Credentials"_s);
  s.remove(CredentialsManager::kSettingsGroup);

}

}  // namespace
