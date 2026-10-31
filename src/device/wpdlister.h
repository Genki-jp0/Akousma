/*
 * Akousma
 * Copyright 2026, Genki (genki_jp0) <86833542+Genki-jp0@users.noreply.github.com>
 * Based on Strawberry's device listers, which were part of Clementine:
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2018-2026, Jonas Kvinge <jonas@jkvinge.net>
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

#ifndef WPDLISTER_H
#define WPDLISTER_H

#include "config.h"

#include <QObject>
#include <QMutex>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QUrl>

#include "includes/scoped_ptr.h"
#include "devicelister.h"

class QTimer;
class QThread;
class WpdScopedComInit;
class WpdConnection;

// Windows only: lists the MTP devices (phones, music players) Windows reaches through the Windows Portable Devices API.
// Drives (USB sticks, players in mass storage mode, "MSC" devices in WPD) are left to the GIO lister, which shows them
// by drive letter, and cameras (PTP) are left out.
class WpdLister : public DeviceLister {
  Q_OBJECT

 public:
  explicit WpdLister(QObject *parent = nullptr);
  ~WpdLister() override;

  QStringList DeviceUniqueIDs() override;
  QVariantList DeviceIcons(const QString &id) override;
  QString DeviceManufacturer(const QString &id) override;
  QString DeviceModel(const QString &id) override;
  quint64 DeviceCapacity(const QString &id) override;
  quint64 DeviceFreeSpace(const QString &id) override;
  QVariantMap DeviceHardwareInfo(const QString &id) override;

  QString MakeFriendlyName(const QString &id) override;
  QList<QUrl> MakeDeviceUrls(const QString &id) override;

 public Q_SLOTS:
  void UpdateDeviceFreeSpace(const QString &id) override;
  void ShutDown() override;

 protected:
  bool Init() override;

 private Q_SLOTS:
  void ScheduleRescan();
  void Rescan();

 private:
  struct DeviceInfo {
    QString pnp_device_id;
    QString friendly_name;
    QString manufacturer;
    QString model;
    QString serial_number;
    QString protocol;
    int type = -1;
    int storage_count = 0;
    quint64 capacity = 0;
    quint64 free_space = 0;
  };

  static void DevicesChanged(void *context);
  bool ReadDevice(const QString &pnp_device_id, DeviceInfo *info);
  // The connection the lister is using, so ShutDown() can cancel it from another thread.
  void SetBusyConnection(WpdConnection *connection);
  bool shutting_down();

 private:
  QMutex mutex_;
  QMap<QString, DeviceInfo> devices_;  // Unique ID -> device

  QMutex connection_mutex_;
  WpdConnection *busy_connection_;
  bool shutting_down_;

  ScopedPtr<WpdScopedComInit> com_;
  QThread *com_thread_;
  void *notification_;
  QTimer *rescan_timer_;
  QTimer *poll_timer_;
};

#endif  // WPDLISTER_H
