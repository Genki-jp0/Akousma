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
#include <QHostAddress>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QTest>

#include "core/localredirectserver.h"

using namespace Qt::Literals::StringLiterals;

namespace {

// Sends a request to the server and returns the reply, the server closes the connection after replying.
QByteArray SendRequest(const int port, const QByteArray &request) {

  QTcpSocket socket;
  socket.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(port));
  if (!socket.waitForConnected(5000)) return QByteArray();
  socket.write(request);
  socket.flush();

  QByteArray reply;
  const bool disconnected = QTest::qWaitFor([&socket, &reply]() {
    reply += socket.readAll();
    return socket.state() == QAbstractSocket::UnconnectedState;
  }, 5000);
  Q_UNUSED(disconnected)
  reply += socket.readAll();

  return reply;

}

QByteArray Get(const QString &target) {
  return "GET " + target.toUtf8() + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
}

bool IsNotFound(const QByteArray &reply) {
  return reply.startsWith("HTTP/1.0 404");
}

bool IsOk(const QByteArray &reply) {
  return reply.startsWith("HTTP/1.0 200");
}

}  // namespace

TEST(LocalRedirectServerTest, RandomPath) {

  LocalRedirectServer server;
  server.set_use_random_path(true);
  ASSERT_TRUE(server.Listen());

  EXPECT_EQ(33, server.path().length());
  EXPECT_EQ(server.path(), server.url().path());
  EXPECT_EQ(server.port(), server.url().port());

  LocalRedirectServer other_server;
  other_server.set_use_random_path(true);
  ASSERT_TRUE(other_server.Listen());
  EXPECT_NE(server.path(), other_server.path());

}

TEST(LocalRedirectServerTest, WithoutRandomPathTheUrlHasTheRootPath) {

  LocalRedirectServer server;
  server.set_expected_state(u"state"_s);
  ASSERT_TRUE(server.Listen());

  EXPECT_EQ(u"/"_s, server.path());
  EXPECT_EQ(u"/"_s, server.url().path());

}

// F18-1: a token or code sent to the port by someone else than the web browser of the login.
TEST(LocalRedirectServerTest, RejectsRequestsWithoutTheRandomPath) {

  LocalRedirectServer server;
  server.set_use_random_path(true);
  ASSERT_TRUE(server.Listen());
  QSignalSpy spy(&server, &LocalRedirectServer::Finished);

  EXPECT_TRUE(IsNotFound(SendRequest(server.port(), Get(u"/?token=attacker"_s))));
  EXPECT_TRUE(IsNotFound(SendRequest(server.port(), Get(u"/?code_autorisation=attacker"_s))));
  EXPECT_TRUE(IsNotFound(SendRequest(server.port(), Get(u"/?error=x"_s))));
  EXPECT_TRUE(IsNotFound(SendRequest(server.port(), Get(u"/favicon.ico"_s))));
  EXPECT_TRUE(IsNotFound(SendRequest(server.port(), Get(server.path() + u"x?token=attacker"_s))));
  EXPECT_TRUE(IsNotFound(SendRequest(server.port(), Get(server.path() + u"/?token=attacker"_s))));
  EXPECT_TRUE(IsNotFound(SendRequest(server.port(), "POST " + server.path().toUtf8() + "?token=attacker HTTP/1.1\r\nContent-Length: 0\r\n\r\n")));
  EXPECT_TRUE(IsNotFound(SendRequest(server.port(), "GET http://localhost" + server.path().toUtf8() + "?token=attacker HTTP/1.1\r\n\r\n")));

  // None of them ended the login.
  EXPECT_EQ(0, spy.count());
  EXPECT_TRUE(server.isListening());

  // The redirect of the login is still accepted.
  EXPECT_TRUE(IsOk(SendRequest(server.port(), Get(server.path() + u"?token=user"_s))));
  ASSERT_TRUE(spy.count() == 1 || spy.wait(5000));
  EXPECT_TRUE(server.success());
  EXPECT_EQ(u"user"_s, QUrlQuery(server.request_url()).queryItemValue(u"token"_s));
  EXPECT_FALSE(server.isListening());

}

// F18-1, F18-3: an OAuth code or error without the state of the login.
TEST(LocalRedirectServerTest, RejectsRequestsWithoutTheState) {

  LocalRedirectServer server;
  server.set_expected_state(u"Ab3-._~xyz-1234"_s);
  ASSERT_TRUE(server.Listen());
  QSignalSpy spy(&server, &LocalRedirectServer::Finished);

  EXPECT_TRUE(IsNotFound(SendRequest(server.port(), Get(u"/?code=attacker"_s))));
  EXPECT_TRUE(IsNotFound(SendRequest(server.port(), Get(u"/?error=x"_s))));
  EXPECT_TRUE(IsNotFound(SendRequest(server.port(), Get(u"/?code=attacker&state="_s))));
  EXPECT_TRUE(IsNotFound(SendRequest(server.port(), Get(u"/?code=attacker&state=Ab3-._~xyz-1235"_s))));
  EXPECT_TRUE(IsNotFound(SendRequest(server.port(), Get(u"/?state=Ab3-._~xyz-1234"_s))));

  EXPECT_EQ(0, spy.count());
  EXPECT_TRUE(server.isListening());

  EXPECT_TRUE(IsOk(SendRequest(server.port(), Get(u"/?code=user&state=Ab3-._~xyz-1234"_s))));
  ASSERT_TRUE(spy.count() == 1 || spy.wait(5000));
  EXPECT_TRUE(server.success());
  EXPECT_EQ(u"user"_s, QUrlQuery(server.request_url()).queryItemValue(u"code"_s));

}

TEST(LocalRedirectServerTest, AcceptsAnErrorWithTheState) {

  LocalRedirectServer server;
  server.set_expected_state(u"state"_s);
  ASSERT_TRUE(server.Listen());
  QSignalSpy spy(&server, &LocalRedirectServer::Finished);

  EXPECT_TRUE(IsOk(SendRequest(server.port(), Get(u"/?error=access_denied&state=state"_s))));
  ASSERT_TRUE(spy.count() == 1 || spy.wait(5000));
  EXPECT_TRUE(server.success());
  EXPECT_EQ(u"access_denied"_s, QUrlQuery(server.request_url()).queryItemValue(u"error"_s));

}

TEST(LocalRedirectServerTest, WithoutSecretNothingIsAccepted) {

  LocalRedirectServer server;
  ASSERT_TRUE(server.Listen());
  QSignalSpy spy(&server, &LocalRedirectServer::Finished);

  EXPECT_TRUE(IsNotFound(SendRequest(server.port(), Get(u"/?token=x"_s))));
  EXPECT_TRUE(IsNotFound(SendRequest(server.port(), Get(u"/?code=x&state="_s))));

  EXPECT_EQ(0, spy.count());

}

// F18-6: an abandoned login doesn't leave the port open.
TEST(LocalRedirectServerTest, Timeout) {

  LocalRedirectServer server;
  server.set_use_random_path(true);
  server.set_timeout_secs(1);
  ASSERT_TRUE(server.Listen());
  QSignalSpy spy(&server, &LocalRedirectServer::Finished);

  ASSERT_TRUE(spy.wait(5000));
  EXPECT_FALSE(server.success());
  EXPECT_FALSE(server.error().isEmpty());
  EXPECT_FALSE(server.isListening());

}
