/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2018-2026, Jonas Kvinge <jonas@jkvinge.net>
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

#include <utility>
#include <memory>
#include <optional>

#include <QObject>
#include <QApplication>
#include <QThread>
#include <QMutex>
#include <QIODevice>
#include <QDir>
#include <QFile>
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUuid>
#include <QVariant>
#include <QSqlDatabase>
#include <QSqlError>

#include "includes/shared_ptr.h"
#include "core/database.h"
#include "core/logging.h"
#include "core/scopedtransaction.h"
#include "core/song.h"
#include "core/sqlquery.h"
#include "core/sqlrow.h"
#include "collection/collectionbackend.h"
#include "tagreader/tagreaderclient.h"
#include "playlistitem.h"
#include "playlistitemsavedata.h"
#include "songplaylistitem.h"
#include "playlistbackend.h"
#include "playlistparsers/cueparser.h"
#include "smartplaylists/playlistgenerator.h"

using namespace Qt::Literals::StringLiterals;
using std::make_shared;

namespace {

constexpr int kSongTableJoins = 2;

// The type column of a playlist item holds a Song::Source.
// A value which is not one, for example written by a newer version or a damaged database, must not be cast to Song::Source and used as one.
bool IsKnownSource(const int value) {

  switch (static_cast<Song::Source>(value)) {
    case Song::Source::Unknown:
    case Song::Source::LocalFile:
    case Song::Source::Collection:
    case Song::Source::CDDA:
    case Song::Source::Device:
    case Song::Source::Stream:
    case Song::Source::Tidal:
    case Song::Source::Subsonic:
    case Song::Source::Qobuz:
    case Song::Source::SomaFM:
    case Song::Source::RadioParadise:
    case Song::Source::Spotify:
    case Song::Source::RadioBrowser:
    case Song::Source::Plex:
    case Song::Source::Jellyfin:
      return true;
  }

  return false;

}

std::optional<Song::Source> SourceFromTypeValue(const QVariant &value) {

  if (value.isNull()) return std::nullopt;

  bool ok = false;
  const int source = value.toInt(&ok);
  if (!ok || !IsKnownSource(source)) return std::nullopt;

  return static_cast<Song::Source>(source);

}

}  // namespace

PlaylistBackend::PlaylistBackend(const SharedPtr<Database> database,
                                 const SharedPtr<TagReaderClient> tagreader_client,
                                 const SharedPtr<CollectionBackend> collection_backend,
                                 QObject *parent)
    : QObject(parent),
      database_(database),
      tagreader_client_(tagreader_client),
      collection_backend_(collection_backend),
      original_thread_(nullptr) {

  setObjectName(QLatin1String(QObject::metaObject()->className()));

  original_thread_ = thread();

}

void PlaylistBackend::Close() {

  if (database_) {
    QMutexLocker l(database_->Mutex());
    database_->Close();
  }

}

void PlaylistBackend::ExitAsync() {
  QMetaObject::invokeMethod(this, &PlaylistBackend::Exit, Qt::QueuedConnection);
}

void PlaylistBackend::Exit() {

  Q_ASSERT(QThread::currentThread() == thread());

  moveToThread(original_thread_);
  Q_EMIT ExitFinished();

}

PlaylistBackend::PlaylistList PlaylistBackend::GetAllPlaylists() {
  return GetPlaylists(GetPlaylistsFlags::GetPlaylists_All);
}

PlaylistBackend::PlaylistList PlaylistBackend::GetAllOpenPlaylists() {
  return GetPlaylists(GetPlaylistsFlags::GetPlaylists_OpenInUi);
}

PlaylistBackend::PlaylistList PlaylistBackend::GetAllFavoritePlaylists() {
  return GetPlaylists(GetPlaylistsFlags::GetPlaylists_Favorite);
}

PlaylistBackend::PlaylistList PlaylistBackend::GetPlaylists(const GetPlaylistsFlags flags) {

  QMutexLocker l(database_->Mutex());
  QSqlDatabase db(database_->Connect());

  PlaylistList ret;

  QStringList condition_list;
  if (flags & GetPlaylistsFlags::GetPlaylists_OpenInUi) {
    condition_list << u"ui_order != -1"_s;
  }
  if (flags & GetPlaylistsFlags::GetPlaylists_Favorite) {
    condition_list << u"is_favorite != 0"_s;
  }
  QString condition;
  if (!condition_list.isEmpty()) {
    condition = " WHERE "_L1 + condition_list.join(" OR "_L1);
  }

  SqlQuery q(db);
  q.prepare(u"SELECT ROWID, name, last_played, special_type, ui_path, is_favorite, dynamic_playlist_type, dynamic_playlist_data, dynamic_playlist_backend FROM playlists "_s + condition + u" ORDER BY ui_order"_s);
  if (!q.Exec()) {
    database_->ReportErrors(q);
    return ret;
  }

  while (q.next()) {
    Playlist p;
    p.id = q.value(0).toInt();
    p.name = q.value(1).toString();
    p.last_played = q.value(2).toInt();
    p.special_type = q.value(3).toString();
    p.ui_path = q.value(4).toString();
    p.favorite = q.value(5).toBool();
    p.dynamic_type = static_cast<PlaylistGenerator::Type>(q.value(6).toInt());
    p.dynamic_data = q.value(7).toByteArray();
    p.dynamic_backend = q.value(8).toString();
    ret << p;
  }

  // A step which fails ends the loop like the last row does.
  if (q.lastError().isValid()) {
    database_->ReportErrors(q);
  }

  return ret;

}

PlaylistBackend::Playlist PlaylistBackend::GetPlaylist(const int id) {

  QMutexLocker l(database_->Mutex());
  QSqlDatabase db(database_->Connect());

  SqlQuery q(db);
  q.prepare(u"SELECT ROWID, name, last_played, special_type, ui_path, is_favorite, dynamic_playlist_type, dynamic_playlist_data, dynamic_playlist_backend FROM playlists WHERE ROWID=:id"_s);

  q.BindValue(u":id"_s, id);
  if (!q.Exec()) {
    database_->ReportErrors(q);
    return Playlist();
  }

  if (!q.next()) {
    if (q.lastError().isValid()) {
      database_->ReportErrors(q);
    }
    return Playlist();
  }

  Playlist p;
  p.id = q.value(0).toInt();
  p.name = q.value(1).toString();
  p.last_played = q.value(2).toInt();
  p.special_type = q.value(3).toString();
  p.ui_path = q.value(4).toString();
  p.favorite = q.value(5).toBool();
  p.dynamic_type = static_cast<PlaylistGenerator::Type>(q.value(6).toInt());
  p.dynamic_data = q.value(7).toByteArray();
  p.dynamic_backend = q.value(8).toString();

  return p;

}

QString PlaylistBackend::PlaylistItemsQuery() {

  return QStringLiteral("SELECT %1, %2, p.type, p.uuid FROM playlist_items AS p "
                        "LEFT JOIN songs ON p.type = songs.source AND p.collection_id = songs.ROWID "
                        "WHERE p.playlist = :playlist"
                        ).arg(Song::JoinSpec(u"songs"_s),
                              Song::JoinSpec(u"p"_s));

}

std::optional<PlaylistItemPtrList> PlaylistBackend::GetPlaylistItems(const int playlist_id) {

  std::optional<PlaylistItemPtrList> playlist_items;

  {

    QMutexLocker l(database_->Mutex());
    QSqlDatabase db(database_->Connect());
    SqlQuery q(db);
    // Forward iterations only may be faster
    q.setForwardOnly(true);
    q.prepare(PlaylistItemsQuery());
    q.BindValue(u":playlist"_s, playlist_id);
    // Don't return early on errors, the connection must be closed below.
    if (q.Exec()) {
      PlaylistItemPtrList items;
      while (q.next()) {
        PlaylistItemPtr item = NewPlaylistItemFromQuery(SqlRow(q));
        if (item) items << item;
      }
      // A step which fails ends the loop like the last row does, the items read until then are not the whole playlist.
      if (q.lastError().isValid()) {
        database_->ReportErrors(q);
      }
      else {
        playlist_items = items;
      }
    }
    else {
      database_->ReportErrors(q);
    }

  }

  if (playlist_items) {
    RestoreCueData(*playlist_items);
  }

  if (QThread::currentThread() != thread() && QThread::currentThread() != qApp->thread()) {
    Close();
  }

  return playlist_items;

}

std::optional<SongList> PlaylistBackend::GetPlaylistSongs(const int playlist_id) {

  const std::optional<PlaylistItemPtrList> playlist_items = GetPlaylistItems(playlist_id);
  if (!playlist_items) return std::nullopt;

  SongList songs;
  songs.reserve(playlist_items->count());
  for (const PlaylistItemPtr &item : *playlist_items) {
    songs << item->EffectiveMetadata();
  }

  return songs;

}

PlaylistItemPtr PlaylistBackend::NewPlaylistItemFromQuery(const SqlRow &row) {

  // The song tables get joined first
  const int playlist_row = static_cast<int>(Song::kRowIdColumns.count()) * kSongTableJoins;
  const QVariant type = row.value(playlist_row);
  const QUuid uuid = QUuid::fromString(row.value(playlist_row + 1).toString());

  const std::optional<Song::Source> source = SourceFromTypeValue(type);
  if (!source) {
    // Keep what was saved with the item rather than guessing its kind from the type.
    qLog(Warning) << "Playlist item" << uuid << "has an unknown type" << type << "loading it from its saved metadata";
    PlaylistItemPtr item = NewFileItemFromSavedMetadata(row, uuid);
    if (!item) {
      qLog(Warning) << "Playlist item" << uuid << "has no saved URL, skipping it";
      return nullptr;
    }
    return item;
  }

  PlaylistItemPtr item = PlaylistItem::NewFromSource(*source, uuid);
  const bool success = item->InitFromQuery(row);

  if (*source == Song::Source::Collection) {
    // Collection items are joined with the songs table by ROWID, no song with that ROWID means the song was deleted from the collection.
    // The item is then restored from the metadata saved with it as a file, instead of as a collection item without a song.
    if (row.value(0).isNull() || !success || item->OriginalMetadata().url().isEmpty()) {
      item = NewFileItemFromSavedMetadata(row, uuid);
      if (!item) {
        qLog(Warning) << "The collection song of playlist item" << uuid << "no longer exists, and the item has no saved URL, skipping it";
        return nullptr;
      }
    }
  }
  else if (!success) {
    qLog(Warning) << "Could not load playlist item" << uuid << "skipping it";
    return nullptr;
  }

  return item;

}

void PlaylistBackend::RestoreCueData(PlaylistItemPtrList &playlist_items) {

  // The CUE sheets and tags are read after the database mutex is released, since reading them can take a while, and other threads, also the GUI thread, wait for the mutex.
  // It's probable that we'll have a few songs associated with the same CUE, so we're caching results of parsing CUEs
  SharedPtr<NewSongFromQueryState> state = make_shared<NewSongFromQueryState>();
  for (PlaylistItemPtr &item : playlist_items) {
    item = RestoreCueData(item, state);
  }

}

PlaylistItemPtr PlaylistBackend::NewFileItemFromSavedMetadata(const SqlRow &row, const QUuid &uuid) {

  // The playlist_items columns follow the songs table columns.
  Song song;
  song.InitFromQuery(row, false, static_cast<int>(Song::kRowIdColumns.count()));
  if (song.url().isEmpty()) return nullptr;

  // The ROWID read is the playlist row's own, and the song is no longer in the collection.
  const Song::Source source = song.url().isLocalFile() ? Song::Source::LocalFile : Song::Source::Unknown;
  song.set_id(-1);
  song.set_directory_id(-1);
  song.set_source(source);

  PlaylistItemPtr item = make_shared<SongPlaylistItem>(source, uuid);
  item->SetOriginalMetadata(song);

  return item;

}

Song PlaylistBackend::ReloadPlaylistItem(PlaylistItemPtr item) const {

  const Song original_song = item->OriginalMetadata();
  if (!original_song.url().isLocalFile()) return Song();

  Song result = original_song;
  const TagReaderResult tag_result = tagreader_client_->ReadFileBlocking(result.url().toLocalFile(), &result);
  if (!tag_result.success()) {
    qLog(Error) << "Could not reload file" << result.url() << tag_result.error_string();
    return Song();
  }

  return result;

}

// If song had a CUE and the CUE still exists, the metadata from it will be applied here.

PlaylistItemPtr PlaylistBackend::RestoreCueData(PlaylistItemPtr item, SharedPtr<NewSongFromQueryState> state) {

  // We need collection to run a CueParser; also, this method applies only to file-type PlaylistItems
  if (item->source() != Song::Source::LocalFile) return item;

  CueParser cue_parser(tagreader_client_, collection_backend_);

  const Song song = item->EffectiveMetadata();
  // We're only interested in .cue songs here
  if (!song.has_cue()) return item;

  QString cue_path = song.cue_path();
  // If .cue was deleted - reload the song
  if (!QFile::exists(cue_path)) {
    const Song reloaded_song = ReloadPlaylistItem(item);
    if (reloaded_song.is_valid()) {
      item->SetOriginalMetadata(reloaded_song);
    }
    return item;
  }

  SongList songs;
  {
    QMutexLocker locker(&state->mutex_);

    if (!state->cached_cues_.contains(cue_path)) {
      QFile cue_file(cue_path);
      if (!cue_file.open(QIODevice::ReadOnly)) return item;

      songs = cue_parser.Load(&cue_file, cue_path, QDir(cue_path.section(u'/', 0, -2))).songs;
      cue_file.close();
      state->cached_cues_[cue_path] = songs;
    }
    else {
      songs = state->cached_cues_[cue_path];
    }
  }

  for (const Song &from_list : std::as_const(songs)) {
    if (from_list.url().toEncoded() == song.url().toEncoded() && from_list.beginning_nanosec() == song.beginning_nanosec()) {
      // We found a matching section; replace the input item with a new one containing CUE metadata
      return make_shared<SongPlaylistItem>(from_list);
    }
  }

  // There's no such section in the related .cue -> reload the song
  const Song reloaded_song = ReloadPlaylistItem(item);
  if (reloaded_song.is_valid()) {
    item->SetOriginalMetadata(reloaded_song);
  }

  return item;

}

void PlaylistBackend::SavePlaylistAsync(const int playlist_id, const PlaylistItemSaveDataList &items, const int last_played, PlaylistGeneratorPtr dynamic) {

  QMetaObject::invokeMethod(this, "SavePlaylist", Qt::QueuedConnection, Q_ARG(int, playlist_id), Q_ARG(PlaylistItemSaveDataList, items), Q_ARG(int, last_played), Q_ARG(PlaylistGeneratorPtr, dynamic));

}

void PlaylistBackend::SavePlaylist(const int playlist_id, const PlaylistItemSaveDataList &items, const int last_played, PlaylistGeneratorPtr dynamic) {

  QMutexLocker l(database_->Mutex());
  QSqlDatabase db(database_->Connect());

  qLog(Debug) << "Saving playlist" << playlist_id;

  ScopedTransaction transaction(&db);

  // The save is queued, the items of a playlist which was removed before it runs are not saved again.
  {
    SqlQuery q(db);
    q.prepare(u"SELECT ROWID FROM playlists WHERE ROWID = :playlist"_s);
    q.BindValue(u":playlist"_s, playlist_id);
    if (!q.Exec()) {
      database_->ReportErrors(q);
      return;
    }
    if (!q.next()) {
      qLog(Debug) << "Not saving removed playlist" << playlist_id;
      return;
    }
  }

  // Clear the existing items in the playlist
  {
    SqlQuery q(db);
    q.prepare(u"DELETE FROM playlist_items WHERE playlist = :playlist"_s);
    q.BindValue(u":playlist"_s, playlist_id);
    if (!q.Exec()) {
      database_->ReportErrors(q);
      return;
    }
  }

  // Save the new ones
  for (const PlaylistItemSaveData &item : items) {
    SqlQuery q(db);
    q.prepare(u"INSERT INTO playlist_items (playlist, type, uuid, collection_id, "_s + Song::kColumnSpec + u") VALUES (:playlist, :type, :uuid, :collection_id, "_s + Song::kBindSpec + u")"_s);
    q.BindValue(u":playlist"_s, playlist_id);
    q.BindValue(u":type"_s, static_cast<int>(item.source));
    q.BindValue(u":uuid"_s, item.uuid.toString(QUuid::WithoutBraces));
    q.BindValue(u":collection_id"_s, item.collection_id);
    item.song.BindToQuery(&q);
    if (!q.Exec()) {
      database_->ReportErrors(q);
      return;
    }
  }

  if (!DetachMissingCollectionSongs(db, playlist_id)) return;

  // Update the last played track number
  {
    SqlQuery q(db);
    q.prepare(u"UPDATE playlists SET last_played=:last_played, dynamic_playlist_type=:dynamic_type, dynamic_playlist_data=:dynamic_data, dynamic_playlist_backend=:dynamic_backend WHERE ROWID=:playlist"_s);
    q.BindValue(u":last_played"_s, last_played);
    if (dynamic) {
      q.BindValue(u":dynamic_type"_s, static_cast<int>(dynamic->type()));
      q.BindValue(u":dynamic_data"_s, dynamic->Save());
      q.BindValue(u":dynamic_backend"_s, dynamic->collection()->songs_table());
    }
    else {
      q.BindValue(u":dynamic_type"_s, 0);
      q.BindValue(u":dynamic_data"_s, QByteArray());
      q.BindValue(u":dynamic_backend"_s, QString());
    }
    q.BindValue(u":playlist"_s, playlist_id);
    if (!q.Exec()) {
      database_->ReportErrors(q);
      return;
    }
  }

  transaction.Commit();

}

void PlaylistBackend::SavePlaylistItemsAsync(const int playlist_id, const PlaylistItemSaveDataList &items) {

  QMetaObject::invokeMethod(this, "SavePlaylistItems", Qt::QueuedConnection, Q_ARG(int, playlist_id), Q_ARG(PlaylistItemSaveDataList, items));

}

void PlaylistBackend::SavePlaylistItems(const int playlist_id, const PlaylistItemSaveDataList &items) {

  QMutexLocker l(database_->Mutex());
  QSqlDatabase db(database_->Connect());

  qLog(Debug) << "Saving" << items.count() << "items in playlist" << playlist_id;

  ScopedTransaction transaction(&db);

  // The row order of a playlist is its ROWID order, which an update leaves alone, unlike the delete and re-insert SavePlaylist() does.
  for (const PlaylistItemSaveData &item : items) {
    SqlQuery q(db);
    q.prepare(u"UPDATE playlist_items SET type=:type, collection_id=:collection_id, "_s + Song::kUpdateSpec + u" WHERE playlist=:playlist AND uuid=:uuid"_s);
    q.BindValue(u":playlist"_s, playlist_id);
    q.BindValue(u":type"_s, static_cast<int>(item.source));
    q.BindValue(u":uuid"_s, item.uuid.toString(QUuid::WithoutBraces));
    q.BindValue(u":collection_id"_s, item.collection_id);
    item.song.BindToQuery(&q);
    if (!q.Exec()) {
      database_->ReportErrors(q);
      return;
    }
  }

  if (!DetachMissingCollectionSongs(db, playlist_id)) return;

  transaction.Commit();

}

bool PlaylistBackend::DetachMissingCollectionSongs(const QSqlDatabase &db, const int playlist_id) {

  // Collection items are saved with the ROWID of their song, which SQLite gives to the next song added once the song is deleted.
  // An item whose song no longer exists, or whose ROWID now belongs to a song with another URL, is changed to a file item with the metadata saved with it.
  // Otherwise it would load as an empty item, or as the other song.
  SqlQuery q(db);
  q.prepare(u"UPDATE playlist_items SET type = :file_type, source = :file_source, collection_id = NULL "
             "WHERE playlist = :playlist AND type = :collection_type AND IFNULL(url, '') != '' "
             "AND NOT EXISTS (SELECT 1 FROM songs WHERE songs.ROWID = playlist_items.collection_id AND songs.source = :collection_source AND songs.url = playlist_items.url)"_s);
  q.BindValue(u":file_type"_s, static_cast<int>(Song::Source::LocalFile));
  q.BindValue(u":file_source"_s, static_cast<int>(Song::Source::LocalFile));
  q.BindValue(u":playlist"_s, playlist_id);
  q.BindValue(u":collection_type"_s, static_cast<int>(Song::Source::Collection));
  q.BindValue(u":collection_source"_s, static_cast<int>(Song::Source::Collection));
  if (!q.Exec()) {
    database_->ReportErrors(q);
    return false;
  }

  return true;

}

void PlaylistBackend::SavePlaylistLastPlayedAsync(const int playlist_id, const int last_played) {

  QMetaObject::invokeMethod(this, "SavePlaylistLastPlayed", Qt::QueuedConnection, Q_ARG(int, playlist_id), Q_ARG(int, last_played));

}

void PlaylistBackend::SavePlaylistLastPlayed(const int playlist_id, const int last_played) {

  QMutexLocker l(database_->Mutex());
  QSqlDatabase db(database_->Connect());

  qLog(Debug) << "Saving last played" << last_played << "in playlist" << playlist_id;

  SqlQuery q(db);
  q.prepare(u"UPDATE playlists SET last_played=:last_played WHERE ROWID=:playlist"_s);
  q.BindValue(u":last_played"_s, last_played);
  q.BindValue(u":playlist"_s, playlist_id);

  if (!q.Exec()) {
    database_->ReportErrors(q);
  }

}

int PlaylistBackend::CreatePlaylist(const QString &name, const QString &special_type) {

  QMutexLocker l(database_->Mutex());
  QSqlDatabase db(database_->Connect());

  SqlQuery q(db);
  q.prepare(u"INSERT INTO playlists (name, special_type) VALUES (:name, :special_type)"_s);
  q.BindValue(u":name"_s, name);
  q.BindValue(u":special_type"_s, special_type);
  if (!q.Exec()) {
    database_->ReportErrors(q);
    return -1;
  }

  return q.lastInsertId().toInt();

}

void PlaylistBackend::RemovePlaylistAsync(const int id) {

  QMetaObject::invokeMethod(this, [this, id]() { RemovePlaylist(id); }, Qt::QueuedConnection);

}

void PlaylistBackend::NotifyPlaylistSavedAsync(const int id, const quint64 sequence) {

  QMetaObject::invokeMethod(this, [this, id, sequence]() { Q_EMIT PlaylistSaved(id, sequence); }, Qt::QueuedConnection);

}

void PlaylistBackend::RemovePlaylist(int id) {

  QMutexLocker l(database_->Mutex());
  QSqlDatabase db(database_->Connect());

  ScopedTransaction transaction(&db);

  {
    SqlQuery q(db);
    q.prepare(u"DELETE FROM playlists WHERE ROWID=:id"_s);
    q.BindValue(u":id"_s, id);
    if (!q.Exec()) {
      database_->ReportErrors(q);
      return;
    }
  }

  {
    SqlQuery q(db);
    q.prepare(u"DELETE FROM playlist_items WHERE playlist=:id"_s);
    q.BindValue(u":id"_s, id);
    if (!q.Exec()) {
      database_->ReportErrors(q);
      return;
    }
  }

  transaction.Commit();

}

void PlaylistBackend::RenamePlaylist(const int id, const QString &new_name) {

  QMutexLocker l(database_->Mutex());
  QSqlDatabase db(database_->Connect());
  SqlQuery q(db);
  q.prepare(u"UPDATE playlists SET name=:name WHERE ROWID=:id"_s);
  q.BindValue(u":name"_s, new_name);
  q.BindValue(u":id"_s, id);

  if (!q.Exec()) {
    database_->ReportErrors(q);
  }

}

void PlaylistBackend::FavoritePlaylist(const int id, const bool is_favorite) {

  QMutexLocker l(database_->Mutex());
  QSqlDatabase db(database_->Connect());
  SqlQuery q(db);
  q.prepare(u"UPDATE playlists SET is_favorite=:is_favorite WHERE ROWID=:id"_s);
  q.BindValue(u":is_favorite"_s, is_favorite ? 1 : 0);
  q.BindValue(u":id"_s, id);

  if (!q.Exec()) {
    database_->ReportErrors(q);
  }

}

void PlaylistBackend::SetPlaylistOrder(const QList<int> &ids) {

  QMutexLocker l(database_->Mutex());
  QSqlDatabase db(database_->Connect());
  ScopedTransaction transaction(&db);

  SqlQuery q(db);
  q.prepare(u"UPDATE playlists SET ui_order=-1"_s);
  if (!q.Exec()) {
    database_->ReportErrors(q);
    return;
  }

  q.prepare(u"UPDATE playlists SET ui_order=:index WHERE ROWID=:id"_s);
  for (int i = 0; i < ids.count(); ++i) {
    q.BindValue(u":index"_s, i);
    q.BindValue(u":id"_s, ids[i]);
    if (!q.Exec()) {
      database_->ReportErrors(q);
      return;
    }
  }

  transaction.Commit();

}

void PlaylistBackend::SetPlaylistUiPath(const int id, const QString &path) {

  QMutexLocker l(database_->Mutex());
  QSqlDatabase db(database_->Connect());
  SqlQuery q(db);
  q.prepare(u"UPDATE playlists SET ui_path=:path WHERE ROWID=:id"_s);

  ScopedTransaction transaction(&db);

  q.BindValue(u":path"_s, path);
  q.BindValue(u":id"_s, id);
  if (!q.Exec()) {
    database_->ReportErrors(q);
    return;
  }

  transaction.Commit();

}
