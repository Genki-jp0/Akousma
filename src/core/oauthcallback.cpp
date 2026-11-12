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

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <QRandomGenerator>

#include "oauthcallback.h"

using namespace Qt::Literals::StringLiterals;

namespace OAuthCallback {

bool SecretsEqual(const QString &expected, const QString &received) {

  const QByteArray expected_data = expected.toUtf8();
  const QByteArray received_data = received.toUtf8();

  if (expected_data.isEmpty() || received_data.isEmpty()) return false;

  // Go through the whole secret without stopping at the first difference, and count a different length as a difference.
  unsigned int difference = expected_data.size() == received_data.size() ? 0U : 1U;
  for (qsizetype i = 0; i < expected_data.size(); ++i) {
    const unsigned int expected_byte = static_cast<unsigned char>(expected_data.at(i));
    const unsigned int received_byte = i < received_data.size() ? static_cast<unsigned char>(received_data.at(i)) : 0U;
    difference |= expected_byte ^ received_byte;
  }

  return difference == 0U;

}

bool StateMatches(const QString &expected_state, const QUrlQuery &url_query) {

  if (expected_state.isEmpty()) return false;
  if (!url_query.hasQueryItem(u"state"_s)) return false;

  return SecretsEqual(expected_state, url_query.queryItemValue(u"state"_s, QUrl::FullyDecoded));

}

QString RandomRedirectPath() {

  quint32 random_data[4];
  QRandomGenerator::system()->fillRange(random_data);

  QString path = u"/"_s;
  for (const quint32 value : random_data) {
    path += QStringLiteral("%1").arg(value, 8, 16, QLatin1Char('0'));
  }

  return path;

}

bool PathMatches(const QString &expected_path, const QUrl &request_url) {

  if (expected_path.isEmpty() || !request_url.isValid()) return false;

  return SecretsEqual(expected_path, request_url.path(QUrl::FullyEncoded));

}

bool IsPendingRedirectRequest(const QByteArray &method, const QUrl &request_url, const QString &expected_path, const QString &expected_state) {

  if (method != "GET") return false;
  if (!request_url.isValid()) return false;

  // Without a secret of the login, nothing ties a request to it, so don't accept any.
  if (expected_path.isEmpty() && expected_state.isEmpty()) return false;

  if (!expected_path.isEmpty() && !PathMatches(expected_path, request_url)) return false;

  if (!expected_state.isEmpty() && !StateMatches(expected_state, QUrlQuery(request_url))) return false;

  return true;

}

bool MatchesRedirectUrl(const QUrl &url, const QUrl &redirect_url) {

  if (!url.isValid() || !redirect_url.isValid()) return false;

  // Scheme and host are case insensitive and QUrl stores them in lower case, the path is compared as is.
  return url.scheme() == redirect_url.scheme() &&
         url.host() == redirect_url.host() &&
         url.port() == redirect_url.port() &&
         url.path() == redirect_url.path() &&
         url.userInfo().isEmpty();

}

}  // namespace OAuthCallback
