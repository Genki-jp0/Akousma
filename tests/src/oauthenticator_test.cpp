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

#include "config.h"

#include "gtest_include.h"

#include <cstring>

#include <QList>
#include <QPointer>
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>
#include <QIODevice>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QDesktopServices>
#include <QCoreApplication>
#include <QSignalSpy>

#include "includes/shared_ptr.h"
#include "core/settings.h"
#include "core/networkaccessmanager.h"
#include "core/oauthenticator.h"
#include "urlrecorder.h"

using std::make_shared;
using namespace Qt::Literals::StringLiterals;

// clazy:excludeall=non-pod-global-static

namespace {

constexpr char kSettingsGroup[] = "OAuthenticatorTest";
constexpr char kAuthorizeUrl[] = "https://login.example.com/authorize";
constexpr char kAccessTokenUrl[] = "https://login.example.com/token";
constexpr char kRedirectUrl[] = "tidal://login/auth";

// A reply which is finished by the test.
class ControlledNetworkReply : public QNetworkReply {
 public:
  explicit ControlledNetworkReply(const QNetworkRequest &network_request, const QNetworkAccessManager::Operation operation, QObject *parent = nullptr) : QNetworkReply(parent), pos_(0) {
    setRequest(network_request);
    setUrl(network_request.url());
    setOperation(operation);
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
  }

  void abort() override {
    if (isFinished()) return;
    setError(QNetworkReply::OperationCanceledError, u"Operation canceled"_s);
    setFinished(true);
    Q_EMIT finished();
  }

  void Finish(const int http_status, const QByteArray &data) {
    if (isFinished()) return;
    data_ = data;
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, http_status);
    setFinished(true);
    Q_EMIT finished();
  }

  qint64 bytesAvailable() const override { return data_.size() - pos_ + QNetworkReply::bytesAvailable(); }

 protected:
  qint64 readData(char *data, const qint64 maxlen) override {
    const qint64 len = qMin(maxlen, data_.size() - pos_);
    if (len <= 0) return -1;
    memcpy(data, data_.constData() + pos_, static_cast<size_t>(len));
    pos_ += len;
    return len;
  }

 private:
  QByteArray data_;
  qint64 pos_;
};

// Records the requests, and keeps the replies open until the test finishes them.
class RecordingNetworkAccessManager : public NetworkAccessManager {
 public:
  QList<QPointer<ControlledNetworkReply>> replies;
  QList<QUrl> urls;
  QList<QUrlQuery> bodies;

  void FinishReply(const int i, const int http_status, const QByteArray &data) {
    if (replies.value(i)) replies.value(i)->Finish(http_status, data);
  }

 protected:
  QNetworkReply *createRequest(const Operation op, const QNetworkRequest &network_request, QIODevice *outgoing_data) override {
    ControlledNetworkReply *reply = new ControlledNetworkReply(network_request, op, this);
    replies << reply;
    urls << network_request.url();
    bodies << QUrlQuery(QString::fromUtf8(outgoing_data ? outgoing_data->readAll() : QByteArray()));
    return reply;
  }
};

// Opening the authorize URL is recorded instead of starting the web browser.
class UrlHandlerGuard {
 public:
  explicit UrlHandlerGuard(UrlRecorder *recorder) {
    QDesktopServices::setUrlHandler(u"https"_s, recorder, "OpenUrl");
  }
  ~UrlHandlerGuard() {
    QDesktopServices::unsetUrlHandler(u"https"_s);
  }
  Q_DISABLE_COPY(UrlHandlerGuard)
};

void ClearSettings() {

  Settings s;
  s.remove(QLatin1String(kSettingsGroup));

}

// A valid session of the user, as left by an earlier login.
void StoreSession() {

  Settings s;
  s.remove(QLatin1String(kSettingsGroup));
  s.beginGroup(QLatin1String(kSettingsGroup));
  s.setValue(u"token_type"_s, u"Bearer"_s);
  s.setValue(u"access_token"_s, u"user_access_token"_s);
  s.setValue(u"refresh_token"_s, u"user_refresh_token"_s);
  s.setValue(u"expires_in"_s, 3600LL);
  s.setValue(u"login_time"_s, QDateTime::currentSecsSinceEpoch());
  s.endGroup();

}

QString StoredAccessToken() {

  Settings s;
  s.beginGroup(QLatin1String(kSettingsGroup));
  const QString access_token = s.value(u"access_token"_s).toString();
  s.endGroup();
  return access_token;

}

// Set up like Tidal: the redirect URL has a custom scheme, which the application receives from the operating system.
void Configure(OAuthenticator *oauth) {

  oauth->set_settings_group(QLatin1String(kSettingsGroup));
  oauth->set_type(OAuthenticator::Type::Authorization_Code);
  oauth->set_authorize_url(QUrl(QLatin1String(kAuthorizeUrl)));
  oauth->set_redirect_url(QUrl(QLatin1String(kRedirectUrl)));
  oauth->set_access_token_url(QUrl(QLatin1String(kAccessTokenUrl)));
  oauth->set_client_id(u"client_id"_s);
  oauth->set_scope(u"r_usr w_usr"_s);
  oauth->set_use_local_redirect_server(false);
  oauth->set_port_type(OAuthenticator::PortType::SetToRedirectURL);
  oauth->set_use_pkce(true);

}

QUrl Callback(const QString &query) {
  return QUrl(QLatin1String(kRedirectUrl) + u"?"_s + query);
}

// Starts a login and returns its state, taken from the authorize URL opened in the web browser.
QString StartLogin(OAuthenticator *oauth, UrlRecorder *recorder) {

  const qsizetype count = recorder->urls().count();
  oauth->Authenticate();
  if (recorder->urls().count() != count + 1) return QString();
  return QUrlQuery(recorder->urls().last()).queryItemValue(u"state"_s, QUrl::FullyDecoded);

}

}  // namespace

// F17-1: tidal://login/auth?code=x&state= reached the token endpoint with no login pending, because the empty state compared equal to the null state.
TEST(OAuthenticatorTest, IgnoresCallbacksWithoutPendingLogin) {

  StoreSession();

  SharedPtr<RecordingNetworkAccessManager> network = make_shared<RecordingNetworkAccessManager>();
  OAuthenticator oauth(network);
  Configure(&oauth);
  oauth.LoadSession();
  ASSERT_TRUE(oauth.authenticated());
  EXPECT_FALSE(oauth.login_pending());

  QSignalSpy spy(&oauth, &OAuthenticator::AuthenticationFinished);

  oauth.ExternalAuthorizationUrlReceived(Callback(u"code=x&state="_s));
  oauth.ExternalAuthorizationUrlReceived(Callback(u"code=x&state"_s));
  oauth.ExternalAuthorizationUrlReceived(Callback(u"code=x"_s));
  oauth.ExternalAuthorizationUrlReceived(Callback(u"error=x&state="_s));
  oauth.ExternalAuthorizationUrlReceived(Callback(u"error_description=Spoofed&state="_s));
  QCoreApplication::processEvents();

  EXPECT_TRUE(network->urls.isEmpty());
  EXPECT_EQ(0, spy.count());
  EXPECT_TRUE(oauth.authenticated());
  EXPECT_EQ(u"user_access_token"_s, StoredAccessToken());

  ClearSettings();

}

TEST(OAuthenticatorTest, AcceptsOnlyTheStateOfThePendingLoginOnce) {

  ClearSettings();

  UrlRecorder recorder;
  UrlHandlerGuard url_handler_guard(&recorder);

  SharedPtr<RecordingNetworkAccessManager> network = make_shared<RecordingNetworkAccessManager>();
  OAuthenticator oauth(network);
  Configure(&oauth);

  const QString state = StartLogin(&oauth, &recorder);
  ASSERT_FALSE(state.isEmpty());
  EXPECT_TRUE(oauth.login_pending());
  const QString code_challenge = QUrlQuery(recorder.urls().last()).queryItemValue(u"code_challenge"_s);
  EXPECT_FALSE(code_challenge.isEmpty());

  QSignalSpy spy(&oauth, &OAuthenticator::AuthenticationFinished);

  // Wrong, empty or missing state, or another URL than the redirect URL.
  oauth.ExternalAuthorizationUrlReceived(Callback(u"code=attacker&state="_s));
  oauth.ExternalAuthorizationUrlReceived(Callback(u"code=attacker&state"_s));
  oauth.ExternalAuthorizationUrlReceived(Callback(u"code=attacker"_s));
  oauth.ExternalAuthorizationUrlReceived(Callback(u"code=attacker&state="_s + state + u"x"_s));
  oauth.ExternalAuthorizationUrlReceived(Callback(u"code=attacker&state="_s + state.left(state.length() - 1)));
  oauth.ExternalAuthorizationUrlReceived(QUrl(u"tidal://login/other?code=attacker&state="_s + state));
  oauth.ExternalAuthorizationUrlReceived(QUrl(u"tidal://evil/auth?code=attacker&state="_s + state));
  QCoreApplication::processEvents();

  // Ignored, and the login is still pending.
  EXPECT_TRUE(network->urls.isEmpty());
  EXPECT_EQ(0, spy.count());
  EXPECT_TRUE(oauth.login_pending());

  // The redirect of the login.
  oauth.ExternalAuthorizationUrlReceived(Callback(u"code=user&state="_s + state));
  ASSERT_EQ(1, network->urls.count());
  EXPECT_EQ(QUrl(QLatin1String(kAccessTokenUrl)), network->urls.at(0));
  const QUrlQuery body = network->bodies.at(0);
  EXPECT_EQ(u"authorization_code"_s, body.queryItemValue(u"grant_type"_s));
  EXPECT_EQ(u"user"_s, body.queryItemValue(u"code"_s));
  EXPECT_FALSE(body.queryItemValue(u"code_verifier"_s).isEmpty());
  EXPECT_FALSE(oauth.login_pending());

  // The state can only be used once.
  oauth.ExternalAuthorizationUrlReceived(Callback(u"code=user&state="_s + state));
  oauth.ExternalAuthorizationUrlReceived(Callback(u"code=attacker&state="_s + state));
  QCoreApplication::processEvents();
  EXPECT_EQ(1, network->urls.count());

  network->FinishReply(0, 200, R"({"token_type": "Bearer", "access_token": "new_access_token", "refresh_token": "new_refresh_token", "expires_in": 3600})");
  ASSERT_EQ(1, spy.count());
  EXPECT_TRUE(spy.at(0).at(0).toBool());
  EXPECT_EQ(u"new_access_token"_s, oauth.access_token());

  ClearSettings();

}

// F18-3: an error without the state of the login could end it.
TEST(OAuthenticatorTest, ErrorWithoutTheStateDoesNotEndTheLogin) {

  ClearSettings();

  UrlRecorder recorder;
  UrlHandlerGuard url_handler_guard(&recorder);

  SharedPtr<RecordingNetworkAccessManager> network = make_shared<RecordingNetworkAccessManager>();
  OAuthenticator oauth(network);
  Configure(&oauth);

  const QString state = StartLogin(&oauth, &recorder);
  ASSERT_FALSE(state.isEmpty());

  QSignalSpy spy(&oauth, &OAuthenticator::AuthenticationFinished);

  oauth.ExternalAuthorizationUrlReceived(Callback(u"error=access_denied"_s));
  oauth.ExternalAuthorizationUrlReceived(Callback(u"error_description=Spoofed&state=wrong"_s));
  QCoreApplication::processEvents();
  EXPECT_EQ(0, spy.count());
  EXPECT_TRUE(oauth.login_pending());

  // An error from the service, with the state, ends the login.
  oauth.ExternalAuthorizationUrlReceived(Callback(u"error=access_denied&state="_s + state));
  ASSERT_EQ(1, spy.count());
  EXPECT_FALSE(spy.at(0).at(0).toBool());
  EXPECT_FALSE(oauth.login_pending());
  EXPECT_TRUE(network->urls.isEmpty());

}

TEST(OAuthenticatorTest, NewLoginReplacesThePendingLogin) {

  ClearSettings();

  UrlRecorder recorder;
  UrlHandlerGuard url_handler_guard(&recorder);

  SharedPtr<RecordingNetworkAccessManager> network = make_shared<RecordingNetworkAccessManager>();
  OAuthenticator oauth(network);
  Configure(&oauth);

  const QString old_state = StartLogin(&oauth, &recorder);
  const QString new_state = StartLogin(&oauth, &recorder);
  ASSERT_FALSE(old_state.isEmpty());
  ASSERT_FALSE(new_state.isEmpty());
  EXPECT_NE(old_state, new_state);

  oauth.ExternalAuthorizationUrlReceived(Callback(u"code=old&state="_s + old_state));
  QCoreApplication::processEvents();
  EXPECT_TRUE(network->urls.isEmpty());

  oauth.ExternalAuthorizationUrlReceived(Callback(u"code=new&state="_s + new_state));
  ASSERT_EQ(1, network->urls.count());
  EXPECT_EQ(u"new"_s, network->bodies.at(0).queryItemValue(u"code"_s));

}

// F17-1: an invalid_grant answer to a code exchange deleted the stored session, although the failed request was not a refresh.
TEST(OAuthenticatorTest, FailedCodeExchangeKeepsTheSession) {

  StoreSession();

  UrlRecorder recorder;
  UrlHandlerGuard url_handler_guard(&recorder);

  SharedPtr<RecordingNetworkAccessManager> network = make_shared<RecordingNetworkAccessManager>();
  OAuthenticator oauth(network);
  Configure(&oauth);
  oauth.LoadSession();
  ASSERT_TRUE(oauth.authenticated());

  const QString state = StartLogin(&oauth, &recorder);
  ASSERT_FALSE(state.isEmpty());

  QSignalSpy spy(&oauth, &OAuthenticator::AuthenticationFinished);

  oauth.ExternalAuthorizationUrlReceived(Callback(u"code=bad&state="_s + state));
  ASSERT_EQ(1, network->urls.count());
  network->FinishReply(0, 400, R"({"error": "invalid_grant", "error_description": "Invalid authorization code"})");

  ASSERT_EQ(1, spy.count());
  EXPECT_FALSE(spy.at(0).at(0).toBool());
  // The session was not cleared.
  EXPECT_FALSE(spy.at(0).at(2).toBool());
  EXPECT_TRUE(oauth.authenticated());
  EXPECT_EQ(u"user_access_token"_s, oauth.access_token());
  EXPECT_EQ(u"user_access_token"_s, StoredAccessToken());

  ClearSettings();

}

// A refresh rejected with invalid_grant still clears the session, the refresh token can't be used again.
TEST(OAuthenticatorTest, RefreshRejectedWithInvalidGrantClearsTheSession) {

  StoreSession();

  SharedPtr<RecordingNetworkAccessManager> network = make_shared<RecordingNetworkAccessManager>();
  OAuthenticator oauth(network);
  Configure(&oauth);
  oauth.LoadSession();
  ASSERT_TRUE(oauth.authenticated());

  QSignalSpy spy(&oauth, &OAuthenticator::AuthenticationFinished);

  ASSERT_TRUE(oauth.RefreshAccessToken());
  ASSERT_EQ(1, network->urls.count());
  EXPECT_EQ(u"refresh_token"_s, network->bodies.at(0).queryItemValue(u"grant_type"_s));
  network->FinishReply(0, 400, R"({"error": "invalid_grant"})");

  ASSERT_EQ(1, spy.count());
  EXPECT_FALSE(spy.at(0).at(0).toBool());
  EXPECT_TRUE(spy.at(0).at(2).toBool());
  EXPECT_FALSE(oauth.authenticated());
  EXPECT_TRUE(StoredAccessToken().isEmpty());

  ClearSettings();

}
