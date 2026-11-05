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

#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QVersionNumber>

#include "includes/shared_ptr.h"

class QWidget;
class QNetworkReply;
class NetworkAccessManager;

// Checks the latest release of a GitHub repository (owner/name) for a newer version of Akousma: Help > Check for updates, and a few
// seconds after startup at most once a day when enabled in Settings > Behavior. The release tag is the version, with or without a
// leading "v" ("v1.0.1" or "1.0.1", up to four numbers, nothing after them); drafts and pre-releases are not updates.
// A newer version is shown with its release notes in a MessageDialog, where the automatic check can be told to skip that version.
// Only a manual check tells when there is no newer version or the check failed, an automatic check just logs it.
//
// The checker only informs and links to the release page, it never downloads or runs anything. It asks GitHub's API over HTTPS
// with the system's certificate checks, follows redirects only to HTTPS on the same host, reads at most kMaxReplySize bytes,
// shows the release name and notes as escaped plain text and links only to the repository's releases on github.com.
class UpdateChecker : public QObject {
  Q_OBJECT

 public:
  // api_url is for tests only, empty is GitHub's API for repository.
  explicit UpdateChecker(SharedPtr<NetworkAccessManager> network, const QString &repository, const QString &current_version, QWidget *parent, const QUrl &api_url = QUrl());

  static constexpr qint64 kMaxReplySize = 1024 * 1024;

  enum class Status {
    UpdateAvailable,
    UpToDate,
    NoRelease,
    RateLimited,
    Error
  };

  struct Result {
    Status status = Status::Error;
    QString version;
    QString name;
    QString notes;
    QUrl url;
    QString error;
  };

  // Whether repository is a GitHub owner/name.
  static bool IsValidRepository(const QString &repository);
  static QUrl ApiUrl(const QString &repository);
  // The version of a release tag, null when the tag is not a version.
  static QVersionNumber VersionFromTag(const QString &tag);
  // html_url when it is a release page of repository on github.com, otherwise the repository's releases page.
  static QUrl ReleasePageUrl(const QString &repository, const QString &html_url);
  // The beginning of the release notes, escaped for a rich text label.
  static QString ReleaseNotesHtml(const QString &notes);
  // What GitHub's answer to a latest release request means for current_version.
  static Result ParseReply(const int http_status, const QByteArray &rate_limit_remaining, const QString &network_error, const QByteArray &data, const QString &repository, const QString &current_version);

  void CheckOnStartup();

 Q_SIGNALS:
  void CheckFinished(const UpdateChecker::Result &result);

 public Q_SLOTS:
  void CheckNow();

 private:
  void Check(const bool manual);
  void ReplyFinished(QNetworkReply *reply);
  void Finish(const Result &result);
  void ShowResult(const Result &result);

 private:
  SharedPtr<NetworkAccessManager> network_;
  QWidget *parent_;
  QString repository_;
  QString current_version_;
  QUrl api_url_;
  QNetworkReply *reply_;
  QString reply_error_;
  bool manual_;
};

#endif  // UPDATECHECKER_H
