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
#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QString>
#include <QUrl>

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

void WpdLoader::LoadDatabase() {

  const int task_id = task_manager_->StartTask(tr("Loading MTP device"));
  Q_EMIT TaskStarted(task_id);

  bool success = false;
  {
    WpdScopedComInit com;
    success = TryLoad();
  }

  moveToThread(original_thread_);

  task_manager_->SetTaskFinished(task_id);
  Q_EMIT LoadFinished(success, connection_.release());

}

bool WpdLoader::TryLoad() {

  const QString pnp_device_id = WpdMetadata::PnpDeviceIdFromDeviceUrl(url_);
  connection_ = make_unique<WpdConnection>(pnp_device_id);
  if (!connection_->is_valid()) {
    Q_EMIT Error(tr("Error connecting MTP device %1: %2").arg(pnp_device_id, connection_->error_text()));
    return false;
  }

  if (connection_->Storages().isEmpty()) {
    // Phones show no storage until they are unlocked and allow access to their files.
    Q_EMIT Error(tr("The MTP device has no storage it allows access to. Unlock it, allow access to its files (\"File transfer\"), then connect it again."));
    return false;
  }

  SongList songs;
  int songs_read_by_tags = 0;
  const bool listed = connection_->ListSongs([this, &songs, &songs_read_by_tags](const WpdMetadata::ObjectInfo &info) {
    if (abort_) return false;

    Song song = WpdMetadata::ToSong(info);
    if (!song.is_valid()) return true;

    // No metadata from the device (some players and phones): read the tags from the start of the file.
    if (info.title.isEmpty() && info.artist.isEmpty() && info.album.isEmpty() && tagreader_client_) {
      const QString suffix = QFileInfo(song.basefilename()).suffix();
      QTemporaryFile temp_file(QDir::tempPath() + u"/akousma-wpd-XXXXXX."_s + suffix);
      if (temp_file.open()) {
        temp_file.close();
        if (connection_->DownloadHead(info.object_id, temp_file.fileName(), kTagReadBytes)) {
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
            ++songs_read_by_tags;
          }
        }
      }
    }

    songs << song;
    return true;
  });

  if (abort_) return false;

  if (!listed) {
    // Keep what the collection had rather than replacing it with part of the songs.
    Q_EMIT Error(tr("Error reading the songs on MTP device %1: %2").arg(pnp_device_id, connection_->error_text()));
    backend_->Close();
    return false;
  }

  qLog(Debug) << "WPD device" << pnp_device_id << "has" << songs.count() << "songs," << songs_read_by_tags << "read from their tags";

  // Need to remove all the existing songs in the database first
  backend_->DeleteSongs(backend_->FindSongsInDirectory(1));

  // Add the songs we've just loaded
  backend_->AddOrUpdateSongs(songs);

  // This is done in the loader thread so close the unique DB connection.
  backend_->Close();

  return true;

}
