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

#include <atomic>
#include <memory>
#include <optional>
#include <thread>

#include "gtest_include.h"

#include <QtGlobal>
#include <QByteArray>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QUuid>
#include <QVariant>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>

#ifndef Q_OS_WIN32
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

#include "includes/shared_ptr.h"
#include "core/database.h"
#include "core/memorydatabase.h"
#include "core/song.h"
#include "core/taskmanager.h"
#include "collection/collectionbackend.h"
#include "collection/collectionmanager.h"
#include "collection/collectionplaylistitem.h"
#include "playlist/playlistbackend.h"
#include "playlist/playlistitem.h"
#include "playlist/songplaylistitem.h"
#include "tagreader/tagreaderclient.h"

using std::make_shared;
using namespace Qt::Literals::StringLiterals;

// clazy:excludeall=non-pod-global-static,returning-void-expression

namespace {

class PlaylistBackendTest : public ::testing::Test {
 protected:
  PlaylistBackendTest()
      : tagreader_client_(new TagReaderClient()),
        tagreader_client_thread_(new QThread()) {
    tagreader_client_->moveToThread(tagreader_client_thread_);
    tagreader_client_thread_->start();
  }

  ~PlaylistBackendTest() override {
    tagreader_client_thread_->exit();
    tagreader_client_thread_->wait(5000);
    delete tagreader_client_thread_;
  }

  void SetUp() override {

    ASSERT_TRUE(temp_dir_.isValid());
    // The CUE sheets refer to this file, the songs from a CUE sheet are only complete when the file exists.
    ASSERT_TRUE(QFile::copy(u":/audio/strawberry.flac"_s, temp_dir_.filePath(u"album.flac"_s)));
    database_ = make_shared<Database>(make_shared<TaskManager>(), nullptr, temp_dir_.filePath(u"strawberry.db"_s));
    collection_backend_ = make_shared<CollectionBackend>();
    collection_backend_->Init(database_, make_shared<TaskManager>(), Song::Source::Collection, QLatin1String(CollectionManager::kSongsTable), QLatin1String(CollectionManager::kDirsTable), QLatin1String(CollectionManager::kSubdirsTable));
    playlist_backend_ = make_shared<PlaylistBackend>(database_, tagreader_client_, collection_backend_);

  }

  void TearDown() override {
    database_->Close();
  }

  // Saves a playlist with a song from a CUE sheet.
  int SaveCueSongPlaylist(const QString &cue_path, const QString &audio_path) const {

    const int playlist_id = playlist_backend_->CreatePlaylist(u"CUE"_s, QString());
    if (playlist_id == -1) return -1;

    Song song(Song::Source::LocalFile);
    song.Init(u"Saved title"_s, u"Artist"_s, u"Album"_s, 0, 180'000'000'000LL);
    song.set_url(QUrl::fromLocalFile(audio_path));
    song.set_cue_path(cue_path);
    song.set_filetype(Song::FileType::FLAC);
    song.set_valid(true);

    PlaylistItemSaveDataList items_save_data;
    items_save_data << make_shared<SongPlaylistItem>(song)->CreateSaveData();
    playlist_backend_->SavePlaylist(playlist_id, items_save_data, -1, nullptr);

    return playlist_id;

  }

  QTemporaryDir temp_dir_;
  SharedPtr<TagReaderClient> tagreader_client_;
  QThread *tagreader_client_thread_;
  SharedPtr<Database> database_;
  SharedPtr<CollectionBackend> collection_backend_;
  SharedPtr<PlaylistBackend> playlist_backend_;
};

TEST_F(PlaylistBackendTest, RestoresCueData) {

  const QString cue_path = temp_dir_.filePath(u"album.cue"_s);
  const QString audio_path = temp_dir_.filePath(u"album.flac"_s);
  {
    QFile cue_file(cue_path);
    ASSERT_TRUE(cue_file.open(QIODevice::WriteOnly));
    cue_file.write("FILE \"album.flac\" WAVE\n  TRACK 01 AUDIO\n    TITLE \"CUE title\"\n    INDEX 01 00:00:00\n");
  }
  const int playlist_id = SaveCueSongPlaylist(cue_path, audio_path);
  ASSERT_NE(-1, playlist_id);

  const std::optional<PlaylistItemPtrList> items = playlist_backend_->GetPlaylistItems(playlist_id);
  ASSERT_TRUE(items.has_value());
  ASSERT_EQ(1, items->count());
  EXPECT_EQ(u"CUE title"_s, items->first()->EffectiveMetadata().title());

  const std::optional<SongList> songs = playlist_backend_->GetPlaylistSongs(playlist_id);
  ASSERT_TRUE(songs.has_value());
  ASSERT_EQ(1, songs->count());
  EXPECT_EQ(u"CUE title"_s, songs->first().title());

}

#ifndef Q_OS_WIN32

// Regression test: the CUE sheets were read while the database mutex was held, so other threads, also the GUI thread, waited for the files to be read.
TEST_F(PlaylistBackendTest, CueDataIsRestoredWithoutDatabaseMutex) {

  // The CUE sheet is a FIFO, opening it for reading waits until it's opened for writing, so the test can check the mutex while the CUE sheet is read.
  const QString cue_path = temp_dir_.filePath(u"album.cue"_s);
  const QString audio_path = temp_dir_.filePath(u"album.flac"_s);
  const QByteArray cue_path_data = QFile::encodeName(cue_path);
  ASSERT_EQ(0, mkfifo(cue_path_data.constData(), 0600));
  const int playlist_id = SaveCueSongPlaylist(cue_path, audio_path);
  ASSERT_NE(-1, playlist_id);

  std::atomic<bool> items_returned = false;
  std::atomic<bool> mutex_locked = false;
  std::thread cue_writer([this, &cue_path_data, &items_returned, &mutex_locked]() {
    // Opening for writing returns when the playlist backend has opened the CUE sheet for reading.
    const int fd = open(cue_path_data.constData(), O_WRONLY);
    if (fd < 0) return;
    // The test opened the CUE sheet for reading, since the playlist backend didn't.
    if (items_returned) {
      close(fd);
      return;
    }
    if (database_->Mutex()->tryLock(2000)) {
      mutex_locked = true;
      database_->Mutex()->unlock();
    }
    const QByteArray cue_data = "FILE \"album.flac\" WAVE\n  TRACK 01 AUDIO\n    TITLE \"CUE title\"\n    INDEX 01 00:00:00\n";
    const ssize_t written = write(fd, cue_data.constData(), static_cast<size_t>(cue_data.size()));
    Q_UNUSED(written);
    close(fd);
  });

  const std::optional<PlaylistItemPtrList> items = playlist_backend_->GetPlaylistItems(playlist_id);

  // When the playlist backend didn't open the CUE sheet, the writer waits for a reader, so it's opened for reading here, and the test fails instead of hanging.
  items_returned = true;
  const int fd = open(cue_path_data.constData(), O_RDONLY | O_NONBLOCK);
  cue_writer.join();
  if (fd >= 0) close(fd);

  EXPECT_TRUE(mutex_locked);
  ASSERT_TRUE(items.has_value());
  ASSERT_EQ(1, items->count());
  EXPECT_EQ(u"CUE title"_s, items->first()->EffectiveMetadata().title());

}

#endif  // Q_OS_WIN32

// Collection items of deleted songs, and reads which fail.
class PlaylistBackendItemsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    database_ = make_shared<MemoryDatabase>(nullptr);
    collection_backend_ = make_shared<CollectionBackend>();
    collection_backend_->Init(database_, nullptr, Song::Source::Collection, QLatin1String(CollectionManager::kSongsTable), QLatin1String(CollectionManager::kDirsTable), QLatin1String(CollectionManager::kSubdirsTable));
    playlist_backend_ = make_shared<PlaylistBackend>(database_, nullptr, nullptr);

    // The directory gets ID 1.
    collection_backend_->AddDirectory(u"/music"_s);

    playlist_id_ = playlist_backend_->CreatePlaylist(u"Playlist"_s, QString());
  }

  // Adds a song to the collection, and returns it with its ID.
  Song AddCollectionSong(const QString &title, const QString &filename) {
    Song song(Song::Source::Collection);
    song.set_directory_id(1);
    song.set_title(title);
    song.set_artist(u"Artist"_s);
    song.set_album(u"Album"_s);
    song.set_url(QUrl::fromLocalFile(u"/music/"_s + filename));
    song.set_mtime(1);
    song.set_ctime(1);
    song.set_filesize(1);

    QSignalSpy added_spy(&*collection_backend_, &CollectionBackend::SongsAdded);
    collection_backend_->AddOrUpdateSongs(SongList() << song);
    if (added_spy.count() != 1) return Song();
    const SongList added_songs = added_spy[0][0].value<SongList>();
    if (added_songs.count() != 1) return Song();

    // As stored in the collection.
    return collection_backend_->GetSongById(added_songs.first().id());
  }

  void SaveItems(const PlaylistItemPtrList &items) {
    PlaylistItemSaveDataList items_save_data;
    for (const PlaylistItemPtr &item : items) {
      items_save_data << item->CreateSaveData();
    }
    playlist_backend_->SavePlaylist(playlist_id_, items_save_data, -1, nullptr);
  }

  // Runs a statement on the database directly, as an older version or another program would.
  bool Exec(const QString &statement, const QVariantList &values = QVariantList()) {
    QSqlDatabase db(database_->Connect());
    QSqlQuery q(db);
    if (!q.prepare(statement)) return false;
    for (const QVariant &value : values) {
      q.addBindValue(value);
    }
    return q.exec();
  }

  bool DeleteSongRow(const int id) {
    return Exec(QStringLiteral("DELETE FROM %1 WHERE ROWID = ?").arg(QLatin1String(CollectionManager::kSongsTable)), QVariantList() << id);
  }

  // Gives the song the ROWID of a deleted song, as SQLite does for the next song added to a songs table without AUTOINCREMENT (before schema 31).
  Song ReuseRowId(const Song &song, const int rowid) {
    if (song.id() != rowid && !Exec(QStringLiteral("UPDATE %1 SET ROWID = ? WHERE ROWID = ?").arg(QLatin1String(CollectionManager::kSongsTable)), QVariantList() << rowid << song.id())) return Song();
    return collection_backend_->GetSongById(rowid);
  }

  PlaylistItemPtrList LoadItems() {
    const std::optional<PlaylistItemPtrList> items = playlist_backend_->GetPlaylistItems(playlist_id_);
    EXPECT_TRUE(items.has_value());
    return items.value_or(PlaylistItemPtrList());
  }

  SharedPtr<Database> database_;
  SharedPtr<CollectionBackend> collection_backend_;
  SharedPtr<PlaylistBackend> playlist_backend_;
  int playlist_id_ = -1;
};

TEST_F(PlaylistBackendItemsTest, CollectionItemIsLoadedFromTheCollection) {

  ASSERT_NE(-1, playlist_id_);
  const Song song = AddCollectionSong(u"Song"_s, u"song.flac"_s);
  ASSERT_TRUE(song.is_valid());

  SaveItems(PlaylistItemPtrList() << make_shared<CollectionPlaylistItem>(song));

  const PlaylistItemPtrList items = LoadItems();
  ASSERT_EQ(1, items.count());
  EXPECT_EQ(Song::Source::Collection, items[0]->source());
  EXPECT_TRUE(items[0]->IsLocalCollectionItem());
  EXPECT_EQ(song.id(), items[0]->OriginalMetadata().id());
  EXPECT_EQ(song.url(), items[0]->OriginalMetadata().url());
  EXPECT_EQ(u"Song"_s, items[0]->OriginalMetadata().title());

}

// Regression test: a collection item whose song was deleted loaded as an item without URL or title, or was dropped.
TEST_F(PlaylistBackendItemsTest, CollectionItemOfDeletedSongIsLoadedAsFile) {

  ASSERT_NE(-1, playlist_id_);
  const Song song = AddCollectionSong(u"Deleted"_s, u"deleted.flac"_s);
  ASSERT_TRUE(song.is_valid());

  SaveItems(PlaylistItemPtrList() << make_shared<CollectionPlaylistItem>(song));
  ASSERT_TRUE(DeleteSongRow(song.id()));

  const PlaylistItemPtrList items = LoadItems();
  ASSERT_EQ(1, items.count());
  EXPECT_EQ(Song::Source::LocalFile, items[0]->source());
  EXPECT_FALSE(items[0]->IsLocalCollectionItem());
  const Song metadata = items[0]->OriginalMetadata();
  EXPECT_EQ(Song::Source::LocalFile, metadata.source());
  EXPECT_EQ(-1, metadata.id());
  EXPECT_EQ(song.url(), metadata.url());
  EXPECT_EQ(u"Deleted"_s, metadata.title());
  EXPECT_EQ(u"Artist"_s, metadata.artist());

  const std::optional<SongList> songs = playlist_backend_->GetPlaylistSongs(playlist_id_);
  ASSERT_TRUE(songs.has_value());
  ASSERT_EQ(1, songs->count());
  EXPECT_EQ(song.url(), songs->first().url());

}

// Regression test: deleting a collection song left the playlist rows pointing at its ROWID, which SQLite gives to the next song added.
TEST_F(PlaylistBackendItemsTest, DeleteSongsKeepsPlaylistItemsAsFiles) {

  ASSERT_NE(-1, playlist_id_);
  const Song song = AddCollectionSong(u"Removed"_s, u"removed.flac"_s);
  ASSERT_TRUE(song.is_valid());

  // A row as saved by earlier versions, without the song's metadata.
  ASSERT_TRUE(Exec(u"INSERT INTO playlist_items (playlist, type, uuid, collection_id) VALUES (?, ?, ?, ?)"_s, QVariantList() << playlist_id_ << static_cast<int>(Song::Source::Collection) << QUuid::createUuid().toString(QUuid::WithoutBraces) << song.id()));

  collection_backend_->DeleteSongs(SongList() << song);

  // The next song added gets the ROWID of the deleted song.
  const Song other_song = ReuseRowId(AddCollectionSong(u"Other"_s, u"other.flac"_s), song.id());
  ASSERT_TRUE(other_song.is_valid());
  ASSERT_EQ(song.id(), other_song.id());

  const PlaylistItemPtrList items = LoadItems();
  ASSERT_EQ(1, items.count());
  EXPECT_EQ(Song::Source::LocalFile, items[0]->source());
  const Song metadata = items[0]->OriginalMetadata();
  EXPECT_EQ(song.url(), metadata.url());
  EXPECT_EQ(u"Removed"_s, metadata.title());
  EXPECT_EQ(u"Album"_s, metadata.album());

}

// Regression test: an open playlist saved its item of a deleted song with the old ROWID, after the ROWID was given to another song the item loaded as that song.
TEST_F(PlaylistBackendItemsTest, SaveKeepsItemOfReusedRowIdAsFile) {

  ASSERT_NE(-1, playlist_id_);
  const Song song = AddCollectionSong(u"Old"_s, u"old.flac"_s);
  ASSERT_TRUE(song.is_valid());
  const Song kept_song = AddCollectionSong(u"Kept"_s, u"kept.flac"_s);
  ASSERT_TRUE(kept_song.is_valid());

  const PlaylistItemPtr item = make_shared<CollectionPlaylistItem>(song);
  const PlaylistItemPtr kept_item = make_shared<CollectionPlaylistItem>(kept_song);

  // Deleted without changing the playlist rows, and the ROWID given to another song.
  ASSERT_TRUE(DeleteSongRow(kept_song.id()));
  ASSERT_TRUE(DeleteSongRow(song.id()));
  const Song new_song = ReuseRowId(AddCollectionSong(u"New"_s, u"new.flac"_s), song.id());
  ASSERT_TRUE(new_song.is_valid());
  ASSERT_EQ(song.id(), new_song.id());
  const Song readded_kept_song = ReuseRowId(AddCollectionSong(u"Kept"_s, u"kept.flac"_s), kept_song.id());
  ASSERT_TRUE(readded_kept_song.is_valid());
  ASSERT_EQ(kept_song.id(), readded_kept_song.id());

  // The playlist still has the items with the old ROWIDs, and is saved.
  SaveItems(PlaylistItemPtrList() << item << kept_item);

  const PlaylistItemPtrList items = LoadItems();
  ASSERT_EQ(2, items.count());

  EXPECT_EQ(Song::Source::LocalFile, items[0]->source());
  EXPECT_EQ(song.url(), items[0]->OriginalMetadata().url());
  EXPECT_EQ(u"Old"_s, items[0]->OriginalMetadata().title());

  // The song with the same ROWID and URL is the same file, it stays a collection item.
  EXPECT_EQ(Song::Source::Collection, items[1]->source());
  EXPECT_EQ(kept_song.url(), items[1]->OriginalMetadata().url());

}

// Regression test: the type column was cast to Song::Source without a check.
TEST_F(PlaylistBackendItemsTest, ItemWithUnknownTypeIsLoadedFromSavedMetadata) {

  ASSERT_NE(-1, playlist_id_);

  const QString insert = u"INSERT INTO playlist_items (playlist, type, uuid, title, url, source) VALUES (?, ?, ?, ?, ?, ?)"_s;
  ASSERT_TRUE(Exec(insert, QVariantList() << playlist_id_ << 1000 << QUuid::createUuid().toString(QUuid::WithoutBraces) << u"Future"_s << QUrl::fromLocalFile(u"/music/future.flac"_s).toString(QUrl::FullyEncoded) << 1000));
  ASSERT_TRUE(Exec(insert, QVariantList() << playlist_id_ << u"not a number"_s << QUuid::createUuid().toString(QUuid::WithoutBraces) << u"Text"_s << u"http://example.com/stream"_s << 5));
  // Nothing to restore an item from.
  ASSERT_TRUE(Exec(insert, QVariantList() << playlist_id_ << -1 << QUuid::createUuid().toString(QUuid::WithoutBraces) << u"No URL"_s << QString() << 1));

  const PlaylistItemPtrList items = LoadItems();
  ASSERT_EQ(2, items.count());

  EXPECT_EQ(Song::Source::LocalFile, items[0]->source());
  EXPECT_EQ(Song::Source::LocalFile, items[0]->OriginalMetadata().source());
  EXPECT_EQ(u"Future"_s, items[0]->OriginalMetadata().title());

  EXPECT_EQ(Song::Source::Unknown, items[1]->source());
  EXPECT_EQ(Song::Source::Unknown, items[1]->OriginalMetadata().source());
  EXPECT_EQ(QUrl(u"http://example.com/stream"_s), items[1]->OriginalMetadata().url());

}

// Regression test: a failed read returned an empty playlist, which was then saved over the saved items.
TEST_F(PlaylistBackendItemsTest, FailedReadIsNotAnEmptyPlaylist) {

  ASSERT_NE(-1, playlist_id_);

  {
    const std::optional<PlaylistItemPtrList> items = playlist_backend_->GetPlaylistItems(playlist_id_);
    ASSERT_TRUE(items.has_value());
    EXPECT_TRUE(items->isEmpty());
  }

  const Song song = AddCollectionSong(u"Song"_s, u"song.flac"_s);
  ASSERT_TRUE(song.is_valid());
  SaveItems(PlaylistItemPtrList() << make_shared<CollectionPlaylistItem>(song));

  ASSERT_TRUE(Exec(u"ALTER TABLE playlist_items RENAME TO playlist_items_moved"_s));
  EXPECT_FALSE(playlist_backend_->GetPlaylistItems(playlist_id_).has_value());
  EXPECT_FALSE(playlist_backend_->GetPlaylistSongs(playlist_id_).has_value());

  ASSERT_TRUE(Exec(u"ALTER TABLE playlist_items_moved RENAME TO playlist_items"_s));
  const std::optional<PlaylistItemPtrList> items = playlist_backend_->GetPlaylistItems(playlist_id_);
  ASSERT_TRUE(items.has_value());
  EXPECT_EQ(1, items->count());

}

}  // namespace
