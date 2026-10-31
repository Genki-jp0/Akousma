/*
 * Akousma
 * Copyright 2026, Genki (genki_jp0) <86833542+Genki-jp0@users.noreply.github.com>
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

#ifndef WPDCONNECTION_H
#define WPDCONNECTION_H

#include "config.h"

#include <functional>

#include <QtGlobal>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

#include "includes/scoped_ptr.h"
#include "core/song.h"
#include "wpdmetadata.h"

// Windows only: MTP devices through the Windows Portable Devices (WPD) API, the layer Windows itself uses for MTP
// (Explorer, Windows Media Player). This header has no Windows types, everything Windows is in wpdconnection.cpp.
//
// COM: the WPD objects are created with CLSID_PortableDeviceFTM (free threaded), so a connection can be used from any
// thread that has COM, and from threads without it while the MTA is kept alive (WpdMtaUsage). Create a ScopedComInit
// on threads that create connections.

// CoInitializeEx(COINIT_MULTITHREADED) for the lifetime of the object; uninitializes only what it initialized
// (a thread that already is in an apartment, like the GUI thread, is left as it is).
class WpdScopedComInit {
 public:
  WpdScopedComInit();
  ~WpdScopedComInit();
  bool initialized() const { return initialized_; }

 private:
  Q_DISABLE_COPY(WpdScopedComInit)
  bool initialized_;
};

// Keeps the process' multithreaded apartment alive (CoIncrementMTAUsage), so connections stay usable when the thread
// that made them ends.
class WpdMtaUsage {
 public:
  WpdMtaUsage();
  ~WpdMtaUsage();

 private:
  Q_DISABLE_COPY(WpdMtaUsage)
  void *cookie_;
};

class WpdConnection {
 public:
  struct DeviceInfo {
    QString friendly_name;
    QString manufacturer;
    QString model;
    QString serial_number;
    QString protocol;
    int type = -1;  // WPD_DEVICE_TYPES, -1 if the device doesn't say
  };

  struct StorageInfo {
    QString object_id;
    QString description;
    quint64 capacity = 0;
    quint64 free_space = 0;
  };

  // Why opening the device failed.
  enum class ErrorKind { None, AccessDenied, Busy, Other };

  // How far ListSongs() is.
  struct WalkProgress {
    int folders = 0;          // Folders listed
    int folders_skipped = 0;  // Folders left out (WpdMetadata::SkipFolder())
    int objects = 0;          // Files and folders seen
    double fraction = 0.0;    // 0 to 1, an estimate (WpdMetadata::ProgressSpan)
  };

  using ProgressFunction = std::function<void(const float progress)>;
  // Called for every song found; return false to stop.
  using ObjectFunction = std::function<bool(const WpdMetadata::ObjectInfo &info)>;
  // Called after every folder ListSongs() looked into.
  using WalkProgressFunction = std::function<void(const WalkProgress &progress)>;

  // Opens the device (read and write; read only if the device refuses writing), unless open is false: then Open()
  // opens it, so another thread can Cancel() it while it opens.
  explicit WpdConnection(const QString &pnp_device_id, const bool open = true);
  ~WpdConnection();

  bool Open();
  bool is_valid() const;
  QString error_text() const { return error_text_; }
  ErrorKind error_kind() const { return error_kind_; }
  QString pnp_device_id() const { return pnp_device_id_; }

  // Thread safe: stops what the connection is doing in another thread (IPortableDevice::Cancel() etc.), and makes it
  // fail what it's asked to do from now on.
  void Cancel();
  bool cancelled() const;

  // Thread safe: the WPD call the connection is waiting for and since how many milliseconds; empty if none.
  QString CurrentOperation(qint64 *elapsed_msec) const;

  // The COM apartment of the calling thread, for the log ("MTA", "STA", "none" etc.).
  static QString ApartmentName();

  // The PnP device IDs of the WPD devices that are connected. The calling thread needs COM.
  static QStringList DevicePnpIds();
  // The friendly name Windows shows for a device (Explorer's name), empty if there is none.
  static QString FriendlyNameOfDevice(const QString &pnp_device_id);

  // Hotplug: calls callback(context) (on a thread pool thread) when a WPD device arrives or is removed.
  // Returns a handle for UnregisterArrivalNotification(), or nullptr.
  static void *RegisterArrivalNotification(void (*callback)(void *context), void *context);
  static void UnregisterArrivalNotification(void *handle);

  bool ReadDeviceInfo(DeviceInfo *info);
  QList<StorageInfo> Storages();
  // The audio formats the device takes (IPortableDeviceCapabilities::GetSupportedFormats(WPD_CONTENT_TYPE_AUDIO)).
  QList<QUuid> SupportedAudioFormats();

  // Walks the storages and calls callback for every song, progress after every folder. Leaves out the folders
  // WpdMetadata::SkipFolder() names. The properties of a folder's files are read in bulk
  // (IPortableDevicePropertiesBulk) when the device can, else one file at a time; the full metadata only for songs.
  // Returns false if callback stopped it, it was cancelled or the device failed.
  bool ListSongs(const ObjectFunction &callback, const WalkProgressFunction &progress = WalkProgressFunction());

  // Reads the start of an object into a file, up to max_bytes (for reading tags of songs the device has no metadata for).
  bool DownloadHead(const QString &object_id, const QString &filename, const qint64 max_bytes);

  // The child with this name (case insensitive), empty if there is none.
  QString FindChild(const QString &parent_id, const QString &name);
  // The folder with this name under parent, created if it isn't there. Empty with error_text set on failure.
  QString FindOrCreateFolder(const QString &parent_id, const QString &name);
  // Copies source to a new object under parent. Returns the object's ID, empty with error_text set on failure.
  QString Upload(const QString &parent_id, const QString &filename, const QString &source, const Song &metadata, const ProgressFunction &progress);
  bool Delete(const QString &object_id);

 private:
  Q_DISABLE_COPY(WpdConnection)
  struct Private;
  ScopedPtr<Private> d_;
  QString pnp_device_id_;
  QString error_text_;
  ErrorKind error_kind_;
};

#endif  // WPDCONNECTION_H
