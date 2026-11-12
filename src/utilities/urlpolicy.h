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

#ifndef URLPOLICY_H
#define URLPOLICY_H

#include "config.h"

#include <QtGlobal>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QDir>

#include "core/song.h"

// Which URLs Akousma accepts from sources it does not control: radio directories, streaming service and cover provider
// replies, playlist files and D-Bus clients. Each check is made where the URL is parsed, and the player checks the URL of
// a song again before it hands it to a URL handler or to the engine, against the source the song belongs to.
// The checks only look at the URL, except where a function says it looks at the file system.
namespace UrlPolicy {

// The largest local cover file the cover loader reads.
constexpr qint64 kMaxCoverFileSize = 64LL * 1024LL * 1024LL;

// http or https with a host.
bool IsWebUrl(const QUrl &url);

// https with a host.
bool IsSecureWebUrl(const QUrl &url);

// A path that starts with two slashes or backslashes in any mix (//host/share, \\host\share, \\?\UNC\host), which Windows
// opens as a UNC path, making the machine connect and authenticate to the named host.
bool IsUncPath(const QString &path);

// A file: URL with a host, which QUrl::toLocalFile() turns into a UNC path (//host/path).
bool IsRemoteFileUrl(const QUrl &url);

// A file: URL without a host.
bool IsLocalFileUrl(const QUrl &url);

// A radio station stream URL from a directory (Radio Browser, SomaFM, Radio Paradise): http or https with a host.
bool IsRadioStreamUrl(const QUrl &url);

// A stream URL or cover URL returned by a streaming service API (Tidal, Qobuz, Spotify): https with a host.
bool IsServiceStreamUrl(const QUrl &url);

// A cover image URL from a cover provider, a radio directory or service metadata: http or https with a host.
bool IsRemoteCoverUrl(const QUrl &url);

// spotify:track:<base62 id>, the only Spotify URL the Spotify engine plays.
bool IsSpotifyTrackUrl(const QUrl &url);

// The data: URL the Tidal URL handler builds around a DASH manifest (data:application/dash+xml;base64,...).
bool IsDashManifestDataUrl(const QUrl &url);

// The URI schemes accepted from MPRIS clients (OpenUri and AddTrack), also returned by SupportedUriSchemes.
QStringList MprisUriSchemes();

// A URI from an MPRIS client: http or https with a host, or a file: URL without a host that names a regular file.
// Looks at the file system for a file: URL.
bool IsMprisUriAllowed(const QUrl &url);

// The cover URL of an XSPF <image> element: an http or https URL with a host is returned as it is, a file: URL without a
// host or a path is returned as a file: URL when it names a regular file inside the directory of the playlist,
// anything else gives an empty URL. Looks at the file system for a file: URL or a path.
QUrl PlaylistCoverUrl(const QString &value, const QDir &dir);

// A local cover file the cover loader may read: a regular file (or a Qt resource) of at most kMaxCoverFileSize bytes, not
// a directory, device, FIFO or socket. UNC paths are not refused here, the covers of a collection on a share use them;
// untrusted sources are checked where they are parsed. Looks at the file system.
bool IsCoverFileAllowed(const QString &filename);

// Whether the URL handler for the scheme of the URL may load the URL of a song from the source: only when the song
// belongs to the service of the handler, so a radio station or another service can't address a service's handler.
bool IsUrlHandlerAllowed(const QUrl &url, const Song::Source source);

// Whether the engine may play the URL for a song from the source.
// Songs the user added from local files, the collection, a device or a CD play as before, also from a UNC path.
// A UNC path (a file: URL with a host) is refused for every other source.
// Radio station URLs must be http or https with a host, Tidal and Qobuz stream URLs https with a host (or the DASH manifest
// data: URL the Tidal handler builds), Spotify URLs spotify:track:<id>, and Subsonic, Plex and Jellyfin stream URLs http
// or https with a host (the server the user configured).
bool IsEngineUrlAllowed(const QUrl &url, const Song::Source source);

}  // namespace UrlPolicy

#endif  // URLPOLICY_H
