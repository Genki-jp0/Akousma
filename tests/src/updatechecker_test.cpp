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
#include <QList>
#include <QPair>
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

  EXPECT_EQ(QUrl(u"https://api.github.com/repos/Genki-jp0/Akousma"_s), UpdateChecker::ApiUrl(u"Genki-jp0/Akousma"_s));
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

// Pre-releases.

constexpr char kCommit[] = "abc1234";

QByteArray ReleaseJson(const QString &tag, const bool prerelease, const QString &published, const bool draft = false) {
  return u"{\"tag_name\": \"%1\", \"prerelease\": %2, \"draft\": %3, \"published_at\": \"%4\", \"html_url\": \"https://github.com/Genki-jp0/Akousma/releases/tag/%1\"}"_s.arg(tag, prerelease ? u"true"_s : u"false"_s, draft ? u"true"_s : u"false"_s, published).toUtf8();
}

QByteArray ReleasesJson(const QList<QByteArray> &releases) {
  QByteArray json = "[";
  for (int i = 0; i < releases.size(); ++i) {
    if (i > 0) json += ",";
    json += releases[i];
  }
  return json + "]";
}

UpdateChecker::Result ParseReleases(const QByteArray &data, const QString &current_version = u"1.0.0"_s, const QString &current_commit = QString()) {
  return UpdateChecker::ParseReleasesReply(200, "59", QString(), data, QLatin1String(kRepository), current_version, current_commit);
}

TEST(UpdateCheckerTest, Commit) {

  EXPECT_TRUE(UpdateChecker::IsValidCommit(u"abc1234"_s));
  EXPECT_TRUE(UpdateChecker::IsValidCommit(u"0123456789abcdef0123456789ABCDEF01234567"_s));
  EXPECT_FALSE(UpdateChecker::IsValidCommit(QString()));
  EXPECT_FALSE(UpdateChecker::IsValidCommit(u"abc"_s));
  EXPECT_FALSE(UpdateChecker::IsValidCommit(u"zzzzzzz"_s));
  EXPECT_FALSE(UpdateChecker::IsValidCommit(u"../abc1234"_s));
  EXPECT_FALSE(UpdateChecker::IsValidCommit(u"abc1234/.."_s));
  EXPECT_FALSE(UpdateChecker::IsValidCommit(u"0123456789abcdef0123456789abcdef012345678"_s));

}

TEST(UpdateCheckerTest, PreReleaseTags) {

  const QVersionNumber v101(1, 0, 1);
  struct Case { QString tag; QVersionNumber version; QString commit; };
  const QList<Case> cases = {
    {u"1.0.1+gitabc1234"_s, v101, u"abc1234"_s},
    {u"v1.0.1+gitABC1234"_s, v101, u"abc1234"_s},
    {u"v1.0.1+gabc1234"_s, v101, u"abc1234"_s},
    {u"v1.0.1-gabc1234"_s, v101, u"abc1234"_s},
    {u"v1.0.1-abc1234"_s, v101, u"abc1234"_s},
    {u"abc1234"_s, QVersionNumber(), u"abc1234"_s},
    {u"gabc1234"_s, QVersionNumber(), u"abc1234"_s},
    {u"0123456789abcdef0123456789abcdef01234567"_s, QVersionNumber(), u"0123456789abcdef0123456789abcdef01234567"_s},
    // Seven or more hex digits are a commit in a pre-release tag.
    {u"1234567"_s, QVersionNumber(), u"1234567"_s},
    {u"v1.0.1"_s, v101, QString()},
  };
  for (const Case &c : cases) {
    const UpdateChecker::Tag tag = UpdateChecker::ParseTag(c.tag, true);
    EXPECT_EQ(c.version, tag.version) << c.tag.toStdString();
    EXPECT_EQ(c.commit, tag.commit) << c.tag.toStdString();
  }

  // Only pre-releases can have a commit.
  EXPECT_FALSE(UpdateChecker::ParseTag(u"abc1234"_s, false).IsValid());
  EXPECT_FALSE(UpdateChecker::ParseTag(u"1.0.1+gitabc1234"_s, false).IsValid());
  EXPECT_EQ(QVersionNumber(1234567), UpdateChecker::ParseTag(u"1234567"_s, false).version);

  for (const QString &tag : {u"abc"_s, u"zzzzzzz"_s, u"../abc1234"_s, u"abc1234/../x"_s, u"v1.0.1-abc"_s, u"v1.0.1+gitzzzzzzz"_s, u"v1.0.1-rc1"_s, u"v1.0.1+gitabc1234<script>"_s, u"abc1234 "_s, u"g"_s, u"1.0.1-"_s}) {
    EXPECT_FALSE(UpdateChecker::ParseTag(tag, true).IsValid()) << tag.toStdString();
  }

  UpdateChecker::Tag tag;
  tag.version = v101;
  EXPECT_EQ(u"1.0.1"_s, UpdateChecker::DisplayVersion(tag));
  tag.commit = u"abc1234def"_s;
  EXPECT_EQ(u"1.0.1 (git abc1234)"_s, UpdateChecker::DisplayVersion(tag));
  tag.version = QVersionNumber();
  EXPECT_EQ(u"git abc1234"_s, UpdateChecker::DisplayVersion(tag));

}

TEST(UpdateCheckerTest, ReleasesNewestByPublicationDate) {

  // The newest by date, not the first, and drafts are left out.
  const UpdateChecker::Result result = ParseReleases(ReleasesJson({
    ReleaseJson(u"v1.0.1"_s, false, u"2026-01-01T00:00:00Z"_s),
    ReleaseJson(u"v1.0.3"_s, false, u"2026-03-01T00:00:00Z"_s, true),
    ReleaseJson(u"v1.0.2"_s, false, u"2026-02-01T00:00:00Z"_s),
  }));
  EXPECT_EQ(UpdateChecker::Status::UpdateAvailable, result.status);
  EXPECT_EQ(u"1.0.2"_s, result.version);
  EXPECT_FALSE(result.prerelease);

  // A pre-release of a newer version is compared by version.
  const UpdateChecker::Result prerelease = ParseReleases(ReleasesJson({ReleaseJson(u"v1.0.0"_s, false, u"2026-01-01T00:00:00Z"_s), ReleaseJson(u"1.0.1+gitabc1234"_s, true, u"2026-02-01T00:00:00Z"_s)}));
  EXPECT_EQ(UpdateChecker::Status::UpdateAvailable, prerelease.status);
  EXPECT_TRUE(prerelease.prerelease);
  EXPECT_EQ(u"1.0.1 (git abc1234)"_s, prerelease.version);

  // Nothing usable: no releases, only drafts, junk.
  EXPECT_EQ(UpdateChecker::Status::NoRelease, ParseReleases("[]").status);
  EXPECT_EQ(UpdateChecker::Status::NoRelease, ParseReleases(ReleasesJson({ReleaseJson(u"v9.0.0"_s, false, u"2026-01-01T00:00:00Z"_s, true)})).status);
  EXPECT_EQ(UpdateChecker::Status::NoRelease, ParseReleases(R"json([1, "x", null, {"tag_name": 9, "published_at": "2026-01-01T00:00:00Z"}, {"tag_name": "nightly", "prerelease": true, "published_at": "2026-01-01T00:00:00Z"}, {"tag_name": "v9.0.0"}, {"tag_name": "v9.0.0", "published_at": "yesterday"}])json").status);
  EXPECT_EQ(UpdateChecker::Status::Error, ParseReleases(R"json({"tag_name": "v9.0.0"})json").status);
  EXPECT_EQ(UpdateChecker::Status::Error, ParseReleases("<html>").status);
  EXPECT_EQ(UpdateChecker::Status::NoRelease, UpdateChecker::ParseReleasesReply(404, "59", QString(), QByteArray(), QLatin1String(kRepository), u"1.0.0"_s, QString()).status);
  EXPECT_EQ(UpdateChecker::Status::RateLimited, UpdateChecker::ParseReleasesReply(403, "0", QString(), QByteArray(), QLatin1String(kRepository), u"1.0.0"_s, QString()).status);

  // Only the first kMaxReleases count.
  QList<QByteArray> many;
  for (int i = 0; i < UpdateChecker::kMaxReleases; ++i) many << ReleaseJson(u"v1.0.0"_s, false, u"2026-01-01T00:00:00Z"_s);
  many << ReleaseJson(u"v9.0.0"_s, false, u"2026-12-01T00:00:00Z"_s);
  EXPECT_EQ(UpdateChecker::Status::UpToDate, ParseReleases(ReleasesJson(many)).status);

}

TEST(UpdateCheckerTest, StableReleaseNewerThanPreReleaseBuild) {

  const UpdateChecker::Result result = ParseReleases(ReleasesJson({ReleaseJson(u"abc1234"_s, true, u"2026-01-01T00:00:00Z"_s), ReleaseJson(u"v1.0.1"_s, false, u"2026-02-01T00:00:00Z"_s)}), u"1.0.0"_s, QLatin1String(kCommit));
  EXPECT_EQ(UpdateChecker::Status::UpdateAvailable, result.status);
  EXPECT_EQ(u"1.0.1"_s, result.version);

  // The release this pre-release build came after is not newer.
  EXPECT_EQ(UpdateChecker::Status::UpToDate, ParseReleases(ReleasesJson({ReleaseJson(u"v1.0.0"_s, false, u"2026-02-01T00:00:00Z"_s)}), u"1.0.0"_s, QLatin1String(kCommit)).status);

}

TEST(UpdateCheckerTest, PreReleaseCommitOfThisBuild) {

  // A build of a commit needs GitHub's comparison, unless it's the same commit.
  const UpdateChecker::Result compare = ParseReleases(ReleasesJson({ReleaseJson(u"1.0.0+gitdef5678"_s, true, u"2026-02-01T00:00:00Z"_s)}), u"1.0.0"_s, QLatin1String(kCommit));
  EXPECT_EQ(UpdateChecker::Status::NeedsCompare, compare.status);
  EXPECT_EQ(u"abc1234"_s, compare.compare_base);
  EXPECT_EQ(u"def5678"_s, compare.compare_head);
  EXPECT_EQ(UpdateChecker::Status::NeedsCompare, ParseReleases(ReleasesJson({ReleaseJson(u"def5678"_s, true, u"2026-02-01T00:00:00Z"_s)}), u"1.0.0"_s, QLatin1String(kCommit)).status);
  EXPECT_EQ(UpdateChecker::Status::UpToDate, ParseReleases(ReleasesJson({ReleaseJson(u"1.0.0+gitabc1234"_s, true, u"2026-02-01T00:00:00Z"_s)}), u"1.0.0"_s, QLatin1String(kCommit)).status);
  EXPECT_EQ(UpdateChecker::Status::UpToDate, ParseReleases(ReleasesJson({ReleaseJson(u"abc1234def5678abc1234def5678abc1234def56"_s, true, u"2026-02-01T00:00:00Z"_s)}), u"1.0.0"_s, QLatin1String(kCommit)).status);
  // An invalid commit of this build is no commit.
  EXPECT_NE(UpdateChecker::Status::NeedsCompare, ParseReleases(ReleasesJson({ReleaseJson(u"def5678"_s, true, u"2026-02-01T00:00:00Z"_s)}), u"1.0.0"_s, u"../x"_s).status);

  UpdateChecker::Result release = compare;
  for (const auto &[status, expected] : QList<QPair<QString, UpdateChecker::Status>>{{u"ahead"_s, UpdateChecker::Status::UpdateAvailable}, {u"diverged"_s, UpdateChecker::Status::UpdateAvailable}, {u"behind"_s, UpdateChecker::Status::UpToDate}, {u"identical"_s, UpdateChecker::Status::UpToDate}}) {
    const UpdateChecker::Result result = UpdateChecker::ParseCompareReply(200, "58", QString(), u"{\"url\": \"x\", \"status\": \"%1\", \"ahead_by\": 1}"_s.arg(status).toUtf8(), release);
    EXPECT_EQ(expected, result.status) << status.toStdString();
    EXPECT_EQ(u"1.0.0 (git def5678)"_s, result.version);
  }
  EXPECT_EQ(UpdateChecker::Status::Error, UpdateChecker::ParseCompareReply(404, "58", QString(), QByteArray(), release).status);
  EXPECT_EQ(UpdateChecker::Status::RateLimited, UpdateChecker::ParseCompareReply(403, "0", QString(), QByteArray(), release).status);
  EXPECT_EQ(UpdateChecker::Status::Error, UpdateChecker::ParseCompareReply(200, "58", QString(), R"json({"status": "newer"})json", release).status);
  EXPECT_EQ(UpdateChecker::Status::Error, UpdateChecker::ParseCompareReply(200, "58", QString(), R"json(["status", "ahead"])json", release).status);

}

TEST(UpdateCheckerTest, PreReleaseCommitOfReleaseBuild) {

  // Without a commit, this build is the release of its version, a commit published after it is newer.
  const QByteArray release = ReleaseJson(u"v1.0.0"_s, false, u"2026-02-01T00:00:00Z"_s);
  EXPECT_EQ(UpdateChecker::Status::UpdateAvailable, ParseReleases(ReleasesJson({release, ReleaseJson(u"def5678"_s, true, u"2026-03-01T00:00:00Z"_s)})).status);
  EXPECT_EQ(UpdateChecker::Status::UpdateAvailable, ParseReleases(ReleasesJson({release, ReleaseJson(u"1.0.0+gitdef5678"_s, true, u"2026-03-01T00:00:00Z"_s)})).status);
  // Can't tell without the release of this version.
  EXPECT_EQ(UpdateChecker::Status::Error, ParseReleases(ReleasesJson({ReleaseJson(u"def5678"_s, true, u"2026-03-01T00:00:00Z"_s)})).status);

}

TEST(UpdateCheckerTest, CompareStatus) {

  EXPECT_EQ(u"ahead"_s, UpdateChecker::CompareStatus(R"json({"url": "x", "base_commit": {"commit": {"message": "x"}}, "status": "ahead", "files": [{"status": "modified"}]})json"));
  // Only a key, not text in a string.
  EXPECT_TRUE(UpdateChecker::CompareStatus(R"json({"message": "\"status\": \"ahead\""})json").isEmpty());
  EXPECT_TRUE(UpdateChecker::CompareStatus(R"json({"status": "modified"})json").isEmpty());
  EXPECT_TRUE(UpdateChecker::CompareStatus("<html>\"status\": \"ahead\"").isEmpty());
  // It must be in the beginning.
  QByteArray late = R"json({"padding": ")json" + QByteArray(UpdateChecker::kCompareStatusMaxOffset, 'a') + R"json(", "status": "ahead"})json";
  EXPECT_TRUE(UpdateChecker::CompareStatus(late).isEmpty());

}

}  // namespace
