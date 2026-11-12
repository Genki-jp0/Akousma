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

#include "gtest_include.h"

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

#include "core/oauthcallback.h"

using namespace Qt::Literals::StringLiterals;

namespace {

QUrlQuery Query(const QString &query) {
  return QUrlQuery(query);
}

}  // namespace

TEST(OAuthCallbackTest, SecretsEqual) {

  EXPECT_TRUE(OAuthCallback::SecretsEqual(u"abc123-._~"_s, u"abc123-._~"_s));
  EXPECT_FALSE(OAuthCallback::SecretsEqual(u"abc123"_s, u"abc124"_s));
  EXPECT_FALSE(OAuthCallback::SecretsEqual(u"abc123"_s, u"Abc123"_s));
  // A prefix or a longer value is not the secret.
  EXPECT_FALSE(OAuthCallback::SecretsEqual(u"abc123"_s, u"abc"_s));
  EXPECT_FALSE(OAuthCallback::SecretsEqual(u"abc"_s, u"abc123"_s));
  EXPECT_FALSE(OAuthCallback::SecretsEqual(u"abc"_s, QString::fromLatin1("abc\0", 4)));
  EXPECT_TRUE(OAuthCallback::SecretsEqual(u"ståte"_s, u"ståte"_s));
  EXPECT_FALSE(OAuthCallback::SecretsEqual(u"ståte"_s, u"state"_s));

}

TEST(OAuthCallbackTest, EmptySecretsNeverMatch) {

  // F17-1: the state is a null string until a login is started, and an empty "state=" item compared equal to it.
  EXPECT_FALSE(OAuthCallback::SecretsEqual(QString(), QString()));
  EXPECT_FALSE(OAuthCallback::SecretsEqual(QString(), u""_s));
  EXPECT_FALSE(OAuthCallback::SecretsEqual(u""_s, u""_s));
  EXPECT_FALSE(OAuthCallback::SecretsEqual(QString(), u"abc"_s));
  EXPECT_FALSE(OAuthCallback::SecretsEqual(u"abc"_s, QString()));

}

TEST(OAuthCallbackTest, StateMatches) {

  const QString state = u"Ab3-._~xyz"_s;

  EXPECT_TRUE(OAuthCallback::StateMatches(state, Query(u"code=x&state=Ab3-._~xyz"_s)));
  EXPECT_TRUE(OAuthCallback::StateMatches(state, Query(u"state=Ab3-._~xyz&error=access_denied"_s)));
  EXPECT_TRUE(OAuthCallback::StateMatches(u"Ab3-12345"_s, Query(u"code=x&state=Ab3%2D12345"_s)));

  EXPECT_FALSE(OAuthCallback::StateMatches(state, Query(u"code=x"_s)));
  EXPECT_FALSE(OAuthCallback::StateMatches(state, Query(u"code=x&state="_s)));
  EXPECT_FALSE(OAuthCallback::StateMatches(state, Query(u"code=x&state"_s)));
  EXPECT_FALSE(OAuthCallback::StateMatches(state, Query(u"code=x&state=Ab3"_s)));
  EXPECT_FALSE(OAuthCallback::StateMatches(state, Query(u"code=x&state=Ab3-._~xyz0"_s)));
  EXPECT_FALSE(OAuthCallback::StateMatches(state, Query(u"code=x&STATE=Ab3-._~xyz"_s)));

}

TEST(OAuthCallbackTest, NoPendingLoginRejectsEveryState) {

  // No login pending: the expected state is empty or null, and nothing matches it.
  EXPECT_FALSE(OAuthCallback::StateMatches(QString(), Query(u"code=x&state="_s)));
  EXPECT_FALSE(OAuthCallback::StateMatches(QString(), Query(u"code=x&state"_s)));
  EXPECT_FALSE(OAuthCallback::StateMatches(QString(), Query(u"code=x"_s)));
  EXPECT_FALSE(OAuthCallback::StateMatches(u""_s, Query(u"code=x&state="_s)));
  EXPECT_FALSE(OAuthCallback::StateMatches(QString(), Query(u"code=x&state=abc"_s)));

}

TEST(OAuthCallbackTest, RandomRedirectPath) {

  const QString path = OAuthCallback::RandomRedirectPath();
  ASSERT_EQ(33, path.length());
  EXPECT_TRUE(path.startsWith(u'/'));
  for (qsizetype i = 1; i < path.length(); ++i) {
    const QChar c = path.at(i);
    EXPECT_TRUE((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f')) << path.toStdString();
  }

  // The path is usable as is in a URL.
  const QUrl url(u"http://127.0.0.1:1234"_s + path, QUrl::StrictMode);
  EXPECT_TRUE(url.isValid());
  EXPECT_EQ(path, url.path());

  // A new path every time.
  EXPECT_NE(path, OAuthCallback::RandomRedirectPath());
  EXPECT_NE(OAuthCallback::RandomRedirectPath(), OAuthCallback::RandomRedirectPath());

}

TEST(OAuthCallbackTest, PathMatches) {

  const QString path = u"/0123456789abcdef0123456789abcdef"_s;

  EXPECT_TRUE(OAuthCallback::PathMatches(path, QUrl(u"http://localhost:1234/0123456789abcdef0123456789abcdef?token=x"_s)));
  EXPECT_TRUE(OAuthCallback::PathMatches(path, QUrl(u"http://localhost:1234/0123456789abcdef0123456789abcdef"_s)));

  EXPECT_FALSE(OAuthCallback::PathMatches(path, QUrl(u"http://localhost:1234/?token=x"_s)));
  EXPECT_FALSE(OAuthCallback::PathMatches(path, QUrl(u"http://localhost:1234?token=x"_s)));
  EXPECT_FALSE(OAuthCallback::PathMatches(path, QUrl(u"http://localhost:1234/0123456789abcdef0123456789abcde?token=x"_s)));
  EXPECT_FALSE(OAuthCallback::PathMatches(path, QUrl(u"http://localhost:1234/0123456789abcdef0123456789abcdef/?token=x"_s)));
  EXPECT_FALSE(OAuthCallback::PathMatches(path, QUrl(u"http://localhost:1234/0123456789ABCDEF0123456789ABCDEF?token=x"_s)));
  EXPECT_FALSE(OAuthCallback::PathMatches(path, QUrl(u"http://localhost:1234/x/../0123456789abcdef0123456789abcdef?token=x"_s)));
  EXPECT_FALSE(OAuthCallback::PathMatches(QString(), QUrl(u"http://localhost:1234/?token=x"_s)));
  EXPECT_FALSE(OAuthCallback::PathMatches(path, QUrl()));

}

TEST(OAuthCallbackTest, PendingRedirectRequestWithPath) {

  const QString path = u"/0123456789abcdef0123456789abcdef"_s;
  const QUrl redirect(u"http://localhost:1234/0123456789abcdef0123456789abcdef?token=x"_s);

  EXPECT_TRUE(OAuthCallback::IsPendingRedirectRequest("GET", redirect, path, QString()));

  // F18-1: a token sent to the port without the random path of the login.
  EXPECT_FALSE(OAuthCallback::IsPendingRedirectRequest("GET", QUrl(u"http://localhost:1234/?token=x"_s), path, QString()));
  EXPECT_FALSE(OAuthCallback::IsPendingRedirectRequest("GET", QUrl(u"http://localhost:1234/?error=x"_s), path, QString()));
  EXPECT_FALSE(OAuthCallback::IsPendingRedirectRequest("GET", QUrl(u"http://localhost:1234/favicon.ico"_s), path, QString()));

  // Only GET.
  EXPECT_FALSE(OAuthCallback::IsPendingRedirectRequest("POST", redirect, path, QString()));
  EXPECT_FALSE(OAuthCallback::IsPendingRedirectRequest("HEAD", redirect, path, QString()));
  EXPECT_FALSE(OAuthCallback::IsPendingRedirectRequest("get", redirect, path, QString()));
  EXPECT_FALSE(OAuthCallback::IsPendingRedirectRequest(QByteArray(), redirect, path, QString()));

  EXPECT_FALSE(OAuthCallback::IsPendingRedirectRequest("GET", QUrl(), path, QString()));

}

TEST(OAuthCallbackTest, PendingRedirectRequestWithState) {

  const QString state = u"Ab3-._~xyz-1234"_s;

  EXPECT_TRUE(OAuthCallback::IsPendingRedirectRequest("GET", QUrl(u"http://localhost:1234/?code=x&state=Ab3-._~xyz-1234"_s), QString(), state));
  // The path isn't checked when it isn't random, it is chosen by the redirect page or registered with the service.
  EXPECT_TRUE(OAuthCallback::IsPendingRedirectRequest("GET", QUrl(u"http://localhost:1234/callback?code=x&state=Ab3-._~xyz-1234"_s), QString(), state));
  // An error from the service carries the state too.
  EXPECT_TRUE(OAuthCallback::IsPendingRedirectRequest("GET", QUrl(u"http://localhost:1234/?error=access_denied&state=Ab3-._~xyz-1234"_s), QString(), state));

  // F18-1, F18-3: a code or an error without the state of the login can't end it.
  EXPECT_FALSE(OAuthCallback::IsPendingRedirectRequest("GET", QUrl(u"http://localhost:1234/?code=x"_s), QString(), state));
  EXPECT_FALSE(OAuthCallback::IsPendingRedirectRequest("GET", QUrl(u"http://localhost:1234/?error=x"_s), QString(), state));
  EXPECT_FALSE(OAuthCallback::IsPendingRedirectRequest("GET", QUrl(u"http://localhost:1234/?code=x&state="_s), QString(), state));
  EXPECT_FALSE(OAuthCallback::IsPendingRedirectRequest("GET", QUrl(u"http://localhost:1234/?code=x&state=Ab3-._~xyz-1235"_s), QString(), state));
  EXPECT_FALSE(OAuthCallback::IsPendingRedirectRequest("POST", QUrl(u"http://localhost:1234/?code=x&state=Ab3-._~xyz-1234"_s), QString(), state));

}

TEST(OAuthCallbackTest, PendingRedirectRequestWithPathAndState) {

  const QString path = u"/0123456789abcdef0123456789abcdef"_s;
  const QString state = u"state"_s;

  EXPECT_TRUE(OAuthCallback::IsPendingRedirectRequest("GET", QUrl(u"http://localhost:1234/0123456789abcdef0123456789abcdef?code=x&state=state"_s), path, state));
  EXPECT_FALSE(OAuthCallback::IsPendingRedirectRequest("GET", QUrl(u"http://localhost:1234/?code=x&state=state"_s), path, state));
  EXPECT_FALSE(OAuthCallback::IsPendingRedirectRequest("GET", QUrl(u"http://localhost:1234/0123456789abcdef0123456789abcdef?code=x"_s), path, state));

}

TEST(OAuthCallbackTest, PendingRedirectRequestWithoutSecretIsNeverAccepted) {

  EXPECT_FALSE(OAuthCallback::IsPendingRedirectRequest("GET", QUrl(u"http://localhost:1234/?code=x"_s), QString(), QString()));
  EXPECT_FALSE(OAuthCallback::IsPendingRedirectRequest("GET", QUrl(u"http://localhost:1234/?code=x&state="_s), QString(), QString()));
  EXPECT_FALSE(OAuthCallback::IsPendingRedirectRequest("GET", QUrl(u"http://localhost:1234/?token=x"_s), QString(), QString()));

}

TEST(OAuthCallbackTest, MatchesRedirectUrl) {

  const QUrl redirect_url(u"tidal://login/auth"_s);

  EXPECT_TRUE(OAuthCallback::MatchesRedirectUrl(QUrl(u"tidal://login/auth?code=x&state=y"_s), redirect_url));
  EXPECT_TRUE(OAuthCallback::MatchesRedirectUrl(QUrl(u"tidal://login/auth"_s), redirect_url));
  // Scheme and host are case insensitive.
  EXPECT_TRUE(OAuthCallback::MatchesRedirectUrl(QUrl(u"TIDAL://Login/auth?code=x&state=y"_s), redirect_url));

  EXPECT_FALSE(OAuthCallback::MatchesRedirectUrl(QUrl(u"tidal://login/other?code=x&state=y"_s), redirect_url));
  EXPECT_FALSE(OAuthCallback::MatchesRedirectUrl(QUrl(u"tidal://login/auth/?code=x&state=y"_s), redirect_url));
  EXPECT_FALSE(OAuthCallback::MatchesRedirectUrl(QUrl(u"tidal://login/Auth?code=x&state=y"_s), redirect_url));
  EXPECT_FALSE(OAuthCallback::MatchesRedirectUrl(QUrl(u"tidal://login?code=x&state=y"_s), redirect_url));
  EXPECT_FALSE(OAuthCallback::MatchesRedirectUrl(QUrl(u"tidal://evil/auth?code=x&state=y"_s), redirect_url));
  EXPECT_FALSE(OAuthCallback::MatchesRedirectUrl(QUrl(u"tidal://login:80/auth?code=x&state=y"_s), redirect_url));
  EXPECT_FALSE(OAuthCallback::MatchesRedirectUrl(QUrl(u"tidal://user@login/auth?code=x&state=y"_s), redirect_url));
  EXPECT_FALSE(OAuthCallback::MatchesRedirectUrl(QUrl(u"https://login/auth?code=x&state=y"_s), redirect_url));
  EXPECT_FALSE(OAuthCallback::MatchesRedirectUrl(QUrl(), redirect_url));
  EXPECT_FALSE(OAuthCallback::MatchesRedirectUrl(QUrl(u"tidal://login/auth"_s), QUrl()));

}
