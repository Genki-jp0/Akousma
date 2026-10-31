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

#ifndef WPDDEVICE_H
#define WPDDEVICE_H

#include "config.h"

#include <QObject>
#include <QMutex>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "includes/scoped_ptr.h"
#include "includes/shared_ptr.h"
#include "core/song.h"
#include "connecteddevice.h"

class QThread;
class DeviceLister;
class DeviceManager;
class TaskManager;
class Database;
class TagReaderClient;
class AlbumCoverLoader;
class WpdLoader;
class WpdConnection;
class WpdMtaUsage;

// Windows only: an MTP device (phone, music player) through the Windows Portable Devices API. Mirrors MtpDevice (libmtp).
// Songs are copied to Music\<the organize destination> on the storage with the most free space.
class WpdDevice : public ConnectedDevice {
  Q_OBJECT

 public:
  Q_INVOKABLE WpdDevice(const QUrl &url,
                        DeviceLister *lister,
                        const QString &unique_id,
                        DeviceManager *device_manager,
                        const SharedPtr<TaskManager> task_manager,
                        const SharedPtr<Database> database,
                        const SharedPtr<TagReaderClient> tagreader_client,
                        const SharedPtr<AlbumCoverLoader> albumcover_loader,
                        const int database_id,
                        const bool first_time,
                        QObject *parent = nullptr);

  ~WpdDevice() override;

  static QStringList url_schemes();

  bool Init() override;
  void ConnectAsync() override;
  void Close() override;
  bool IsLoading() override { return loader_; }

  bool GetSupportedFiletypes(QList<Song::FileType> *ret) override;

  bool StartCopy(QList<Song::FileType> *supported_types) override;
  bool CopyToStorage(const CopyJob &job, QString &error_text) override;
  bool FinishCopy(const bool success, QString &error_text) override;

  void StartDelete() override;
  bool DeleteFromStorage(const DeleteJob &job) override;
  bool FinishDelete(const bool success, QString &error_text) override;

 private Q_SLOTS:
  void LoadFinished(const bool success, WpdConnection *connection);
  void LoaderError(const QString &message);

 private:
  bool GetSupportedFiletypes(QList<Song::FileType> *ret, WpdConnection *connection);
  QString MusicFolder(QString &error_text);
  QString Folder(const QString &parent_id, const QString &name, QString &error_text);

 private:
  ScopedPtr<WpdMtaUsage> mta_usage_;
  const SharedPtr<TaskManager> task_manager_;
  const SharedPtr<TagReaderClient> tagreader_client_;

  WpdLoader *loader_;
  QThread *loader_thread_;
  bool closing_;

  QMutex db_busy_;
  bool db_busy_locked_;
  SongList songs_to_add_;
  SongList songs_to_remove_;

  // Folders found or made during one copy: "<parent object ID>/<lower case name>" -> object ID.
  QHash<QString, QString> folders_;
  QString music_folder_id_;

  ScopedPtr<WpdConnection> connection_;
};

#endif  // WPDDEVICE_H
