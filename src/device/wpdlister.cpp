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

#include "config.h"

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QMutex>
#include <QMap>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QUrl>

#include "core/logging.h"
#include "wpdmetadata.h"
#include "wpdconnection.h"
#include "wpdlister.h"

using namespace Qt::Literals::StringLiterals;

namespace {

// Windows sends several notifications for one device; rescan once they settle.
constexpr int kRescanDelayMsec = 500;
// While a phone shows no storage (locked, or not set to file transfer), look again every so often: allowing access
// doesn't add a device.
constexpr int kNoStorageRescanMsec = 5000;
// Without device notifications (CM_Register_Notification failed), look for devices this often.
constexpr int kPollMsec = 10000;

// WPD_DEVICE_TYPES
constexpr int kDeviceTypeCamera = 1;
constexpr int kDeviceTypePhone = 3;
constexpr int kDeviceTypeVideo = 4;

}  // namespace

WpdLister::WpdLister(QObject *parent)
    : DeviceLister(parent),
      com_thread_(nullptr),
      notification_(nullptr),
      rescan_timer_(nullptr),
      poll_timer_(nullptr) {}

WpdLister::~WpdLister() {
  ShutDown();
}

bool WpdLister::Init() {

  // The lister's thread is in the multithreaded apartment for its whole life.
  com_.reset(new WpdScopedComInit);
  com_thread_ = QThread::currentThread();

  rescan_timer_ = new QTimer(this);
  rescan_timer_->setSingleShot(true);
  QObject::connect(rescan_timer_, &QTimer::timeout, this, &WpdLister::Rescan);

  notification_ = WpdConnection::RegisterArrivalNotification(&WpdLister::DevicesChanged, this);
  if (!notification_) {
    qLog(Warning) << "No notifications for MTP devices, looking for them every" << kPollMsec / 1000 << "seconds";
    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(kPollMsec);
    QObject::connect(poll_timer_, &QTimer::timeout, this, &WpdLister::Rescan);
    poll_timer_->start();
  }

  Rescan();

  return true;

}

void WpdLister::ShutDown() {

  if (notification_) {
    WpdConnection::UnregisterArrivalNotification(notification_);
    notification_ = nullptr;
  }

  // The timer and the apartment belong to the lister's thread; ShutDown() can also be called from the main thread when
  // the application is torn down without the lister exiting first.
  if (QThread::currentThread() == com_thread_) {
    if (rescan_timer_) rescan_timer_->stop();
    if (poll_timer_) poll_timer_->stop();
    com_.reset();
  }
  else {
    Q_UNUSED(com_.release());
  }

}

void WpdLister::DevicesChanged(void *context) {

  // On a thread pool thread.
  QMetaObject::invokeMethod(static_cast<WpdLister*>(context), "ScheduleRescan", Qt::QueuedConnection);

}

void WpdLister::ScheduleRescan() {

  if (!rescan_timer_) return;
  rescan_timer_->start(kRescanDelayMsec);

}

bool WpdLister::ReadDevice(const QString &pnp_device_id, DeviceInfo *info) {

  WpdConnection connection(pnp_device_id);
  if (!connection.is_valid()) {
    qLog(Debug) << "Could not open WPD device" << pnp_device_id << connection.error_text();
    return false;
  }

  WpdConnection::DeviceInfo device_info;
  if (!connection.ReadDeviceInfo(&device_info)) {
    qLog(Debug) << "Could not read WPD device" << pnp_device_id << connection.error_text();
    return false;
  }

  if (WpdMetadata::IsMassStorageProtocol(device_info.protocol)) {
    qLog(Debug) << "WPD device" << pnp_device_id << "is a drive (" << device_info.protocol << "), listed by its drive letter";
    return false;
  }
  if (device_info.protocol.startsWith("PTP"_L1, Qt::CaseInsensitive) || device_info.type == kDeviceTypeCamera || device_info.type == kDeviceTypeVideo) {
    qLog(Debug) << "WPD device" << pnp_device_id << "is a camera (" << device_info.protocol << device_info.type << "), not listed";
    return false;
  }

  info->pnp_device_id = pnp_device_id;
  info->manufacturer = device_info.manufacturer;
  info->model = device_info.model;
  info->serial_number = device_info.serial_number;
  info->protocol = device_info.protocol;
  info->type = device_info.type;

  // The name Explorer shows (the user can rename phones), then the device's own name.
  info->friendly_name = WpdConnection::FriendlyNameOfDevice(pnp_device_id);
  if (info->friendly_name.isEmpty()) info->friendly_name = device_info.friendly_name;
  if (info->friendly_name.isEmpty()) info->friendly_name = QStringList({ device_info.manufacturer, device_info.model }).join(u' ').trimmed();
  if (info->friendly_name.isEmpty()) info->friendly_name = tr("MTP device");

  const QList<WpdConnection::StorageInfo> storages = connection.Storages();
  info->storage_count = static_cast<int>(storages.count());
  for (const WpdConnection::StorageInfo &storage : storages) {
    info->capacity += storage.capacity;
    info->free_space += storage.free_space;
  }

  qLog(Debug) << "WPD device" << info->friendly_name << info->protocol << "with" << info->storage_count << "storages," << info->free_space << "of" << info->capacity << "bytes free";

  return true;

}

void WpdLister::Rescan() {

  const QStringList pnp_device_ids = WpdConnection::DevicePnpIds();

  QMap<QString, DeviceInfo> old_devices;
  {
    QMutexLocker l(&mutex_);
    old_devices = devices_;
  }

  QMap<QString, DeviceInfo> devices;
  bool waiting_for_storage = false;
  for (const QString &pnp_device_id : pnp_device_ids) {
    // Devices that are listed already and show their storage are kept as they are.
    bool known = false;
    for (auto it = old_devices.cbegin(); it != old_devices.cend(); ++it) {
      if (it.value().pnp_device_id == pnp_device_id && it.value().storage_count > 0) {
        devices.insert(it.key(), it.value());
        known = true;
        break;
      }
    }
    if (known) continue;

    DeviceInfo info;
    if (!ReadDevice(pnp_device_id, &info)) continue;
    if (info.storage_count == 0) waiting_for_storage = true;
    devices.insert(WpdMetadata::UniqueId(info.manufacturer, info.model, info.serial_number, pnp_device_id), info);
  }

  {
    QMutexLocker l(&mutex_);
    devices_ = devices;
  }

  for (auto it = old_devices.cbegin(); it != old_devices.cend(); ++it) {
    if (!devices.contains(it.key())) Q_EMIT DeviceRemoved(it.key());
  }
  for (auto it = devices.cbegin(); it != devices.cend(); ++it) {
    if (!old_devices.contains(it.key())) {
      Q_EMIT DeviceAdded(it.key());
    }
    else {
      const DeviceInfo &old_info = old_devices[it.key()];
      if (old_info.storage_count != it.value().storage_count || old_info.capacity != it.value().capacity || old_info.free_space != it.value().free_space || old_info.friendly_name != it.value().friendly_name) {
        Q_EMIT DeviceChanged(it.key());
      }
    }
  }

  if (waiting_for_storage && rescan_timer_ && !rescan_timer_->isActive()) {
    rescan_timer_->start(kNoStorageRescanMsec);
  }

}

QStringList WpdLister::DeviceUniqueIDs() {

  QMutexLocker l(&mutex_);
  return devices_.keys();

}

QVariantList WpdLister::DeviceIcons(const QString &id) {

  QVariantList ret;
  QMutexLocker l(&mutex_);
  if (!devices_.contains(id)) return ret;
  const DeviceInfo &info = devices_[id];

  ret << GuessIconForModel(info.manufacturer, info.model);
  if (info.type == kDeviceTypePhone) ret << u"device-phone"_s;
  ret << u"device"_s;
  return ret;

}

QString WpdLister::DeviceManufacturer(const QString &id) {

  QMutexLocker l(&mutex_);
  return devices_.contains(id) ? devices_[id].manufacturer : QString();

}

QString WpdLister::DeviceModel(const QString &id) {

  QMutexLocker l(&mutex_);
  return devices_.contains(id) ? devices_[id].model : QString();

}

quint64 WpdLister::DeviceCapacity(const QString &id) {

  QMutexLocker l(&mutex_);
  return devices_.contains(id) ? devices_[id].capacity : 0;

}

quint64 WpdLister::DeviceFreeSpace(const QString &id) {

  QMutexLocker l(&mutex_);
  return devices_.contains(id) ? devices_[id].free_space : 0;

}

QVariantMap WpdLister::DeviceHardwareInfo(const QString &id) {

  QVariantMap ret;
  QMutexLocker l(&mutex_);
  if (!devices_.contains(id)) return ret;
  const DeviceInfo &info = devices_[id];

  ret[QStringLiteral(QT_TR_NOOP("Protocol"))] = info.protocol;
  ret[QStringLiteral(QT_TR_NOOP("Serial number"))] = info.serial_number;
  ret[QStringLiteral(QT_TR_NOOP("Storages"))] = info.storage_count;
  ret[QStringLiteral(QT_TR_NOOP("Device"))] = info.pnp_device_id;
  return ret;

}

QString WpdLister::MakeFriendlyName(const QString &id) {

  QMutexLocker l(&mutex_);
  return devices_.contains(id) ? devices_[id].friendly_name : QString();

}

QList<QUrl> WpdLister::MakeDeviceUrls(const QString &id) {

  QMutexLocker l(&mutex_);
  if (!devices_.contains(id)) return QList<QUrl>();
  return QList<QUrl>() << WpdMetadata::DeviceUrl(devices_[id].pnp_device_id);

}

void WpdLister::UpdateDeviceFreeSpace(const QString &id) {

  QString pnp_device_id;
  {
    QMutexLocker l(&mutex_);
    if (!devices_.contains(id)) return;
    pnp_device_id = devices_[id].pnp_device_id;
  }

  WpdConnection connection(pnp_device_id);
  if (!connection.is_valid()) return;

  quint64 capacity = 0;
  quint64 free_space = 0;
  const QList<WpdConnection::StorageInfo> storages = connection.Storages();
  for (const WpdConnection::StorageInfo &storage : storages) {
    capacity += storage.capacity;
    free_space += storage.free_space;
  }

  {
    QMutexLocker l(&mutex_);
    if (!devices_.contains(id)) return;
    DeviceInfo &info = devices_[id];
    info.storage_count = static_cast<int>(storages.count());
    info.capacity = capacity;
    info.free_space = free_space;
  }

  Q_EMIT DeviceChanged(id);

}
