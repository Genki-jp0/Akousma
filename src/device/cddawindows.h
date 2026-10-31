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

#ifndef CDDAWINDOWS_H
#define CDDAWINDOWS_H

#include "config.h"

#include <QtGlobal>
#include <QString>
#include <QStringList>

// CD drives on Windows, through the Win32 API: which drive letters are CD drives, what the drive is, and whether the disc
// in it changed. Drives are named D: (see CDDAUrl::NormalizeDevice). None of this needs administrator rights, a disc in
// the drive or exclusive access, and none of it spins a disc up.
namespace CDDAWindows {

// The drive letters of the CD / DVD / BD drives (GetLogicalDrives, GetDriveType DRIVE_CDROM), in order: D:, E:...
// This is also how libcdio finds them (cdio_get_devices), which returns \\.\D: instead.
QStringList Drives();

// The drive's vendor and model, as the drive reports them (IOCTL_STORAGE_QUERY_PROPERTY). False if the drive can't be
// asked; vendor or model can still be empty (many drives report no vendor).
bool DriveDescription(const QString &drive, QString &vendor, QString &model);

// The number of times the drive's media changed since Windows started (IOCTL_STORAGE_CHECK_VERIFY2), or -1 while there
// is no disc that can be read (none, the tray is open, or the drive is still spinning the disc up).
qint64 MediaChangeCount(const QString &drive);

}  // namespace CDDAWindows

#endif  // CDDAWINDOWS_H
