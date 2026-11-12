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

#include <QtGlobal>
#include <QChar>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include "core/song.h"
#include "urlpolicy.h"

using namespace Qt::Literals::StringLiterals;

namespace UrlPolicy {

namespace {

bool SchemeIs(const QUrl &url, const QString &scheme) {
  return url.scheme().compare(scheme, Qt::CaseInsensitive) == 0;
}

bool IsSlash(const QChar c) {
  return c == u'/' || c == u'\\';
}

// Whether the path is the directory or inside it, both already canonical.
bool IsInsideDirectory(const QString &path, const QString &directory) {

  if (path.isEmpty() || directory.isEmpty()) return false;

#ifdef Q_OS_WIN
  constexpr Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
  constexpr Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif

  if (directory.endsWith(u'/')) {
    return path.startsWith(directory, cs);
  }

  return path.startsWith(directory + u'/', cs);

}

}  // namespace

bool IsWebUrl(const QUrl &url) {
  return url.isValid() && (SchemeIs(url, u"http"_s) || SchemeIs(url, u"https"_s)) && !url.host().isEmpty();
}

bool IsSecureWebUrl(const QUrl &url) {
  return url.isValid() && SchemeIs(url, u"https"_s) && !url.host().isEmpty();
}

bool IsUncPath(const QString &path) {
  return path.length() >= 2 && IsSlash(path.at(0)) && IsSlash(path.at(1));
}

bool IsRemoteFileUrl(const QUrl &url) {
  // file:////host/share has no host, but its path is //host/share, which is a UNC path too.
  return SchemeIs(url, u"file"_s) && (!url.host().isEmpty() || IsUncPath(url.path()));
}

bool IsLocalFileUrl(const QUrl &url) {
  return url.isValid() && SchemeIs(url, u"file"_s) && !IsRemoteFileUrl(url) && !url.path().isEmpty();
}

bool IsRadioStreamUrl(const QUrl &url) {
  return IsWebUrl(url);
}

bool IsServiceStreamUrl(const QUrl &url) {
  return IsSecureWebUrl(url);
}

bool IsRemoteCoverUrl(const QUrl &url) {
  return IsWebUrl(url);
}

bool IsSpotifyTrackUrl(const QUrl &url) {

  static const QRegularExpression regex_spotify_track(u"^track:[0-9A-Za-z]+$"_s);
  return url.isValid() && SchemeIs(url, u"spotify"_s) && url.host().isEmpty() && url.path().contains(regex_spotify_track);

}

bool IsDashManifestDataUrl(const QUrl &url) {
  return url.isValid() && SchemeIs(url, u"data"_s) && url.path().startsWith("application/dash+xml;base64,"_L1);
}

QStringList MprisUriSchemes() {
  return QStringList() << u"file"_s << u"http"_s << u"https"_s;
}

bool IsMprisUriAllowed(const QUrl &url) {

  if (IsWebUrl(url)) return true;

  if (IsLocalFileUrl(url)) {
    return QFileInfo(url.toLocalFile()).isFile();
  }

  return false;

}

QUrl PlaylistCoverUrl(const QString &value, const QDir &dir) {

  const QString trimmed_value = value.trimmed();
  if (trimmed_value.isEmpty()) return QUrl();

  QString filename;

  // At least two letters, so a Windows drive letter (C:/...) is a path.
  static const QRegularExpression regex_url_scheme(u"^[a-z][a-z0-9+.\\-]+:"_s, QRegularExpression::CaseInsensitiveOption);
  if (trimmed_value.contains(regex_url_scheme)) {
    const QUrl url(trimmed_value);
    if (IsRemoteCoverUrl(url)) return url;
    if (!IsLocalFileUrl(url)) return QUrl();
    filename = url.toLocalFile();
  }
  else {
    filename = trimmed_value;
  }

  if (IsUncPath(filename)) return QUrl();

  if (!QDir::isAbsolutePath(filename)) {
    filename = dir.absoluteFilePath(filename);
  }

  // Compare canonical paths, so neither .. nor a symbolic link leads out of the directory of the playlist.
  const QFileInfo fileinfo(filename);
  if (!fileinfo.isFile()) return QUrl();
  const QString canonical_filename = fileinfo.canonicalFilePath();
  if (!IsInsideDirectory(canonical_filename, dir.canonicalPath())) return QUrl();

  return QUrl::fromLocalFile(canonical_filename);

}

bool IsCoverFileAllowed(const QString &filename) {

  if (filename.isEmpty()) return false;

  // Not a directory, device, FIFO or socket: /dev/zero never ends and a FIFO blocks the cover loader thread.
  const QFileInfo fileinfo(filename);
  return fileinfo.isFile() && fileinfo.size() <= kMaxCoverFileSize;

}

bool IsUrlHandlerAllowed(const QUrl &url, const Song::Source source) {

  if (!url.isValid() || url.scheme().isEmpty()) return false;

  // The URL handlers handle the schemes of the streaming services (tidal:, qobuz:, subsonic:, plex:, jellyfin:), which Song::SourceFromURL() maps to the source of their songs.
  return Song::SourceFromURL(url) == source;

}

bool IsEngineUrlAllowed(const QUrl &url, const Song::Source source) {

  switch (source) {
    // Songs the user added from their own files, collection, devices or CDs.
    case Song::Source::LocalFile:
    case Song::Source::Collection:
    case Song::Source::Device:
    case Song::Source::CDDA:
      return true;

    // Streams the user added by URL, from a playlist file or a drop. The playlist parsers and the song loader decide which schemes these have.
    case Song::Source::Stream:
    case Song::Source::Unknown:
      return !IsRemoteFileUrl(url);

    case Song::Source::RadioBrowser:
    case Song::Source::SomaFM:
    case Song::Source::RadioParadise:
      return IsRadioStreamUrl(url);

    case Song::Source::Tidal:
      return IsServiceStreamUrl(url) || IsDashManifestDataUrl(url);

    case Song::Source::Qobuz:
      return IsServiceStreamUrl(url);

    case Song::Source::Spotify:
      return IsSpotifyTrackUrl(url);

    // The server the user configured, which can be http on a local network.
    case Song::Source::Subsonic:
    case Song::Source::Plex:
    case Song::Source::Jellyfin:
      return IsWebUrl(url);
  }

  return false;

}

}  // namespace UrlPolicy
