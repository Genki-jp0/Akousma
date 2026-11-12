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

#include "gtest_include.h"

#include <memory>

#include <QtGlobal>

#ifdef Q_OS_UNIX
#  include <unistd.h>
#endif

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QTemporaryDir>
#include <QStorageInfo>
#include <QScopeGuard>

#include "core/song.h"
#include "core/musicstorage.h"
#include "core/filesystemmusicstorage.h"

using namespace Qt::Literals::StringLiterals;

// clazy:excludeall=non-pod-global-static,returning-void-expression

namespace {

class FilesystemMusicStorageTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.isValid());
    source_path_ = temp_dir_.filePath(u"source"_s);
    root_path_ = temp_dir_.filePath(u"collection"_s);
    ASSERT_TRUE(QDir().mkpath(source_path_ + u"/Album"_s));
    ASSERT_TRUE(QDir().mkpath(root_path_));
  }

  static bool CreateFile(const QString &filename, const QByteArray &data = QByteArray("data")) {
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const bool success = file.write(data) == data.size();
    file.close();
    return success;
  }

  static QByteArray ReadFile(const QString &filename) {
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) return QByteArray();
    return file.readAll();
  }

  // Moves a song from the source album directory to Artist/Album in the collection, with the given cover.
  bool MoveSong(const QString &filename, const QString &cover_filename, const bool overwrite = false) {
    MusicStorage::CopyJob job;
    job.source_ = source_path_ + u"/Album/"_s + filename;
    job.original_root_ = source_path_;
    job.destination_ = u"Artist/Album/"_s + filename;
    job.remove_original_ = true;
    job.overwrite_ = overwrite;
    job.albumcover_ = true;
    job.cover_source_ = cover_filename;
    job.cover_dest_ = u"Artist/Album/"_s + QFileInfo(cover_filename).fileName();
    FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
    QString error_text;
    return storage.CopyToStorage(job, error_text);
  }

  // The files in the directory, also hidden ones, like a temporary file left by copying.
  static QStringList Entries(const QString &path) {
    return QDir(path).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
  }

  QTemporaryDir temp_dir_;
  QString source_path_;
  QString root_path_;
};

TEST_F(FilesystemMusicStorageTest, MoveKeepsCoverSharedWithOtherSongs) {

  const QString cover_filename = source_path_ + u"/Album/cover.jpg"_s;
  ASSERT_TRUE(CreateFile(source_path_ + u"/Album/01.flac"_s));
  ASSERT_TRUE(CreateFile(source_path_ + u"/Album/02.flac"_s));
  ASSERT_TRUE(CreateFile(cover_filename));

  ASSERT_TRUE(MoveSong(u"01.flac"_s, cover_filename));

  EXPECT_TRUE(QFile::exists(root_path_ + u"/Artist/Album/01.flac"_s));
  EXPECT_FALSE(QFile::exists(source_path_ + u"/Album/01.flac"_s));
  EXPECT_TRUE(QFile::exists(root_path_ + u"/Artist/Album/cover.jpg"_s));
  // The song left behind still has its cover.
  EXPECT_TRUE(QFile::exists(cover_filename));

}

TEST_F(FilesystemMusicStorageTest, MoveLastSongRemovesCoverAndDirectory) {

  const QString cover_filename = source_path_ + u"/Album/cover.jpg"_s;
  ASSERT_TRUE(CreateFile(source_path_ + u"/Album/01.flac"_s));
  ASSERT_TRUE(CreateFile(cover_filename));

  ASSERT_TRUE(MoveSong(u"01.flac"_s, cover_filename));

  EXPECT_TRUE(QFile::exists(root_path_ + u"/Artist/Album/01.flac"_s));
  EXPECT_TRUE(QFile::exists(root_path_ + u"/Artist/Album/cover.jpg"_s));
  EXPECT_FALSE(QFile::exists(cover_filename));
  EXPECT_FALSE(QDir(source_path_ + u"/Album"_s).exists());

}

TEST_F(FilesystemMusicStorageTest, MoveLastSongRemovesCoverCopiedByPreviousSong) {

  const QString cover_filename = source_path_ + u"/Album/cover.jpg"_s;
  ASSERT_TRUE(CreateFile(source_path_ + u"/Album/01.flac"_s));
  ASSERT_TRUE(CreateFile(source_path_ + u"/Album/02.flac"_s));
  ASSERT_TRUE(CreateFile(cover_filename));

  // The first song copies the cover, the last song finds the same cover in the destination.
  ASSERT_TRUE(MoveSong(u"01.flac"_s, cover_filename));
  ASSERT_TRUE(MoveSong(u"02.flac"_s, cover_filename));

  EXPECT_TRUE(QFile::exists(root_path_ + u"/Artist/Album/cover.jpg"_s));
  EXPECT_FALSE(QFile::exists(cover_filename));
  EXPECT_FALSE(QDir(source_path_ + u"/Album"_s).exists());

}

TEST_F(FilesystemMusicStorageTest, MoveLastSongKeepsCoverWhenDestinationHasOtherCover) {

  const QString cover_filename = source_path_ + u"/Album/cover.jpg"_s;
  ASSERT_TRUE(CreateFile(source_path_ + u"/Album/01.flac"_s));
  ASSERT_TRUE(CreateFile(cover_filename, QByteArray("source cover")));

  // The destination already has another cover with the same filename, which is not overwritten.
  ASSERT_TRUE(QDir().mkpath(root_path_ + u"/Artist/Album"_s));
  ASSERT_TRUE(CreateFile(root_path_ + u"/Artist/Album/cover.jpg"_s, QByteArray("destination cover")));

  ASSERT_TRUE(MoveSong(u"01.flac"_s, cover_filename));

  EXPECT_TRUE(QFile::exists(root_path_ + u"/Artist/Album/01.flac"_s));
  EXPECT_EQ(QByteArray("destination cover"), ReadFile(root_path_ + u"/Artist/Album/cover.jpg"_s));
  // The cover was not copied, so it's kept.
  EXPECT_EQ(QByteArray("source cover"), ReadFile(cover_filename));

}

TEST_F(FilesystemMusicStorageTest, MoveTranscodedRemovesOriginal) {

  // The source is the transcoded temporary file, the original is removed instead of it.
  const QString transcode_path = temp_dir_.filePath(u"cache/organize"_s);
  ASSERT_TRUE(QDir().mkpath(transcode_path));
  const QString transcoded_filename = transcode_path + u"/01.mp3"_s;
  const QString original_filename = source_path_ + u"/Album/01.flac"_s;
  const QString cover_filename = source_path_ + u"/Album/cover.jpg"_s;
  ASSERT_TRUE(CreateFile(transcoded_filename, QByteArray("transcoded")));
  ASSERT_TRUE(CreateFile(original_filename, QByteArray("original")));
  ASSERT_TRUE(CreateFile(cover_filename));

  MusicStorage::CopyJob job;
  job.source_ = transcoded_filename;
  job.original_source_ = original_filename;
  job.original_root_ = source_path_;
  job.destination_ = u"Artist/Album/01.mp3"_s;
  job.remove_original_ = true;
  job.albumcover_ = true;
  job.cover_source_ = cover_filename;
  job.cover_dest_ = u"Artist/Album/cover.jpg"_s;
  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  ASSERT_TRUE(storage.CopyToStorage(job, error_text));

  EXPECT_EQ(QByteArray("transcoded"), ReadFile(root_path_ + u"/Artist/Album/01.mp3"_s));
  EXPECT_TRUE(QFile::exists(root_path_ + u"/Artist/Album/cover.jpg"_s));
  EXPECT_FALSE(QFile::exists(original_filename));
  // The directory of the original is removed, not the directory of the transcoded file.
  EXPECT_FALSE(QDir(source_path_ + u"/Album"_s).exists());
  EXPECT_TRUE(QDir(transcode_path).exists());

}

TEST_F(FilesystemMusicStorageTest, MoveKeepsCoverOutsideSongDirectory) {

  // A cover in the cache is shared with all the songs of the album.
  const QString cache_path = temp_dir_.filePath(u"cache"_s);
  ASSERT_TRUE(QDir().mkpath(cache_path));
  const QString cover_filename = cache_path + u"/0123456789abcdef.jpg"_s;
  ASSERT_TRUE(CreateFile(source_path_ + u"/Album/01.flac"_s));
  ASSERT_TRUE(CreateFile(cover_filename));

  ASSERT_TRUE(MoveSong(u"01.flac"_s, cover_filename));

  EXPECT_TRUE(QFile::exists(root_path_ + u"/Artist/Album/0123456789abcdef.jpg"_s));
  EXPECT_TRUE(QFile::exists(cover_filename));

}

TEST_F(FilesystemMusicStorageTest, MoveInSameDirectoryWithOverwriteKeepsCover) {

  ASSERT_TRUE(QDir().mkpath(root_path_ + u"/Artist/Album"_s));
  const QString cover_filename = root_path_ + u"/Artist/Album/cover.jpg"_s;
  ASSERT_TRUE(CreateFile(root_path_ + u"/Artist/Album/01.flac"_s));
  ASSERT_TRUE(CreateFile(cover_filename));

  // The cover is already the cover in the destination directory, overwriting it would remove it.
  MusicStorage::CopyJob job;
  job.source_ = root_path_ + u"/Artist/Album/01.flac"_s;
  job.destination_ = u"Artist/Album/01 Title.flac"_s;
  job.remove_original_ = true;
  job.overwrite_ = true;
  job.albumcover_ = true;
  job.cover_source_ = cover_filename;
  job.cover_dest_ = u"Artist/Album/cover.jpg"_s;
  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  ASSERT_TRUE(storage.CopyToStorage(job, error_text));

  EXPECT_TRUE(QFile::exists(root_path_ + u"/Artist/Album/01 Title.flac"_s));
  EXPECT_TRUE(QFile::exists(cover_filename));

}

TEST_F(FilesystemMusicStorageTest, MoveRemovesEmptyDirectoriesUpToOriginalRoot) {

  // Reorganizing inside the collection directory removes the old empty directories, but not the collection directory itself, or the empty directories above it.
  ASSERT_TRUE(QDir(source_path_).removeRecursively());
  const QString original_filename = root_path_ + u"/Old Artist/Old Album/01.flac"_s;
  ASSERT_TRUE(QDir().mkpath(QFileInfo(original_filename).path()));
  ASSERT_TRUE(CreateFile(original_filename));

  MusicStorage::CopyJob job;
  job.source_ = original_filename;
  job.original_root_ = root_path_;
  job.destination_ = u"Artist/Album/01.flac"_s;
  job.remove_original_ = true;
  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  ASSERT_TRUE(storage.CopyToStorage(job, error_text));

  EXPECT_TRUE(QFile::exists(root_path_ + u"/Artist/Album/01.flac"_s));
  EXPECT_FALSE(QDir(root_path_ + u"/Old Artist"_s).exists());
  EXPECT_TRUE(QDir(root_path_).exists());

  // Moving the last song out of the collection directory leaves the collection directory and the empty directories above it.
  const QString destination_path = temp_dir_.filePath(u"destination"_s);
  ASSERT_TRUE(QDir().mkpath(destination_path));
  MusicStorage::CopyJob job_out;
  job_out.source_ = root_path_ + u"/Artist/Album/01.flac"_s;
  job_out.original_root_ = root_path_;
  job_out.destination_ = u"01.flac"_s;
  job_out.remove_original_ = true;
  FilesystemMusicStorage storage_out(Song::Source::Collection, destination_path);
  ASSERT_TRUE(storage_out.CopyToStorage(job_out, error_text));

  EXPECT_TRUE(QFile::exists(destination_path + u"/01.flac"_s));
  EXPECT_FALSE(QDir(root_path_ + u"/Artist"_s).exists());
  EXPECT_TRUE(QDir(root_path_).exists());
  EXPECT_TRUE(QDir(temp_dir_.path()).exists());

}

TEST_F(FilesystemMusicStorageTest, MoveWithoutOriginalRootKeepsDirectories) {

  // A file outside the collection and device directories, like one in the downloads directory, doesn't remove any directories.
  const QString original_filename = source_path_ + u"/Album/01.flac"_s;
  ASSERT_TRUE(CreateFile(original_filename));

  MusicStorage::CopyJob job;
  job.source_ = original_filename;
  job.destination_ = u"Artist/Album/01.flac"_s;
  job.remove_original_ = true;
  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  ASSERT_TRUE(storage.CopyToStorage(job, error_text));

  EXPECT_TRUE(QFile::exists(root_path_ + u"/Artist/Album/01.flac"_s));
  EXPECT_FALSE(QFile::exists(original_filename));
  EXPECT_TRUE(QDir(source_path_ + u"/Album"_s).exists());

}

#ifndef Q_OS_WIN32
TEST_F(FilesystemMusicStorageTest, MoveKeepsDirectoryOutsideOriginalRootLinkedFromInside) {

  // The album directory is outside the source root, but linked from inside it, so it's not removed.
  const QString external_path = temp_dir_.filePath(u"external"_s);
  ASSERT_TRUE(QDir().mkpath(external_path + u"/Album"_s));
  ASSERT_TRUE(QFile::link(external_path, source_path_ + u"/linked"_s));
  const QString original_filename = source_path_ + u"/linked/Album/01.flac"_s;
  ASSERT_TRUE(CreateFile(original_filename));

  MusicStorage::CopyJob job;
  job.source_ = original_filename;
  job.original_root_ = source_path_;
  job.destination_ = u"Artist/Album/01.flac"_s;
  job.remove_original_ = true;
  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  ASSERT_TRUE(storage.CopyToStorage(job, error_text));

  EXPECT_TRUE(QFile::exists(root_path_ + u"/Artist/Album/01.flac"_s));
  EXPECT_FALSE(QFile::exists(external_path + u"/Album/01.flac"_s));
  EXPECT_TRUE(QDir(external_path + u"/Album"_s).exists());

}
#endif

// A song moved to another filesystem is copied, then the original is removed.
TEST_F(FilesystemMusicStorageTest, MoveToOtherFilesystem) {

  QTemporaryDir other_temp_dir(u"/dev/shm/strawberry_test-XXXXXX"_s);
  if (!other_temp_dir.isValid() || QStorageInfo(other_temp_dir.path()).rootPath() == QStorageInfo(root_path_).rootPath()) {
    GTEST_SKIP() << "No other filesystem for the source.";
  }

  const QString source_path = other_temp_dir.filePath(u"Album"_s);
  ASSERT_TRUE(QDir().mkpath(source_path));
  ASSERT_TRUE(CreateFile(source_path + u"/01.flac"_s, QByteArray("song data")));
  const QFile::Permissions permissions = QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser | QFile::ReadGroup;
  ASSERT_TRUE(QFile::setPermissions(source_path + u"/01.flac"_s, permissions));
  // Not all permissions are supported on all platforms, so compare with the permissions the source has.
  const QFile::Permissions source_permissions = QFile::permissions(source_path + u"/01.flac"_s);

  MusicStorage::CopyJob job;
  job.source_ = source_path + u"/01.flac"_s;
  job.original_root_ = other_temp_dir.path();
  job.destination_ = u"Artist/Album/01.flac"_s;
  job.remove_original_ = true;
  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  ASSERT_TRUE(storage.CopyToStorage(job, error_text)) << error_text.toStdString();

  EXPECT_EQ(QByteArray("song data"), ReadFile(root_path_ + u"/Artist/Album/01.flac"_s));
  EXPECT_EQ(source_permissions, QFile::permissions(root_path_ + u"/Artist/Album/01.flac"_s));
  EXPECT_FALSE(QFile::exists(source_path + u"/01.flac"_s));
  EXPECT_FALSE(QFileInfo::exists(source_path));
  // No temporary file of the copy is left.
  EXPECT_EQ(QStringList() << u"01.flac"_s, QDir(root_path_ + u"/Artist/Album"_s).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot));

}

// A link on the same filesystem as the destination is renamed, also when the file it links to is on another filesystem, it's not replaced with a copy of the file.
TEST_F(FilesystemMusicStorageTest, MoveLinkToFileOnOtherFilesystem) {

  QTemporaryDir other_temp_dir(u"/dev/shm/strawberry_test-XXXXXX"_s);
  if (!other_temp_dir.isValid() || QStorageInfo(other_temp_dir.path()).rootPath() == QStorageInfo(root_path_).rootPath()) {
    GTEST_SKIP() << "No other filesystem for the linked file.";
  }

  const QString target_filename = other_temp_dir.filePath(u"01.flac"_s);
  ASSERT_TRUE(CreateFile(target_filename, QByteArray("song data")));
  ASSERT_TRUE(QFile::link(target_filename, source_path_ + u"/Album/01.flac"_s));

  MusicStorage::CopyJob job;
  job.source_ = source_path_ + u"/Album/01.flac"_s;
  job.original_root_ = source_path_;
  job.destination_ = u"Artist/Album/01.flac"_s;
  job.remove_original_ = true;
  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  ASSERT_TRUE(storage.CopyToStorage(job, error_text)) << error_text.toStdString();

  const QFileInfo dest_fileinfo(root_path_ + u"/Artist/Album/01.flac"_s);
  EXPECT_TRUE(dest_fileinfo.isSymLink());
  EXPECT_EQ(target_filename, dest_fileinfo.symLinkTarget());
  EXPECT_TRUE(QFile::exists(target_filename));
  EXPECT_FALSE(QFileInfo(source_path_ + u"/Album/01.flac"_s).isSymLink());

}

// A copied song and its cover are copied to temporary files which are renamed, the source is kept, and no temporary file is left.
TEST_F(FilesystemMusicStorageTest, CopyKeepsSourceAndPermissions) {

  const QString filename = source_path_ + u"/Album/01.flac"_s;
  const QString cover_filename = source_path_ + u"/Album/cover.jpg"_s;
  ASSERT_TRUE(CreateFile(filename, QByteArray("song data")));
  ASSERT_TRUE(CreateFile(cover_filename, QByteArray("cover data")));
  const QFile::Permissions permissions = QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser | QFile::ReadGroup;
  ASSERT_TRUE(QFile::setPermissions(filename, permissions));
  // Not all permissions are supported on all platforms, so compare with the permissions the source has.
  const QFile::Permissions source_permissions = QFile::permissions(filename);

  MusicStorage::CopyJob job;
  job.source_ = filename;
  job.destination_ = u"Artist/Album/01.flac"_s;
  job.albumcover_ = true;
  job.cover_source_ = cover_filename;
  job.cover_dest_ = u"Artist/Album/cover.jpg"_s;
  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  ASSERT_TRUE(storage.CopyToStorage(job, error_text)) << error_text.toStdString();

  EXPECT_EQ(QByteArray("song data"), ReadFile(root_path_ + u"/Artist/Album/01.flac"_s));
  EXPECT_EQ(source_permissions, QFile::permissions(root_path_ + u"/Artist/Album/01.flac"_s));
  EXPECT_EQ(QByteArray("cover data"), ReadFile(root_path_ + u"/Artist/Album/cover.jpg"_s));
  EXPECT_TRUE(QFile::exists(filename));
  EXPECT_TRUE(QFile::exists(cover_filename));
  EXPECT_EQ(QStringList() << u"01.flac"_s << u"cover.jpg"_s, QDir(root_path_ + u"/Artist/Album"_s).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name));

  // Copying again doesn't replace the copy.
  ASSERT_TRUE(CreateFile(filename, QByteArray("other song data")));
  EXPECT_FALSE(storage.CopyToStorage(job, error_text));
  EXPECT_EQ(QByteArray("song data"), ReadFile(root_path_ + u"/Artist/Album/01.flac"_s));

}

// The filename is made from the tags, so it can be as long as the filesystem allows, the temporary file for copying it must not have a longer name.
TEST_F(FilesystemMusicStorageTest, CopyLongFilename) {

  const QString filename = source_path_ + u"/Album/01.flac"_s;
  ASSERT_TRUE(CreateFile(filename, QByteArray("song data")));
  // 250 bytes, the names are limited to 255 bytes on most filesystems.
  const QString long_filename = QString(80, QChar(0x97F3)) + u"long.flac"_s + QString(1, u'x');
  ASSERT_EQ(250, long_filename.toUtf8().size());

  MusicStorage::CopyJob job;
  job.source_ = filename;
  job.destination_ = u"Artist/Album/"_s + long_filename;
  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  ASSERT_TRUE(storage.CopyToStorage(job, error_text)) << error_text.toStdString();

  EXPECT_EQ(QByteArray("song data"), ReadFile(root_path_ + u"/Artist/Album/"_s + long_filename));
  EXPECT_EQ(QStringList() << long_filename, QDir(root_path_ + u"/Artist/Album"_s).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot));

}

// When the song can't be renamed, and it's copied instead, but the original can't be removed, the copy is removed, and the original is kept.
TEST_F(FilesystemMusicStorageTest, MoveFromReadOnlyDirectoryKeepsOriginal) {

  const QString filename = source_path_ + u"/Album/01.flac"_s;
  ASSERT_TRUE(CreateFile(filename, QByteArray("song data")));
  const QString album_path = source_path_ + u"/Album"_s;
  const QFile::Permissions album_permissions = QFile::permissions(album_path);
  ASSERT_TRUE(QFile::setPermissions(album_path, QFile::ReadOwner | QFile::ExeOwner | QFile::ReadUser | QFile::ExeUser));
  const auto restore_permissions = qScopeGuard([&album_path, album_permissions]() { QFile::setPermissions(album_path, album_permissions); });
  {
    QFile file(album_path + u"/test"_s);
    if (file.open(QIODevice::WriteOnly)) {
      file.remove();
      GTEST_SKIP() << "The directory is still writable, the test is running as root.";
    }
  }

  MusicStorage::CopyJob job;
  job.source_ = filename;
  job.original_root_ = source_path_;
  job.destination_ = u"Artist/Album/01.flac"_s;
  job.remove_original_ = true;
  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  EXPECT_FALSE(storage.CopyToStorage(job, error_text));

  EXPECT_EQ(QByteArray("song data"), ReadFile(filename));
  EXPECT_EQ(QStringList(), QDir(root_path_ + u"/Artist/Album"_s).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot));

}

// A song is never moved over an existing file, also not over a link to a file which doesn't exist, which is not seen as an existing destination before moving.
TEST_F(FilesystemMusicStorageTest, MoveDoesNotReplaceExistingLink) {

#ifdef Q_OS_WIN
  GTEST_SKIP() << "QFile::link() creates a shortcut with the .lnk suffix on Windows, not a link at the destination.";
#endif

  const QString filename = source_path_ + u"/Album/01.flac"_s;
  ASSERT_TRUE(CreateFile(filename, QByteArray("song data")));
  const QString dest_filename = root_path_ + u"/Artist/Album/01.flac"_s;
  ASSERT_TRUE(QDir().mkpath(root_path_ + u"/Artist/Album"_s));
  ASSERT_TRUE(QFile::link(temp_dir_.filePath(u"missing.flac"_s), dest_filename));

  MusicStorage::CopyJob job;
  job.source_ = filename;
  job.original_root_ = source_path_;
  job.destination_ = u"Artist/Album/01.flac"_s;
  job.remove_original_ = true;
  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  EXPECT_FALSE(storage.CopyToStorage(job, error_text));

  EXPECT_EQ(QByteArray("song data"), ReadFile(filename));
  EXPECT_TRUE(QFileInfo(dest_filename).isSymLink());
  EXPECT_EQ(QStringList() << u"01.flac"_s, QDir(root_path_ + u"/Artist/Album"_s).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot));

}

// Overwriting doesn't remove the destination first, it's replaced by the new file, and no temporary file is left.
TEST_F(FilesystemMusicStorageTest, MoveWithOverwriteReplacesDestinationAndCover) {

  const QString cover_filename = source_path_ + u"/Album/cover.jpg"_s;
  ASSERT_TRUE(CreateFile(source_path_ + u"/Album/01.flac"_s, QByteArray("new song")));
  ASSERT_TRUE(CreateFile(cover_filename, QByteArray("new cover")));
  ASSERT_TRUE(QDir().mkpath(root_path_ + u"/Artist/Album"_s));
  ASSERT_TRUE(CreateFile(root_path_ + u"/Artist/Album/01.flac"_s, QByteArray("old song")));
  ASSERT_TRUE(CreateFile(root_path_ + u"/Artist/Album/cover.jpg"_s, QByteArray("old cover")));

  ASSERT_TRUE(MoveSong(u"01.flac"_s, cover_filename, true));

  EXPECT_EQ(QByteArray("new song"), ReadFile(root_path_ + u"/Artist/Album/01.flac"_s));
  EXPECT_EQ(QByteArray("new cover"), ReadFile(root_path_ + u"/Artist/Album/cover.jpg"_s));
  EXPECT_FALSE(QFile::exists(source_path_ + u"/Album/01.flac"_s));
  EXPECT_EQ(QStringList() << u"01.flac"_s << u"cover.jpg"_s, Entries(root_path_ + u"/Artist/Album"_s));

}

// When the song can't be moved, the destination which would be overwritten is kept, and so is its cover.
TEST_F(FilesystemMusicStorageTest, MoveWithOverwriteKeepsDestinationWhenSourceIsMissing) {

  ASSERT_TRUE(QDir().mkpath(root_path_ + u"/Artist/Album"_s));
  ASSERT_TRUE(CreateFile(root_path_ + u"/Artist/Album/01.flac"_s, QByteArray("old song")));
  ASSERT_TRUE(CreateFile(root_path_ + u"/Artist/Album/cover.jpg"_s, QByteArray("old cover")));

  // Neither the song nor the cover exists at the source, for example when the song was already moved to the destination.
  EXPECT_FALSE(MoveSong(u"01.flac"_s, source_path_ + u"/Album/cover.jpg"_s, true));

  EXPECT_EQ(QByteArray("old song"), ReadFile(root_path_ + u"/Artist/Album/01.flac"_s));
  EXPECT_EQ(QByteArray("old cover"), ReadFile(root_path_ + u"/Artist/Album/cover.jpg"_s));
  EXPECT_EQ(QStringList() << u"01.flac"_s << u"cover.jpg"_s, Entries(root_path_ + u"/Artist/Album"_s));

}

TEST_F(FilesystemMusicStorageTest, CopyWithOverwriteReplacesDestinationAndKeepsSource) {

  const QString filename = source_path_ + u"/Album/01.flac"_s;
  const QString cover_filename = source_path_ + u"/Album/cover.jpg"_s;
  ASSERT_TRUE(CreateFile(filename, QByteArray("new song")));
  ASSERT_TRUE(CreateFile(cover_filename, QByteArray("new cover")));
  ASSERT_TRUE(QDir().mkpath(root_path_ + u"/Artist/Album"_s));
  ASSERT_TRUE(CreateFile(root_path_ + u"/Artist/Album/01.flac"_s, QByteArray("old song")));
  ASSERT_TRUE(CreateFile(root_path_ + u"/Artist/Album/cover.jpg"_s, QByteArray("old cover")));

  MusicStorage::CopyJob job;
  job.source_ = filename;
  job.destination_ = u"Artist/Album/01.flac"_s;
  job.overwrite_ = true;
  job.albumcover_ = true;
  job.cover_source_ = cover_filename;
  job.cover_dest_ = u"Artist/Album/cover.jpg"_s;
  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  ASSERT_TRUE(storage.CopyToStorage(job, error_text)) << error_text.toStdString();

  EXPECT_EQ(QByteArray("new song"), ReadFile(root_path_ + u"/Artist/Album/01.flac"_s));
  EXPECT_EQ(QByteArray("new cover"), ReadFile(root_path_ + u"/Artist/Album/cover.jpg"_s));
  EXPECT_EQ(QByteArray("new song"), ReadFile(filename));
  EXPECT_EQ(QByteArray("new cover"), ReadFile(cover_filename));
  EXPECT_EQ(QStringList() << u"01.flac"_s << u"cover.jpg"_s, Entries(root_path_ + u"/Artist/Album"_s));

}

TEST_F(FilesystemMusicStorageTest, CopyWithOverwriteKeepsDestinationWhenSourceIsMissing) {

  ASSERT_TRUE(QDir().mkpath(root_path_ + u"/Artist/Album"_s));
  ASSERT_TRUE(CreateFile(root_path_ + u"/Artist/Album/01.flac"_s, QByteArray("old song")));
  ASSERT_TRUE(CreateFile(root_path_ + u"/Artist/Album/cover.jpg"_s, QByteArray("old cover")));

  MusicStorage::CopyJob job;
  job.source_ = source_path_ + u"/Album/01.flac"_s;
  job.destination_ = u"Artist/Album/01.flac"_s;
  job.overwrite_ = true;
  job.albumcover_ = true;
  job.cover_source_ = source_path_ + u"/Album/cover.jpg"_s;
  job.cover_dest_ = u"Artist/Album/cover.jpg"_s;
  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  EXPECT_FALSE(storage.CopyToStorage(job, error_text));

  EXPECT_EQ(QByteArray("old song"), ReadFile(root_path_ + u"/Artist/Album/01.flac"_s));
  EXPECT_EQ(QByteArray("old cover"), ReadFile(root_path_ + u"/Artist/Album/cover.jpg"_s));
  EXPECT_EQ(QStringList() << u"01.flac"_s << u"cover.jpg"_s, Entries(root_path_ + u"/Artist/Album"_s));

}

// A song moved to another filesystem over an existing file is copied to a temporary file, which replaces the destination.
TEST_F(FilesystemMusicStorageTest, MoveWithOverwriteToOtherFilesystemReplacesDestination) {

  QTemporaryDir other_temp_dir(u"/dev/shm/strawberry_test-XXXXXX"_s);
  if (!other_temp_dir.isValid() || QStorageInfo(other_temp_dir.path()).rootPath() == QStorageInfo(root_path_).rootPath()) {
    GTEST_SKIP() << "No other filesystem for the source.";
  }

  const QString source_path = other_temp_dir.filePath(u"Album"_s);
  ASSERT_TRUE(QDir().mkpath(source_path));
  ASSERT_TRUE(CreateFile(source_path + u"/01.flac"_s, QByteArray("new song")));
  ASSERT_TRUE(QDir().mkpath(root_path_ + u"/Artist/Album"_s));
  ASSERT_TRUE(CreateFile(root_path_ + u"/Artist/Album/01.flac"_s, QByteArray("old song")));

  MusicStorage::CopyJob job;
  job.source_ = source_path + u"/01.flac"_s;
  job.original_root_ = other_temp_dir.path();
  job.destination_ = u"Artist/Album/01.flac"_s;
  job.remove_original_ = true;
  job.overwrite_ = true;
  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  ASSERT_TRUE(storage.CopyToStorage(job, error_text)) << error_text.toStdString();

  EXPECT_EQ(QByteArray("new song"), ReadFile(root_path_ + u"/Artist/Album/01.flac"_s));
  EXPECT_FALSE(QFile::exists(source_path + u"/01.flac"_s));
  EXPECT_EQ(QStringList() << u"01.flac"_s, Entries(root_path_ + u"/Artist/Album"_s));

}

// On a case-sensitive filesystem, a destination which only differs in case is another file, which is replaced when overwriting.
TEST_F(FilesystemMusicStorageTest, MoveWithOverwriteReplacesOtherFileDifferingInCase) {

  ASSERT_TRUE(QDir().mkpath(root_path_ + u"/Artist/Album"_s));
  ASSERT_TRUE(CreateFile(root_path_ + u"/Artist/Album/01 help.flac"_s, QByteArray("new song")));
  if (QFile::exists(root_path_ + u"/Artist/Album/01 Help.flac"_s)) {
    GTEST_SKIP() << "The temporary directory is on a case-insensitive filesystem.";
  }
  ASSERT_TRUE(CreateFile(root_path_ + u"/Artist/Album/01 Help.flac"_s, QByteArray("old song")));

  MusicStorage::CopyJob job;
  job.source_ = root_path_ + u"/Artist/Album/01 help.flac"_s;
  job.original_root_ = root_path_;
  job.destination_ = u"Artist/Album/01 Help.flac"_s;
  job.remove_original_ = true;
  job.overwrite_ = true;
  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  ASSERT_TRUE(storage.CopyToStorage(job, error_text)) << error_text.toStdString();

  EXPECT_EQ(QByteArray("new song"), ReadFile(root_path_ + u"/Artist/Album/01 Help.flac"_s));
  EXPECT_EQ(QStringList() << u"01 Help.flac"_s, Entries(root_path_ + u"/Artist/Album"_s));

}

#ifndef Q_OS_WIN
// The destination is the source through a linked directory, so it's the same file, which is not replaced with itself.
TEST_F(FilesystemMusicStorageTest, MoveWithOverwriteThroughLinkedDirectoryKeepsFileAndCover) {

  ASSERT_TRUE(QDir().mkpath(root_path_ + u"/Artist/Album"_s));
  ASSERT_TRUE(CreateFile(root_path_ + u"/Artist/Album/01.flac"_s, QByteArray("song data")));
  ASSERT_TRUE(CreateFile(root_path_ + u"/Artist/Album/cover.jpg"_s, QByteArray("cover data")));
  ASSERT_TRUE(QFile::link(root_path_ + u"/Artist"_s, root_path_ + u"/Linked"_s));

  MusicStorage::CopyJob job;
  job.source_ = root_path_ + u"/Artist/Album/01.flac"_s;
  job.original_root_ = root_path_;
  job.destination_ = u"Linked/Album/01.flac"_s;
  job.remove_original_ = true;
  job.overwrite_ = true;
  job.albumcover_ = true;
  job.cover_source_ = root_path_ + u"/Artist/Album/cover.jpg"_s;
  job.cover_dest_ = u"Linked/Album/cover.jpg"_s;
  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  ASSERT_TRUE(storage.CopyToStorage(job, error_text)) << error_text.toStdString();

  EXPECT_EQ(QByteArray("song data"), ReadFile(root_path_ + u"/Artist/Album/01.flac"_s));
  EXPECT_EQ(QByteArray("cover data"), ReadFile(root_path_ + u"/Artist/Album/cover.jpg"_s));
  EXPECT_EQ(QStringList() << u"01.flac"_s << u"cover.jpg"_s, Entries(root_path_ + u"/Artist/Album"_s));

}

// The source is a link to the destination, replacing the destination with the link would remove the file.
TEST_F(FilesystemMusicStorageTest, MoveWithOverwriteOfLinkToDestinationKeepsFile) {

  ASSERT_TRUE(QDir().mkpath(root_path_ + u"/Artist/Album"_s));
  const QString dest_filename = root_path_ + u"/Artist/Album/01.flac"_s;
  ASSERT_TRUE(CreateFile(dest_filename, QByteArray("song data")));
  ASSERT_TRUE(QFile::link(dest_filename, source_path_ + u"/Album/01.flac"_s));

  ASSERT_TRUE(MoveSong(u"01.flac"_s, QString(), true));

  EXPECT_FALSE(QFileInfo(dest_filename).isSymLink());
  EXPECT_EQ(QByteArray("song data"), ReadFile(dest_filename));

}

// The destination is a link to the source, the source is not replaced through it.
TEST_F(FilesystemMusicStorageTest, MoveWithOverwriteOntoLinkToSourceKeepsFile) {

  const QString filename = source_path_ + u"/Album/01.flac"_s;
  ASSERT_TRUE(CreateFile(filename, QByteArray("song data")));
  ASSERT_TRUE(QDir().mkpath(root_path_ + u"/Artist/Album"_s));
  ASSERT_TRUE(QFile::link(filename, root_path_ + u"/Artist/Album/01.flac"_s));

  ASSERT_TRUE(MoveSong(u"01.flac"_s, QString(), true));

  EXPECT_EQ(QByteArray("song data"), ReadFile(filename));
  EXPECT_EQ(QByteArray("song data"), ReadFile(root_path_ + u"/Artist/Album/01.flac"_s));

}

// An existing link at the destination is replaced, not the file it links to.
TEST_F(FilesystemMusicStorageTest, MoveWithOverwriteReplacesLinkNotItsTarget) {

  const QString other_filename = temp_dir_.filePath(u"other.flac"_s);
  ASSERT_TRUE(CreateFile(other_filename, QByteArray("other song")));
  ASSERT_TRUE(CreateFile(source_path_ + u"/Album/01.flac"_s, QByteArray("new song")));
  ASSERT_TRUE(QDir().mkpath(root_path_ + u"/Artist/Album"_s));
  const QString dest_filename = root_path_ + u"/Artist/Album/01.flac"_s;
  ASSERT_TRUE(QFile::link(other_filename, dest_filename));

  ASSERT_TRUE(MoveSong(u"01.flac"_s, QString(), true));

  EXPECT_FALSE(QFileInfo(dest_filename).isSymLink());
  EXPECT_EQ(QByteArray("new song"), ReadFile(dest_filename));
  EXPECT_EQ(QByteArray("other song"), ReadFile(other_filename));

}
#endif

#ifdef Q_OS_UNIX
// The destination is another hard link to the source, so it's the same file, which is not replaced with itself.
TEST_F(FilesystemMusicStorageTest, MoveWithOverwriteOntoHardLinkKeepsFile) {

  const QString filename = source_path_ + u"/Album/01.flac"_s;
  ASSERT_TRUE(CreateFile(filename, QByteArray("song data")));
  ASSERT_TRUE(QDir().mkpath(root_path_ + u"/Artist/Album"_s));
  const QString dest_filename = root_path_ + u"/Artist/Album/01.flac"_s;
  if (::link(QFile::encodeName(filename).constData(), QFile::encodeName(dest_filename).constData()) != 0) {
    GTEST_SKIP() << "The filesystem doesn't support hard links.";
  }

  ASSERT_TRUE(MoveSong(u"01.flac"_s, QString(), true));

  EXPECT_EQ(QByteArray("song data"), ReadFile(dest_filename));

}
#endif

// The tests run in AKOUSMA_TEST_CASE_INSENSITIVE_DIR when it's set, for example a FAT, exFAT or casefolded directory on Linux, or a ciopfs mount with -o use_ino, which gives a file the same inode number in every case.
// Otherwise they run in the temporary directory, which is case-insensitive by default on Windows and macOS, and they're skipped when the directory is case-sensitive.
class FilesystemMusicStorageCaseInsensitiveTest : public FilesystemMusicStorageTest {
 protected:
  void SetUp() override {
    const QString path = qEnvironmentVariable("AKOUSMA_TEST_CASE_INSENSITIVE_DIR", QDir::tempPath());
    ci_temp_dir_ = std::make_unique<QTemporaryDir>(path + u"/strawberry_test-XXXXXX"_s);
    ASSERT_TRUE(ci_temp_dir_->isValid()) << path.toStdString();
    ASSERT_TRUE(CreateFile(ci_temp_dir_->filePath(u"probe"_s)));
    if (!QFile::exists(ci_temp_dir_->filePath(u"PROBE"_s))) {
      GTEST_SKIP() << "The directory " << path.toStdString() << " is case-sensitive, set AKOUSMA_TEST_CASE_INSENSITIVE_DIR to a case-insensitive directory.";
    }
    ASSERT_TRUE(QFile::remove(ci_temp_dir_->filePath(u"probe"_s)));
    root_path_ = ci_temp_dir_->filePath(u"collection"_s);
    ASSERT_TRUE(QDir().mkpath(root_path_ + u"/the beatles/Help"_s));
  }

  MusicStorage::CopyJob CaseJob(const QString &source_filename, const QString &destination_filename, const bool remove_original, const bool overwrite) const {
    MusicStorage::CopyJob job;
    job.source_ = root_path_ + u"/the beatles/Help/"_s + source_filename;
    job.original_root_ = root_path_;
    job.destination_ = u"The Beatles/Help/"_s + destination_filename;
    job.remove_original_ = remove_original;
    job.overwrite_ = overwrite;
    job.albumcover_ = true;
    job.cover_source_ = root_path_ + u"/the beatles/Help/cover.jpg"_s;
    job.cover_dest_ = u"The Beatles/Help/cover.jpg"_s;
    return job;
  }

  std::unique_ptr<QTemporaryDir> ci_temp_dir_;
};

// Only the case of the filename changes, with overwriting on, the song and the cover are kept, the song is renamed.
TEST_F(FilesystemMusicStorageCaseInsensitiveTest, CaseOnlyRenameWithOverwriteKeepsSongAndCover) {

  ASSERT_TRUE(CreateFile(root_path_ + u"/the beatles/Help/01 - help.flac"_s, QByteArray("song data")));
  ASSERT_TRUE(CreateFile(root_path_ + u"/the beatles/Help/cover.jpg"_s, QByteArray("cover data")));

  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  ASSERT_TRUE(storage.CopyToStorage(CaseJob(u"01 - help.flac"_s, u"01 - Help.flac"_s, true, true), error_text)) << error_text.toStdString();

  // The directories are shared with other songs, so they keep their case.
  EXPECT_EQ(QStringList() << u"01 - Help.flac"_s << u"cover.jpg"_s, Entries(root_path_ + u"/the beatles/Help"_s));
  EXPECT_EQ(QByteArray("song data"), ReadFile(root_path_ + u"/The Beatles/Help/01 - Help.flac"_s));
  EXPECT_EQ(QByteArray("cover data"), ReadFile(root_path_ + u"/The Beatles/Help/cover.jpg"_s));

}

// Nothing is overwritten when only the case changes, so the song is also renamed without overwriting.
TEST_F(FilesystemMusicStorageCaseInsensitiveTest, CaseOnlyRenameWithoutOverwrite) {

  ASSERT_TRUE(CreateFile(root_path_ + u"/the beatles/Help/01 - help.flac"_s, QByteArray("song data")));
  ASSERT_TRUE(CreateFile(root_path_ + u"/the beatles/Help/cover.jpg"_s, QByteArray("cover data")));

  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  ASSERT_TRUE(storage.CopyToStorage(CaseJob(u"01 - help.flac"_s, u"01 - Help.flac"_s, true, false), error_text)) << error_text.toStdString();

  EXPECT_EQ(QStringList() << u"01 - Help.flac"_s << u"cover.jpg"_s, Entries(root_path_ + u"/the beatles/Help"_s));
  EXPECT_EQ(QByteArray("song data"), ReadFile(root_path_ + u"/The Beatles/Help/01 - Help.flac"_s));

}

// A copy to the same file in another case is not made, the file is kept as it is.
TEST_F(FilesystemMusicStorageCaseInsensitiveTest, CaseOnlyCopyWithOverwriteKeepsSongAndCover) {

  ASSERT_TRUE(CreateFile(root_path_ + u"/the beatles/Help/01 - help.flac"_s, QByteArray("song data")));
  ASSERT_TRUE(CreateFile(root_path_ + u"/the beatles/Help/cover.jpg"_s, QByteArray("cover data")));

  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  ASSERT_TRUE(storage.CopyToStorage(CaseJob(u"01 - help.flac"_s, u"01 - Help.flac"_s, false, true), error_text)) << error_text.toStdString();

  EXPECT_EQ(QStringList() << u"01 - help.flac"_s << u"cover.jpg"_s, Entries(root_path_ + u"/the beatles/Help"_s));
  EXPECT_EQ(QByteArray("song data"), ReadFile(root_path_ + u"/the beatles/Help/01 - help.flac"_s));
  EXPECT_EQ(QByteArray("cover data"), ReadFile(root_path_ + u"/the beatles/Help/cover.jpg"_s));

}

// Only the case of the directory changes, and the filename changes, the cover in the directory is the same file as the destination cover, so it's kept.
TEST_F(FilesystemMusicStorageCaseInsensitiveTest, DirectoryCaseOnlyWithOverwriteKeepsCover) {

  ASSERT_TRUE(CreateFile(root_path_ + u"/the beatles/Help/01.flac"_s, QByteArray("song data")));
  ASSERT_TRUE(CreateFile(root_path_ + u"/the beatles/Help/cover.jpg"_s, QByteArray("cover data")));

  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  ASSERT_TRUE(storage.CopyToStorage(CaseJob(u"01.flac"_s, u"01 - Help.flac"_s, true, true), error_text)) << error_text.toStdString();

  EXPECT_EQ(QStringList() << u"01 - Help.flac"_s << u"cover.jpg"_s, Entries(root_path_ + u"/the beatles/Help"_s));
  EXPECT_EQ(QByteArray("song data"), ReadFile(root_path_ + u"/The Beatles/Help/01 - Help.flac"_s));
  EXPECT_EQ(QByteArray("cover data"), ReadFile(root_path_ + u"/The Beatles/Help/cover.jpg"_s));

}

// A transcoded song replaces its original, which only differs in case, the original is not removed afterwards, since it's the transcoded song now.
TEST_F(FilesystemMusicStorageCaseInsensitiveTest, TranscodedCaseOnlyKeepsTranscodedSong) {

  const QString transcode_path = ci_temp_dir_->filePath(u"cache/organize"_s);
  ASSERT_TRUE(QDir().mkpath(transcode_path));
  const QString transcoded_filename = transcode_path + u"/01 - Help.mp3"_s;
  ASSERT_TRUE(CreateFile(transcoded_filename, QByteArray("transcoded")));
  const QString original_filename = root_path_ + u"/the beatles/Help/01 - help.mp3"_s;
  ASSERT_TRUE(CreateFile(original_filename, QByteArray("original")));

  MusicStorage::CopyJob job;
  job.source_ = transcoded_filename;
  job.original_source_ = original_filename;
  job.original_root_ = root_path_;
  job.destination_ = u"The Beatles/Help/01 - Help.mp3"_s;
  job.remove_original_ = true;
  job.overwrite_ = true;
  FilesystemMusicStorage storage(Song::Source::Collection, root_path_);
  QString error_text;
  ASSERT_TRUE(storage.CopyToStorage(job, error_text)) << error_text.toStdString();

  EXPECT_EQ(QByteArray("transcoded"), ReadFile(root_path_ + u"/The Beatles/Help/01 - Help.mp3"_s));
  EXPECT_EQ(1, Entries(root_path_ + u"/the beatles/Help"_s).count());

}

}  // namespace
