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

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>

#include "core/song.h"
#include "utilities/urlpolicy.h"

using namespace Qt::Literals::StringLiterals;

// clazy:excludeall=non-pod-global-static

namespace {

bool WriteFile(const QString &filename, const QByteArray &data) {
  QFile file(filename);
  if (!file.open(QIODevice::WriteOnly)) return false;
  return file.write(data) == data.size();
}

QUrl DashManifestUrl() {
  // Built like TidalStreamURLRequest does.
  QUrl url;
  url.setScheme(u"data"_s);
  url.setPath(u"application/dash+xml;base64,PE1QRD48L01QRD4="_s);
  return url;
}

TEST(UrlPolicyTest, WebUrls) {

  EXPECT_TRUE(UrlPolicy::IsWebUrl(QUrl(u"http://example.com/stream"_s)));
  EXPECT_TRUE(UrlPolicy::IsWebUrl(QUrl(u"https://example.com:8443/stream.mp3?x=1"_s)));
  EXPECT_TRUE(UrlPolicy::IsWebUrl(QUrl(u"HTTPS://Example.com/"_s)));
  EXPECT_TRUE(UrlPolicy::IsWebUrl(QUrl(u"http://192.168.1.1/"_s)));

  EXPECT_FALSE(UrlPolicy::IsWebUrl(QUrl()));
  EXPECT_FALSE(UrlPolicy::IsWebUrl(QUrl(u"http:stream"_s)));
  EXPECT_FALSE(UrlPolicy::IsWebUrl(QUrl(u"http:///stream"_s)));
  EXPECT_FALSE(UrlPolicy::IsWebUrl(QUrl(u"file:///etc/passwd"_s)));
  EXPECT_FALSE(UrlPolicy::IsWebUrl(QUrl(u"file://host/share/a.mp3"_s)));
  EXPECT_FALSE(UrlPolicy::IsWebUrl(QUrl(u"ftp://example.com/a.mp3"_s)));
  EXPECT_FALSE(UrlPolicy::IsWebUrl(QUrl(u"rtsp://example.com/a"_s)));
  EXPECT_FALSE(UrlPolicy::IsWebUrl(QUrl(u"smb://host/share/a.mp3"_s)));
  EXPECT_FALSE(UrlPolicy::IsWebUrl(QUrl(u"tidal:12345"_s)));
  EXPECT_FALSE(UrlPolicy::IsWebUrl(QUrl(u"data:text/plain,abc"_s)));
  EXPECT_FALSE(UrlPolicy::IsWebUrl(QUrl(u"example.com/stream"_s)));

  EXPECT_TRUE(UrlPolicy::IsSecureWebUrl(QUrl(u"https://example.com/a.flac"_s)));
  EXPECT_FALSE(UrlPolicy::IsSecureWebUrl(QUrl(u"http://example.com/a.flac"_s)));
  EXPECT_FALSE(UrlPolicy::IsSecureWebUrl(QUrl(u"https:///a.flac"_s)));
  EXPECT_FALSE(UrlPolicy::IsSecureWebUrl(QUrl(u"file:///a.flac"_s)));

  EXPECT_TRUE(UrlPolicy::IsRadioStreamUrl(QUrl(u"http://ice.example.com:8000/live"_s)));
  EXPECT_FALSE(UrlPolicy::IsRadioStreamUrl(QUrl(u"file:///dev/zero"_s)));
  EXPECT_FALSE(UrlPolicy::IsRadioStreamUrl(QUrl(u"subsonic:1"_s)));
  EXPECT_FALSE(UrlPolicy::IsRadioStreamUrl(QUrl(u"cdda:///dev/sr0/1"_s)));

  EXPECT_TRUE(UrlPolicy::IsServiceStreamUrl(QUrl(u"https://sp-pr-fa.audio.tidal.com/a.flac"_s)));
  EXPECT_FALSE(UrlPolicy::IsServiceStreamUrl(QUrl(u"http://sp-pr-fa.audio.tidal.com/a.flac"_s)));

  EXPECT_TRUE(UrlPolicy::IsRemoteCoverUrl(QUrl(u"http://example.com/cover.jpg"_s)));
  EXPECT_FALSE(UrlPolicy::IsRemoteCoverUrl(QUrl(u"file:///dev/zero"_s)));
  EXPECT_FALSE(UrlPolicy::IsRemoteCoverUrl(QUrl(u"qrc:/pictures/cdcase.png"_s)));

}

TEST(UrlPolicyTest, UncPaths) {

  EXPECT_TRUE(UrlPolicy::IsUncPath(u"//host/share/a.mp3"_s));
  EXPECT_TRUE(UrlPolicy::IsUncPath(u"\\\\host\\share\\a.mp3"_s));
  EXPECT_TRUE(UrlPolicy::IsUncPath(u"\\/host/share/a.mp3"_s));
  EXPECT_TRUE(UrlPolicy::IsUncPath(u"/\\host\\share\\a.mp3"_s));
  EXPECT_TRUE(UrlPolicy::IsUncPath(u"\\\\?\\UNC\\host\\share\\a.mp3"_s));

  EXPECT_FALSE(UrlPolicy::IsUncPath(QString()));
  EXPECT_FALSE(UrlPolicy::IsUncPath(u"/"_s));
  EXPECT_FALSE(UrlPolicy::IsUncPath(u"/music/a.mp3"_s));
  EXPECT_FALSE(UrlPolicy::IsUncPath(u"\\music\\a.mp3"_s));
  EXPECT_FALSE(UrlPolicy::IsUncPath(u"C:\\Music\\a.mp3"_s));
  EXPECT_FALSE(UrlPolicy::IsUncPath(u"C:/Music/a.mp3"_s));
  EXPECT_FALSE(UrlPolicy::IsUncPath(u"a.mp3"_s));

}

TEST(UrlPolicyTest, FileUrls) {

  EXPECT_TRUE(UrlPolicy::IsRemoteFileUrl(QUrl(u"file://host/share/a.mp3"_s)));
  EXPECT_TRUE(UrlPolicy::IsRemoteFileUrl(QUrl(u"file:////host/share/a.mp3"_s)));
  EXPECT_TRUE(UrlPolicy::IsRemoteFileUrl(QUrl(u"file://localhost/a.mp3"_s)));
  EXPECT_FALSE(UrlPolicy::IsRemoteFileUrl(QUrl(u"file:///music/a.mp3"_s)));
  EXPECT_FALSE(UrlPolicy::IsRemoteFileUrl(QUrl::fromLocalFile(u"/music/a.mp3"_s)));
  EXPECT_FALSE(UrlPolicy::IsRemoteFileUrl(QUrl(u"http://host/a.mp3"_s)));

  EXPECT_TRUE(UrlPolicy::IsLocalFileUrl(QUrl(u"file:///music/a.mp3"_s)));
  EXPECT_TRUE(UrlPolicy::IsLocalFileUrl(QUrl::fromLocalFile(u"/music/a.mp3"_s)));
  EXPECT_FALSE(UrlPolicy::IsLocalFileUrl(QUrl(u"file://host/share/a.mp3"_s)));
  EXPECT_FALSE(UrlPolicy::IsLocalFileUrl(QUrl(u"file:////host/share/a.mp3"_s)));
  EXPECT_FALSE(UrlPolicy::IsLocalFileUrl(QUrl(u"http://host/a.mp3"_s)));
  EXPECT_FALSE(UrlPolicy::IsLocalFileUrl(QUrl(u"/music/a.mp3"_s)));

}

TEST(UrlPolicyTest, SpotifyTrackUrls) {

  EXPECT_TRUE(UrlPolicy::IsSpotifyTrackUrl(QUrl(u"spotify:track:4uLU6hMCjMI75M1A2tKUQC"_s)));

  EXPECT_FALSE(UrlPolicy::IsSpotifyTrackUrl(QUrl(u"spotify:track:"_s)));
  EXPECT_FALSE(UrlPolicy::IsSpotifyTrackUrl(QUrl(u"spotify:track:../../x"_s)));
  EXPECT_FALSE(UrlPolicy::IsSpotifyTrackUrl(QUrl(u"spotify:album:4uLU6hMCjMI75M1A2tKUQC"_s)));
  EXPECT_FALSE(UrlPolicy::IsSpotifyTrackUrl(QUrl(u"spotify:local:Artist:Album:Title:180"_s)));
  EXPECT_FALSE(UrlPolicy::IsSpotifyTrackUrl(QUrl(u"spotify://host/track:abc"_s)));
  EXPECT_FALSE(UrlPolicy::IsSpotifyTrackUrl(QUrl(u"https://open.spotify.com/track/4uLU6hMCjMI75M1A2tKUQC"_s)));
  EXPECT_FALSE(UrlPolicy::IsSpotifyTrackUrl(QUrl(u"file:///track:abc"_s)));

}

TEST(UrlPolicyTest, DashManifestDataUrls) {

  EXPECT_TRUE(UrlPolicy::IsDashManifestDataUrl(DashManifestUrl()));
  EXPECT_FALSE(UrlPolicy::IsDashManifestDataUrl(QUrl(u"data:text/html;base64,PGh0bWw+"_s)));
  EXPECT_FALSE(UrlPolicy::IsDashManifestDataUrl(QUrl(u"https://example.com/application/dash+xml;base64,"_s)));

}

TEST(UrlPolicyTest, MprisUris) {

  QTemporaryDir temp_dir;
  ASSERT_TRUE(temp_dir.isValid());
  const QString filename = temp_dir.filePath(u"song.mp3"_s);
  ASSERT_TRUE(WriteFile(filename, "ID3"));

  EXPECT_TRUE(UrlPolicy::IsMprisUriAllowed(QUrl::fromLocalFile(filename)));
  EXPECT_TRUE(UrlPolicy::IsMprisUriAllowed(QUrl(u"http://example.com/stream"_s)));
  EXPECT_TRUE(UrlPolicy::IsMprisUriAllowed(QUrl(u"https://example.com/stream"_s)));

  // A directory would be walked recursively.
  EXPECT_FALSE(UrlPolicy::IsMprisUriAllowed(QUrl::fromLocalFile(temp_dir.path())));
  EXPECT_FALSE(UrlPolicy::IsMprisUriAllowed(QUrl::fromLocalFile(temp_dir.filePath(u"missing.mp3"_s))));
  EXPECT_FALSE(UrlPolicy::IsMprisUriAllowed(QUrl(u"file://host/share/a.mp3"_s)));
  EXPECT_FALSE(UrlPolicy::IsMprisUriAllowed(QUrl(u"tidal:12345"_s)));
  EXPECT_FALSE(UrlPolicy::IsMprisUriAllowed(QUrl(u"subsonic:12345"_s)));
  EXPECT_FALSE(UrlPolicy::IsMprisUriAllowed(QUrl(u"cdda:///dev/sr0/1"_s)));
  EXPECT_FALSE(UrlPolicy::IsMprisUriAllowed(QUrl(u"udp://0.0.0.0:5004"_s)));
  EXPECT_FALSE(UrlPolicy::IsMprisUriAllowed(QUrl(u"rtsp://192.168.1.1/a"_s)));
  EXPECT_FALSE(UrlPolicy::IsMprisUriAllowed(QUrl(u"smb://host/share/a.mp3"_s)));
  EXPECT_FALSE(UrlPolicy::IsMprisUriAllowed(QUrl()));
#ifdef Q_OS_UNIX
  EXPECT_FALSE(UrlPolicy::IsMprisUriAllowed(QUrl(u"file:///dev/zero"_s)));
#endif

  EXPECT_EQ(QStringList() << u"file"_s << u"http"_s << u"https"_s, UrlPolicy::MprisUriSchemes());

}

TEST(UrlPolicyTest, PlaylistCoverUrls) {

  QTemporaryDir playlist_dir;
  ASSERT_TRUE(playlist_dir.isValid());
  QTemporaryDir other_dir;
  ASSERT_TRUE(other_dir.isValid());

  ASSERT_TRUE(QDir(playlist_dir.path()).mkdir(u"covers"_s));
  const QString cover = playlist_dir.filePath(u"covers/cover.jpg"_s);
  ASSERT_TRUE(WriteFile(cover, "not really a JPEG"));
  const QString outside_cover = other_dir.filePath(u"cover.jpg"_s);
  ASSERT_TRUE(WriteFile(outside_cover, "not really a JPEG"));

  const QDir dir(playlist_dir.path());
  const QUrl cover_url = QUrl::fromLocalFile(QFileInfo(cover).canonicalFilePath());

  // Web URLs are kept as they are.
  EXPECT_EQ(QUrl(u"http://example.com/cover.jpg"_s), UrlPolicy::PlaylistCoverUrl(u"http://example.com/cover.jpg"_s, dir));
  EXPECT_EQ(QUrl(u"https://example.com/cover.jpg"_s), UrlPolicy::PlaylistCoverUrl(u" https://example.com/cover.jpg "_s, dir));

  // Image files next to the playlist, as a relative path, an absolute path or a file: URL.
  EXPECT_EQ(cover_url, UrlPolicy::PlaylistCoverUrl(u"covers/cover.jpg"_s, dir));
  EXPECT_EQ(cover_url, UrlPolicy::PlaylistCoverUrl(u"./covers/../covers/cover.jpg"_s, dir));
  EXPECT_EQ(cover_url, UrlPolicy::PlaylistCoverUrl(cover, dir));
  EXPECT_EQ(cover_url, UrlPolicy::PlaylistCoverUrl(QUrl::fromLocalFile(cover).toString(), dir));

  // Anything else is dropped.
  EXPECT_TRUE(UrlPolicy::PlaylistCoverUrl(QString(), dir).isEmpty());
  EXPECT_TRUE(UrlPolicy::PlaylistCoverUrl(u"   "_s, dir).isEmpty());
  EXPECT_TRUE(UrlPolicy::PlaylistCoverUrl(outside_cover, dir).isEmpty());
  EXPECT_TRUE(UrlPolicy::PlaylistCoverUrl(QUrl::fromLocalFile(outside_cover).toString(), dir).isEmpty());
  EXPECT_TRUE(UrlPolicy::PlaylistCoverUrl(dir.relativeFilePath(outside_cover), dir).isEmpty());
  EXPECT_TRUE(UrlPolicy::PlaylistCoverUrl(u"covers"_s, dir).isEmpty());
  EXPECT_TRUE(UrlPolicy::PlaylistCoverUrl(u"covers/missing.jpg"_s, dir).isEmpty());
  EXPECT_TRUE(UrlPolicy::PlaylistCoverUrl(u"file:///dev/zero"_s, dir).isEmpty());
  EXPECT_TRUE(UrlPolicy::PlaylistCoverUrl(u"/dev/zero"_s, dir).isEmpty());
  EXPECT_TRUE(UrlPolicy::PlaylistCoverUrl(u"file://attacker.example/share/a.jpg"_s, dir).isEmpty());
  EXPECT_TRUE(UrlPolicy::PlaylistCoverUrl(u"file:////attacker.example/share/a.jpg"_s, dir).isEmpty());
  EXPECT_TRUE(UrlPolicy::PlaylistCoverUrl(u"//attacker.example/share/a.jpg"_s, dir).isEmpty());
  EXPECT_TRUE(UrlPolicy::PlaylistCoverUrl(u"\\\\attacker.example\\share\\a.jpg"_s, dir).isEmpty());
  EXPECT_TRUE(UrlPolicy::PlaylistCoverUrl(u"http:///cover.jpg"_s, dir).isEmpty());
  EXPECT_TRUE(UrlPolicy::PlaylistCoverUrl(u"ftp://example.com/cover.jpg"_s, dir).isEmpty());
  EXPECT_TRUE(UrlPolicy::PlaylistCoverUrl(u"data:image/png;base64,iVBORw0KGgo="_s, dir).isEmpty());
  EXPECT_TRUE(UrlPolicy::PlaylistCoverUrl(u"qrc:/pictures/cdcase.png"_s, dir).isEmpty());

#ifdef Q_OS_UNIX
  // A symbolic link next to the playlist doesn't lead out of its directory.
  const QString link = playlist_dir.filePath(u"link.jpg"_s);
  ASSERT_TRUE(QFile::link(outside_cover, link));
  EXPECT_TRUE(UrlPolicy::PlaylistCoverUrl(u"link.jpg"_s, dir).isEmpty());
#endif

}

TEST(UrlPolicyTest, CoverFiles) {

  QTemporaryDir temp_dir;
  ASSERT_TRUE(temp_dir.isValid());

  const QString cover = temp_dir.filePath(u"cover.jpg"_s);
  ASSERT_TRUE(WriteFile(cover, "not really a JPEG"));
  EXPECT_TRUE(UrlPolicy::IsCoverFileAllowed(cover));

  // The default cover is a Qt resource.
  EXPECT_TRUE(UrlPolicy::IsCoverFileAllowed(u":/pictures/cdcase.png"_s));

  EXPECT_FALSE(UrlPolicy::IsCoverFileAllowed(QString()));
  EXPECT_FALSE(UrlPolicy::IsCoverFileAllowed(temp_dir.path()));
  EXPECT_FALSE(UrlPolicy::IsCoverFileAllowed(temp_dir.filePath(u"missing.jpg"_s)));
#ifdef Q_OS_UNIX
  EXPECT_FALSE(UrlPolicy::IsCoverFileAllowed(u"/dev/zero"_s));
  EXPECT_FALSE(UrlPolicy::IsCoverFileAllowed(u"/dev/null"_s));
#endif

  // Sparse files, so the test doesn't write the data.
  const QString largest = temp_dir.filePath(u"largest.jpg"_s);
  {
    QFile file(largest);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_TRUE(file.resize(UrlPolicy::kMaxCoverFileSize));
  }
  EXPECT_TRUE(UrlPolicy::IsCoverFileAllowed(largest));

  const QString too_large = temp_dir.filePath(u"too_large.jpg"_s);
  {
    QFile file(too_large);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_TRUE(file.resize(UrlPolicy::kMaxCoverFileSize + 1));
  }
  EXPECT_FALSE(UrlPolicy::IsCoverFileAllowed(too_large));

}

TEST(UrlPolicyTest, UrlHandlerOnlyForItsOwnSource) {

  EXPECT_TRUE(UrlPolicy::IsUrlHandlerAllowed(QUrl(u"tidal:12345"_s), Song::Source::Tidal));
  EXPECT_TRUE(UrlPolicy::IsUrlHandlerAllowed(QUrl(u"qobuz:12345"_s), Song::Source::Qobuz));
  EXPECT_TRUE(UrlPolicy::IsUrlHandlerAllowed(QUrl(u"subsonic:12345"_s), Song::Source::Subsonic));
  EXPECT_TRUE(UrlPolicy::IsUrlHandlerAllowed(QUrl(u"plex:12345"_s), Song::Source::Plex));
  EXPECT_TRUE(UrlPolicy::IsUrlHandlerAllowed(QUrl(u"jellyfin:12345"_s), Song::Source::Jellyfin));

  // A radio station, a stream from a playlist or a song of another service can't address the handler of a service.
  EXPECT_FALSE(UrlPolicy::IsUrlHandlerAllowed(QUrl(u"tidal:12345"_s), Song::Source::RadioBrowser));
  EXPECT_FALSE(UrlPolicy::IsUrlHandlerAllowed(QUrl(u"tidal:12345"_s), Song::Source::SomaFM));
  EXPECT_FALSE(UrlPolicy::IsUrlHandlerAllowed(QUrl(u"tidal:12345"_s), Song::Source::Stream));
  EXPECT_FALSE(UrlPolicy::IsUrlHandlerAllowed(QUrl(u"tidal:12345"_s), Song::Source::Unknown));
  EXPECT_FALSE(UrlPolicy::IsUrlHandlerAllowed(QUrl(u"plex:12345"_s), Song::Source::Subsonic));
  EXPECT_FALSE(UrlPolicy::IsUrlHandlerAllowed(QUrl(u"subsonic:12345"_s), Song::Source::Collection));
  EXPECT_FALSE(UrlPolicy::IsUrlHandlerAllowed(QUrl(), Song::Source::Tidal));

}

TEST(UrlPolicyTest, EngineUrlsForUserSources) {

  // The user's own files, collection, devices and CDs play as before, also from a share.
  for (const Song::Source source : {Song::Source::LocalFile, Song::Source::Collection, Song::Source::Device}) {
    EXPECT_TRUE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"file:///music/a.flac"_s), source));
    EXPECT_TRUE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"file://nas/music/a.flac"_s), source));
  }
  EXPECT_TRUE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"cdda:///dev/sr0/3"_s), Song::Source::CDDA));

  // Streams the user added keep the schemes the song loader and the playlist parsers allow, but not a UNC path.
  for (const Song::Source source : {Song::Source::Stream, Song::Source::Unknown}) {
    EXPECT_TRUE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"http://example.com/live"_s), source));
    EXPECT_TRUE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"rtsp://example.com/live"_s), source));
    EXPECT_TRUE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"mms://example.com/live"_s), source));
    EXPECT_FALSE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"file://attacker.example/share/a.mp3"_s), source));
    EXPECT_FALSE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"file:////attacker.example/share/a.mp3"_s), source));
  }

}

TEST(UrlPolicyTest, EngineUrlsForRadios) {

  for (const Song::Source source : {Song::Source::RadioBrowser, Song::Source::SomaFM, Song::Source::RadioParadise}) {
    EXPECT_TRUE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"http://ice.example.com:8000/live"_s), source));
    EXPECT_TRUE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"https://ice.example.com/live.aac"_s), source));
    EXPECT_FALSE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"file:///home/user/a.mp3"_s), source));
    EXPECT_FALSE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"file://attacker.example/share/a.mp3"_s), source));
    EXPECT_FALSE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"udp://0.0.0.0:5004"_s), source));
    EXPECT_FALSE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"cdda:///dev/sda/1"_s), source));
    EXPECT_FALSE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"fd://0"_s), source));
    EXPECT_FALSE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"spotify:track:4uLU6hMCjMI75M1A2tKUQC"_s), source));
  }

}

TEST(UrlPolicyTest, EngineUrlsForStreamingServices) {

  EXPECT_TRUE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"https://sp-pr-fa.audio.tidal.com/a.flac"_s), Song::Source::Tidal));
  EXPECT_TRUE(UrlPolicy::IsEngineUrlAllowed(DashManifestUrl(), Song::Source::Tidal));
  EXPECT_FALSE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"http://sp-pr-fa.audio.tidal.com/a.flac"_s), Song::Source::Tidal));
  EXPECT_FALSE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"file:///etc/passwd"_s), Song::Source::Tidal));

  EXPECT_TRUE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"https://streaming-qobuz-std.akamaized.net/file"_s), Song::Source::Qobuz));
  EXPECT_FALSE(UrlPolicy::IsEngineUrlAllowed(DashManifestUrl(), Song::Source::Qobuz));
  EXPECT_FALSE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"file://attacker.example/share/a.flac"_s), Song::Source::Qobuz));

  EXPECT_TRUE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"spotify:track:4uLU6hMCjMI75M1A2tKUQC"_s), Song::Source::Spotify));
  EXPECT_FALSE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"https://open.spotify.com/album/abc"_s), Song::Source::Spotify));
  EXPECT_FALSE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"file:///tmp/a.ogg"_s), Song::Source::Spotify));

  // Subsonic, Plex and Jellyfin servers are configured by the user and can be http on a local network.
  for (const Song::Source source : {Song::Source::Subsonic, Song::Source::Plex, Song::Source::Jellyfin}) {
    EXPECT_TRUE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"http://192.168.1.10:4533/rest/stream"_s), source));
    EXPECT_TRUE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"https://music.example.com/stream"_s), source));
    EXPECT_FALSE(UrlPolicy::IsEngineUrlAllowed(QUrl(u"file:///home/user/a.mp3"_s), source));
  }

}

}  // namespace
