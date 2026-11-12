/*
 * Akousma
 * Copyright 2026, Genki (genki_jp0) <genki_jp0@users.noreply.github.com>
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

#ifndef STREAMTLSVERIFICATION_H
#define STREAMTLSVERIFICATION_H

#include <QByteArray>

// How the TLS certificate of a stream fetched by GStreamer is checked (the "ssl-strict" property of the HTTP source element).
// A URL handler for a service with its own "Verify server certificate" setting (Subsonic, Plex, Jellyfin) sets Verify or NoVerify, so the stream follows that server's setting,
// the same as the Qt requests to that server do. Every other stream uses Default, which follows the global "strict SSL" backend setting.
enum class StreamTlsVerification {
  Default,
  Verify,
  NoVerify
};

namespace StreamTls {

// Verify or NoVerify from a service's "Verify server certificate" setting.
StreamTlsVerification FromVerifyCertificate(const bool verify_certificate);

// Whether the source element should be strict, ie. reject a certificate which can't be verified.
// global_strict_ssl is the global "strict SSL" backend setting, which is only used for Default.
bool StrictSSL(const bool global_strict_ssl, const StreamTlsVerification verification);

// Picks the verification for a new source element from the URI it was created for.
// A pipeline has the current stream and, for gapless playback, the next one, and the source for either can be set up while the other is still known to the pipeline.
// When source_uri matches neither, the source gets Verify if either stream needs it, otherwise Default, so a source that can't be matched never gets an exception it was not given.
StreamTlsVerification ForSourceUri(const QByteArray &source_uri, const QByteArray &current_uri, const StreamTlsVerification current_verification, const QByteArray &next_uri, const StreamTlsVerification next_verification);

// The global "strict SSL" backend setting.
bool GlobalStrictSSL();

}  // namespace StreamTls

#endif  // STREAMTLSVERIFICATION_H
