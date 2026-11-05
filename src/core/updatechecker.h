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
#include <QList>
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QDateTime>
#include <QVersionNumber>

#include "includes/shared_ptr.h"

class QWidget;
class QNetworkReply;
class QJsonObject;
class NetworkAccessManager;

// Checks the releases of a GitHub repository (owner/name) for a newer version of Akousma: Help > Check for updates, and a few
// seconds after startup at most once a day when enabled in Settings > Behavior.
// Without "Include pre-releases" it asks for the latest release, whose tag is a version, with or without a leading "v" ("v1.0.1"
// or "1.0.1", up to four numbers, nothing after them). With it, it takes the newest of the last kMaxReleases releases by publication
// date, a pre-release tag can also have a git commit ("1.0.1+gitabc1234", "v1.0.1-gabc1234", "v1.0.1-abc1234") or be only a git
// commit ("abc1234", "gabc1234"). A release without a commit, or with another version than this build, is compared by version.
// A commit of this build's version, or without a version, is compared with this build's commit by GitHub's compare API: newer when
// it is ahead of it or diverged from it. A build without a commit (a release build) takes it as newer when it was published after
// the release of this build's version. Drafts are never updates.
// A newer version is shown with its release notes in a MessageDialog, where the automatic check can be told to skip that version.
// Only a manual check tells when there is no newer version or the check failed, an automatic check just logs it.
//
// The checker only informs and links to the release page, it never downloads or runs anything. It asks GitHub's API over HTTPS
// with the system's certificate checks, at most two requests per check, follows redirects only to HTTPS on the same host, reads
// at most kMaxReplySize bytes, puts only checked versions and commits in URLs, shows the release name and notes as escaped plain
// text and links only to the repository's releases on github.com.
class UpdateChecker : public QObject {
  Q_OBJECT

 public:
  // api_url is for tests only, empty is GitHub's API for repository.
  explicit UpdateChecker(SharedPtr<NetworkAccessManager> network, const QString &repository, const QString &current_version, const QString &current_commit, QWidget *parent, const QUrl &api_url = QUrl());

  static constexpr qint64 kMaxReplySize = 1024 * 1024;
  static constexpr int kMaxReleases = 20;
  // Where the status of a comparison must be, GitHub's comparison also has the changed files.
  static constexpr qint64 kCompareStatusMaxOffset = 256 * 1024;

  enum class Status {
    UpdateAvailable,
    UpToDate,
    NoRelease,
    RateLimited,
    Error,
    NeedsCompare
  };

  struct Tag {
    QVersionNumber version;
    QString commit;
    bool IsValid() const { return !version.isNull() || !commit.isEmpty(); }
  };

  struct Result {
    Status status = Status::Error;
    QString tag_name;
    Tag tag;
    bool prerelease = false;
    QDateTime published;
    QString version;  // As shown: "1.0.1", "1.0.1 (git abc1234)" or "git abc1234".
    QString name;
    QString notes;
    QUrl url;
    QString error;
    QString compare_base;  // This build's commit and the release's commit, with Status::NeedsCompare.
    QString compare_head;
  };

  // Whether repository is a GitHub owner/name.
  static bool IsValidRepository(const QString &repository);
  // https://api.github.com/repos/<repository>
  static QUrl ApiUrl(const QString &repository);
  // Whether commit is an abbreviated or full git commit hash.
  static bool IsValidCommit(const QString &commit);
  // The version and commit of a release tag, invalid when the tag is neither. Only pre-releases can have a commit.
  static Tag ParseTag(const QString &tag, const bool prerelease);
  // The version of a release tag, null when the tag is not a version.
  static QVersionNumber VersionFromTag(const QString &tag);
  static QString DisplayVersion(const Tag &tag);
  // html_url when it is a release page of repository on github.com, otherwise the repository's releases page.
  static QUrl ReleasePageUrl(const QString &repository, const QString &html_url);
  // The beginning of the release notes, escaped for a rich text label.
  static QString ReleaseNotesHtml(const QString &notes);
  // What GitHub's answer to a latest release request means for this build.
  static Result ParseReply(const int http_status, const QByteArray &rate_limit_remaining, const QString &network_error, const QByteArray &data, const QString &repository, const QString &current_version, const QString &current_commit = QString());
  // The same for a releases request, which includes pre-releases. Status::NeedsCompare when GitHub must compare the commits.
  static Result ParseReleasesReply(const int http_status, const QByteArray &rate_limit_remaining, const QString &network_error, const QByteArray &data, const QString &repository, const QString &current_version, const QString &current_commit);
  // Whether release (Status::NeedsCompare) is newer, from GitHub's comparison of this build's commit with the release's commit.
  static Result ParseCompareReply(const int http_status, const QByteArray &rate_limit_remaining, const QString &network_error, const QByteArray &data, const Result &release);
  // The status of GitHub's comparison, found in its beginning: "ahead", "behind", "identical", "diverged" or empty.
  static QString CompareStatus(const QByteArray &data);

  void CheckOnStartup();

 Q_SIGNALS:
  void CheckFinished(const UpdateChecker::Result &result);

 public Q_SLOTS:
  void CheckNow();

 private:
  enum class Request {
    Latest,
    Releases,
    Compare
  };

  static Result HttpResult(const int http_status, const QByteArray &rate_limit_remaining, const QString &network_error, bool *handled);
  static bool ParseRelease(const QJsonObject &json_object, const QString &repository, Result *result);
  static Result Decide(const Result &release, const QList<Result> &releases, const QString &current_version, const QString &current_commit);

  void Check(const bool manual);
  void Get(const Request request, const QString &path, const QString &query = QString());
  void ReplyFinished(QNetworkReply *reply);
  void Finish(const Result &result);
  void ShowResult(const Result &result);
  QString CurrentDisplayVersion() const;

 private:
  SharedPtr<NetworkAccessManager> network_;
  QWidget *parent_;
  QString repository_;
  QString current_version_;
  QString current_commit_;
  QUrl api_url_;
  QNetworkReply *reply_;
  Request request_;
  QString reply_error_;
  QByteArray compare_head_;
  Result pending_;
  bool manual_;
};

#endif  // UPDATECHECKER_H
