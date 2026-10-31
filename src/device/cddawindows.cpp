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

#include "config.h"

#include <windows.h>
#include <winioctl.h>

#include <QtGlobal>
#include <QByteArray>
#include <QString>
#include <QStringList>

#include "cddawindows.h"

using namespace Qt::Literals::StringLiterals;

namespace {

// A handle to the drive (\\.\D:, the device, not a folder on the disc), with the given access; 0 is enough to ask about the
// device, FILE_READ_ATTRIBUTES to ask about its media. Shared, so a player or Explorer using the drive is not in the way.
class DriveHandle {
 public:
  explicit DriveHandle(const QString &drive, const DWORD access) {
    const QString path = u"\\\\.\\"_s + drive;
    handle_ = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  }
  ~DriveHandle() {
    if (is_valid()) CloseHandle(handle_);
  }
  bool is_valid() const { return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr; }
  HANDLE handle() const { return handle_; }

 private:
  Q_DISABLE_COPY(DriveHandle)
  HANDLE handle_;
};

// A string in a STORAGE_DEVICE_DESCRIPTOR: ASCII at the given offset, often padded with spaces; offset 0 means none.
QString DescriptorString(const QByteArray &buffer, const DWORD returned, const DWORD offset) {

  if (offset == 0 || offset >= returned) return QString();
  const char *begin = buffer.constData() + offset;
  const qsizetype max_length = static_cast<qsizetype>(returned - offset);
  qsizetype length = 0;
  while (length < max_length && begin[length] != '\0') ++length;
  return QString::fromLatin1(begin, length).simplified();

}

}  // namespace

namespace CDDAWindows {

QStringList Drives() {

  QStringList drives;
  const DWORD logical_drives = GetLogicalDrives();
  for (int i = 0; i < 26; ++i) {
    if ((logical_drives & (static_cast<DWORD>(1) << i)) == 0) continue;
    const QString drive = QString(QChar(static_cast<char16_t>(u'A' + i))) + u':';
    const QString root = drive + u'\\';
    if (GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16())) == DRIVE_CDROM) {
      drives << drive;
    }
  }

  return drives;

}

bool DriveDescription(const QString &drive, QString &vendor, QString &model) {

  vendor.clear();
  model.clear();

  DriveHandle drive_handle(drive, 0);
  if (!drive_handle.is_valid()) return false;

  STORAGE_PROPERTY_QUERY query{};
  query.PropertyId = StorageDeviceProperty;
  query.QueryType = PropertyStandardQuery;
  QByteArray buffer(1024, '\0');
  DWORD returned = 0;
  if (!DeviceIoControl(drive_handle.handle(), IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer.data(), static_cast<DWORD>(buffer.size()), &returned, nullptr) || returned < sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
    return false;
  }

  const STORAGE_DEVICE_DESCRIPTOR *descriptor = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer.constData());
  vendor = DescriptorString(buffer, returned, descriptor->VendorIdOffset);
  model = DescriptorString(buffer, returned, descriptor->ProductIdOffset);

  return true;

}

qint64 MediaChangeCount(const QString &drive) {

  DriveHandle drive_handle(drive, FILE_READ_ATTRIBUTES);
  if (!drive_handle.is_valid()) return -1;

  ULONG count = 0;
  DWORD returned = 0;
  if (!DeviceIoControl(drive_handle.handle(), IOCTL_STORAGE_CHECK_VERIFY2, nullptr, 0, &count, sizeof(count), &returned, nullptr)) {
    return -1;
  }
  // Drives that don't count media changes still say whether there's a disc: 0 then, and a change shows as no disc in between.
  return returned >= sizeof(count) ? static_cast<qint64>(count) : 0;

}

}  // namespace CDDAWindows
