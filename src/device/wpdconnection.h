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

  using ProgressFunction = std::function<void(const float progress)>;
  // Called for every song found; return false to stop.
  using ObjectFunction = std::function<bool(const WpdMetadata::ObjectInfo &info)>;

  // Opens the device (read and write; read only if the device refuses writing).
  explicit WpdConnection(const QString &pnp_device_id);
  ~WpdConnection();

  bool is_valid() const;
  QString error_text() const { return error_text_; }
  QString pnp_device_id() const { return pnp_device_id_; }

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

  // Walks the storages and calls callback for every song. Skips the Android folder (app data) and hidden folders at
  // the top of a storage. Returns false if callback stopped it or the device failed.
  bool ListSongs(const ObjectFunction &callback);

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
};

#endif  // WPDCONNECTION_H
