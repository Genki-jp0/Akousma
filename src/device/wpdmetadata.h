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

#ifndef WPDMETADATA_H
#define WPDMETADATA_H

#include "config.h"

#include <QtGlobal>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUuid>

#include "core/song.h"

// The platform independent part of the Windows Portable Devices (WPD) backend for MTP devices: object formats, URLs,
// metadata and the folders a copied song goes to. Kept free of Windows headers so it can be tested everywhere.
namespace WpdMetadata {

// What the loader reads from one object on the device. Empty strings and zeros mean the device didn't report the property.
struct ObjectInfo {
  QString object_id;
  QString name;                // WPD_OBJECT_NAME
  QString original_file_name;  // WPD_OBJECT_ORIGINAL_FILE_NAME
  QUuid format;                // WPD_OBJECT_FORMAT
  bool audio_content_type = false;  // WPD_OBJECT_CONTENT_TYPE is WPD_CONTENT_TYPE_AUDIO
  quint64 size = 0;            // WPD_OBJECT_SIZE
  qint64 mtime = 0;            // WPD_OBJECT_DATE_MODIFIED, seconds since the epoch
  QString title;               // WPD_MEDIA_TITLE
  QString artist;              // WPD_MEDIA_ARTIST
  QString album_artist;        // WPD_MEDIA_ALBUM_ARTIST
  QString album;               // WPD_MUSIC_ALBUM
  QString genre;               // WPD_MEDIA_GENRE
  QString composer;            // WPD_MEDIA_COMPOSER
  int track = 0;               // WPD_MUSIC_TRACK
  int year = 0;                // from WPD_MEDIA_RELEASE_DATE
  quint64 duration_ms = 0;     // WPD_MEDIA_DURATION
  int samplerate = 0;          // WPD_MEDIA_SAMPLE_RATE
  int bitrate = 0;             // WPD_AUDIO_BITRATE, bits per second
  int bitdepth = 0;            // WPD_AUDIO_BIT_DEPTH
  uint playcount = 0;          // WPD_MEDIA_USE_COUNT
};

// The URL scheme of WPD devices and of the songs on them.
QString UrlScheme();

// WPD object formats for MTP object formats are {XXXX0000-AE6C-4804-98BA-C57B46965FE7}, XXXX being the MTP format code.
QUuid MtpFormat(const quint16 format_code);
// WPD_OBJECT_FORMAT_M4A, the one audio format that isn't an MTP format code.
QUuid M4aFormat();

// The file type of an object, from its format, or from the file name's extension when the format says nothing
// (WPD_OBJECT_FORMAT_UNSPECIFIED, as Android reports for formats it has no MTP code for, like Opus).
Song::FileType FileTypeForFormat(const QUuid &format, const QString &filename);
// The format a file of this type is created with on the device; WPD_OBJECT_FORMAT_UNSPECIFIED if there's none.
QUuid FormatForFileType(const Song::FileType filetype);
// The file types for the audio formats a device supports (IPortableDeviceCapabilities::GetSupportedFormats), without duplicates.
QList<Song::FileType> FileTypesForFormats(const QList<QUuid> &formats);

// The file types assumed for a device that doesn't tell which formats it takes.
QList<Song::FileType> DefaultFileTypes();

// Whether an object is a song: audio content type, or an audio format or file name.
bool IsAudioObject(const ObjectInfo &info);

// Whether the loader leaves out a folder: hidden folders (".thumbnails", ".Trash" etc.) anywhere, and at the top of a
// storage (depth 0) the folders phones keep app data, photos, videos, system sounds and messenger media in (Android,
// DCIM, Pictures, Movies, Alarms, Notifications, Ringtones, LOST.DIR, WhatsApp, Telegram). Case insensitive.
bool SkipFolder(const QString &name, const int depth);

// The part of the loading progress (0 to 1) a folder covers: [begin, end). A folder's own files take the first share
// and each subfolder one more, so the progress only goes forward and reaches end when the last subfolder is done.
struct ProgressSpan {
  double begin = 0.0;
  double end = 1.0;
  // The span of the files directly in the folder (part -1) or of subfolder part (0 to parts - 1) of parts subfolders.
  ProgressSpan Part(const int part, const int parts) const;
};

// Song URLs: wpd:/<object ID, percent encoded>. Object IDs are opaque strings, any character can be in them.
QUrl SongUrl(const QString &object_id);
QString ObjectIdFromSongUrl(const QUrl &url);

// Device URLs: wpd:?pnpid=<PnP device ID>, what IPortableDevice::Open() takes.
QUrl DeviceUrl(const QString &pnp_device_id);
QString PnpDeviceIdFromDeviceUrl(const QUrl &url);

// An ID that stays the same when the device is plugged into another USB port: manufacturer, model and serial number,
// or the PnP device ID when the device has no serial number.
QString UniqueId(const QString &manufacturer, const QString &model, const QString &serial_number, const QString &pnp_device_id);

// MTP devices reached through WPD that are drives (protocol "MSC: ...") are listed by the GIO lister already.
bool IsMassStorageProtocol(const QString &protocol);

// A song for the collection of the device.
Song ToSong(const ObjectInfo &info);

// A file or folder name that devices and Windows accept: no \ / : * ? " < > |, control characters, trailing dots or spaces.
QString SafeName(const QString &name);
// The folders and the file name of a copied song, from the organize destination ("Artist/Album/01 - Title.mp3"),
// each made safe; ".", ".." and empty parts are dropped. The last item is the file name; empty if there is none.
QStringList DestinationPath(const QString &destination);

// The year of a WPD date (VT_DATE: days since 1899-12-30), 0 if there is none.
int YearFromOleDate(const double ole_date);
// Seconds since the epoch of a WPD date (VT_DATE), 0 if there is none.
qint64 SecondsFromOleDate(const double ole_date);

}  // namespace WpdMetadata

#endif  // WPDMETADATA_H
