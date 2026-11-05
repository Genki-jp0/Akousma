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
#include <QString>
#include <QUrl>
#include <QDateTime>
#include <QTimer>
#include <QVersionNumber>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
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

}  // namespace

UpdateChecker::UpdateChecker(SharedPtr<NetworkAccessManager> network, const QString &repository, const QString &current_version, QWidget *parent, const QUrl &api_url)
    : QObject(parent),
      network_(network),
      parent_(parent),
      repository_(repository),
      current_version_(current_version),
      api_url_(api_url.isEmpty() ? ApiUrl(repository) : api_url),
      reply_(nullptr),
      manual_(false) {}

bool UpdateChecker::IsValidRepository(const QString &repository) {

  static const QRegularExpression repository_re(u"^[A-Za-z0-9][A-Za-z0-9-]{0,38}/[A-Za-z0-9._-]{1,100}$"_s);
  return repository_re.match(repository).hasMatch() && !repository.endsWith(u"/."_s) && !repository.endsWith(u"/.."_s);

}

QUrl UpdateChecker::ApiUrl(const QString &repository) {

  if (!IsValidRepository(repository)) return QUrl();
  return QUrl(u"https://api.github.com/repos/%1/releases/latest"_s.arg(repository));

}

QVersionNumber UpdateChecker::VersionFromTag(const QString &tag) {

  // Only numbers, so nothing like "1.0.1-rc1" or "1.0.1<b>" passes as 1.0.1.
  static const QRegularExpression tag_re(u"^[vV]?[0-9]{1,9}(\\.[0-9]{1,9}){0,3}$"_s);
  if (!tag_re.match(tag).hasMatch()) return QVersionNumber();

  return QVersionNumber::fromString(tag.startsWith(u'v') || tag.startsWith(u'V') ? tag.mid(1) : tag);

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

UpdateChecker::Result UpdateChecker::ParseReply(const int http_status, const QByteArray &rate_limit_remaining, const QString &network_error, const QByteArray &data, const QString &repository, const QString &current_version) {

  Result result;

  if (http_status == 0) {
    result.error = network_error;
    return result;
  }
  // No release yet.
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

  const QJsonValue tag_value = json_object["tag_name"_L1];
  QString html_url;
  if (!tag_value.isString() || !OptionalString(json_object["name"_L1], &result.name) || !OptionalString(json_object["body"_L1], &result.notes) || !OptionalString(json_object["html_url"_L1], &html_url)) {
    result.name.clear();
    result.notes.clear();
    result.error = tr("GitHub's answer is not a release.");
    return result;
  }
  const QVersionNumber latest_version = VersionFromTag(tag_value.toString());
  if (latest_version.isNull()) {
    result.name.clear();
    result.notes.clear();
    result.error = tr("The tag of the latest release is not a version number.");
    return result;
  }

  result.version = latest_version.toString();
  result.name.truncate(kReleaseNameMaxLength);
  result.url = ReleasePageUrl(repository, html_url);
  // Normalized, so 1.0 and 1.0.0 are the same version.
  result.status = latest_version.normalized() > QVersionNumber::fromString(current_version).normalized() ? Status::UpdateAvailable : Status::UpToDate;

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

  qLog(Debug) << "Checking for updates" << api_url_;

  QNetworkRequest network_request(api_url_);
  network_request.setRawHeader("Accept", "application/vnd.github+json");
  network_request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
  // Each redirect is checked in the redirected() handler below.
  network_request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
  network_request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
  network_request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
  network_request.setTransferTimeout(kTransferTimeoutMsec);

  reply_error_.clear();
  reply_ = network_->get(network_request);
  QNetworkReply *reply = reply_;

  // Only to HTTPS on the same host, for example GitHub's redirect for a renamed repository.
  QObject::connect(reply, &QNetworkReply::redirected, this, [this, reply](const QUrl &url) {
    if (url.scheme() == "https"_L1 && url.host() == api_url_.host() && url.port(443) == api_url_.port(443) && url.userInfo().isEmpty()) {
      qLog(Debug) << "Update check redirected to" << url;
      Q_EMIT reply->redirectAllowed();
    }
    else {
      reply_error_ = tr("Refused a redirect to %1").arg(url.toString());
      reply->abort();
    }
  });
  QObject::connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](const qint64 bytes_received, const qint64 bytes_total) {
    if (reply->isRunning() && (bytes_received > kMaxReplySize || bytes_total > kMaxReplySize)) {
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
  const QString network_error = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
  const Result result = ParseReply(http_status, reply->rawHeader("X-RateLimit-Remaining"), network_error, reply->read(kMaxReplySize + 1), repository_, current_version_);

  // Only a check that reached GitHub counts for the once a day check, so one without network is tried again at the next start.
  if (http_status != 0) {
    Settings s;
    s.beginGroup(kSettingsGroup);
    s.setValue(kLastCheck, QDateTime::currentDateTime());
    s.endGroup();
  }

  Finish(result);

}

void UpdateChecker::Finish(const Result &result) {

  ShowResult(result);
  manual_ = false;
  Q_EMIT CheckFinished(result);

}

void UpdateChecker::ShowResult(const Result &result) {

  QString message;
  QMessageBox::Icon icon = QMessageBox::Information;

  switch (result.status) {
    case Status::UpdateAvailable:
      qLog(Info) << "Akousma" << result.version << "is available" << result.url;
      break;
    case Status::UpToDate:
      qLog(Debug) << "Akousma is up to date, the latest release is" << result.version;
      message = tr("You are running the latest version of Akousma (%1).").arg(current_version_);
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

  // A version skipped in the dialog is still shown by a manual check.
  const QString skip_version = QString::fromLatin1(kSkipVersion).arg(result.version);
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
  message = u"<p><b>"_s + tr("Akousma %1 is available, you are running %2.").arg(result.version, current_version_).toHtmlEscaped() + u"</b></p>"_s;
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
  update_message->ShowMessage(tr("Update available"), message, IconLoader::Load(u"akousma"_s));

}
