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

#ifndef OAUTHCALLBACK_H
#define OAUTHCALLBACK_H

#include <QByteArray>
#include <QString>
#include <QUrl>

class QUrlQuery;

// Checks for login callbacks (OAuth redirects, the Last.fm and Qobuz redirects), which arrive from the web browser and so can also be sent by anyone else who can reach the local redirect server or open a URL with the app.
// A callback is only accepted when it carries the secret of the login the user started: the OAuth "state", or a random path in the redirect URL.
namespace OAuthCallback {

// Compares a secret with a received value in a time that only depends on the length of the secret, so the time taken doesn't tell how much of a guess was right.
// Returns false when either is empty: a value that was never generated, or has been used, must not match anything, also not an empty one.
bool SecretsEqual(const QString &expected, const QString &received);

// Returns true if the "state" query item is present and equal to the state generated for the pending login.
// Returns false when no login is pending, i.e. the expected state is empty.
bool StateMatches(const QString &expected_state, const QUrlQuery &url_query);

// Returns a random path for a loopback redirect URL, "/" followed by 32 hex digits (128 bits from the system generator).
QString RandomRedirectPath();

// Returns true if the path of the request URL is exactly the expected path, compared as a secret.
bool PathMatches(const QString &expected_path, const QUrl &request_url);

// Returns true if a request that arrived on the local redirect server belongs to the pending login.
// The request must be a GET. When an expected path is given the path must match it, when an expected state is given the "state" query item must match it.
// At least one of the two must be given, a server without a secret of the login never accepts a request.
bool IsPendingRedirectRequest(const QByteArray &method, const QUrl &request_url, const QString &expected_path, const QString &expected_state);

// Returns true if the URL has the scheme, host and path of the redirect URL, e.g. tidal://login/auth, so other URLs of a custom scheme aren't taken for a login callback.
bool MatchesRedirectUrl(const QUrl &url, const QUrl &redirect_url);

}  // namespace OAuthCallback

#endif  // OAUTHCALLBACK_H
