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

#include <QWidget>
#include <QList>
#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>
#include <QTimer>
#include <QVersionNumber>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QMessageBox>

#include "includes/shared_ptr.h"
#include "constants/behavioursettings.h"
#include "core/logging.h"
#include "core/settings.h"
#include "core/networkaccessmanager.h"
#include "core/iconloader.h"
#include "dialogs/messagedialog.h"
#include "updatechecker.h"

using namespace Qt::Literals::StringLiterals;

namespace {

constexpr char kSettingsGroup[] = "UpdateChecker";
constexpr char kLastCheck[] = "last_check";
constexpr char kSkipVersion[] = "skip_%1";
constexpr int kStartupDelayMsec = 5000;
constexpr int kTransferTimeoutMsec = 30000;
constexpr qint64 kCheckIntervalSec = 24 * 60 * 60;
constexpr qsizetype kReleaseNameMaxLength = 200;
constexpr qsizetype kReleaseNotesMaxLength = 1000;

// A JSON value which must be a string, or missing or null for an empty string.
bool OptionalString(const QJsonValue &value, QString *string) {

  if (value.isUndefined() || value.isNull()) return true;
  if (!value.isString()) return false;
  *string = value.toString();
  return true;

}

bool SameCommit(const QString &commit1, const QString &commit2) {
  return commit1.startsWith(commit2, Qt::CaseInsensitive) || commit2.startsWith(commit1, Qt::CaseInsensitive);
}

}  // namespace

UpdateChecker::UpdateChecker(SharedPtr<NetworkAccessManager> network, const QString &repository, const QString &current_version, const QString &current_commit, QWidget *parent, const QUrl &api_url)
    : QObject(parent),
      network_(network),
      parent_(parent),
      repository_(repository),
      current_version_(current_version),
      current_commit_(IsValidCommit(current_commit) ? current_commit.toLower() : QString()),
      api_url_(api_url.isEmpty() ? ApiUrl(repository) : api_url),
      reply_(nullptr),
      request_(Request::Latest),
      manual_(false) {}

bool UpdateChecker::IsValidRepository(const QString &repository) {

  static const QRegularExpression repository_re(u"^[A-Za-z0-9][A-Za-z0-9-]{0,38}/[A-Za-z0-9._-]{1,100}$"_s);
  return repository_re.match(repository).hasMatch() && !repository.endsWith(u"/."_s) && !repository.endsWith(u"/.."_s);

}

QUrl UpdateChecker::ApiUrl(const QString &repository) {

  if (!IsValidRepository(repository)) return QUrl();
  return QUrl(u"https://api.github.com/repos/%1"_s.arg(repository));

}

bool UpdateChecker::IsValidCommit(const QString &commit) {

  static const QRegularExpression commit_re(u"^[0-9a-fA-F]{7,40}$"_s);
  return commit_re.match(commit).hasMatch();

}

UpdateChecker::Tag UpdateChecker::ParseTag(const QString &tag, const bool prerelease) {

  // Only numbers, so nothing like "1.0.1-rc1" or "1.0.1<b>" passes as 1.0.1.
  static const QRegularExpression version_re(u"^[vV]?([0-9]{1,9}(?:\\.[0-9]{1,9}){0,3})$"_s);
  // Pre-releases: a version with a commit as in Akousma's package version (1.0.1+gitabc1234) or git describe (v1.0.1-gabc1234), or only a commit.
  static const QRegularExpression version_commit_re(u"^[vV]?([0-9]{1,9}(?:\\.[0-9]{1,9}){0,3})(?:\\+git|\\+g|-g|-)([0-9a-fA-F]{7,40})$"_s);
  static const QRegularExpression commit_re(u"^g?([0-9a-fA-F]{7,40})$"_s);

  Tag result;

  if (prerelease) {
    QRegularExpressionMatch match = commit_re.match(tag);
    if (match.hasMatch()) {
      result.commit = match.captured(1).toLower();
      return result;
    }
    match = version_commit_re.match(tag);
    if (match.hasMatch()) {
      result.version = QVersionNumber::fromString(match.captured(1));
      result.commit = match.captured(2).toLower();
      return result;
    }
  }

  const QRegularExpressionMatch match = version_re.match(tag);
  if (match.hasMatch()) {
    result.version = QVersionNumber::fromString(match.captured(1));
  }

  return result;

}

QVersionNumber UpdateChecker::VersionFromTag(const QString &tag) {
  return ParseTag(tag, false).version;
}

QString UpdateChecker::DisplayVersion(const Tag &tag) {

  if (tag.commit.isEmpty()) return tag.version.toString();
  if (tag.version.isNull()) return u"git %1"_s.arg(tag.commit.left(7));
  return u"%1 (git %2)"_s.arg(tag.version.toString(), tag.commit.left(7));

}

QUrl UpdateChecker::ReleasePageUrl(const QString &repository, const QString &html_url) {

  const QUrl releases_url(u"https://github.com/%1/releases"_s.arg(repository));

  const QUrl url = QUrl(html_url, QUrl::StrictMode).adjusted(QUrl::NormalizePathSegments | QUrl::RemoveQuery | QUrl::RemoveFragment);
  if (url.isValid() && url.scheme() == "https"_L1 && url.host() == "github.com"_L1 && url.port(443) == 443 && url.userInfo().isEmpty() && url.path().startsWith(u"/%1/releases/"_s.arg(repository), Qt::CaseInsensitive)) {
    return url;
  }

  return releases_url;

}

QString UpdateChecker::ReleaseNotesHtml(const QString &notes) {

  // Long release notes are cut at the end of a line, the release page has all of them.
  QString text = notes.trimmed();
  if (text.length() > kReleaseNotesMaxLength) {
    const qsizetype end_of_line = text.lastIndexOf(u'\n', kReleaseNotesMaxLength);
    text.truncate(end_of_line > 0 ? end_of_line : kReleaseNotesMaxLength);
    text.append(u"\n..."_s);
  }
  if (text.isEmpty()) return QString();

  // Plain text, so the notes can't add links, images or other HTML to the message.
  return u"<p>"_s + text.toHtmlEscaped().replace(u"\r\n"_s, u"\n"_s).replace(u'\n', u"<br />"_s) + u"</p>"_s;

}

UpdateChecker::Result UpdateChecker::HttpResult(const int http_status, const QByteArray &rate_limit_remaining, const QString &network_error, bool *handled) {

  Result result;
  *handled = true;

  if (http_status == 0) {
    result.error = network_error;
    return result;
  }
  if (http_status == 404) {
    result.status = Status::NoRelease;
    return result;
  }
  if (http_status == 429 || (http_status == 403 && rate_limit_remaining == "0")) {
    result.status = Status::RateLimited;
    return result;
  }
  if (http_status != 200) {
    result.error = network_error.isEmpty() ? tr("Received HTTP code %1").arg(http_status) : network_error;
    return result;
  }

  *handled = false;
  return result;

}

bool UpdateChecker::ParseRelease(const QJsonObject &json_object, const QString &repository, Result *result) {

  const QJsonValue tag_value = json_object["tag_name"_L1];
  QString html_url;
  QString published;
  if (!tag_value.isString() || !OptionalString(json_object["name"_L1], &result->name) || !OptionalString(json_object["body"_L1], &result->notes) || !OptionalString(json_object["html_url"_L1], &html_url) || !OptionalString(json_object["published_at"_L1], &published)) {
    return false;
  }

  result->prerelease = json_object["prerelease"_L1].toBool();
  result->tag = ParseTag(tag_value.toString(), result->prerelease);
  if (!result->tag.IsValid()) return false;

  result->tag_name = tag_value.toString();
  result->version = DisplayVersion(result->tag);
  result->published = QDateTime::fromString(published, Qt::ISODate);
  result->name.truncate(kReleaseNameMaxLength);
  result->url = ReleasePageUrl(repository, html_url);

  return true;

}

UpdateChecker::Result UpdateChecker::Decide(const Result &release, const QList<Result> &releases, const QString &current_version, const QString &current_commit) {

  Result result = release;
  // Normalized, so 1.0 and 1.0.0 are the same version.
  const QVersionNumber version = QVersionNumber::fromString(current_version).normalized();
  const QString commit = IsValidCommit(current_commit) ? current_commit.toLower() : QString();

  // A release, or a pre-release of another version.
  if (release.tag.commit.isEmpty() || (!release.tag.version.isNull() && release.tag.version.normalized() != version)) {
    result.status = release.tag.version.normalized() > version ? Status::UpdateAvailable : Status::UpToDate;
    return result;
  }

  // A commit, compared with the commit of this build.
  if (!commit.isEmpty()) {
    if (SameCommit(release.tag.commit, commit)) {
      result.status = Status::UpToDate;
    }
    else {
      result.status = Status::NeedsCompare;
      result.compare_base = commit;
      result.compare_head = release.tag.commit;
    }
    return result;
  }

  // A build without a commit is a release, a commit published after this release is newer.
  for (const Result &other_release : releases) {
    if (other_release.tag.commit.isEmpty() && other_release.tag.version.normalized() == version && other_release.published.isValid()) {
      result.status = release.published > other_release.published ? Status::UpdateAvailable : Status::UpToDate;
      return result;
    }
  }

  result.status = Status::Error;
  result.error = tr("Can't tell whether pre-release %1 is newer than this build.").arg(release.version);
  return result;

}

UpdateChecker::Result UpdateChecker::ParseReply(const int http_status, const QByteArray &rate_limit_remaining, const QString &network_error, const QByteArray &data, const QString &repository, const QString &current_version, const QString &current_commit) {

  bool handled = false;
  Result result = HttpResult(http_status, rate_limit_remaining, network_error, &handled);
  if (handled) return result;

  if (data.size() > kMaxReplySize) {
    result.error = tr("GitHub's answer is too large.");
    return result;
  }

  QJsonParseError json_error;
  const QJsonDocument json_document = QJsonDocument::fromJson(data, &json_error);
  if (json_error.error != QJsonParseError::NoError || !json_document.isObject()) {
    result.error = tr("GitHub's answer is not a release.");
    return result;
  }
  const QJsonObject json_object = json_document.object();

  // GitHub's latest release is never one of them.
  if (json_object["draft"_L1].toBool() || json_object["prerelease"_L1].toBool()) {
    result.status = Status::NoRelease;
    return result;
  }

  Result release;
  if (!json_object["tag_name"_L1].isString()) {
    result.error = tr("GitHub's answer is not a release.");
    return result;
  }
  if (!ParseRelease(json_object, repository, &release)) {
    result.error = tr("The tag of the latest release is not a version number.");
    return result;
  }

  return Decide(release, QList<Result>() << release, current_version, current_commit);

}

UpdateChecker::Result UpdateChecker::ParseReleasesReply(const int http_status, const QByteArray &rate_limit_remaining, const QString &network_error, const QByteArray &data, const QString &repository, const QString &current_version, const QString &current_commit) {

  bool handled = false;
  Result result = HttpResult(http_status, rate_limit_remaining, network_error, &handled);
  if (handled) return result;

  if (data.size() > kMaxReplySize) {
    result.error = tr("GitHub's answer is too large.");
    return result;
  }

  QJsonParseError json_error;
  const QJsonDocument json_document = QJsonDocument::fromJson(data, &json_error);
  if (json_error.error != QJsonParseError::NoError || !json_document.isArray()) {
    result.error = tr("GitHub's answer is not a list of releases.");
    return result;
  }

  // Releases which are not Akousma's, for example without a version or commit in the tag, are left out.
  QList<Result> releases;
  const QJsonArray json_array = json_document.array();
  for (qsizetype i = 0; i < json_array.size() && i < kMaxReleases; ++i) {
    if (!json_array[i].isObject()) continue;
    const QJsonObject json_object = json_array[i].toObject();
    if (json_object["draft"_L1].toBool()) continue;
    Result release;
    if (!ParseRelease(json_object, repository, &release) || !release.published.isValid()) continue;
    releases << release;
  }

  if (releases.isEmpty()) {
    result.status = Status::NoRelease;
    return result;
  }

  qsizetype newest = 0;
  for (qsizetype i = 1; i < releases.size(); ++i) {
    if (releases[i].published > releases[newest].published) newest = i;
  }

  return Decide(releases[newest], releases, current_version, current_commit);

}

QString UpdateChecker::CompareStatus(const QByteArray &data) {

  // Comparisons start with the comparison's own keys, the status is before the commits and files, which can be large.
  // Strings in JSON escape quotes, so a quote right after "status" is a key, and only the comparison's own status has these values.
  static const QRegularExpression status_re(u"\"status\"\\s*:\\s*\"(ahead|behind|identical|diverged)\""_s);
  const QByteArray head = data.left(kCompareStatusMaxOffset).trimmed();
  if (!head.startsWith('{')) return QString();

  const QRegularExpressionMatch match = status_re.match(QString::fromUtf8(head));
  return match.hasMatch() ? match.captured(1) : QString();

}

UpdateChecker::Result UpdateChecker::ParseCompareReply(const int http_status, const QByteArray &rate_limit_remaining, const QString &network_error, const QByteArray &data, const Result &release) {

  bool handled = false;
  Result result = HttpResult(http_status, rate_limit_remaining, network_error, &handled);
  if (handled) {
    // GitHub doesn't have one of the commits, for example a build of a commit which was never pushed.
    if (result.status == Status::NoRelease) {
      result.status = Status::Error;
      result.error = tr("GitHub does not have the commit of this build, %1, to compare it with pre-release %2.").arg(release.compare_base, release.version);
    }
    return result;
  }

  const QString compare_status = CompareStatus(data);
  if (compare_status.isEmpty()) {
    result.error = tr("GitHub's answer is not a comparison.");
    return result;
  }

  result = release;
  result.compare_base.clear();
  result.compare_head.clear();
  // A diverged pre-release has commits this build doesn't have, for example after a rebase, it was published as the newer one.
  result.status = compare_status == "ahead"_L1 || compare_status == "diverged"_L1 ? Status::UpdateAvailable : Status::UpToDate;

  return result;

}

void UpdateChecker::CheckOnStartup() {

  Settings s;
  s.beginGroup(BehaviourSettings::kSettingsGroup);
  const bool check_for_updates = s.value(BehaviourSettings::kCheckForUpdates, BehaviourSettings::kDefaultCheckForUpdates).toBool();
  s.endGroup();
  if (!check_for_updates) return;

  s.beginGroup(kSettingsGroup);
  const QDateTime last_check = s.value(kLastCheck).toDateTime();
  s.endGroup();
  const QDateTime now = QDateTime::currentDateTime();
  // A last check in the future is from a wrong clock.
  if (last_check.isValid() && last_check <= now && last_check.secsTo(now) < kCheckIntervalSec) return;

  QTimer::singleShot(kStartupDelayMsec, this, [this]() { Check(false); });

}

void UpdateChecker::CheckNow() {
  Check(true);
}

void UpdateChecker::Check(const bool manual) {

  // A manual check while the automatic one runs shows its result.
  manual_ = manual_ || manual;
  if (reply_) return;

  if (api_url_.isEmpty() || !api_url_.isValid() || api_url_.scheme() != "https"_L1) {
    Result result;
    result.error = api_url_.isEmpty() ? tr("\"%1\" is not a GitHub repository.").arg(repository_) : tr("The update check needs an HTTPS address, not \"%1\".").arg(api_url_.toString());
    Finish(result);
    return;
  }

  Settings s;
  s.beginGroup(BehaviourSettings::kSettingsGroup);
  const bool check_for_prereleases = s.value(BehaviourSettings::kCheckForPreReleases, BehaviourSettings::kDefaultCheckForPreReleases).toBool();
  s.endGroup();

  if (check_for_prereleases) {
    Get(Request::Releases, u"/releases"_s, u"per_page=%1"_s.arg(kMaxReleases));
  }
  else {
    Get(Request::Latest, u"/releases/latest"_s);
  }

}

void UpdateChecker::Get(const Request request, const QString &path, const QString &query) {

  QUrl url(api_url_);
  url.setPath(api_url_.path() + path);
  if (!query.isEmpty()) url.setQuery(query);
  qLog(Debug) << "Checking for updates" << url;

  QNetworkRequest network_request(url);
  network_request.setRawHeader("Accept", "application/vnd.github+json");
  network_request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
  // Each redirect is checked in the redirected() handler below.
  network_request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
  network_request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
  network_request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
  network_request.setTransferTimeout(kTransferTimeoutMsec);

  request_ = request;
  reply_error_.clear();
  compare_head_.clear();
  reply_ = network_->get(network_request);
  QNetworkReply *reply = reply_;

  // Only to HTTPS on the same host, for example GitHub's redirect for a renamed repository.
  QObject::connect(reply, &QNetworkReply::redirected, this, [this, reply](const QUrl &redirect_url) {
    if (redirect_url.scheme() == "https"_L1 && redirect_url.host() == api_url_.host() && redirect_url.port(443) == api_url_.port(443) && redirect_url.userInfo().isEmpty()) {
      qLog(Debug) << "Update check redirected to" << redirect_url;
      Q_EMIT reply->redirectAllowed();
    }
    else {
      reply_error_ = tr("Refused a redirect to %1").arg(redirect_url.toString());
      reply->abort();
    }
  });
  QObject::connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](const qint64 bytes_received, const qint64 bytes_total) {
    if (!reply->isRunning()) return;
    // A comparison is only needed up to its status, which must be in its beginning, the rest can be large.
    if (request_ == Request::Compare) {
      if (bytes_received >= kCompareStatusMaxOffset) {
        const QByteArray head = reply->peek(kCompareStatusMaxOffset);
        if (CompareStatus(head).isEmpty()) {
          reply_error_ = tr("GitHub's answer is not a comparison.");
        }
        else {
          compare_head_ = head;
        }
        reply->abort();
      }
      return;
    }
    if (bytes_received > kMaxReplySize || bytes_total > kMaxReplySize) {
      reply_error_ = tr("GitHub's answer is too large.");
      reply->abort();
    }
  });
  QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() { ReplyFinished(reply); });

}

void UpdateChecker::ReplyFinished(QNetworkReply *reply) {

  reply->deleteLater();
  if (reply != reply_) return;
  reply_ = nullptr;

  if (!reply_error_.isEmpty()) {
    Result result;
    result.error = reply_error_;
    Finish(result);
    return;
  }

  const int http_status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QByteArray rate_limit_remaining = reply->rawHeader("X-RateLimit-Remaining");
  const QString network_error = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();

  // Only a check that reached GitHub counts for the once a day check, so one without network is tried again at the next start.
  if (http_status != 0) {
    Settings s;
    s.beginGroup(kSettingsGroup);
    s.setValue(kLastCheck, QDateTime::currentDateTime());
    s.endGroup();
  }

  switch (request_) {
    case Request::Latest:
      Finish(ParseReply(http_status, rate_limit_remaining, network_error, reply->read(kMaxReplySize + 1), repository_, current_version_, current_commit_));
      break;
    case Request::Releases: {
      const Result result = ParseReleasesReply(http_status, rate_limit_remaining, network_error, reply->read(kMaxReplySize + 1), repository_, current_version_, current_commit_);
      if (result.status != Status::NeedsCompare) {
        Finish(result);
        break;
      }
      // Both are checked commits.
      pending_ = result;
      Get(Request::Compare, u"/compare/%1...%2"_s.arg(result.compare_base, result.compare_head), u"per_page=1"_s);
      break;
    }
    case Request::Compare:
      if (compare_head_.isEmpty()) {
        Finish(ParseCompareReply(http_status, rate_limit_remaining, network_error, reply->read(kMaxReplySize + 1), pending_));
      }
      else {
        Finish(ParseCompareReply(http_status, rate_limit_remaining, QString(), compare_head_, pending_));
      }
      break;
  }

}

void UpdateChecker::Finish(const Result &result) {

  ShowResult(result);
  manual_ = false;
  pending_ = Result();
  Q_EMIT CheckFinished(result);

}

QString UpdateChecker::CurrentDisplayVersion() const {

  Tag tag;
  tag.version = QVersionNumber::fromString(current_version_);
  tag.commit = current_commit_;
  return tag.version.isNull() ? current_version_ : DisplayVersion(tag);

}

void UpdateChecker::ShowResult(const Result &result) {

  QString message;
  QMessageBox::Icon icon = QMessageBox::Information;

  switch (result.status) {
    case Status::UpdateAvailable:
      qLog(Info) << "Akousma" << (result.prerelease ? "pre-release" : "") << result.version << "is available" << result.url;
      break;
    case Status::UpToDate:
      qLog(Debug) << "Akousma is up to date, the newest release is" << result.version;
      message = tr("You are running the latest version of Akousma (%1).").arg(CurrentDisplayVersion());
      break;
    case Status::NoRelease:
      qLog(Debug) << "No release of" << repository_ << "on GitHub yet";
      message = tr("No release of Akousma has been published yet.");
      break;
    case Status::RateLimited:
      qLog(Warning) << "Could not check for updates, GitHub's rate limit was reached";
      message = tr("GitHub does not answer more update checks from your network for now. Try again later.");
      icon = QMessageBox::Warning;
      break;
    case Status::Error:
    case Status::NeedsCompare:
      qLog(Warning) << "Could not check for updates:" << result.error;
      message = tr("Could not check for updates: %1").arg(result.error);
      icon = QMessageBox::Warning;
      break;
  }

  if (result.status != Status::UpdateAvailable) {
    if (manual_) {
      QMessageBox *messagebox = new QMessageBox(icon, tr("Check for updates"), message, QMessageBox::Close, parent_);
      messagebox->setTextFormat(Qt::PlainText);
      messagebox->setAttribute(Qt::WA_DeleteOnClose);
      messagebox->show();
    }
    return;
  }

  // A version skipped in the dialog is still shown by a manual check. The tag is checked by ParseTag(), so it's a safe key.
  const QString skip_version = QString::fromLatin1(kSkipVersion).arg(result.tag_name);
  if (!manual_) {
    Settings s;
    s.beginGroup(kSettingsGroup);
    const bool skip = s.value(skip_version, false).toBool();
    s.endGroup();
    if (skip) {
      qLog(Debug) << "Skipping Akousma" << result.version;
      return;
    }
  }

  // Everything from GitHub is escaped, the only link is the release page checked by ReleasePageUrl().
  const QString available = result.prerelease ? tr("Akousma pre-release %1 is available, you are running %2.") : tr("Akousma %1 is available, you are running %2.");
  message = u"<p><b>"_s + available.arg(result.version, CurrentDisplayVersion()).toHtmlEscaped() + u"</b></p>"_s;
  if (!result.name.isEmpty()) {
    message += u"<p>"_s + result.name.toHtmlEscaped() + u"</p>"_s;
  }
  message += ReleaseNotesHtml(result.notes);
  message += u"<p><a href=\"%1\">%2</a></p>"_s.arg(result.url.toString(QUrl::FullyEncoded).toHtmlEscaped(), tr("Download it from the release page").toHtmlEscaped());

  MessageDialog *update_message = new MessageDialog(parent_);
  update_message->set_settings_group(QLatin1String(kSettingsGroup));
  update_message->set_do_not_show_message_again(skip_version);
  update_message->set_do_not_show_message_again_text(tr("Skip this version"));
  update_message->setAttribute(Qt::WA_DeleteOnClose);
  update_message->ShowMessage(result.prerelease ? tr("Pre-release available") : tr("Update available"), message, IconLoader::Load(u"akousma"_s));

}
