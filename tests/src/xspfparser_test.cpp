/*
 * Strawberry Music Player
 * Copyright 2026, Jonas Kvinge <jonas@jkvinge.net>
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

#include "config.h"

#include "gtest_include.h"
#include "test_utils.h"

#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QString>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include "core/song.h"
#include "playlistparsers/xspfparser.h"
#include "playlistparsers/parserbase.h"
#include "tagreader/tagreaderclient.h"

using namespace Qt::Literals::StringLiterals;

// clazy:excludeall=non-pod-global-static

namespace {

ParserBase::LoadResult LoadXSPF(QByteArray data) {

  XSPFParser parser(nullptr, nullptr);
  QBuffer buffer(&data);
  if (!buffer.open(QIODevice::ReadOnly)) return ParserBase::LoadResult();
  return parser.Load(&buffer, QString(), QDir::temp(), false);

}

QUrl LoadImage(const QByteArray &image) {

  const ParserBase::LoadResult result = LoadXSPF("<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\"><trackList><track><location>http://example.com/one.mp3</location><image>" + image + "</image></track></trackList></playlist>");
  return result.songs.size() == 1 ? result.songs[0].art_manual() : QUrl();

}

TEST(XSPFParserTest, PlaylistTitle) {

  const QByteArray data = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                          "<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\">\n"
                          "  <title>My Playlist</title>\n"
                          "  <trackList>\n"
                          "    <track><location>http://example.com/one.mp3</location><title>Track One</title></track>\n"
                          "  </trackList>\n"
                          "</playlist>\n";

  const ParserBase::LoadResult result = LoadXSPF(data);
  EXPECT_EQ(u"My Playlist"_s, result.playlist_name);
  ASSERT_EQ(1, result.songs.size());
  EXPECT_EQ(u"Track One"_s, result.songs[0].title());

}

// A playlist without a title has no playlist name, it used to get the title of the first track.
TEST(XSPFParserTest, PlaylistWithoutTitleHasNoName) {

  const QByteArray data = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                          "<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\">\n"
                          "  <creator>Someone</creator>\n"
                          "  <trackList>\n"
                          "    <track><location>http://example.com/one.mp3</location><title>Track One</title></track>\n"
                          "    <track><location>http://example.com/two.mp3</location><title>Track Two</title></track>\n"
                          "  </trackList>\n"
                          "</playlist>\n";

  const ParserBase::LoadResult result = LoadXSPF(data);
  EXPECT_TRUE(result.playlist_name.isEmpty());
  ASSERT_EQ(2, result.songs.size());
  EXPECT_EQ(u"Track One"_s, result.songs[0].title());
  EXPECT_EQ(u"Track Two"_s, result.songs[1].title());

}

// The title can be after the track list.
TEST(XSPFParserTest, PlaylistTitleAfterTrackList) {

  const QByteArray data = "<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\">"
                          "<trackList><track><location>http://example.com/one.mp3</location><title>Track One</title></track></trackList>"
                          "<title>My Playlist</title>"
                          "</playlist>";

  const ParserBase::LoadResult result = LoadXSPF(data);
  EXPECT_EQ(u"My Playlist"_s, result.playlist_name);
  ASSERT_EQ(1, result.songs.size());

}

QUrl LoadImageIn(const QDir &dir, const QByteArray &image) {

  XSPFParser parser(nullptr, nullptr);
  QByteArray data = "<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\"><trackList><track><location>http://example.com/one.mp3</location><image>" + image + "</image></track></trackList></playlist>";
  QBuffer buffer(&data);
  if (!buffer.open(QIODevice::ReadOnly)) return QUrl();
  const ParserBase::LoadResult result = parser.Load(&buffer, QString(), dir, false);
  return result.songs.size() == 1 ? result.songs[0].art_manual() : QUrl();

}

bool CreateFile(const QString &filename) {

  QFile file(filename);
  return QDir().mkpath(QFileInfo(filename).absolutePath()) && file.open(QIODevice::WriteOnly) && file.write("image") == 5;

}

QUrl CanonicalFileUrl(const QString &filename) {
  return QUrl::fromLocalFile(QFileInfo(filename).canonicalFilePath());
}

// Regression test: a local image was loaded as a URL without a scheme, so the cover was never loaded.
// Local images are only loaded from the directory of the playlist.
TEST(XSPFParserTest, ImageWithAbsolutePathIsLocalFile) {

  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString filename = tmp.filePath(u"cover.jpg"_s);
  ASSERT_TRUE(CreateFile(filename));

  const QUrl url = LoadImageIn(QDir(tmp.path()), filename.toUtf8());
  EXPECT_TRUE(url.isLocalFile());
  EXPECT_EQ(CanonicalFileUrl(filename), url);

}

// Images are saved relative to the playlist, depending on the playlist path setting.
TEST(XSPFParserTest, ImageWithRelativePathIsResolvedFromPlaylist) {

  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString playlist_dir = tmp.filePath(u"playlists"_s);
  const QString filename = playlist_dir + u"/covers/cover.jpg"_s;
  ASSERT_TRUE(CreateFile(filename));
  ASSERT_TRUE(CreateFile(tmp.filePath(u"cover.jpg"_s)));

  EXPECT_EQ(CanonicalFileUrl(filename), LoadImageIn(QDir(playlist_dir), "covers/cover.jpg"));
  // Outside the directory of the playlist.
  EXPECT_TRUE(LoadImageIn(QDir(playlist_dir), "../cover.jpg").isEmpty());

}

// Saved filenames are percent encoded.
TEST(XSPFParserTest, ImageFilenameIsDecoded) {

  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString filename = tmp.filePath(u"a%b.jpg"_s);
  ASSERT_TRUE(CreateFile(filename));

  EXPECT_EQ(CanonicalFileUrl(filename), LoadImageIn(QDir(tmp.path()), QUrl::toPercentEncoding(filename, "/")));

}

TEST(XSPFParserTest, ImageWithSchemeIsKept) {

  EXPECT_EQ(QUrl(u"https://example.com/cover.jpg"_s), LoadImage("https://example.com/cover.jpg"));

  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString filename = tmp.filePath(u"cover.jpg"_s);
  ASSERT_TRUE(CreateFile(filename));
  EXPECT_EQ(CanonicalFileUrl(filename), LoadImageIn(QDir(tmp.path()), QUrl::fromLocalFile(filename).toEncoded()));

}

QByteArray SaveXSPF(const Song &song, const QDir &dir, const PlaylistSettings::PathType path_type) {

  XSPFParser parser(nullptr, nullptr);
  QByteArray data;
  QBuffer buffer(&data);
  if (!buffer.open(QIODevice::WriteOnly)) return QByteArray();
  parser.Save(u"Playlist"_s, SongList() << song, &buffer, dir, path_type);
  return data;

}

Song MakeSong(const QUrl &url) {

  Song song(Song::Source::Stream);
  song.Init(u"Title"_s, u"Artist"_s, u"Album"_s, 180000000000LL);
  song.set_url(url);
  return song;

}

QByteArray LoadLocationXml(const QByteArray &location) {
  return QByteArray("<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\"><trackList><track><location>" + location + "</location></track></trackList></playlist>");
}

// Regression test: the colon after the scheme was percent encoded, so the location was no longer a URL.
TEST(XSPFParserTest, SaveUrlIsWrittenAsUrl) {

  const QByteArray data = SaveXSPF(MakeSong(QUrl(u"http://example.com/a%20b.mp3"_s)), QDir::temp(), PlaylistSettings::PathType::Automatic);

  EXPECT_TRUE(data.contains("<location>http://example.com/a%20b.mp3</location>")) << data.constData();

}

TEST(XSPFParserTest, SaveImageUrlIsWrittenAsUrl) {

  Song song = MakeSong(QUrl(u"http://example.com/a.mp3"_s));
  song.set_art_manual(QUrl(u"https://example.com/cover.jpg"_s));

  const QByteArray data = SaveXSPF(song, QDir::temp(), PlaylistSettings::PathType::Automatic);

  EXPECT_TRUE(data.contains("<image>https://example.com/cover.jpg</image>")) << data.constData();

}

TEST(XSPFParserTest, SaveAbsolutePathIsFileUrl) {

  const QUrl url = QUrl::fromLocalFile(u"/music/a b#1.mp3"_s);

  const QByteArray data = SaveXSPF(MakeSong(url), QDir::temp(), PlaylistSettings::PathType::Absolute);

  EXPECT_TRUE(data.contains(QByteArray("<location>" + url.toEncoded() + "</location>"))) << data.constData();
  EXPECT_TRUE(data.contains("a%20b%231.mp3")) << data.constData();

}

// Regression test: spaces were left unencoded, which isn't valid in a URI.
TEST(XSPFParserTest, SaveRelativePathIsPercentEncoded) {

  const QDir dir = QDir::temp();
  const QUrl url = QUrl::fromLocalFile(dir.absoluteFilePath(u"sub dir/ab:c.mp3"_s));

  const QByteArray data = SaveXSPF(MakeSong(url), dir, PlaylistSettings::PathType::Relative);

  EXPECT_TRUE(data.contains("<location>sub%20dir/ab%3Ac.mp3</location>")) << data.constData();

}

// Regression test: a relative file name looking like a URL was loaded as a URL when the file was missing.
TEST(XSPFParserTest, SaveRelativePathLookingLikeUrlHasPathInFront) {

  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QDir dir(tmp.path());
  Song song = MakeSong(QUrl::fromLocalFile(dir.absoluteFilePath(u"ab:c.mp3"_s)));
  song.set_art_manual(QUrl::fromLocalFile(dir.absoluteFilePath(u"ab:c.jpg"_s)));

  const QByteArray data = SaveXSPF(song, dir, PlaylistSettings::PathType::Relative);

  EXPECT_TRUE(data.contains("<location>./ab%3Ac.mp3</location>")) << data.constData();
  EXPECT_TRUE(data.contains("<image>./ab%3Ac.jpg</image>")) << data.constData();
#ifndef Q_OS_WIN
  // Local images are only loaded when the file is next to the playlist.
  ASSERT_TRUE(CreateFile(dir.absoluteFilePath(u"ab:c.jpg"_s)));
  EXPECT_EQ(CanonicalFileUrl(dir.absoluteFilePath(u"ab:c.jpg"_s)), LoadImageIn(dir, "./ab%3Ac.jpg"));
#endif

}

// A URL is not decoded when it's loaded, a decoded %2F would be a path separator.
TEST(XSPFParserTest, LoadUrlIsKeptEncoded) {

  const ParserBase::LoadResult result = LoadXSPF(LoadLocationXml("http://example.com/a%2Fb.mp3"));

  ASSERT_EQ(1, result.songs.size());
  EXPECT_EQ(QByteArray("http://example.com/a%2Fb.mp3"), result.songs[0].url().toEncoded());

}

// Playlists saved by older versions have the whole URL percent encoded, and % encoded twice.
TEST(XSPFParserTest, LoadUrlSavedByOlderVersion) {

  const ParserBase::LoadResult result = LoadXSPF(LoadLocationXml("http%3A//example.com/a%2520b.mp3"));

  ASSERT_EQ(1, result.songs.size());
  EXPECT_EQ(QByteArray("http://example.com/a%20b.mp3"), result.songs[0].url().toEncoded());

}

// Regression test: decoding a file URL before it was parsed made the rest of the path after %23 a fragment.
TEST(XSPFParserTest, ImageFileUrlIsKeptEncoded) {

  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString filename = tmp.filePath(u"a#b.jpg"_s);
  ASSERT_TRUE(CreateFile(filename));

  EXPECT_EQ(QFileInfo(filename).canonicalFilePath(), LoadImageIn(QDir(tmp.path()), QUrl::fromLocalFile(filename).toEncoded()).toLocalFile());

}

class XSPFParserFileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    tagreader_client_ = new TagReaderClient();
    tagreader_client_thread_ = new QThread();
    tagreader_client_->moveToThread(tagreader_client_thread_);
    // The client has to be deleted by its own thread, which only happens as the thread finishes.
    QObject::connect(tagreader_client_thread_, &QThread::finished, tagreader_client_, &QObject::deleteLater);
    tagreader_client_thread_->start();
  }

  void TearDown() override {
    const QPointer<TagReaderClient> tagreader_client(tagreader_client_);
    tagreader_client_thread_->exit();
    EXPECT_TRUE(tagreader_client_thread_->wait(5000));
    EXPECT_TRUE(tagreader_client.isNull());
    delete tagreader_client_thread_;
  }

  // Saves a playlist with the file, and returns the file the playlist loads back.
  QString RoundTrip(const QDir &dir, const QString &song_path, const PlaylistSettings::PathType path_type) const {
    XSPFParser parser(SharedPtr<TagReaderClient>(tagreader_client_, [](TagReaderClient *client) { Q_UNUSED(client) }), nullptr);
    QByteArray data;
    {
      QBuffer buffer(&data);
      if (!buffer.open(QIODevice::WriteOnly)) return QString();
      parser.Save(u"Playlist"_s, SongList() << MakeSong(QUrl::fromLocalFile(song_path)), &buffer, dir, path_type);
    }
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::ReadOnly)) return QString();
    const ParserBase::LoadResult result = parser.Load(&buffer, QString(), dir, false);
    return result.songs.size() == 1 ? QFileInfo(result.songs[0].url().toLocalFile()).canonicalFilePath() : QString();
  }

  QThread *tagreader_client_thread_ = nullptr;
  TagReaderClient *tagreader_client_ = nullptr;
};

TEST_F(XSPFParserFileTest, FileNamesAreLoadedBack) {

  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  TemporaryResource audio(u":/audio/strawberry.mp3"_s);
  ASSERT_TRUE(QFile::exists(audio.fileName()));
  const QDir dir(tmp.path());

  const QString song_path = tmp.filePath(u"a b#1 100%41 Ελληνικά.mp3"_s);
  ASSERT_TRUE(QFile::copy(audio.fileName(), song_path));
  const QString canonical_path = QFileInfo(song_path).canonicalFilePath();

  EXPECT_EQ(canonical_path, RoundTrip(dir, song_path, PlaylistSettings::PathType::Relative));
  EXPECT_EQ(canonical_path, RoundTrip(dir, song_path, PlaylistSettings::PathType::Absolute));

}

#ifndef Q_OS_WIN
// Regression test: a relative file name which looks like a URL because of a colon was loaded as a URL.
TEST_F(XSPFParserFileTest, FileNameWithColonIsLoadedBack) {

  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  TemporaryResource audio(u":/audio/strawberry.mp3"_s);
  ASSERT_TRUE(QFile::exists(audio.fileName()));

  const QString song_path = tmp.filePath(u"ab:c.mp3"_s);
  ASSERT_TRUE(QFile::copy(audio.fileName(), song_path));

  EXPECT_EQ(QFileInfo(song_path).canonicalFilePath(), RoundTrip(QDir(tmp.path()), song_path, PlaylistSettings::PathType::Relative));

}
#endif

// The cover loader reads a local cover file whole, a playlist must not choose a device, a file elsewhere or a UNC path.
TEST(XSPFParserTest, ImageOnlyWebUrlOrFileNextToPlaylist) {

  const QByteArray data = "<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\"><trackList>"
                          "<track><location>http://example.com/one.mp3</location><image>file:///dev/zero</image></track>"
                          "<track><location>http://example.com/two.mp3</location><image>file://attacker.example/share/a.jpg</image></track>"
                          "<track><location>http://example.com/three.mp3</location><image>//attacker.example/share/a.jpg</image></track>"
                          "<track><location>http://example.com/four.mp3</location><image>https://example.com/cover.jpg</image></track>"
                          "</trackList></playlist>";

  const ParserBase::LoadResult result = LoadXSPF(data);
  ASSERT_EQ(4, result.songs.size());
  EXPECT_TRUE(result.songs[0].art_manual().isEmpty());
  EXPECT_TRUE(result.songs[1].art_manual().isEmpty());
  EXPECT_TRUE(result.songs[2].art_manual().isEmpty());
  EXPECT_EQ(QUrl(u"https://example.com/cover.jpg"_s), result.songs[3].art_manual());

}

// A UNC path makes Windows authenticate to the host when the file is probed, such entries are skipped.
TEST(XSPFParserTest, LocationOnAnotherHostIsSkipped) {

  const QByteArray data = "<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\"><trackList>"
                          "<track><location>file://attacker.example/share/a.mp3</location></track>"
                          "<track><location>file:////attacker.example/share/a.mp3</location></track>"
                          "<track><location>//attacker.example/share/a.mp3</location></track>"
                          "<track><location>\\\\attacker.example\\share\\a.mp3</location></track>"
                          "<track><location>http://example.com/one.mp3</location></track>"
                          "</trackList></playlist>";

  const ParserBase::LoadResult result = LoadXSPF(data);
  ASSERT_EQ(1, result.songs.size());
  EXPECT_EQ(QUrl(u"http://example.com/one.mp3"_s), result.songs[0].url());

}

}  // namespace
