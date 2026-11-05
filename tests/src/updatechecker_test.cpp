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
#include <QVersionNumber>

#include "core/updatechecker.h"

using namespace Qt::Literals::StringLiterals;

// clazy:excludeall=non-pod-global-static

namespace {

constexpr char kRepository[] = "Genki-jp0/Akousma";
constexpr char kRelease[] = R"({"tag_name": "v1.0.1", "name": "Akousma 1.0.1", "body": "Fixes", "draft": false, "prerelease": false, "html_url": "https://github.com/Genki-jp0/Akousma/releases/tag/v1.0.1"})";

UpdateChecker::Result Parse(const QByteArray &data, const QString &current_version = u"1.0.0"_s) {
  return UpdateChecker::ParseReply(200, "59", QString(), data, QLatin1String(kRepository), current_version);
}

TEST(UpdateCheckerTest, Repository) {

  EXPECT_TRUE(UpdateChecker::IsValidRepository(u"Genki-jp0/Akousma"_s));
  EXPECT_TRUE(UpdateChecker::IsValidRepository(u"owner/repo.name_2"_s));
  EXPECT_FALSE(UpdateChecker::IsValidRepository(QString()));
  EXPECT_FALSE(UpdateChecker::IsValidRepository(u"Akousma"_s));
  EXPECT_FALSE(UpdateChecker::IsValidRepository(u"owner/.."_s));
  EXPECT_FALSE(UpdateChecker::IsValidRepository(u"owner/repo/releases"_s));
  EXPECT_FALSE(UpdateChecker::IsValidRepository(u"owner/repo?x=1"_s));
  EXPECT_FALSE(UpdateChecker::IsValidRepository(u"evil.example/repo"_s));

  EXPECT_EQ(QUrl(u"https://api.github.com/repos/Genki-jp0/Akousma/releases/latest"_s), UpdateChecker::ApiUrl(u"Genki-jp0/Akousma"_s));
  EXPECT_TRUE(UpdateChecker::ApiUrl(u"owner/repo#x"_s).isEmpty());

}

TEST(UpdateCheckerTest, VersionFromTag) {

  EXPECT_EQ(QVersionNumber(1, 0, 1), UpdateChecker::VersionFromTag(u"v1.0.1"_s));
  EXPECT_EQ(QVersionNumber(1, 0, 1), UpdateChecker::VersionFromTag(u"V1.0.1"_s));
  EXPECT_EQ(QVersionNumber(1, 0, 1), UpdateChecker::VersionFromTag(u"1.0.1"_s));
  EXPECT_EQ(QVersionNumber(1, 2), UpdateChecker::VersionFromTag(u"v1.2"_s));
  EXPECT_EQ(QVersionNumber({1, 2, 3, 4}), UpdateChecker::VersionFromTag(u"1.2.3.4"_s));
  EXPECT_TRUE(UpdateChecker::VersionFromTag(u""_s).isNull());
  EXPECT_TRUE(UpdateChecker::VersionFromTag(u"v"_s).isNull());
  EXPECT_TRUE(UpdateChecker::VersionFromTag(u" v1.2 "_s).isNull());
  EXPECT_TRUE(UpdateChecker::VersionFromTag(u"v2.0.0-rc1"_s).isNull());
  EXPECT_TRUE(UpdateChecker::VersionFromTag(u"1.0.0<script>"_s).isNull());
  EXPECT_TRUE(UpdateChecker::VersionFromTag(u"v99999999999999999999"_s).isNull());
  EXPECT_TRUE(UpdateChecker::VersionFromTag(u"1.2.3.4.5"_s).isNull());
  EXPECT_TRUE(UpdateChecker::VersionFromTag(u"1..2"_s).isNull());
  EXPECT_TRUE(UpdateChecker::VersionFromTag(u"release-1.0.1"_s).isNull());
  EXPECT_TRUE(UpdateChecker::VersionFromTag(u"latest"_s).isNull());

}

TEST(UpdateCheckerTest, NewerRelease) {

  const UpdateChecker::Result result = Parse(kRelease);
  EXPECT_EQ(UpdateChecker::Status::UpdateAvailable, result.status);
  EXPECT_EQ(u"1.0.1"_s, result.version);
  EXPECT_EQ(u"Akousma 1.0.1"_s, result.name);
  EXPECT_EQ(u"Fixes"_s, result.notes);
  EXPECT_EQ(QUrl(u"https://github.com/Genki-jp0/Akousma/releases/tag/v1.0.1"_s), result.url);

  // Versions are compared as numbers, not as text.
  EXPECT_EQ(UpdateChecker::Status::UpdateAvailable, Parse(R"({"tag_name": "v1.10.0"})", u"1.9.0"_s).status);
  EXPECT_EQ(UpdateChecker::Status::UpdateAvailable, Parse(R"({"tag_name": "1.0.0.1"})", u"1.0.0"_s).status);

}

TEST(UpdateCheckerTest, SameOrOlderRelease) {

  EXPECT_EQ(UpdateChecker::Status::UpToDate, Parse(kRelease, u"1.0.1"_s).status);
  EXPECT_EQ(UpdateChecker::Status::UpToDate, Parse(kRelease, u"1.1.0"_s).status);
  EXPECT_EQ(UpdateChecker::Status::UpToDate, Parse(R"({"tag_name": "1.0"})", u"1.0.0"_s).status);
  EXPECT_EQ(UpdateChecker::Status::UpToDate, Parse(R"({"tag_name": "v1.0.0"})", u"1.0"_s).status);

}

TEST(UpdateCheckerTest, DraftsAndPreReleasesAreNotUpdates) {

  EXPECT_EQ(UpdateChecker::Status::NoRelease, Parse(R"({"tag_name": "v2.0.0", "draft": true})").status);
  EXPECT_EQ(UpdateChecker::Status::NoRelease, Parse(R"({"tag_name": "v2.0.0", "prerelease": true})").status);

}

TEST(UpdateCheckerTest, NoRelease) {

  EXPECT_EQ(UpdateChecker::Status::NoRelease, UpdateChecker::ParseReply(404, "59", u"Not Found"_s, R"({"message": "Not Found"})", QLatin1String(kRepository), u"1.0.0"_s).status);

}

TEST(UpdateCheckerTest, RateLimited) {

  EXPECT_EQ(UpdateChecker::Status::RateLimited, UpdateChecker::ParseReply(403, "0", u"Forbidden"_s, QByteArray(), QLatin1String(kRepository), u"1.0.0"_s).status);
  EXPECT_EQ(UpdateChecker::Status::RateLimited, UpdateChecker::ParseReply(429, QByteArray(), u"Too Many Requests"_s, QByteArray(), QLatin1String(kRepository), u"1.0.0"_s).status);
  // Forbidden for another reason.
  EXPECT_EQ(UpdateChecker::Status::Error, UpdateChecker::ParseReply(403, "59", u"Forbidden"_s, QByteArray(), QLatin1String(kRepository), u"1.0.0"_s).status);

}

TEST(UpdateCheckerTest, Errors) {

  const UpdateChecker::Result no_network = UpdateChecker::ParseReply(0, QByteArray(), u"SSL handshake failed"_s, QByteArray(), QLatin1String(kRepository), u"1.0.0"_s);
  EXPECT_EQ(UpdateChecker::Status::Error, no_network.status);
  EXPECT_EQ(u"SSL handshake failed"_s, no_network.error);

  EXPECT_EQ(UpdateChecker::Status::Error, UpdateChecker::ParseReply(500, QByteArray(), QString(), QByteArray(), QLatin1String(kRepository), u"1.0.0"_s).status);

}

TEST(UpdateCheckerTest, MaliciousReplies) {

  // Too large.
  QByteArray huge = R"({"tag_name": "v9.0.0", "body": ")";
  huge.append(QByteArray(UpdateChecker::kMaxReplySize, 'a'));
  huge.append(R"("})");
  EXPECT_EQ(UpdateChecker::Status::Error, Parse(huge).status);

  // Not JSON, not an object, missing or wrong types.
  EXPECT_EQ(UpdateChecker::Status::Error, Parse("<html>").status);
  EXPECT_EQ(UpdateChecker::Status::Error, Parse(R"({"tag_name": "v9.0.0")").status);
  EXPECT_EQ(UpdateChecker::Status::Error, Parse(R"(["v9.0.0"])").status);
  EXPECT_EQ(UpdateChecker::Status::Error, Parse(R"({})").status);
  EXPECT_EQ(UpdateChecker::Status::Error, Parse(R"({"tag_name": 9})").status);
  EXPECT_EQ(UpdateChecker::Status::Error, Parse(R"({"tag_name": ["v9.0.0"]})").status);
  EXPECT_EQ(UpdateChecker::Status::Error, Parse(R"({"tag_name": "v9.0.0", "name": {"a": 1}})").status);
  EXPECT_EQ(UpdateChecker::Status::Error, Parse(R"({"tag_name": "v9.0.0", "body": 1})").status);
  EXPECT_EQ(UpdateChecker::Status::Error, Parse(R"({"tag_name": "v9.0.0", "html_url": true})").status);
  // GitHub has null for a release without name or notes.
  EXPECT_EQ(UpdateChecker::Status::UpdateAvailable, Parse(R"({"tag_name": "v9.0.0", "name": null, "body": null})").status);

  // Tags which are not versions.
  EXPECT_EQ(UpdateChecker::Status::Error, Parse(R"({"tag_name": "v99999999999999999999"})").status);
  EXPECT_EQ(UpdateChecker::Status::Error, Parse(R"({"tag_name": "1.0.0<script>"})").status);
  EXPECT_EQ(UpdateChecker::Status::Error, Parse(R"({"tag_name": "nightly"})").status);

  // A very long name is cut.
  EXPECT_LE(Parse(R"({"tag_name": "v9.0.0", "name": ")" + QByteArray(10000, 'n') + R"("})").name.length(), 200);

}

TEST(UpdateCheckerTest, ReleasePageUrl) {

  const QString repository = QLatin1String(kRepository);
  const QUrl releases_url(u"https://github.com/Genki-jp0/Akousma/releases"_s);

  EXPECT_EQ(QUrl(u"https://github.com/Genki-jp0/Akousma/releases/tag/v1.0.1"_s), UpdateChecker::ReleasePageUrl(repository, u"https://github.com/Genki-jp0/Akousma/releases/tag/v1.0.1"_s));
  // Anything else is the repository's releases page.
  for (const QString &html_url : {u""_s,
                                  u"javascript:alert(1)"_s,
                                  u"file:///etc/passwd"_s,
                                  u"http://github.com/Genki-jp0/Akousma/releases/tag/v1.0.1"_s,
                                  u"https://evil.example/Genki-jp0/Akousma/releases/tag/v1.0.1"_s,
                                  u"https://github.com.evil.example/Genki-jp0/Akousma/releases/tag/v1.0.1"_s,
                                  u"https://github.com@evil.example/Genki-jp0/Akousma/releases/tag/v1.0.1"_s,
                                  u"https://user@github.com/Genki-jp0/Akousma/releases/tag/v1.0.1"_s,
                                  u"https://github.com:8443/Genki-jp0/Akousma/releases/tag/v1.0.1"_s,
                                  u"https://github.com/evil/Akousma/releases/tag/v1.0.1"_s,
                                  u"https://github.com/Genki-jp0/Akousma/releases/../../../evil/repo/releases/tag/v1"_s,
                                  u"https://github.com/Genki-jp0/Akousma/archive/v1.0.1.zip"_s}) {
    EXPECT_EQ(releases_url, UpdateChecker::ReleasePageUrl(repository, html_url)) << html_url.toStdString();
  }

  EXPECT_EQ(releases_url, Parse(R"json({"tag_name": "v9.0.0", "html_url": "javascript:alert(1)"})json").url);
  EXPECT_EQ(releases_url, Parse(R"({"tag_name": "v9.0.0", "html_url": "https://evil.example/"})").url);

}

TEST(UpdateCheckerTest, ReleaseNotesHtml) {

  EXPECT_TRUE(UpdateChecker::ReleaseNotesHtml(u"  "_s).isEmpty());

  // HTML in the notes is shown as text, so no links, images or remote resources.
  const QString html = UpdateChecker::ReleaseNotesHtml(u"Fixes\n<img src=\"http://evil.example/x.png\"> <a href=\"file:///etc\">x</a><script>alert(1)</script>"_s);
  EXPECT_FALSE(html.contains(u"<img"_s));
  EXPECT_FALSE(html.contains(u"<a "_s));
  EXPECT_FALSE(html.contains(u"<script"_s));
  EXPECT_TRUE(html.contains(u"&lt;img src=&quot;http://evil.example/x.png&quot;&gt;"_s));
  EXPECT_TRUE(html.contains(u"Fixes<br />"_s));

  // Long notes are cut at the end of a line.
  QString notes;
  for (int i = 0; i < 100; ++i) notes += u"- Change number %1\n"_s.arg(i);
  const QString cut_html = UpdateChecker::ReleaseNotesHtml(notes);
  EXPECT_TRUE(cut_html.contains(u"Change number 0<br />"_s));
  EXPECT_FALSE(cut_html.contains(u"Change number 99"_s));
  EXPECT_TRUE(cut_html.contains(u"..."_s));
  EXPECT_LT(cut_html.length(), 1500);

}

}  // namespace
