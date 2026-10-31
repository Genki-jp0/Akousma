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

#include "gtest_include.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUuid>

#include "constants/timeconstants.h"
#include "core/song.h"
#include "device/wpdmetadata.h"

using namespace Qt::Literals::StringLiterals;

namespace {

// Values from the Windows SDK's PortableDevice.h.
const QUuid kFormatMp3(0x30090000, 0xAE6C, 0x4804, 0x98, 0xBA, 0xC5, 0x7B, 0x46, 0x96, 0x5F, 0xE7);
const QUuid kFormatFlac(0xB9060000, 0xAE6C, 0x4804, 0x98, 0xBA, 0xC5, 0x7B, 0x46, 0x96, 0x5F, 0xE7);
const QUuid kFormatOgg(0xB9020000, 0xAE6C, 0x4804, 0x98, 0xBA, 0xC5, 0x7B, 0x46, 0x96, 0x5F, 0xE7);
const QUuid kFormatMp4(0xB9820000, 0xAE6C, 0x4804, 0x98, 0xBA, 0xC5, 0x7B, 0x46, 0x96, 0x5F, 0xE7);
const QUuid kFormatUnspecified(0x30000000, 0xAE6C, 0x4804, 0x98, 0xBA, 0xC5, 0x7B, 0x46, 0x96, 0x5F, 0xE7);
const QUuid kFormatPropertiesOnly(0x30010000, 0xAE6C, 0x4804, 0x98, 0xBA, 0xC5, 0x7B, 0x46, 0x96, 0x5F, 0xE7);
const QUuid kFormatExifImage(0x38010000, 0xAE6C, 0x4804, 0x98, 0xBA, 0xC5, 0x7B, 0x46, 0x96, 0x5F, 0xE7);
const QUuid kFormatM4a(0x30aba7ac, 0x6ffd, 0x4c23, 0xa3, 0x59, 0x3e, 0x9b, 0x52, 0xf3, 0xf1, 0xc8);

}  // namespace

TEST(WpdMetadataTest, MtpFormatGuids) {

  EXPECT_EQ(WpdMetadata::MtpFormat(0x3009), kFormatMp3);
  EXPECT_EQ(WpdMetadata::MtpFormat(0xB906), kFormatFlac);
  EXPECT_EQ(WpdMetadata::M4aFormat(), kFormatM4a);

}

TEST(WpdMetadataTest, FileTypeForFormat) {

  EXPECT_EQ(WpdMetadata::FileTypeForFormat(kFormatMp3, u"a.mp3"_s), Song::FileType::MPEG);
  EXPECT_EQ(WpdMetadata::FileTypeForFormat(kFormatFlac, u"a.flac"_s), Song::FileType::FLAC);
  EXPECT_EQ(WpdMetadata::FileTypeForFormat(kFormatOgg, u"a.ogg"_s), Song::FileType::OggVorbis);
  EXPECT_EQ(WpdMetadata::FileTypeForFormat(kFormatOgg, u"a.opus"_s), Song::FileType::OggOpus);
  EXPECT_EQ(WpdMetadata::FileTypeForFormat(kFormatM4a, u"a.m4a"_s), Song::FileType::MP4);
  EXPECT_EQ(WpdMetadata::FileTypeForFormat(kFormatMp4, u"a.m4a"_s), Song::FileType::MP4);
  // Android reports formats it has no MTP code for as unspecified: go by the extension.
  EXPECT_EQ(WpdMetadata::FileTypeForFormat(kFormatUnspecified, u"a.opus"_s), Song::FileType::OggOpus);
  EXPECT_EQ(WpdMetadata::FileTypeForFormat(kFormatUnspecified, u"a.OGG"_s), Song::FileType::OggVorbis);
  EXPECT_EQ(WpdMetadata::FileTypeForFormat(kFormatUnspecified, u"a.wv"_s), Song::FileType::WavPack);
  EXPECT_EQ(WpdMetadata::FileTypeForFormat(kFormatUnspecified, u"a.txt"_s), Song::FileType::Unknown);
  EXPECT_EQ(WpdMetadata::FileTypeForFormat(QUuid(), u"a.mp3"_s), Song::FileType::MPEG);

}

TEST(WpdMetadataTest, FormatForFileTypeRoundTrip) {

  for (const Song::FileType filetype : { Song::FileType::MPEG, Song::FileType::FLAC, Song::FileType::OggVorbis, Song::FileType::MP4, Song::FileType::WAV, Song::FileType::ASF, Song::FileType::AIFF }) {
    const QString filename = u"a."_s + Song::ExtensionForFiletype(filetype);
    EXPECT_EQ(WpdMetadata::FileTypeForFormat(WpdMetadata::FormatForFileType(filetype), filename), filetype) << Song::TextForFiletype(filetype).toStdString();
  }
  EXPECT_EQ(WpdMetadata::FormatForFileType(Song::FileType::OggOpus), kFormatOgg);
  EXPECT_EQ(WpdMetadata::FormatForFileType(Song::FileType::WavPack), kFormatUnspecified);

}

TEST(WpdMetadataTest, FileTypesForFormats) {

  const QList<Song::FileType> filetypes = WpdMetadata::FileTypesForFormats({ kFormatMp3, kFormatFlac, kFormatOgg, kFormatM4a, kFormatMp4, kFormatExifImage });
  EXPECT_TRUE(filetypes.contains(Song::FileType::MPEG));
  EXPECT_TRUE(filetypes.contains(Song::FileType::FLAC));
  EXPECT_TRUE(filetypes.contains(Song::FileType::OggVorbis));
  EXPECT_TRUE(filetypes.contains(Song::FileType::OggOpus));
  EXPECT_TRUE(filetypes.contains(Song::FileType::MP4));
  EXPECT_FALSE(filetypes.contains(Song::FileType::WAV));
  EXPECT_EQ(filetypes.count(Song::FileType::MP4), 1);
  EXPECT_EQ(filetypes.count(Song::FileType::OggFlac), 1);

}

TEST(WpdMetadataTest, IsAudioObject) {

  WpdMetadata::ObjectInfo info;
  info.original_file_name = u"song.mp3"_s;
  info.format = kFormatMp3;
  EXPECT_TRUE(WpdMetadata::IsAudioObject(info));

  info.format = kFormatUnspecified;
  info.original_file_name = u"song.opus"_s;
  EXPECT_TRUE(WpdMetadata::IsAudioObject(info));

  info.original_file_name = u"notes.txt"_s;
  EXPECT_FALSE(WpdMetadata::IsAudioObject(info));

  // MP4 is also used for videos.
  info.format = kFormatMp4;
  info.original_file_name = u"clip.mp4"_s;
  EXPECT_FALSE(WpdMetadata::IsAudioObject(info));
  info.original_file_name = u"song.m4a"_s;
  EXPECT_TRUE(WpdMetadata::IsAudioObject(info));

  // Folders and pictures aren't songs, whatever their names.
  info.format = kFormatPropertiesOnly;
  info.original_file_name = u"Music.mp3"_s;
  EXPECT_FALSE(WpdMetadata::IsAudioObject(info));
  info.format = kFormatExifImage;
  EXPECT_FALSE(WpdMetadata::IsAudioObject(info));

  // The content type wins.
  info.audio_content_type = true;
  EXPECT_TRUE(WpdMetadata::IsAudioObject(info));

}

TEST(WpdMetadataTest, SongUrlRoundTrip) {

  for (const QString &object_id : { u"o1A2B"_s, u"F:\\Music\\a b.mp3"_s, u"s10001/folder/%41?x#y"_s, u"\u97f3\u697d/1"_s }) {
    const QUrl url = WpdMetadata::SongUrl(object_id);
    EXPECT_EQ(url.scheme(), u"wpd"_s);
    EXPECT_TRUE(url.isValid());
    EXPECT_EQ(WpdMetadata::ObjectIdFromSongUrl(url), object_id);
    // The URL survives being stored as text in the collection database.
    EXPECT_EQ(WpdMetadata::ObjectIdFromSongUrl(QUrl::fromEncoded(url.toEncoded())), object_id);
  }
  EXPECT_TRUE(WpdMetadata::ObjectIdFromSongUrl(QUrl(u"mtp://host/1"_s)).isEmpty());

}

TEST(WpdMetadataTest, DeviceUrlRoundTrip) {

  const QString pnp_id = u"\\\\?\\usb#vid_04e8&pid_6860&ms_comp_mtp&samsung_android#6&2b2d6b52&0&0000#{6ac27878-a6fa-4155-ba85-f98f491d4f33}"_s;
  const QUrl url = WpdMetadata::DeviceUrl(pnp_id);
  EXPECT_EQ(url.scheme(), u"wpd"_s);
  EXPECT_EQ(WpdMetadata::PnpDeviceIdFromDeviceUrl(url), pnp_id);
  EXPECT_EQ(WpdMetadata::PnpDeviceIdFromDeviceUrl(QUrl::fromEncoded(url.toEncoded())), pnp_id);

}

TEST(WpdMetadataTest, UniqueId) {

  EXPECT_EQ(WpdMetadata::UniqueId(u"Samsung"_s, u"Galaxy"_s, u" R58M \n"_s, u"\\\\?\\USB#1"_s), u"wpd/Samsung/Galaxy/R58M"_s);
  EXPECT_EQ(WpdMetadata::UniqueId(u"Samsung"_s, u"Galaxy"_s, QString(), u"\\\\?\\USB#1"_s), u"wpd/\\\\?\\usb#1"_s);

}

TEST(WpdMetadataTest, MassStorageProtocol) {

  EXPECT_TRUE(WpdMetadata::IsMassStorageProtocol(u"MSC:"_s));
  EXPECT_TRUE(WpdMetadata::IsMassStorageProtocol(u" msc: 1.0"_s));
  EXPECT_FALSE(WpdMetadata::IsMassStorageProtocol(u"MTP: 1.00"_s));
  EXPECT_FALSE(WpdMetadata::IsMassStorageProtocol(QString()));

}

TEST(WpdMetadataTest, ToSong) {

  WpdMetadata::ObjectInfo info;
  info.object_id = u"o42"_s;
  info.name = u"Song Title"_s;
  info.original_file_name = u"01 - Song Title.flac"_s;
  info.format = kFormatFlac;
  info.size = 12345;
  info.mtime = 1700000000;
  info.artist = u" Artist "_s;
  info.album_artist = u"Album Artist"_s;
  info.album = u"Album"_s;
  info.genre = u"Genre"_s;
  info.track = 3;
  info.year = 2024;
  info.duration_ms = 61500;
  info.samplerate = 44100;
  info.bitrate = 905000;
  info.bitdepth = 16;
  info.playcount = 2;

  const Song song = WpdMetadata::ToSong(info);
  EXPECT_TRUE(song.is_valid());
  EXPECT_EQ(song.source(), Song::Source::Device);
  EXPECT_EQ(WpdMetadata::ObjectIdFromSongUrl(song.url()), u"o42"_s);
  EXPECT_EQ(song.basefilename(), u"01 - Song Title.flac"_s);
  EXPECT_EQ(song.filetype(), Song::FileType::FLAC);
  EXPECT_EQ(song.title(), u"Song Title"_s);
  EXPECT_EQ(song.artist(), u"Artist"_s);
  EXPECT_EQ(song.albumartist(), u"Album Artist"_s);
  EXPECT_EQ(song.album(), u"Album"_s);
  EXPECT_EQ(song.genre(), u"Genre"_s);
  EXPECT_EQ(song.track(), 3);
  EXPECT_EQ(song.year(), 2024);
  EXPECT_EQ(song.filesize(), 12345);
  EXPECT_EQ(song.mtime(), 1700000000);
  EXPECT_EQ(song.length_nanosec(), 61500 * kNsecPerMsec);
  EXPECT_EQ(song.samplerate(), 44100);
  EXPECT_EQ(song.bitrate(), 905);
  EXPECT_EQ(song.bitdepth(), 16);
  EXPECT_EQ(song.playcount(), 2U);
  EXPECT_EQ(song.directory_id(), 1);

  // A phone without metadata: the title comes from the file name.
  WpdMetadata::ObjectInfo bare;
  bare.object_id = u"o43"_s;
  bare.name = u"02 Other.mp3"_s;
  bare.format = kFormatMp3;
  const Song bare_song = WpdMetadata::ToSong(bare);
  EXPECT_TRUE(bare_song.is_valid());
  EXPECT_EQ(bare_song.title(), u"02 Other"_s);
  EXPECT_EQ(bare_song.basefilename(), u"02 Other.mp3"_s);

  // Not a song.
  bare.name = u"cover.jpg"_s;
  bare.format = kFormatUnspecified;
  EXPECT_FALSE(WpdMetadata::ToSong(bare).is_valid());

}

TEST(WpdMetadataTest, SafeName) {

  EXPECT_EQ(WpdMetadata::SafeName(u"AC/DC: Live?"_s), u"AC_DC_ Live_"_s);
  EXPECT_EQ(WpdMetadata::SafeName(u"Vol. 2..."_s), u"Vol. 2"_s);
  EXPECT_EQ(WpdMetadata::SafeName(u"  "_s), u"_"_s);
  EXPECT_EQ(WpdMetadata::SafeName(u"con"_s), u"con_"_s);
  EXPECT_EQ(WpdMetadata::SafeName(u"Aux.mp3"_s), u"Aux_.mp3"_s);
  EXPECT_EQ(WpdMetadata::SafeName(u"Console"_s), u"Console"_s);
  EXPECT_EQ(WpdMetadata::SafeName(u"\u97f3\u697d"_s), u"\u97f3\u697d"_s);

}

TEST(WpdMetadataTest, DestinationPath) {

  EXPECT_EQ(WpdMetadata::DestinationPath(u"Artist/Album/01 - Title.mp3"_s), QStringList({ u"Artist"_s, u"Album"_s, u"01 - Title.mp3"_s }));
  EXPECT_EQ(WpdMetadata::DestinationPath(u"/Artist\\Album//./../x.flac"_s), QStringList({ u"Artist"_s, u"Album"_s, u"x.flac"_s }));
  EXPECT_EQ(WpdMetadata::DestinationPath(u"A: B/C?.ogg"_s), QStringList({ u"A_ B"_s, u"C_.ogg"_s }));
  EXPECT_TRUE(WpdMetadata::DestinationPath(u"/./"_s).isEmpty());

}

TEST(WpdMetadataTest, OleDates) {

  // 2024-01-02 12:00 UTC.
  const double ole_date = 45293.5;
  EXPECT_EQ(WpdMetadata::SecondsFromOleDate(ole_date), 1704196800);
  EXPECT_EQ(WpdMetadata::YearFromOleDate(ole_date), 2024);
  EXPECT_EQ(WpdMetadata::YearFromOleDate(0.0), 0);
  EXPECT_EQ(WpdMetadata::SecondsFromOleDate(-1.0), 0);

}
