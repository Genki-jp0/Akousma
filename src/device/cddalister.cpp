/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2018-2021, Jonas Kvinge <jonas@jkvinge.net>
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

#include <config.h>

#include <cstddef>

#include <cdio/cdio.h>
#include <cdio/device.h>

#include <chrono>
#include <utility>

#include <QtGlobal>
#include <QFileInfo>
#include <QByteArray>
#include <QList>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include "cddalister.h"
#include "cddaurl.h"
#ifdef Q_OS_WIN32
#  include "cddawindows.h"
#endif
#include "core/logging.h"

using namespace std::chrono_literals;
using namespace Qt::Literals::StringLiterals;

CDDALister::CDDALister(QObject *parent) : DeviceLister(parent), timer_update_devices_(nullptr) {}

QStringList CDDALister::DeviceUniqueIDs() {

  QMutexLocker l(&mutex_devices_);
  return devices_list_;

}

QVariantList CDDALister::DeviceIcons(const QString &id) {

  Q_UNUSED(id)

  QVariantList icons;
  icons << u"media-optical"_s;
  return icons;

}

QString CDDALister::DeviceManufacturer(const QString &id) {

#ifdef Q_OS_WIN32
  // What the drive tells Windows; libcdio asks the drive itself (SCSI INQUIRY), which needs more access to the drive.
  QString vendor, model;
  if (CDDAWindows::DriveDescription(id, vendor, model) && !vendor.isEmpty()) return vendor;
#endif

  CdIo_t *cdio = cdio_open(id.toLocal8Bit().constData(), DRIVER_DEVICE);
  if (!cdio) return QString();
  cdio_hwinfo_t cd_info;
  if (cdio_get_hwinfo(cdio, &cd_info)) {
    cdio_destroy(cdio);
    return QString::fromUtf8(cd_info.psz_vendor);
  }
  cdio_destroy(cdio);
  return QString();

}

QString CDDALister::DeviceModel(const QString &id) {

#ifdef Q_OS_WIN32
  QString vendor, model;
  if (CDDAWindows::DriveDescription(id, vendor, model) && !model.isEmpty()) return model;
#endif

  CdIo_t *cdio = cdio_open(id.toLocal8Bit().constData(), DRIVER_DEVICE);
  if (!cdio) return QString();
  cdio_hwinfo_t cd_info;
  if (cdio_get_hwinfo(cdio, &cd_info)) {
    cdio_destroy(cdio);
    return QString::fromUtf8(cd_info.psz_model);
  }
  cdio_destroy(cdio);
  return QString();

}

quint64 CDDALister::DeviceCapacity(const QString &id) {

  Q_UNUSED(id)

  return 0;

}

quint64 CDDALister::DeviceFreeSpace(const QString &id) {

  Q_UNUSED(id)

  return 0;

}

QVariantMap CDDALister::DeviceHardwareInfo(const QString &id) {
  Q_UNUSED(id)
  return QVariantMap();
}

QString CDDALister::MakeFriendlyName(const QString &id) {

#ifdef Q_OS_WIN32
  // The drive's model and its letter, the way Windows users know their drives: "HL-DT-ST DVDRAM GP65NB60 (E:)".
  QString model = DeviceModel(id).trimmed();
  if (model.isEmpty()) model = u"CD"_s;
  return model + u" ("_s + id + u')';
#else
  CdIo_t *cdio = cdio_open(id.toLocal8Bit().constData(), DRIVER_DEVICE);
  if (!cdio) return QString();
  cdio_hwinfo_t cd_info;
  if (cdio_get_hwinfo(cdio, &cd_info)) {
    const QString friendly_name = QString::fromUtf8(cd_info.psz_model).trimmed();
    cdio_destroy(cdio);
    return friendly_name;
  }
  cdio_destroy(cdio);
  return u"CD ("_s + id + QLatin1Char(')');
#endif

}

QList<QUrl> CDDALister::MakeDeviceUrls(const QString &id) {
  // cdda:///dev/sr0, cdda:///D: on Windows.
  return QList<QUrl>() << CDDAUrl::DeviceUrl(id);
}

void CDDALister::UnmountDevice(const QString &id) {
  cdio_eject_media_drive(id.toLocal8Bit().constData());
}

void CDDALister::UpdateDeviceFreeSpace(const QString &id) {
  Q_UNUSED(id)
}

bool CDDALister::Init() {

  cdio_init();
#ifdef Q_OS_MACOS
  if (!cdio_have_driver(DRIVER_OSX)) {
    qLog(Error) << "libcdio was compiled without support for macOS!";
  }
#endif

#ifdef Q_OS_WIN32
  if (!cdio_have_driver(DRIVER_WIN32)) {
    qLog(Error) << "libcdio was compiled without support for Windows CD drives";
  }
  // The drive letters of the CD drives, looked at again every 3 seconds: USB drives are plugged in and out, virtual drives
  // (a mounted disc image) come and go. Nothing on the discs is read for this.
  UpdateDevices();
  timer_update_devices_ = new QTimer(this);
  timer_update_devices_->setInterval(3s);
  QObject::connect(timer_update_devices_, &QTimer::timeout, this, &CDDALister::UpdateDevices);
  timer_update_devices_->start();
  return true;
#else
  char **devices = cdio_get_devices(DRIVER_DEVICE);
  if (!devices) {
    qLog(Debug) << "No CD devices found";
    return false;
  }

  for (char **device_ptr = devices; *device_ptr != nullptr; ++device_ptr) {
    QString device = QString::fromUtf8(*device_ptr);
    QFileInfo device_info(device);
    if (device_info.isSymLink()) {
      device = device_info.symLinkTarget();
    }
#ifdef Q_OS_MACOS
    // Every track is detected as a separate device on Darwin. The raw disk looks like /dev/rdisk1
    if (!device.contains(QRegularExpression(u"^/dev/rdisk[0-9]$"_s))) {
      continue;
    }
#endif
    bool added = false;
    {
      QMutexLocker l(&mutex_devices_);
      if (!devices_list_.contains(device)) {
        devices_list_ << device;
        added = true;
      }
    }
    if (added) {
      Q_EMIT DeviceAdded(device);
    }
  }

  cdio_free_device_list(devices);

  return true;
#endif  // Q_OS_WIN32

}

void CDDALister::UpdateDevices() {

#ifdef Q_OS_WIN32
  const QStringList drives = CDDAWindows::Drives();
  QStringList added;
  QStringList removed;
  {
    QMutexLocker l(&mutex_devices_);
    for (const QString &drive : drives) {
      if (!devices_list_.contains(drive)) added << drive;
    }
    for (const QString &drive : std::as_const(devices_list_)) {
      if (!drives.contains(drive)) removed << drive;
    }
    devices_list_ = drives;
  }
  for (const QString &drive : std::as_const(removed)) {
    qLog(Info) << "CD drive" << drive << "is gone";
    Q_EMIT DeviceRemoved(drive);
  }
  for (const QString &drive : std::as_const(added)) {
    qLog(Info) << "CD drive" << drive << "found";
    Q_EMIT DeviceAdded(drive);
  }
#endif

}

void CDDALister::ShutDown() {

  if (timer_update_devices_) {
    timer_update_devices_->stop();
  }

}

bool CDDALister::AskForScan(const QString &id) const {

  Q_UNUSED(id)

  return false;

}
