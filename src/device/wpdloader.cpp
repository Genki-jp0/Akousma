/*
 * Akousma
 * Copyright 2026, Genki (genki_jp0) <86833542+Genki-jp0@users.noreply.github.com>
 * Based on Strawberry's MTP device support, which was part of Clementine:
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2019-2026, Jonas Kvinge <jonas@jkvinge.net>
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

#include <memory>

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QElapsedTimer>
#include <QList>
#include <QString>
#include <QUrl>
#include <QUuid>

#include "includes/shared_ptr.h"
#include "core/logging.h"
#include "core/taskmanager.h"
#include "core/song.h"
#include "collection/collectionbackend.h"
#include "tagreader/tagreaderclient.h"
#include "wpdmetadata.h"
#include "wpdconnection.h"
#include "wpdloader.h"

using namespace Qt::Literals::StringLiterals;
using std::make_unique;

namespace {
// How much of a song is read for its tags when the device has no metadata for it.
constexpr qint64 kTagReadBytes = 2 * 1024 * 1024;
// A device that is busy when it's opened (another program has it, or a phone is still setting up USB) is tried this
// often, this far apart.
constexpr int kOpenAttempts = 3;
constexpr int kOpenRetryMsec = 2000;
// How often the progress shown is updated, and how often it is logged.
constexpr qint64 kProgressUpdateMsec = 250;
constexpr qint64 kProgressLogMsec = 10000;
constexpr quint64 kProgressMax = 1000;
}  // namespace

WpdLoader::WpdLoader(const QUrl &url, const SharedPtr<TaskManager> task_manager, const SharedPtr<TagReaderClient> tagreader_client, const SharedPtr<CollectionBackend> backend, QObject *parent)
    : QObject(parent),
      url_(url),
      task_manager_(task_manager),
      tagreader_client_(tagreader_client),
      backend_(backend),
      original_thread_(thread()),
      abort_(false) {}

WpdLoader::~WpdLoader() = default;

void WpdLoader::Abort() {

  QMutexLocker l(&connection_mutex_);
  abort_ = true;
  if (connection_) connection_->Cancel();

}

QString WpdLoader::CurrentOperation(qint64 *elapsed_msec) {

  QMutexLocker l(&connection_mutex_);
  if (!connection_) {
    if (elapsed_msec) *elapsed_msec = 0;
    return QString();
  }
  return connection_->CurrentOperation(elapsed_msec);

}

void WpdLoader::LoadDatabase() {

  const int task_id = task_manager_->StartTask(tr("Loading MTP device"));
  Q_EMIT TaskStarted(task_id);

  const QString pnp_device_id = WpdMetadata::PnpDeviceIdFromDeviceUrl(url_);
  QElapsedTimer timer;
  timer.start();

  bool success = false;
  ScopedPtr<WpdConnection> connection;
  {
    WpdScopedComInit com;
    qLog(Info) << "Loading WPD device" << pnp_device_id << "in a" << WpdConnection::ApartmentName() << "thread";
    success = TryLoad(task_id);
    {
      QMutexLocker l(&connection_mutex_);
      connection.reset(connection_.release());
    }
    // Nobody takes the connection of an aborted load (the device may be gone already): close it here, while this thread
    // still has COM.
    if (abort_) connection.reset();
  }

  qLog(Info) << "Loading WPD device" << pnp_device_id << (success ? "finished" : (abort_ ? "aborted" : "failed")) << "after" << timer.elapsed() << "ms";

  moveToThread(original_thread_);

  task_manager_->SetTaskFinished(task_id);
  Q_EMIT LoadFinished(success, connection.release());

}

bool WpdLoader::SleepUnlessAborted(const int msec) const {

  QElapsedTimer timer;
  timer.start();
  while (!abort_ && timer.elapsed() < msec) QThread::msleep(100);
  return !abort_;

}

bool WpdLoader::OpenConnection() {

  const QString pnp_device_id = WpdMetadata::PnpDeviceIdFromDeviceUrl(url_);

  for (int attempt = 1; attempt <= kOpenAttempts; ++attempt) {
    {
      // Made in two steps, so Abort() can cancel it while it opens.
      ScopedPtr<WpdConnection> connection = make_unique<WpdConnection>(pnp_device_id, false);
      QMutexLocker l(&connection_mutex_);
      if (abort_) return false;
      connection_.swap(connection);
    }

    QElapsedTimer timer;
    timer.start();
    qLog(Info) << "Opening WPD device" << pnp_device_id << "attempt" << attempt;
    if (connection_->Open()) {
      qLog(Info) << "Opened WPD device" << pnp_device_id << "in" << timer.elapsed() << "ms";
      return true;
    }
    if (abort_) return false;

    qLog(Warning) << "Could not open WPD device" << pnp_device_id << "after" << timer.elapsed() << "ms:" << connection_->error_text();
    if (connection_->error_kind() != WpdConnection::ErrorKind::Busy || attempt == kOpenAttempts) return false;
    if (!SleepUnlessAborted(kOpenRetryMsec)) return false;
  }

  return false;

}

bool WpdLoader::TryLoad(const int task_id) {

  const QString pnp_device_id = WpdMetadata::PnpDeviceIdFromDeviceUrl(url_);

  if (!OpenConnection()) {
    if (abort_) return false;
    const QString error_text = connection_ ? connection_->error_text() : QString();
    switch (connection_ ? connection_->error_kind() : WpdConnection::ErrorKind::Other) {
      case WpdConnection::ErrorKind::AccessDenied:
        Q_EMIT Error(tr("The MTP device %1 didn't allow access to its files: %2. Unlock it, allow access to its data when it asks (\"File transfer\"), then connect it again.").arg(pnp_device_id, error_text));
        break;
      case WpdConnection::ErrorKind::Busy:
        Q_EMIT Error(tr("The MTP device %1 is busy: %2. Wait a moment, then connect it again.").arg(pnp_device_id, error_text));
        break;
      default:
        Q_EMIT Error(tr("Error connecting MTP device %1: %2").arg(pnp_device_id, error_text));
        break;
    }
    return false;
  }

  // Only this thread replaces connection_, so it's used here without the lock.
  WpdConnection *connection = connection_.get();

  const QList<WpdConnection::StorageInfo> storages = connection->Storages();
  if (abort_) return false;
  qLog(Info) << "WPD device" << pnp_device_id << "has" << storages.count() << "storages";
  for (const WpdConnection::StorageInfo &storage : storages) {
    qLog(Info) << "Storage" << storage.description << storage.object_id << storage.free_space << "of" << storage.capacity << "bytes free";
  }
  if (storages.isEmpty()) {
    // Phones show no storage until they are unlocked and allow access to their files.
    Q_EMIT Error(tr("The MTP device has no storage it allows access to. Unlock it, allow access to its files (\"File transfer\"), then connect it again."));
    return false;
  }

  // The formats, so the device properties don't have to ask the device while it is being read.
  const QList<QUuid> formats = connection->SupportedAudioFormats();
  if (abort_) return false;
  QList<Song::FileType> filetypes = WpdMetadata::FileTypesForFormats(formats);
  qLog(Info) << "WPD device" << pnp_device_id << "takes" << formats.count() << "audio formats," << filetypes.count() << "known file types";
  if (filetypes.isEmpty()) filetypes = WpdMetadata::DefaultFileTypes();
  Q_EMIT SupportedFiletypesRead(filetypes);

  task_manager_->SetTaskProgress(task_id, 0, kProgressMax);

  SongList songs;
  int songs_read_by_tags = 0;
  WpdConnection::WalkProgress walk_progress;
  QElapsedTimer update_timer;
  update_timer.start();
  QElapsedTimer log_timer;
  log_timer.start();

  // The task shows the part of the folders walked, and how many folders and songs so far.
  auto show_progress = [this, task_id, &songs, &walk_progress, &update_timer, &log_timer, &pnp_device_id]() {
    if (update_timer.elapsed() < kProgressUpdateMsec) return;
    update_timer.restart();
    task_manager_->SetTaskProgress(task_id, static_cast<quint64>(qBound(0.0, walk_progress.fraction, 1.0) * static_cast<double>(kProgressMax)), kProgressMax);
    task_manager_->SetTaskName(task_id, tr("Loading MTP device: %1 folders, %2 songs").arg(walk_progress.folders).arg(songs.count()));
    if (log_timer.elapsed() >= kProgressLogMsec) {
      log_timer.restart();
      qLog(Info) << "Reading WPD device" << pnp_device_id << ":" << walk_progress.folders << "folders," << walk_progress.objects << "files and folders," << songs.count() << "songs," << static_cast<int>(walk_progress.fraction * 100.0) << "%";
    }
  };

  const bool listed = connection->ListSongs([this, &songs, &songs_read_by_tags, &show_progress, connection](const WpdMetadata::ObjectInfo &info) {
    if (abort_) return false;

    Song song = WpdMetadata::ToSong(info);
    if (!song.is_valid()) return true;

    // No metadata from the device (some players and phones): read the tags from the start of the file.
    if (info.title.isEmpty() && info.artist.isEmpty() && info.album.isEmpty() && tagreader_client_) {
      const QString suffix = QFileInfo(song.basefilename()).suffix();
      QTemporaryFile temp_file(QDir::tempPath() + u"/akousma-wpd-XXXXXX."_s + suffix);
      if (temp_file.open()) {
        temp_file.close();
        if (connection->DownloadHead(info.object_id, temp_file.fileName(), kTagReadBytes)) {
          Song tags;
          if (tagreader_client_->ReadFileBlocking(temp_file.fileName(), &tags).success() && tags.is_valid()) {
            if (!tags.title().isEmpty()) song.set_title(tags.title());
            song.set_artist(tags.artist());
            song.set_albumartist(tags.albumartist());
            song.set_album(tags.album());
            song.set_genre(tags.genre());
            song.set_composer(tags.composer());
            if (tags.track() > 0) song.set_track(tags.track());
            if (tags.disc() > 0) song.set_disc(tags.disc());
            if (tags.year() > 0) song.set_year(tags.year());
            // Only the start of the file was read, so the length, bitrate and size come from the device.
            if (song.samplerate() <= 0) song.set_samplerate(tags.samplerate());
            if (song.bitdepth() <= 0) song.set_bitdepth(tags.bitdepth());
            if (songs_read_by_tags == 0) qLog(Info) << "Reading the tags of songs the device has no metadata for, starting with" << song.basefilename();
            ++songs_read_by_tags;
          }
        }
      }
    }

    songs << song;
    show_progress();
    return !abort_;
  }, [&walk_progress, &show_progress](const WpdConnection::WalkProgress &progress) {
    walk_progress = progress;
    show_progress();
  });

  if (abort_) return false;

  if (!listed) {
    // Keep what the collection had rather than replacing it with part of the songs.
    Q_EMIT Error(tr("Error reading the songs on MTP device %1: %2").arg(pnp_device_id, connection->error_text()));
    backend_->Close();
    return false;
  }

  qLog(Info) << "WPD device" << pnp_device_id << "has" << songs.count() << "songs," << songs_read_by_tags << "read from their tags";

  task_manager_->SetTaskProgress(task_id, kProgressMax, kProgressMax);
  task_manager_->SetTaskName(task_id, tr("Loading MTP device: %1 songs").arg(songs.count()));

  QElapsedTimer db_timer;
  db_timer.start();

  // Need to remove all the existing songs in the database first
  backend_->DeleteSongs(backend_->FindSongsInDirectory(1));

  // Add the songs we've just loaded
  backend_->AddOrUpdateSongs(songs);

  // This is done in the loader thread so close the unique DB connection.
  backend_->Close();

  qLog(Info) << "Saved the songs of WPD device" << pnp_device_id << "in" << db_timer.elapsed() << "ms";

  return true;

}
