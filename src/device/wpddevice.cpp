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

#include <QThread>
#include <QMutex>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "includes/shared_ptr.h"
#include "core/logging.h"
#include "core/musicstorage.h"
#include "collection/collectionmodel.h"
#include "collection/collectionbackend.h"
#include "connecteddevice.h"
#include "wpdmetadata.h"
#include "wpdconnection.h"
#include "wpdloader.h"
#include "wpddevice.h"

using namespace Qt::Literals::StringLiterals;

class TaskManager;
class Database;
class AlbumCoverLoader;
class DeviceLister;
class DeviceManager;

namespace {
// The folder songs are copied to, at the top of the storage; it's where phones and players look for music.
constexpr char kMusicFolder[] = "Music";
}  // namespace

WpdDevice::WpdDevice(const QUrl &url,
                     DeviceLister *lister,
                     const QString &unique_id,
                     DeviceManager *device_manager,
                     const SharedPtr<TaskManager> task_manager,
                     const SharedPtr<Database> database,
                     const SharedPtr<TagReaderClient> tagreader_client,
                     const SharedPtr<AlbumCoverLoader> albumcover_loader,
                     const int database_id,
                     const bool first_time,
                     QObject *parent)
    : ConnectedDevice(url, lister, unique_id, device_manager, task_manager, database, tagreader_client, albumcover_loader, database_id, first_time, parent),
      mta_usage_(new WpdMtaUsage),
      task_manager_(task_manager),
      tagreader_client_(tagreader_client),
      loader_(nullptr),
      loader_thread_(nullptr),
      closing_(false),
      db_busy_locked_(false) {}

WpdDevice::~WpdDevice() {

  if (loader_) {
    loader_thread_->exit();
    loader_thread_->wait();
    loader_->deleteLater();
    loader_ = nullptr;
    if (db_busy_locked_) {
      db_busy_.unlock();
      db_busy_locked_ = false;
    }
    loader_thread_->deleteLater();
  }

}

QStringList WpdDevice::url_schemes() { return QStringList() << WpdMetadata::UrlScheme(); }

bool WpdDevice::Init() {

  InitBackendDirectory(u"/"_s, first_time_, false);
  collection_model_->Init();

  loader_ = new WpdLoader(url_, task_manager_, tagreader_client_, collection_backend_);
  loader_thread_ = new QThread();
  loader_thread_->setObjectName(u"WpdLoader"_s);
  loader_->moveToThread(loader_thread_);

  QObject::connect(loader_, &WpdLoader::Error, this, &WpdDevice::LoaderError);
  QObject::connect(loader_, &WpdLoader::TaskStarted, this, &WpdDevice::TaskStarted);
  QObject::connect(loader_, &WpdLoader::LoadFinished, this, &WpdDevice::LoadFinished);
  QObject::connect(loader_thread_, &QThread::started, loader_, &WpdLoader::LoadDatabase);

  return true;

}

void WpdDevice::ConnectAsync() {

  db_busy_.lock();
  db_busy_locked_ = true;
  loader_thread_->start();

}

void WpdDevice::Close() {

  closing_ = true;

  if (IsLoading()) {
    loader_->Abort();
  }
  else {
    ConnectedDevice::Close();
  }

}

void WpdDevice::LoadFinished(const bool success, WpdConnection *connection) {

  connection_.reset(connection);

  loader_thread_->exit();
  loader_thread_->wait();
  loader_thread_->deleteLater();
  loader_thread_ = nullptr;
  loader_->deleteLater();
  loader_ = nullptr;
  db_busy_.unlock();
  db_busy_locked_ = false;
  if (closing_) {
    ConnectedDevice::Close();
  }
  else {
    Q_EMIT DeviceConnectFinished(unique_id_, success);
  }

}

void WpdDevice::LoaderError(const QString &message) {
  Q_EMIT Error(message);
}

bool WpdDevice::StartCopy(QList<Song::FileType> *supported_types) {

  // Ensure only one "organize files" can be active at any one time
  db_busy_.lock();

  folders_.clear();
  music_folder_id_.clear();

  // The caller always calls FinishCopy(), also when this fails, which unlocks db_busy_, so don't call it here.
  if (!connection_ || !connection_->is_valid()) return false;

  if (supported_types) {
    WpdScopedComInit com;
    if (!GetSupportedFiletypes(supported_types, connection_.get())) return false;
  }

  return true;

}

QString WpdDevice::Folder(const QString &parent_id, const QString &name, QString &error_text) {

  const QString key = parent_id + u'/' + name.toLower();
  if (folders_.contains(key)) return folders_.value(key);

  const QString folder_id = connection_->FindOrCreateFolder(parent_id, name);
  if (folder_id.isEmpty()) {
    error_text = connection_->error_text();
    return QString();
  }
  folders_.insert(key, folder_id);
  return folder_id;

}

QString WpdDevice::MusicFolder(QString &error_text) {

  if (!music_folder_id_.isEmpty()) return music_folder_id_;

  // The storage with the most free space: on phones with a memory card, the user may want the card, but the card is
  // usually the bigger one too.
  const QList<WpdConnection::StorageInfo> storages = connection_->Storages();
  const WpdConnection::StorageInfo *best = nullptr;
  for (const WpdConnection::StorageInfo &storage : storages) {
    if (!best || storage.free_space > best->free_space) best = &storage;
  }
  if (!best) {
    error_text = tr("The MTP device has no storage it allows access to. Unlock it and allow access to its files.");
    return QString();
  }

  music_folder_id_ = Folder(best->object_id, QString::fromLatin1(kMusicFolder), error_text);
  return music_folder_id_;

}

bool WpdDevice::CopyToStorage(const CopyJob &job, QString &error_text) {

  if (!connection_ || !connection_->is_valid()) return false;

  WpdScopedComInit com;

  // The organize destination, e.g. "Artist/Album/01 - Title.mp3", below the Music folder.
  QStringList path = WpdMetadata::DestinationPath(job.destination_);
  if (path.isEmpty()) path << WpdMetadata::SafeName(QFileInfo(job.source_).fileName());
  const QString filename = path.takeLast();

  QString parent_id = MusicFolder(error_text);
  for (const QString &folder : std::as_const(path)) {
    if (parent_id.isEmpty()) break;
    parent_id = Folder(parent_id, folder, error_text);
  }
  if (parent_id.isEmpty()) {
    if (error_text.isEmpty()) error_text = tr("Could not create the folders for %1 on the device").arg(job.destination_);
    qLog(Error) << error_text;
    return false;
  }

  // A file with the same name is replaced only when overwriting was asked for.
  const QString existing_id = connection_->FindChild(parent_id, filename);
  if (!existing_id.isEmpty()) {
    if (!job.overwrite_) {
      error_text = tr("%1 is already on the device").arg(job.destination_);
      qLog(Error) << error_text;
      return false;
    }
    if (!connection_->Delete(existing_id)) {
      error_text = tr("Could not replace %1 on the device: %2").arg(job.destination_, connection_->error_text());
      qLog(Error) << error_text;
      return false;
    }
    songs_to_remove_ << collection_backend_->GetSongsByUrl(WpdMetadata::SongUrl(existing_id));
  }

  const QString object_id = connection_->Upload(parent_id, filename, job.source_, job.metadata_, job.progress_);
  if (object_id.isEmpty()) {
    error_text = connection_->error_text();
    qLog(Error) << error_text;
    return false;
  }

  // Add it to our CollectionModel
  Song metadata_on_device = job.metadata_;
  metadata_on_device.set_source(Song::Source::Device);
  metadata_on_device.set_id(-1);
  metadata_on_device.set_url(WpdMetadata::SongUrl(object_id));
  metadata_on_device.set_basefilename(filename);
  if (metadata_on_device.filetype() == Song::FileType::Unknown) metadata_on_device.set_filetype(Song::FiletypeByExtension(QFileInfo(filename).suffix()));
  metadata_on_device.set_filesize(QFileInfo(job.source_).size());
  metadata_on_device.set_mtime(QDateTime::currentSecsSinceEpoch());
  metadata_on_device.set_ctime(metadata_on_device.mtime());
  metadata_on_device.set_directory_id(1);
  songs_to_add_ << metadata_on_device;

  // Remove the original if requested.
  // The song is on the device now, so it's added to the collection even when this fails, but the move failed.
  if (job.remove_original_ && !QFile::remove(job.original())) {
    error_text = tr("Could not remove original file %1").arg(job.original());
    qLog(Error) << error_text;
    return false;
  }

  return true;

}

bool WpdDevice::FinishCopy(const bool success, QString &error_text) {

  // Update the collection with the songs which were sent or deleted, also when others failed, since they are on the device or removed from it.
  if (!songs_to_add_.isEmpty()) collection_backend_->AddOrUpdateSongs(songs_to_add_);
  if (!songs_to_remove_.isEmpty()) collection_backend_->DeleteSongs(songs_to_remove_);

  songs_to_add_.clear();
  songs_to_remove_.clear();
  folders_.clear();
  music_folder_id_.clear();

  // This is done in the organize thread so close the unique DB connection.
  collection_backend_->Close();

  db_busy_.unlock();

  return ConnectedDevice::FinishCopy(success, error_text);

}

void WpdDevice::StartDelete() { StartCopy(nullptr); }

bool WpdDevice::DeleteFromStorage(const DeleteJob &job) {

  if (!connection_ || !connection_->is_valid()) return false;

  const QString object_id = WpdMetadata::ObjectIdFromSongUrl(job.metadata_.url());
  if (object_id.isEmpty()) return false;

  WpdScopedComInit com;
  if (!connection_->Delete(object_id)) return false;

  // Remove it from our collection model
  songs_to_remove_ << job.metadata_;

  return true;

}

bool WpdDevice::FinishDelete(const bool success, QString &error_text) { return FinishCopy(success, error_text); }

bool WpdDevice::GetSupportedFiletypes(QList<Song::FileType> *ret) {

  QMutexLocker l(&db_busy_);

  WpdScopedComInit com;

  // Use the connection the device already holds.
  if (connection_ && connection_->is_valid()) {
    return GetSupportedFiletypes(ret, connection_.get());
  }

  // There's no connection, for example when loading the device failed, so open one only for this.
  WpdConnection connection(WpdMetadata::PnpDeviceIdFromDeviceUrl(url_));
  if (!connection.is_valid()) {
    qLog(Warning) << "Error connecting to MTP device, couldn't get list of supported filetypes:" << connection.error_text();
    return false;
  }

  return GetSupportedFiletypes(ret, &connection);

}

bool WpdDevice::GetSupportedFiletypes(QList<Song::FileType> *ret, WpdConnection *connection) {

  const QList<Song::FileType> filetypes = WpdMetadata::FileTypesForFormats(connection->SupportedAudioFormats());
  if (filetypes.isEmpty()) {
    qLog(Warning) << "The MTP device didn't tell which audio formats it supports, assuming the common ones";
    *ret << Song::FileType::MPEG << Song::FileType::MP4 << Song::FileType::FLAC << Song::FileType::OggVorbis << Song::FileType::OggOpus << Song::FileType::WAV;
    return true;
  }

  *ret << filetypes;
  return true;

}
