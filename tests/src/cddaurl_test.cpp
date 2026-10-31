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

#include <QString>
#include <QStringList>
#include <QUrl>

#include "core/song.h"
#include "device/cddaurl.h"

using namespace Qt::Literals::StringLiterals;

TEST(CDDAUrlTest, NormalizeDriveLetters) {

  // Every way Windows and libcdio name drive D.
  const QStringList drive_d = QStringList() << u"D:"_s << u"d:"_s << u"D:\\"_s << u"D:/"_s << u"\\\\.\\D:"_s << u"\\\\.\\d:"_s << u"\\\\?\\D:"_s << u"/D:"_s;
  for (const QString &device : drive_d) {
    EXPECT_EQ(CDDAUrl::NormalizeDevice(device), u"D:"_s) << device.toStdString();
  }

}

TEST(CDDAUrlTest, NormalizeLeavesOtherDevicesAlone) {

  const QStringList devices = QStringList() << u"/dev/sr0"_s << u"/dev/cdrom"_s << u"/dev/rdisk1"_s << u"/dev/cd0"_s << u""_s << u"DD:"_s << u"D"_s << u"\\\\.\\CdRom0"_s << u"C:\\Music"_s << u"/D:/3"_s;
  for (const QString &device : devices) {
    EXPECT_EQ(CDDAUrl::NormalizeDevice(device), device) << device.toStdString();
  }

}

TEST(CDDAUrlTest, LinuxUrlsAreUnchanged) {

  // The URLs Akousma made before, built the same way: cdda:// + the device path, + / + the track.
  const QUrl device_url = CDDAUrl::DeviceUrl(u"/dev/sr0"_s);
  EXPECT_EQ(device_url, QUrl(u"cdda:///dev/sr0"_s));
  EXPECT_EQ(device_url.toString(), u"cdda:///dev/sr0"_s);
  EXPECT_EQ(device_url.path(), u"/dev/sr0"_s);
  EXPECT_EQ(CDDAUrl::DeviceFromUrl(device_url), u"/dev/sr0"_s);

  const QUrl track_url = CDDAUrl::TrackUrl(device_url, 3);
  EXPECT_EQ(track_url, QUrl(QStringLiteral("cdda://%1/%2").arg(device_url.path(), QString::number(3))));
  EXPECT_EQ(track_url.toString(), u"cdda:///dev/sr0/3"_s);

}

TEST(CDDAUrlTest, WindowsDriveUrls) {

  for (const QString &device : QStringList() << u"D:"_s << u"\\\\.\\D:"_s << u"d:"_s) {
    const QUrl device_url = CDDAUrl::DeviceUrl(device);
    EXPECT_TRUE(device_url.isValid()) << device.toStdString();
    EXPECT_EQ(device_url.scheme(), u"cdda"_s);
    EXPECT_TRUE(device_url.host().isEmpty());
    EXPECT_EQ(device_url.toString(), u"cdda:///D:"_s) << device.toStdString();
    EXPECT_EQ(device_url.path(), u"/D:"_s);
    // What cdio_open and the cdiocddasrc "device" property get.
    EXPECT_EQ(CDDAUrl::DeviceFromUrl(device_url), u"D:"_s);
  }

  const QUrl track_url = CDDAUrl::TrackUrl(CDDAUrl::DeviceUrl(u"E:"_s), 12);
  EXPECT_TRUE(track_url.isValid());
  EXPECT_EQ(track_url.toString(), u"cdda:///E:/12"_s);
  EXPECT_EQ(Song::SourceFromURL(track_url), Song::Source::CDDA);

}

TEST(CDDAUrlTest, TrackUrlRoundTrip) {

  for (const QString &device : QStringList() << u"/dev/sr0"_s << u"/dev/sr1"_s << u"D:"_s << u"\\\\.\\Z:"_s) {
    for (const int track : {1, 9, 10, 99}) {
      const QUrl track_url = CDDAUrl::TrackUrl(CDDAUrl::DeviceUrl(device), track);
      // As saved in a playlist and read back.
      for (const QUrl &url : {track_url, QUrl(track_url.toString()), QUrl::fromEncoded(track_url.toEncoded())}) {
        QString parsed_device;
        int parsed_track = 0;
        ASSERT_TRUE(CDDAUrl::ParseTrackUrl(url, parsed_device, parsed_track)) << url.toString().toStdString();
        EXPECT_EQ(parsed_device, CDDAUrl::NormalizeDevice(device));
        EXPECT_EQ(parsed_track, track);
      }
    }
  }

}

TEST(CDDAUrlTest, DefaultDriveTrack) {

  const QUrl track_url = CDDAUrl::TrackUrl(QUrl(), 5);
  EXPECT_EQ(track_url.toString(), u"cdda://5a"_s);

  QString device;
  int track = 0;
  EXPECT_TRUE(CDDAUrl::ParseTrackUrl(track_url, device, track));
  EXPECT_TRUE(device.isEmpty());
  EXPECT_EQ(track, 5);

}

TEST(CDDAUrlTest, NotTrackUrls) {

  for (const QString &url_str : QStringList() << u"file:///D:/3"_s << u"cdda:///D:"_s << u"cdda:///D:/x"_s << u"cdda:///D:/0"_s << u"cdda:///dev/sr0/-1"_s << u"cdda://xa"_s) {
    QString device;
    int track = 0;
    EXPECT_FALSE(CDDAUrl::ParseTrackUrl(QUrl(url_str), device, track)) << url_str.toStdString();
    EXPECT_EQ(track, 0);
  }

}
