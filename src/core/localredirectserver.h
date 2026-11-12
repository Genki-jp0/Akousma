/*
 * Strawberry Music Player
 * Copyright 2012, 2014, John Maguire <john.maguire@gmail.com>
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

#ifndef LOCALREDIRECTSERVER_H
#define LOCALREDIRECTSERVER_H

#include <QtGlobal>
#include <QTcpServer>
#include <QHash>
#include <QByteArray>
#include <QString>
#include <QUrl>

class QTcpSocket;
class QTimer;

class LocalRedirectServer : public QTcpServer {
  Q_OBJECT

 public:
  explicit LocalRedirectServer(QObject *parent = nullptr);
  ~LocalRedirectServer() override;

  const QUrl &url() const { return url_; }
  const QUrl &request_url() const { return request_url_; }
  bool success() const { return success_; }
  const QString &error() const { return error_; }

  int port() const { return port_; }
  void set_port(const int port) { port_ = port; }

  // The path of the redirect URL, a random path when set_use_random_path() was enabled, otherwise "/".
  const QString &path() const { return path_; }

  // Puts a random path in the redirect URL when listening, and only accepts the redirect on that path, for logins without an OAuth state to tie the redirect to the login, like Last.fm and Qobuz.
  // Can't be used when the path of the redirect URL is chosen by someone else, like a registered redirect URL or the redirect page OAuthenticator uses with PortType::PassInState.
  void set_use_random_path(const bool use_random_path) { use_random_path_ = use_random_path; }

  // Only accepts the redirect when its "state" query item is this, for OAuth logins.
  // Requests with another state are answered with 404 and the server keeps waiting, so they can't end the login.
  void set_expected_state(const QString &expected_state) { expected_state_ = expected_state; }

  // Stops waiting for the redirect after this many seconds, and finishes with an error.
  void set_timeout_secs(const int timeout_secs) { timeout_secs_ = timeout_secs; }

  bool Listen();

 Q_SIGNALS:
  void Finished();

 public Q_SLOTS:
  void NewConnection();
  void incomingConnection(qintptr socket_descriptor) override;

 private Q_SLOTS:
  void ReadyRead();
  void Disconnected();
  void Timeout();

 private:
  void WriteTemplate(QTcpSocket *socket) const;
  static void WriteNotFound(QTcpSocket *socket);
  QUrl ParseUrlFromRequest(const QByteArray &request, QByteArray *method) const;
  static bool IsRedirect(const QUrl &url);
  void CloseConnections();

 private:
  int port_;
  bool use_random_path_;
  int timeout_secs_;
  QTimer *timer_timeout_;
  QString path_;
  QString expected_state_;
  QUrl url_;
  QUrl request_url_;
  // The request received so far on each connection, a request can arrive in several parts, and other connections than the redirect must not end it.
  QHash<QTcpSocket*, QByteArray> requests_;
  bool success_;
  QString error_;
};

#endif  // LOCALREDIRECTSERVER_H
