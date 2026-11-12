/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2018-2026, Jonas Kvinge <jonas@jkvinge.net>
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

#include <QtGlobal>

#include <optional>
#include <cstdio>
#include <cerrno>

#ifdef Q_OS_UNIX
#  include <sys/stat.h>
#  include <fcntl.h>
#  include <unistd.h>
#endif

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QByteArray>
#include <QIODevice>
#include <QString>
#include <QStringList>
#include <QStorageInfo>
#include <QTemporaryFile>
#include <QRandomGenerator>

#include "core/logging.h"
#include "utilities/fileutils.h"
#include "musicstorage.h"

#include "filesystemmusicstorage.h"

using namespace Qt::Literals::StringLiterals;

namespace {

bool FilesHaveSameContents(const QString &filename1, const QString &filename2) {

  if (QFileInfo(filename1).size() != QFileInfo(filename2).size()) return false;

  QFile file1(filename1);
  QFile file2(filename2);
  if (!file1.open(QIODevice::ReadOnly) || !file2.open(QIODevice::ReadOnly)) return false;

  return file1.readAll() == file2.readAll();

}

// The identity of a file, the device and inode on Unix, the volume serial number and file index on Windows.
struct FileId {
  quint64 device = 0;
  quint64 index = 0;
  bool operator==(const FileId &other) const { return device == other.device && index == other.index; }
  bool operator!=(const FileId &other) const { return !(*this == other); }
};

// Returns the identity of the file, or of the link itself, not the file it links to, when follow_links is false.
// Returns nothing when the file doesn't exist, or the identity can't be read.
std::optional<FileId> GetFileId(const QString &filename, const bool follow_links) {

#if defined(Q_OS_WIN)
  const DWORD flags = FILE_FLAG_BACKUP_SEMANTICS | (follow_links ? static_cast<DWORD>(0) : static_cast<DWORD>(FILE_FLAG_OPEN_REPARSE_POINT));
  const HANDLE handle = ::CreateFileW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(filename).utf16()), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return std::nullopt;
  BY_HANDLE_FILE_INFORMATION information {};
  const bool success = ::GetFileInformationByHandle(handle, &information) != 0;
  ::CloseHandle(handle);
  if (!success) return std::nullopt;
  return FileId { static_cast<quint64>(information.dwVolumeSerialNumber), (static_cast<quint64>(information.nFileIndexHigh) << 32) | static_cast<quint64>(information.nFileIndexLow) };
#elif defined(Q_OS_UNIX)
  const QByteArray path = QFile::encodeName(filename);
  struct stat file_stat {};
  if ((follow_links ? ::stat(path.constData(), &file_stat) : ::lstat(path.constData(), &file_stat)) != 0) return std::nullopt;
  return FileId { static_cast<quint64>(file_stat.st_dev), static_cast<quint64>(file_stat.st_ino) };
#else
  Q_UNUSED(filename);
  Q_UNUSED(follow_links);
  return std::nullopt;
#endif

}

enum class SameFile {
  // Different files, or one of them doesn't exist.
  No,
  // The same file, also through a link, or through a directory spelled in another case, so nothing needs to be renamed.
  Yes,
  // The same file, and its filename only differs in case, so the filesystem is case-insensitive, and the file only needs to be renamed to the new case.
  OtherCase,
};

// Compares the files by their identity, not by their paths, since two paths which only differ in case are the same file on case-insensitive filesystems, like FAT, exFAT, CIFS and casefolded directories, also on Linux, where QFileInfo compares the paths case-sensitively.
// Replacing the destination with the source when they're the same file would remove the file.
SameFile CompareFiles(const QString &source, const QString &destination) {

  const std::optional<FileId> source_id = GetFileId(source, true);
  const std::optional<FileId> destination_id = GetFileId(destination, true);
  if (!source_id.has_value() || !destination_id.has_value()) {
    // Without the identity, the paths are compared, which is case-insensitive on Windows.
    if (QFileInfo::exists(source) && QFileInfo::exists(destination) && QFileInfo(source) == QFileInfo(destination)) return SameFile::Yes;
    return SameFile::No;
  }
  if (source_id.value() != destination_id.value()) return SameFile::No;

  // One of them is a link to the other, or both are links to the same file, the link is not replaced with the file.
  const std::optional<FileId> source_link_id = GetFileId(source, false);
  const std::optional<FileId> destination_link_id = GetFileId(destination, false);
  if (!source_link_id.has_value() || !destination_link_id.has_value() || source_link_id.value() != destination_link_id.value()) return SameFile::Yes;

  // The same directory entry, or another hard link to the file.
  // Only a filename which differs in case is renamed, the directories are shared with other files, so they keep their case.
  // Hard links with another filename are left as they are, the filename could also differ in a way the filesystem ignores, like the Unicode normalization on macOS, then removing one of them would remove the file.
  const QString source_filename = QFileInfo(source).fileName();
  const QString destination_filename = QFileInfo(destination).fileName();
  if (source_filename != destination_filename && source_filename.compare(destination_filename, Qt::CaseInsensitive) == 0) return SameFile::OtherCase;

  return SameFile::Yes;

}

// Renames the file, without copying it when it can't be renamed, like QFile::rename() does, which writes to the destination directly.
// When replace is false, an existing destination is not replaced, also one created by another process at the same time, so the renames are atomic, and fail when the destination exists.
// When replace is true, an existing destination is replaced atomically, it's not removed first, so it's kept when the rename fails.
bool RenameFile(const QString &source, const QString &destination, const bool replace) {

#ifdef Q_OS_WIN
  return ::MoveFileExW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(source).utf16()), reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(destination).utf16()), replace ? MOVEFILE_REPLACE_EXISTING : 0) != 0;
#else
  const QByteArray source_path = QFile::encodeName(source);
  const QByteArray destination_path = QFile::encodeName(destination);
  // rename() replaces the destination, also a link, not the file it links to.
  if (replace) {
    return ::rename(source_path.constData(), destination_path.constData()) == 0;
  }
#  if defined(Q_OS_LINUX) && defined(RENAME_NOREPLACE)
  if (::renameat2(AT_FDCWD, source_path.constData(), AT_FDCWD, destination_path.constData(), RENAME_NOREPLACE) == 0) return true;
  // EINVAL when the filesystem doesn't support it.
  if (errno != EINVAL) return false;
#  endif
#  if defined(Q_OS_DARWIN) && defined(RENAME_EXCL)
  if (::renameatx_np(AT_FDCWD, source_path.constData(), AT_FDCWD, destination_path.constData(), RENAME_EXCL) == 0) return true;
  if (errno != ENOTSUP) return false;
#  endif
  // A link fails when the destination exists, and a symbolic link is linked, not the file it links to.
  if (::linkat(AT_FDCWD, source_path.constData(), AT_FDCWD, destination_path.constData(), 0) == 0) {
    if (::unlink(source_path.constData()) != 0) {
      ::unlink(destination_path.constData());
      return false;
    }
    return true;
  }
  // The filesystem doesn't support links, for example some network and FUSE filesystems, then the file is renamed instead of copying it.
  // The destination is created as an empty file first, which fails when anything exists there, also a link, then it's replaced by the rename, so a file created by another process at the same time is never replaced.
  if (errno != EPERM && errno != EOPNOTSUPP && errno != ENOSYS) return false;
  const int destination_fd = ::open(destination_path.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (destination_fd == -1) return false;
  ::close(destination_fd);
  if (::rename(source_path.constData(), destination_path.constData()) != 0) {
    ::unlink(destination_path.constData());
    return false;
  }
  return true;
#endif

}

// Renames the file to its filename in another case, when the source and the destination are the same file on a case-insensitive filesystem.
// rename() does nothing when both are the same file, for example on Linux with FAT and casefolded directories, so the file is renamed to a temporary filename first, then to the destination.
// When the second rename fails, the file is renamed back, it's never removed or replaced.
bool RenameFileCase(const QString &source, const QString &destination) {

  const QString path = QFileInfo(source).absolutePath();
  for (int attempt = 0; attempt < 16; ++attempt) {
    const QString temporary_filename = path + u"/.akousma-"_s + QString::number(QRandomGenerator::global()->generate(), 16);
    if (!RenameFile(source, temporary_filename, false)) {
      // Another file has the temporary filename, then another one is tried.
      if (GetFileId(temporary_filename, false).has_value()) continue;
      return false;
    }
    if (RenameFile(temporary_filename, destination, false)) return true;
    if (!RenameFile(temporary_filename, source, false)) {
      qLog(Error) << "Could not rename" << temporary_filename << "back to" << source;
    }
    return false;
  }

  return false;

}

// Copies the file to a temporary file in the destination directory, which is renamed to the destination when it's complete, so an interrupted copy doesn't leave a partial file at the destination.
// QFile::copy() copies to the destination directly on Windows.
// The temporary file is renamed with RenameFile(), which doesn't replace an existing file, also one created while copying, then the copy fails, and the temporary file is removed.
// When replace is true, the temporary file replaces the destination atomically, so the destination is only replaced when the copy is complete.
bool CopyFileStaged(const QString &source, const QString &destination, const bool replace) {

  QFile source_file(source);
  if (!source_file.open(QIODevice::ReadOnly)) return false;

  // QTemporaryFile::rename() renames without checking the destination when the filesystem doesn't support links.
  // QTemporaryFile keeps the file open until it's destroyed, also after close(), and on Windows an open file can't be renamed, so it's destroyed before renaming, and the temporary file is removed here when the copy fails.
  QString temporary_filename;
  {
    // The destination filename can be as long as the filesystem allows, so the temporary file has a short name, it's hidden in case it's left when copying is interrupted.
    QTemporaryFile destination_file(QFileInfo(destination).absolutePath() + u"/.akousma-XXXXXX"_s);
    if (!destination_file.open()) return false;

    // Getting the filename creates the temporary file in the directory if it's unnamed.
    temporary_filename = destination_file.fileName();
    if (temporary_filename.isEmpty()) {
      destination_file.remove();
      return false;
    }

    // The copy is also written to the storage before it's renamed, so a deferred write error, for example on a network filesystem, fails the copy, and the original is not removed when moving.
    if (!Utilities::CopyFileContents(&source_file, &destination_file)) {
      destination_file.remove();
      return false;
    }

    // The temporary file is only kept when the copy is complete, it's removed by the destructor otherwise, also when remove() above fails because flushing the file failed again.
    destination_file.setAutoRemove(false);

    // The permissions are set before the copy is renamed, so it doesn't have other permissions at the destination.
    // Some filesystems, like FAT, exFAT and CIFS, can't set them, then the copy has the permissions of the filesystem, like QFile::copy() does.
    if (!destination_file.setPermissions(source_file.permissions())) {
      qLog(Warning) << "Could not set the permissions of" << destination << destination_file.errorString();
    }
  }

  if (!RenameFile(temporary_filename, destination, replace)) {
    QFile::remove(temporary_filename);
    return false;
  }

  return true;

}

// QFile::rename() writes the file to the destination itself when the destination is on another filesystem, so when that is interrupted, for example by quitting, a partial file is left at the destination.
// The file is only renamed on the same filesystem, otherwise it's copied to a temporary file which is renamed when it's complete, then the original is removed.
// Returns true if a file can be renamed from one directory to the other, they're in the same mount, and on Unix on the same device, since for example Btrfs subvolumes in the same mount are different devices.
bool CanRename(const QString &source_path, const QString &destination_path) {

  if (QStorageInfo(source_path).rootPath() != QStorageInfo(destination_path).rootPath()) return false;

#ifdef Q_OS_UNIX
  struct stat source_stat {};
  struct stat destination_stat {};
  if (::stat(QFile::encodeName(source_path).constData(), &source_stat) != 0 || ::stat(QFile::encodeName(destination_path).constData(), &destination_stat) != 0) return false;
  if (source_stat.st_dev != destination_stat.st_dev) return false;
#endif

  return true;

}

// The filesystem of the source is the one of its directory, a link is renamed, not replaced with a copy of the file it links to.
// When the file can't be renamed, it's copied.
// When replace is true, an existing destination is replaced, it's kept when the file can't be moved.
// The source and the destination must not be the same file, see CompareFiles().
bool MoveSongFile(const QString &source, const QString &destination, const bool replace) {

  if (CanRename(QFileInfo(source).absolutePath(), QFileInfo(destination).absolutePath()) && RenameFile(source, destination, replace)) {
    return true;
  }

  if (!CopyFileStaged(source, destination, replace)) return false;

  if (!QFile::remove(source)) {
    QFile::remove(destination);
    return false;
  }

  return true;

}

}  // namespace

FilesystemMusicStorage::FilesystemMusicStorage(const Song::Source source, const QString &root, const std::optional<int> collection_directory_id) : source_(source), root_(root), collection_directory_id_(collection_directory_id) {}

bool FilesystemMusicStorage::CopyToStorage(const CopyJob &job, QString &error_text) {

  const QFileInfo src = QFileInfo(job.source_);
  const QFileInfo dest = QFileInfo(root_ + QLatin1Char('/') + job.destination_);
  // The source is a temporary file when the file was transcoded, then the original is removed instead, and the empty directories are the ones of the original.
  const QFileInfo original = QFileInfo(job.original());

  QFileInfo cover_src;
  QFileInfo cover_dest;
  if (job.albumcover_ && !job.cover_source_.isEmpty() && !job.cover_dest_.isEmpty()) {
    cover_src = QFileInfo(job.cover_source_);
    cover_dest = QFileInfo(root_ + QLatin1Char('/') + job.cover_dest_);
  }

  // Don't do anything if the destination is the same file as the source.
  // On a case-insensitive filesystem, a destination which only differs in the case of the filename is the same file, then the file is only renamed when moving, it's never replaced with itself.
  const SameFile same_file = CompareFiles(src.absoluteFilePath(), dest.absoluteFilePath());
  if (same_file == SameFile::Yes || (same_file == SameFile::OtherCase && !job.remove_original_)) return true;

  // The destination is made from the tags, so make sure it doesn't point outside the root directory.
  const QString root_path = QDir::cleanPath(QDir(root_).absolutePath());
  const QString root_prefix = root_path.endsWith(u'/') ? root_path : root_path + u'/';
  const auto is_inside_root = [&root_prefix](const QFileInfo &fileinfo) { return QDir::cleanPath(fileinfo.absoluteFilePath()).startsWith(root_prefix); };
  for (const QFileInfo &fileinfo : {dest, cover_dest}) {
    if (fileinfo.filePath().isEmpty() || is_inside_root(fileinfo)) continue;
    error_text = QObject::tr("Destination %1 is outside of %2.").arg(QDir::cleanPath(fileinfo.absoluteFilePath()), root_path);
    qLog(Error) << error_text;
    return false;
  }

  // Create directories as required
  QDir dir;
  if (!dir.mkpath(dest.absolutePath())) {
    error_text = QObject::tr("Failed to create directory %1.").arg(dest.dir().absolutePath());
    qLog(Error) << error_text;
    return false;
  }

  // The cover is not copied when it's already the cover in the destination directory, also when the directory is spelled in another case on a case-insensitive filesystem, otherwise it would be replaced with itself.
  if (!cover_src.filePath().isEmpty() && CompareFiles(cover_src.absoluteFilePath(), cover_dest.absoluteFilePath()) != SameFile::No) {
    cover_src = QFileInfo();
    cover_dest = QFileInfo();
  }

  // Copy or move
  // An existing destination is not removed first when overwriting, it's replaced when the new file is complete, so it's kept when the file can't be moved or copied.
  bool result = true;
  if (job.remove_original_) {
    if (same_file == SameFile::OtherCase) {
      result = RenameFileCase(src.absoluteFilePath(), dest.absoluteFilePath());
      if (!result) {
        error_text = QObject::tr("Could not move file %1 to %2.").arg(src.absoluteFilePath(), dest.absoluteFilePath());
        qLog(Error) << error_text;
      }
    }
    else if (dest.exists() && !job.overwrite_) {
      result = false;
      error_text = QObject::tr("Destination file %1 exists, but not allowed to overwrite.").arg(dest.absoluteFilePath());
      qLog(Error) << error_text;
    }
    else {
      result = MoveSongFile(src.absoluteFilePath(), dest.absoluteFilePath(), job.overwrite_);
      if (!result) {
        error_text = QObject::tr("Could not move file %1 to %2.").arg(src.absoluteFilePath(), dest.absoluteFilePath());
        qLog(Error) << error_text;
      }
    }
    // Remove the original of a transcoded file, unless the transcoded file replaced it, also when the destination only differs in case on a case-insensitive filesystem.
    // Some filesystems, like FUSE filesystems without stable inode numbers, give the same file another identity for each case, then the original has the contents of the transcoded file when it was replaced.
    const auto original_replaced = [&original, &dest]() {
      return CompareFiles(original.absoluteFilePath(), dest.absoluteFilePath()) != SameFile::No || (original.absoluteFilePath().compare(dest.absoluteFilePath(), Qt::CaseInsensitive) == 0 && FilesHaveSameContents(original.absoluteFilePath(), dest.absoluteFilePath()));
    };
    if (result && original != src && !original_replaced() && original.exists() && !QFile::remove(original.absoluteFilePath())) {
      result = false;
      error_text = QObject::tr("Could not remove original file %1.").arg(original.absoluteFilePath());
      qLog(Error) << error_text;
    }
    // The cover is copied, not moved, since it's shared with the other songs of the album, or it's the cover in the cache.
    bool cover_copied = false;
    if ((!cover_dest.exists() || job.overwrite_) && !cover_src.filePath().isEmpty() && !cover_dest.filePath().isEmpty()) {
      cover_copied = CopyFileStaged(cover_src.absoluteFilePath(), cover_dest.absoluteFilePath(), job.overwrite_);
      if (!cover_copied) {
        qLog(Warning) << "Could not copy cover" << cover_src.absoluteFilePath() << "to" << cover_dest.absoluteFilePath();
      }
    }
    // Remove the cover when it was next to the song and the song was the last file in that directory, so the directory can be removed.
    // Only when the cover is in the destination, either copied now, or by a previous song of the album, but not when the destination has another cover with the same filename.
    if (result && !cover_src.filePath().isEmpty() && cover_src.absolutePath() == original.absolutePath() && (cover_copied || FilesHaveSameContents(cover_src.absoluteFilePath(), cover_dest.absoluteFilePath()))) {
      const QStringList remaining_entries = QDir(original.absolutePath()).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
      if (remaining_entries == QStringList() << cover_src.fileName()) {
        QFile::remove(cover_src.absoluteFilePath());
      }
    }
    // Remove the directories which are empty now, up to the collection or device directory of the original.
    // Without a limit, this would also remove the collection directory itself, and empty directories above it like the home directory.
    // The directories are compared with links resolved, so a directory outside the root, which is linked from inside it, is not removed.
    if (result && !job.original_root_.isEmpty()) {
      QDir remove_dir(original.absolutePath(), QString(), QDir::Name, QDir::NoDotAndDotDot);
      while (Utilities::PathIsInsideDirectory(remove_dir.absolutePath(), job.original_root_) && remove_dir.isEmpty()) {
        if (!QDir().rmdir(remove_dir.absolutePath())) break;
        remove_dir.cdUp();
      }
    }
  }
  else {
    if (dest.exists() && !job.overwrite_) {
      result = false;
      error_text = QObject::tr("Destination file %1 exists, but not allowed to overwrite").arg(dest.absoluteFilePath());
      qLog(Error) << error_text;
    }
    else {
      result = CopyFileStaged(src.absoluteFilePath(), dest.absoluteFilePath(), job.overwrite_);
      if (!result) {
        error_text = QObject::tr("Could not copy file %1 to %2.").arg(src.absoluteFilePath(), dest.absoluteFilePath());
        qLog(Error) << error_text;
      }
    }
    if ((!cover_dest.exists() || job.overwrite_) && !cover_src.filePath().isEmpty() && !cover_dest.filePath().isEmpty() && !CopyFileStaged(cover_src.absoluteFilePath(), cover_dest.absoluteFilePath(), job.overwrite_)) {
      qLog(Warning) << "Could not copy cover" << cover_src.absoluteFilePath() << "to" << cover_dest.absoluteFilePath();
    }
  }

  return result;

}

bool FilesystemMusicStorage::DeleteFromStorage(const DeleteJob &job) {

  const QString path = job.metadata_.url().toLocalFile();
  const QFileInfo fileInfo(path);

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
  if (job.use_trash_ && QFile::supportsMoveToTrash()) {
#else
  if (job.use_trash_) {
#endif
    if (QFile::moveToTrash(path)) {
      return true;
    }
    qLog(Warning) << "Moving file to trash failed for" << path << ", falling back to direct deletion";
  }

  bool success = false;
  if (fileInfo.isDir()) {
    success = Utilities::RemoveRecursive(path);
  }
  else {
    success = QFile::remove(path);
  }

  if (!success) {
    qLog(Error) << "Failed to delete file" << path;
  }

  return success;

}
