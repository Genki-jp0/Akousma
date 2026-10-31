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

#include <cmath>

#include <QtGlobal>
#include <QList>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QFileInfo>
#include <QDateTime>
#include <QTimeZone>

#include "constants/timeconstants.h"
#include "core/song.h"
#include "wpdmetadata.h"

using namespace Qt::Literals::StringLiterals;

namespace WpdMetadata {

namespace {

// MTP object format codes (MTP 1.1, 3.6.1 / 3.6.2).
constexpr quint16 kFormatUndefined = 0x3000;
constexpr quint16 kFormatAiff = 0x3007;
constexpr quint16 kFormatWave = 0x3008;
constexpr quint16 kFormatMp3 = 0x3009;
constexpr quint16 kFormatAsf = 0x300C;
constexpr quint16 kFormatUndefinedAudio = 0xB900;
constexpr quint16 kFormatWma = 0xB901;
constexpr quint16 kFormatOgg = 0xB902;
constexpr quint16 kFormatAac = 0xB903;
constexpr quint16 kFormatFlac = 0xB906;
constexpr quint16 kFormatMp4 = 0xB982;
constexpr quint16 kFormatMp2 = 0xB983;

// Days between 1899-12-30 (the OLE date epoch) and 1970-01-01.
constexpr double kOleDateUnixEpoch = 25569.0;

bool IsMtpFormat(const QUuid &format, quint16 *format_code) {

  const QUuid base = MtpFormat(0);
  if (format.data2 != base.data2 || format.data3 != base.data3) return false;
  for (int i = 0; i < 8; ++i) {
    if (format.data4[i] != base.data4[i]) return false;
  }
  if ((format.data1 & 0xFFFF) != 0) return false;

  *format_code = static_cast<quint16>(format.data1 >> 16);
  return true;

}

QString Suffix(const QString &filename) {
  return QFileInfo(filename).suffix().toLower();
}

Song::FileType OggFileType(const QString &filename) {

  const QString suffix = Suffix(filename);
  if (suffix == "opus"_L1) return Song::FileType::OggOpus;
  if (suffix == "spx"_L1 || suffix == "speex"_L1) return Song::FileType::OggSpeex;
  if (suffix == "flac"_L1) return Song::FileType::OggFlac;
  return Song::FileType::OggVorbis;

}

bool IsAudioFileType(const Song::FileType filetype) {

  switch (filetype) {
    case Song::FileType::Unknown:
    case Song::FileType::CDDA:
    case Song::FileType::Stream:
      return false;
    default:
      return true;
  }

}

}  // namespace

QString UrlScheme() { return u"wpd"_s; }

QUuid MtpFormat(const quint16 format_code) {
  return QUuid(static_cast<uint>(format_code) << 16, 0xAE6C, 0x4804, 0x98, 0xBA, 0xC5, 0x7B, 0x46, 0x96, 0x5F, 0xE7);
}

QUuid M4aFormat() {
  return QUuid(0x30aba7ac, 0x6ffd, 0x4c23, 0xa3, 0x59, 0x3e, 0x9b, 0x52, 0xf3, 0xf1, 0xc8);
}

Song::FileType FileTypeForFormat(const QUuid &format, const QString &filename) {

  if (format == M4aFormat()) return Song::FileType::MP4;

  quint16 code = 0;
  if (IsMtpFormat(format, &code)) {
    switch (code) {
      case kFormatAiff: return Song::FileType::AIFF;
      case kFormatWave: return Song::FileType::WAV;
      case kFormatMp2:
      case kFormatMp3: return Song::FileType::MPEG;
      case kFormatAsf:
      case kFormatWma: return Song::FileType::ASF;
      case kFormatOgg: return OggFileType(filename);
      case kFormatAac:
      case kFormatMp4: return Song::FileType::MP4;
      case kFormatFlac: return Suffix(filename) == "oga"_L1 ? Song::FileType::OggFlac : Song::FileType::FLAC;
      default: break;
    }
  }

  // Unspecified, undefined audio, or a format without its own file type: go by the extension.
  const QString suffix = Suffix(filename);
  if (suffix == "ogg"_L1 || suffix == "oga"_L1) return OggFileType(filename);
  return Song::FiletypeByExtension(suffix);

}

QUuid FormatForFileType(const Song::FileType filetype) {

  switch (filetype) {
    case Song::FileType::WAV: return MtpFormat(kFormatWave);
    case Song::FileType::AIFF: return MtpFormat(kFormatAiff);
    case Song::FileType::MPEG: return MtpFormat(kFormatMp3);
    case Song::FileType::ASF: return MtpFormat(kFormatWma);
    case Song::FileType::FLAC: return MtpFormat(kFormatFlac);
    case Song::FileType::OggFlac:
    case Song::FileType::OggVorbis:
    case Song::FileType::OggOpus:
    case Song::FileType::OggSpeex: return MtpFormat(kFormatOgg);
    case Song::FileType::MP4:
    case Song::FileType::ALAC: return MtpFormat(kFormatMp4);
    default: break;
  }

  return MtpFormat(kFormatUndefined);

}

QList<Song::FileType> FileTypesForFormats(const QList<QUuid> &formats) {

  QList<Song::FileType> ret;
  auto add = [&ret](const Song::FileType filetype) {
    if (!ret.contains(filetype)) ret << filetype;
  };

  for (const QUuid &format : formats) {
    if (format == M4aFormat()) {
      add(Song::FileType::MP4);
      continue;
    }
    quint16 code = 0;
    if (!IsMtpFormat(format, &code)) continue;
    switch (code) {
      case kFormatAiff: add(Song::FileType::AIFF); break;
      case kFormatWave: add(Song::FileType::WAV); break;
      case kFormatMp2:
      case kFormatMp3: add(Song::FileType::MPEG); break;
      case kFormatAsf:
      case kFormatWma: add(Song::FileType::ASF); break;
      case kFormatAac:
      case kFormatMp4: add(Song::FileType::MP4); break;
      case kFormatFlac:
        add(Song::FileType::FLAC);
        add(Song::FileType::OggFlac);
        break;
      case kFormatOgg:
        add(Song::FileType::OggVorbis);
        add(Song::FileType::OggOpus);
        add(Song::FileType::OggSpeex);
        add(Song::FileType::OggFlac);
        break;
      default: break;
    }
  }

  return ret;

}

QList<Song::FileType> DefaultFileTypes() {
  return QList<Song::FileType>() << Song::FileType::MPEG << Song::FileType::MP4 << Song::FileType::FLAC << Song::FileType::OggVorbis << Song::FileType::OggOpus << Song::FileType::WAV;
}

bool SkipFolder(const QString &name, const int depth) {

  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) return false;
  if (trimmed.startsWith(u'.')) return true;
  if (depth > 0) return false;

  static const QStringList kTopLevelFolders = QStringList() << u"Android"_s
                                                            << u"DCIM"_s
                                                            << u"Pictures"_s
                                                            << u"Movies"_s
                                                            << u"Alarms"_s
                                                            << u"Notifications"_s
                                                            << u"Ringtones"_s
                                                            << u"LOST.DIR"_s
                                                            << u"WhatsApp"_s
                                                            << u"Telegram"_s;
  return kTopLevelFolders.contains(trimmed, Qt::CaseInsensitive);

}

ProgressSpan ProgressSpan::Part(const int part, const int parts) const {

  const double share = (end - begin) / static_cast<double>(qMax(0, parts) + 1);
  const int index = qBound(-1, part, qMax(0, parts) - 1) + 1;
  ProgressSpan ret;
  ret.begin = begin + share * static_cast<double>(index);
  ret.end = index == parts ? end : ret.begin + share;
  return ret;

}

bool IsAudioObject(const ObjectInfo &info) {

  if (info.audio_content_type) return true;

  const QString filename = info.original_file_name.isEmpty() ? info.name : info.original_file_name;
  const QString suffix = Suffix(filename);

  if (info.format == M4aFormat()) return true;

  quint16 code = 0;
  if (IsMtpFormat(info.format, &code)) {
    switch (code) {
      case kFormatAiff:
      case kFormatWave:
      case kFormatMp3:
      case kFormatMp2:
      case kFormatWma:
      case kFormatOgg:
      case kFormatAac:
      case kFormatFlac:
        return true;
      case kFormatMp4:
        // Also used for videos: only audio file names.
        return suffix == "m4a"_L1 || suffix == "m4b"_L1 || suffix == "aac"_L1;
      case kFormatUndefined:
      case kFormatUndefinedAudio:
        break;
      default:
        return false;
    }
  }
  else if (!info.format.isNull()) {
    return false;
  }

  // Undefined or unknown format: by the file name.
  return !suffix.isEmpty() && suffix != "mp4"_L1 && IsAudioFileType(FileTypeForFormat(MtpFormat(kFormatUndefined), filename));

}

QUrl SongUrl(const QString &object_id) {
  return QUrl::fromEncoded(QByteArray(UrlScheme().toLatin1() + ":/" + QUrl::toPercentEncoding(object_id)));
}

QString ObjectIdFromSongUrl(const QUrl &url) {

  if (url.scheme() != UrlScheme()) return QString();
  QByteArray path = url.path(QUrl::FullyEncoded).toLatin1();
  while (path.startsWith('/')) path.remove(0, 1);
  return QString::fromUtf8(QByteArray::fromPercentEncoding(path));

}

QUrl DeviceUrl(const QString &pnp_device_id) {

  QUrl url;
  url.setScheme(UrlScheme());
  QUrlQuery query;
  query.addQueryItem(u"pnpid"_s, QString::fromLatin1(QUrl::toPercentEncoding(pnp_device_id)));
  url.setQuery(query);
  return url;

}

QString PnpDeviceIdFromDeviceUrl(const QUrl &url) {

  if (url.scheme() != UrlScheme()) return QString();
  const QUrlQuery query(url);
  return QString::fromUtf8(QByteArray::fromPercentEncoding(query.queryItemValue(u"pnpid"_s, QUrl::FullyEncoded).toLatin1()));

}

QString UniqueId(const QString &manufacturer, const QString &model, const QString &serial_number, const QString &pnp_device_id) {

  const QString serial = serial_number.trimmed();
  if (!serial.isEmpty()) {
    return u"wpd/%1/%2/%3"_s.arg(manufacturer.trimmed(), model.trimmed(), serial);
  }
  return u"wpd/"_s + pnp_device_id.toLower();

}

bool IsMassStorageProtocol(const QString &protocol) {
  return protocol.trimmed().startsWith("MSC"_L1, Qt::CaseInsensitive);
}

Song ToSong(const ObjectInfo &info) {

  const QString filename = info.original_file_name.isEmpty() ? info.name : info.original_file_name;

  Song song(Song::Source::Device);
  song.set_valid(true);
  song.set_url(SongUrl(info.object_id));
  song.set_basefilename(filename);
  song.set_filetype(FileTypeForFormat(info.format, filename));
  if (song.filetype() == Song::FileType::Unknown) song.set_valid(false);

  QString title = info.title.trimmed();
  if (title.isEmpty()) {
    // Players that are filled by Windows Media Player keep the title in the object name, phones the file name.
    title = info.name.trimmed();
    if (title.isEmpty() || title == filename) title = QFileInfo(filename).completeBaseName();
  }
  song.set_title(title);
  song.set_artist(info.artist.trimmed());
  song.set_albumartist(info.album_artist.trimmed());
  song.set_album(info.album.trimmed());
  song.set_genre(info.genre.trimmed());
  song.set_composer(info.composer.trimmed());
  if (info.track > 0) song.set_track(info.track);
  if (info.year > 0) song.set_year(info.year);

  song.set_filesize(static_cast<qint64>(info.size));
  song.set_mtime(info.mtime);
  song.set_ctime(info.mtime);
  if (info.duration_ms > 0) song.set_length_nanosec(static_cast<qint64>(info.duration_ms) * kNsecPerMsec);
  song.set_samplerate(info.samplerate);
  // WPD reports bits per second, the collection shows kbit/s.
  song.set_bitrate(info.bitrate >= 1000 ? info.bitrate / 1000 : info.bitrate);
  song.set_bitdepth(info.bitdepth);
  song.set_playcount(info.playcount);
  song.set_directory_id(1);

  return song;

}

QString SafeName(const QString &name) {

  QString ret;
  ret.reserve(name.size());
  for (const QChar c : name) {
    if (c.unicode() < 0x20 || c == u'\\' || c == u'/' || c == u':' || c == u'*' || c == u'?' || c == u'"' || c == u'<' || c == u'>' || c == u'|') {
      ret += u'_';
    }
    else {
      ret += c;
    }
  }

  // Windows drops trailing dots and spaces, and many devices refuse them.
  while (!ret.isEmpty() && (ret.endsWith(u'.') || ret.endsWith(u' '))) ret.chop(1);
  ret = ret.trimmed();
  if (ret.isEmpty()) return u"_"_s;

  // Names Windows reserves for devices, also with an extension.
  static const QStringList kReserved = { u"CON"_s, u"PRN"_s, u"AUX"_s, u"NUL"_s,
                                         u"COM1"_s, u"COM2"_s, u"COM3"_s, u"COM4"_s, u"COM5"_s, u"COM6"_s, u"COM7"_s, u"COM8"_s, u"COM9"_s,
                                         u"LPT1"_s, u"LPT2"_s, u"LPT3"_s, u"LPT4"_s, u"LPT5"_s, u"LPT6"_s, u"LPT7"_s, u"LPT8"_s, u"LPT9"_s };
  const QString stem = ret.section(u'.', 0, 0);
  if (kReserved.contains(stem, Qt::CaseInsensitive)) ret.insert(stem.size(), u'_');

  return ret;

}

QStringList DestinationPath(const QString &destination) {

  QStringList ret;
  const QStringList parts = QString(destination).replace(u'\\', u'/').split(u'/', Qt::SkipEmptyParts);
  for (const QString &part : parts) {
    if (part == "."_L1 || part == ".."_L1 || part.trimmed().isEmpty()) continue;
    ret << SafeName(part);
  }
  return ret;

}

qint64 SecondsFromOleDate(const double ole_date) {

  if (!(ole_date > 0.0) || !std::isfinite(ole_date)) return 0;
  return static_cast<qint64>(std::llround((ole_date - kOleDateUnixEpoch) * 86400.0));

}

int YearFromOleDate(const double ole_date) {

  if (!(ole_date > 0.0) || !std::isfinite(ole_date)) return 0;
  return QDateTime::fromSecsSinceEpoch(SecondsFromOleDate(ole_date), QTimeZone::utc()).date().year();

}

}  // namespace WpdMetadata
