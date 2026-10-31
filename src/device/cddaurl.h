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

#ifndef CDDAURL_H
#define CDDAURL_H

#include "config.h"

#include <QString>
#include <QUrl>

// The cdda:// URLs of CD drives and of the tracks on their discs, on every platform.
// A drive is a device path on Linux and the BSDs (/dev/sr0), and a drive letter on Windows (D:, which libcdio also gets
// as \\.\D:). A drive's URL is cdda:// followed by the path, with a slash before a drive letter: cdda:///dev/sr0 and
// cdda:///D:. A track's URL adds the track number: cdda:///dev/sr0/3 and cdda:///D:/3. cdda://3a is track 3 of the
// default drive (the Open audio CD menu item).
// Kept free of libcdio and GStreamer so it can be tested everywhere.
namespace CDDAUrl {

// The URL scheme of CD drives and tracks.
QString UrlScheme();

// The name Akousma uses for a drive: D: for a drive letter in any of the forms Windows and libcdio use (\\.\D:, d:, D:\),
// anything else unchanged.
QString NormalizeDevice(const QString &device);

// The URL of a drive.
QUrl DeviceUrl(const QString &device);

// The drive of a drive URL (cdda:///dev/sr0 -> /dev/sr0, cdda:///D: -> D:), as libcdio and GStreamer take it.
QString DeviceFromUrl(const QUrl &url);

// The URL of a track on the disc in the drive of drive_url; for an empty drive_url, the track on the default drive.
QUrl TrackUrl(const QUrl &drive_url, const int track);

// Splits a track URL into the drive and the track number. The drive is empty for a track on the default drive.
// False for anything that isn't a cdda:// track URL.
bool ParseTrackUrl(const QUrl &url, QString &device, int &track);

}  // namespace CDDAUrl

#endif  // CDDAURL_H
