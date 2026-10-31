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

#include <utility>
#include <initializer_list>

#include <QtGlobal>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QUrl>
#include <QVariant>

#include "includes/scoped_ptr.h"
#include "core/logging.h"
#include "core/settings.h"
#include "core/standardpaths.h"
#include "core/database.h"
#include "playerimporter.h"

using namespace Qt::Literals::StringLiterals;

namespace {

// Akousma's own bookkeeping for the import, kept out of the imported settings.
constexpr char kSettingsGroup[] = "Import";
constexpr char kPendingPlayer[] = "pending_player";
constexpr char kPendingName[] = "pending_name";
constexpr char kPendingItems[] = "pending_items";
constexpr char kFirstRunPromptDone[] = "first_run_prompt_done";
constexpr char kLastImport[] = "last_import";

constexpr char kConnectionName[] = "akousma_player_import";

bool sRestartRequested = false;

// Strawberry keeps a cover cache per source, see Song::ImageCacheDir().
const QStringList &StrawberryCoverDirNames() {
  static const QStringList names = QStringList() << u"albumcovers"_s
                                                 << u"collectionalbumcovers"_s
                                                 << u"subsonicalbumcovers"_s
                                                 << u"plexalbumcovers"_s
                                                 << u"tidalalbumcovers"_s
                                                 << u"spotifyalbumcovers"_s
                                                 << u"qobuzalbumcovers"_s
                                                 << u"jellyfinalbumcovers"_s
                                                 << u"devicealbumcovers"_s;
  return names;
}

// Clementine keeps the covers set by the user in one directory. They belong to collection songs, so they go to Akousma's collection cover cache.
constexpr char kClementineCoverDir[] = "albumcovers";
constexpr char kAkousmaCollectionCoverDir[] = "collectionalbumcovers";

Qt::CaseSensitivity PathCaseSensitivity() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  return Qt::CaseInsensitive;
#else
  return Qt::CaseSensitive;
#endif
}

QString CleanPath(const QString &path) {
  return path.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(path));
}

// Replaces old_dir with new_dir when path is old_dir or is inside it.
bool RewritePath(QString *path, const QString &old_dir, const QString &new_dir) {

  if (old_dir.isEmpty() || new_dir.isEmpty()) return false;

  const QString clean_path = CleanPath(*path);
  const QString clean_old_dir = CleanPath(old_dir);
  if (clean_path.compare(clean_old_dir, PathCaseSensitivity()) == 0) {
    *path = new_dir;
    return true;
  }
  if (clean_path.startsWith(clean_old_dir + u'/', PathCaseSensitivity())) {
    *path = new_dir + clean_path.mid(clean_old_dir.length());
    return true;
  }

  return false;

}

using PathPairs = QList<std::pair<QString, QString>>;

// Points a path in a setting from the old application's directory to Akousma's, when the file is there after the import.
// Paths to files that were not imported keep pointing to the old location, where the file still is.
bool RewriteSettingPath(QString *path, const QString &old_dir, const QString &new_dir) {

  QString new_path = *path;
  if (!RewritePath(&new_path, old_dir, new_dir)) return false;
  if (QFileInfo::exists(*path) && !QFileInfo::exists(new_path)) return false;
  *path = new_path;
  return true;

}

// Rewrites strings, string lists and file URLs in a setting value that point into the old application's directories.
QVariant RewriteSettingValue(const QVariant &value, const PathPairs &dirs, bool *changed) {

  switch (value.metaType().id()) {
    case QMetaType::QString:{
      QString str = value.toString();
      for (const std::pair<QString, QString> &dir : dirs) {
        if (RewriteSettingPath(&str, dir.first, dir.second)) {
          *changed = true;
          return str;
        }
      }
      return value;
    }
    case QMetaType::QStringList:{
      QStringList list = value.toStringList();
      for (QString &str : list) {
        for (const std::pair<QString, QString> &dir : dirs) {
          if (RewriteSettingPath(&str, dir.first, dir.second)) {
            *changed = true;
            break;
          }
        }
      }
      return list;
    }
    case QMetaType::QUrl:{
      const QUrl url = value.toUrl();
      if (!url.isLocalFile()) return value;
      QString path = url.toLocalFile();
      for (const std::pair<QString, QString> &dir : dirs) {
        if (RewriteSettingPath(&path, dir.first, dir.second)) {
          *changed = true;
          return QUrl::fromLocalFile(path);
        }
      }
      return value;
    }
    default:
      return value;
  }

}

QString SqlString(const QString &str) {
  QString escaped = str;
  escaped.replace(u'\'', u"''"_s);
  return u'\'' + escaped + u'\'';
}

QSet<QString> TableColumns(QSqlDatabase &db, const QString &schema, const QString &table) {

  QSet<QString> columns;
  QSqlQuery q(db);
  if (q.exec(QStringLiteral("PRAGMA %1.table_info(%2)").arg(schema, table))) {
    while (q.next()) {
      columns.insert(q.value(1).toString());
    }
  }
  return columns;

}

int CountRows(QSqlDatabase &db, const QString &table) {

  QSqlQuery q(db);
  if (q.exec(u"SELECT COUNT(*) FROM "_s + table) && q.next()) {
    return q.value(0).toInt();
  }
  return 0;

}

QString QueryError(const QSqlQuery &q) {
  return q.lastError().text() + u" ("_s + q.lastQuery().left(200) + u')';
}

// Song columns from the Clementine songs table, as they map to Akousma's songs table.
// Based on upstream Strawberry's dist/scripts/import-from-clementine.sh, with the NOT NULL defaults of Akousma's schema for values Clementine left empty.
struct ColumnMapping {
  const char *destination;
  const char *source;
  const char *fallback;  // Used for NULL values and when Clementine's schema doesn't have the column.
};

constexpr ColumnMapping kSongColumns[] = {
  { "title", "title", nullptr },
  { "album", "album", nullptr },
  { "artist", "artist", nullptr },
  { "albumartist", "albumartist", nullptr },
  { "track", "track", "-1" },
  { "disc", "disc", "-1" },
  { "year", "year", "-1" },
  { "originalyear", "originalyear", "-1" },
  { "genre", "genre", nullptr },
  { "compilation", "compilation", "0" },
  { "composer", "composer", nullptr },
  { "performer", "performer", nullptr },
  { "grouping", "grouping", nullptr },
  { "comment", "comment", nullptr },
  { "lyrics", "lyrics", nullptr },
  { "beginning", "beginning", "0" },
  { "length", "length", "0" },
  { "bitrate", "bitrate", "-1" },
  { "samplerate", "samplerate", "-1" },
  { "directory_id", "directory", "-1" },
  { "filesize", "filesize", "-1" },
  { "mtime", "mtime", "-1" },
  { "ctime", "ctime", "-1" },
  { "unavailable", "unavailable", "0" },
  { "playcount", "playcount", "0" },
  { "skipcount", "skipcount", "0" },
  { "lastplayed", "lastplayed", "-1" },
  { "compilation_detected", "sampler", "0" },
  { "compilation_on", "forced_compilation_on", "0" },
  { "compilation_off", "forced_compilation_off", "0" },
  { "compilation_effective", "effective_compilation", "0" },
  { "art_automatic", "art_automatic", nullptr },
  { "art_manual", "art_manual", nullptr },
  { "effective_albumartist", "effective_albumartist", nullptr },
  { "effective_originalyear", "effective_originalyear", "0" },
  { "cue_path", "cue_path", nullptr },
  { "rating", "rating", "-1" },
  { "bpm", "bpm", nullptr },
};

// Clementine's Song::FileType values differ from Akousma's (Strawberry's) Song::FileType.
constexpr char kFileTypeExpression[] =
  "CASE %1.filetype "
  "WHEN 1 THEN 10 "   // ASF
  "WHEN 2 THEN 2 "    // FLAC
  "WHEN 3 THEN 9 "    // MP4
  "WHEN 4 THEN 12 "   // MPC
  "WHEN 5 THEN 8 "    // MPEG
  "WHEN 6 THEN 4 "    // OggFlac
  "WHEN 7 THEN 7 "    // OggSpeex
  "WHEN 8 THEN 5 "    // OggVorbis
  "WHEN 9 THEN 11 "   // AIFF
  "WHEN 10 THEN 1 "   // WAV
  "WHEN 11 THEN 13 "  // TrueAudio
  "WHEN 12 THEN 90 "  // CDDA
  "WHEN 13 THEN 6 "   // OggOpus
  "WHEN 14 THEN 3 "   // WavPack
  "WHEN 15 THEN 22 "  // SPC
  "WHEN 16 THEN 23 "  // VGM
  "WHEN 17 THEN 17 "  // APE
  "WHEN 99 THEN 91 "  // Stream
  "ELSE 0 END";

// Builds the column list and the SELECT expressions for copying Clementine songs, given the columns Clementine's table has.
void SongColumnExpressions(const QSet<QString> &source_columns, const QString &alias, QStringList *destination, QStringList *expressions) {

  for (const ColumnMapping &mapping : kSongColumns) {
    const QString source = QLatin1String(mapping.source);
    const QString fallback = mapping.fallback ? QLatin1String(mapping.fallback) : QString();
    if (source_columns.contains(source)) {
      *destination << QLatin1String(mapping.destination);
      *expressions << (fallback.isEmpty() ? alias + u'.' + source : QStringLiteral("COALESCE(%1.%2, %3)").arg(alias, source, fallback));
    }
    else if (!fallback.isEmpty()) {
      *destination << QLatin1String(mapping.destination);
      *expressions << fallback;
    }
  }

  // Clementine stores the URL encoded as a blob in "filename", Akousma stores it as text in "url".
  *destination << u"url"_s;
  *expressions << QStringLiteral("COALESCE(CAST(%1.filename AS TEXT), '')").arg(alias);

  *destination << u"filetype"_s;
  *expressions << QString::fromLatin1(kFileTypeExpression).arg(alias);

}

// Clementine keeps paths in the cover columns, and "(embedded)" and "(unset)" for embedded and removed covers.
// Akousma keeps URLs, and the art_embedded and art_unset columns.
int ConvertClementineCovers(QSqlDatabase &db, const QString &table, const QString &old_cover_dir, const QString &new_cover_dir, QStringList *errors) {

  struct Row {
    qint64 rowid;
    QString art_automatic;
    QString art_manual;
  };
  QList<Row> rows;
  {
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT ROWID, art_automatic, art_manual FROM %1 WHERE (art_automatic IS NOT NULL AND art_automatic != '') OR (art_manual IS NOT NULL AND art_manual != '')").arg(table))) {
      *errors << QueryError(q);
      return 0;
    }
    while (q.next()) {
      rows << Row { q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString() };
    }
  }

  const auto to_url = [&old_cover_dir, &new_cover_dir](QString path) -> QString {
    if (path.isEmpty()) return QString();
    // Clementine keeps remote covers of internet services as URLs.
    const QUrl url(path);
    if (url.isValid() && url.scheme().length() > 1) return QString::fromUtf8(url.toEncoded());
    if (!new_cover_dir.isEmpty()) RewritePath(&path, old_cover_dir, new_cover_dir);
    return QString::fromUtf8(QUrl::fromLocalFile(path).toEncoded());
  };

  int converted = 0;
  QSqlQuery update(db);
  update.prepare(QStringLiteral("UPDATE %1 SET art_automatic = :art_automatic, art_manual = :art_manual, art_embedded = :art_embedded, art_unset = :art_unset WHERE ROWID = :rowid").arg(table));
  for (const Row &row : std::as_const(rows)) {
    bool art_embedded = false;
    bool art_unset = false;
    QString art_automatic;
    QString art_manual;
    if (row.art_automatic == "(embedded)"_L1) art_embedded = true;
    else art_automatic = to_url(row.art_automatic);
    if (row.art_manual == "(embedded)"_L1) art_embedded = true;
    else if (row.art_manual == "(unset)"_L1) art_unset = true;
    else art_manual = to_url(row.art_manual);
    update.bindValue(u":art_automatic"_s, art_automatic.isEmpty() ? QVariant() : QVariant(art_automatic));
    update.bindValue(u":art_manual"_s, art_manual.isEmpty() ? QVariant() : QVariant(art_manual));
    update.bindValue(u":art_embedded"_s, art_embedded ? 1 : 0);
    update.bindValue(u":art_unset"_s, art_unset ? 1 : 0);
    update.bindValue(u":rowid"_s, row.rowid);
    if (update.exec()) {
      ++converted;
    }
    else {
      *errors << QueryError(update);
      break;
    }
  }

  return converted;

}

enum class Conversion {
  None,
  Invert,             // Boolean with the opposite meaning.
  PercentToFraction,  // 0-100 to 0.0-1.0.
  Language,           // Clementine language code to an Akousma translation.
  MonoToChannels      // Clementine's mono playback to Akousma's channel count.
};

struct SettingMapping {
  QString source_group;
  QString source_key;
  QString group;
  QString key;
  Conversion conversion;
  QVariant clementine_default;  // Clementine's default, written when Clementine has no value and its default differs from Akousma's.
};

// Clementine settings with an equivalent in Akousma, from Clementine's settings pages and the classes reading them.
// Groups and keys not listed here have no equivalent, or a different format (see CHANGES.txt).
QList<SettingMapping> ClementineSettingMappings() {

  QList<SettingMapping> m;

  // Behaviour: Clementine keeps these in MainWindow, Player and General.
  m << SettingMapping { u"MainWindow"_s, u"showtray"_s, u"Behaviour"_s, u"showtrayicon"_s, Conversion::None, {} };
  m << SettingMapping { u"MainWindow"_s, u"keeprunning"_s, u"Behaviour"_s, u"keeprunning"_s, Conversion::None, {} };
  m << SettingMapping { u"MainWindow"_s, u"startupbehaviour"_s, u"Behaviour"_s, u"startupbehaviour"_s, Conversion::None, {} };
  m << SettingMapping { u"MainWindow"_s, u"doubleclick_addmode"_s, u"Behaviour"_s, u"doubleclick_addmode"_s, Conversion::None, {} };
  m << SettingMapping { u"MainWindow"_s, u"doubleclick_playmode"_s, u"Behaviour"_s, u"doubleclick_playmode"_s, Conversion::None, 2 };
  m << SettingMapping { u"MainWindow"_s, u"doubleclick_playlist_addmode"_s, u"Behaviour"_s, u"doubleclick_playlist_addmode"_s, Conversion::None, {} };
  m << SettingMapping { u"MainWindow"_s, u"menu_playmode"_s, u"Behaviour"_s, u"menu_playmode"_s, Conversion::None, 2 };
  m << SettingMapping { u"MainWindow"_s, u"resume_playback_after_start"_s, u"Behaviour"_s, u"resumeplayback"_s, Conversion::None, {} };
  m << SettingMapping { u"MainWindow"_s, u"geometry"_s, u"MainWindow"_s, u"geometry"_s, Conversion::None, {} };
  m << SettingMapping { u"Player"_s, u"menu_previousmode"_s, u"Behaviour"_s, u"menu_previousmode"_s, Conversion::None, {} };
  m << SettingMapping { u"Player"_s, u"seek_step_sec"_s, u"Behaviour"_s, u"seek_step_sec"_s, Conversion::None, {} };
  m << SettingMapping { u"General"_s, u"language"_s, u"Behaviour"_s, u"language"_s, Conversion::Language, {} };

  // Player
  m << SettingMapping { u"Player"_s, u"volume"_s, u"Player"_s, u"volume"_s, Conversion::None, 50 };
  m << SettingMapping { u"Player"_s, u"stop_play_if_fail"_s, u"Playlist"_s, u"continue_on_error"_s, Conversion::Invert, false };

  // Playlist
  m << SettingMapping { u"Playlist"_s, u"greyoutdeleted"_s, u"Playlist"_s, u"greyout_songs_startup"_s, Conversion::None, {} };
  m << SettingMapping { u"Playlist"_s, u"click_edit_inline"_s, u"Playlist"_s, u"editmetadatainline"_s, Conversion::None, true };
  m << SettingMapping { u"Playlist"_s, u"path_type"_s, u"Playlist"_s, u"path_type"_s, Conversion::None, {} };
  m << SettingMapping { u"Playlist"_s, u"write_metadata"_s, u"Playlist"_s, u"write_metadata"_s, Conversion::None, {} };
  m << SettingMapping { u"Playlist"_s, u"glow_effect"_s, u"Playlist"_s, u"glow_effect"_s, Conversion::None, {} };
  m << SettingMapping { u"PlaylistTabBar"_s, u"warn_close_playlist"_s, u"Playlist"_s, u"warn_close_playlist"_s, Conversion::None, {} };

  // Fading: Clementine's Engine::Base reads these from Player.
  m << SettingMapping { u"Player"_s, u"FadeoutEnabled"_s, u"Backend"_s, u"FadeoutEnabled"_s, Conversion::None, true };
  m << SettingMapping { u"Player"_s, u"CrossfadeEnabled"_s, u"Backend"_s, u"CrossfadeEnabled"_s, Conversion::None, true };
  m << SettingMapping { u"Player"_s, u"AutoCrossfadeEnabled"_s, u"Backend"_s, u"AutoCrossfadeEnabled"_s, Conversion::None, {} };
  m << SettingMapping { u"Player"_s, u"NoCrossfadeSameAlbum"_s, u"Backend"_s, u"NoCrossfadeSameAlbum"_s, Conversion::None, {} };
  m << SettingMapping { u"Player"_s, u"FadeoutPauseEnabled"_s, u"Backend"_s, u"FadeoutPauseEnabled"_s, Conversion::None, {} };
  m << SettingMapping { u"Player"_s, u"FadeoutDuration"_s, u"Backend"_s, u"FadeoutDuration"_s, Conversion::None, {} };
  m << SettingMapping { u"Player"_s, u"FadeoutPauseDuration"_s, u"Backend"_s, u"FadeoutPauseDuration"_s, Conversion::None, {} };

  // GStreamer output, ReplayGain and buffer.
  m << SettingMapping { u"GstEngine"_s, u"sink"_s, u"Backend"_s, u"output"_s, Conversion::None, {} };
  m << SettingMapping { u"GstEngine"_s, u"device"_s, u"Backend"_s, u"device"_s, Conversion::None, {} };
  m << SettingMapping { u"GstEngine"_s, u"rgenabled"_s, u"Backend"_s, u"rgenabled"_s, Conversion::None, {} };
  m << SettingMapping { u"GstEngine"_s, u"rgmode"_s, u"Backend"_s, u"rgmode"_s, Conversion::None, {} };
  m << SettingMapping { u"GstEngine"_s, u"rgpreamp"_s, u"Backend"_s, u"rgpreamp"_s, Conversion::None, {} };
  m << SettingMapping { u"GstEngine"_s, u"rgcompression"_s, u"Backend"_s, u"rgcompression"_s, Conversion::None, {} };
  m << SettingMapping { u"GstEngine"_s, u"bufferduration"_s, u"Backend"_s, u"bufferduration"_s, Conversion::None, {} };
  m << SettingMapping { u"GstEngine"_s, u"bufferminfill"_s, u"Backend"_s, u"bufferlowwatermark"_s, Conversion::PercentToFraction, {} };
  m << SettingMapping { u"GstEngine"_s, u"monoplayback"_s, u"Backend"_s, u"channels_enabled"_s, Conversion::MonoToChannels, {} };

  // Notifications
  for (const QString &key : std::initializer_list<QString> {u"Behaviour"_s, u"Timeout"_s, u"ShowOnVolumeChange"_s, u"ShowOnPlayModeChange"_s, u"ShowOnPausePlayback"_s, u"ShowArt"_s, u"CustomTextEnabled"_s, u"CustomText1"_s, u"CustomText2"_s}) {
    m << SettingMapping { u"OSD"_s, key, u"OSD"_s, key, Conversion::None, {} };
  }
  for (const QString &key : std::initializer_list<QString> {u"foreground_color"_s, u"background_color"_s, u"background_opacity"_s, u"popup_screen"_s, u"popup_pos"_s, u"font"_s, u"disable_duration"_s}) {
    m << SettingMapping { u"OSDPretty"_s, key, u"OSDPretty"_s, key, Conversion::None, {} };
  }

  // Network proxy
  for (const QString &key : std::initializer_list<QString> {u"mode"_s, u"type"_s, u"hostname"_s, u"port"_s, u"use_authentication"_s, u"username"_s, u"password"_s}) {
    m << SettingMapping { u"Proxy"_s, key, u"NetworkProxy"_s, key, Conversion::None, {} };
  }

  // Collection: Clementine keeps these in LibraryWatcher, LibraryView and LibraryBackend.
  m << SettingMapping { u"LibraryWatcher"_s, u"startup_scan"_s, u"Collection"_s, u"startup_scan"_s, Conversion::None, {} };
  m << SettingMapping { u"LibraryWatcher"_s, u"monitor"_s, u"Collection"_s, u"monitor"_s, Conversion::None, {} };
  m << SettingMapping { u"LibraryWatcher"_s, u"cover_art_patterns"_s, u"Collection"_s, u"cover_art_patterns"_s, Conversion::None, {} };
  m << SettingMapping { u"LibraryView"_s, u"auto_open"_s, u"Collection"_s, u"auto_open"_s, Conversion::None, true };
  m << SettingMapping { u"LibraryView"_s, u"pretty_covers"_s, u"Collection"_s, u"pretty_covers"_s, Conversion::None, {} };
  m << SettingMapping { u"LibraryView"_s, u"show_dividers"_s, u"Collection"_s, u"show_dividers"_s, Conversion::None, {} };
  m << SettingMapping { u"LibraryBackend"_s, u"save_ratings_in_file"_s, u"Collection"_s, u"save_ratings"_s, Conversion::None, {} };
  m << SettingMapping { u"LibraryBackend"_s, u"save_statistics_in_file"_s, u"Collection"_s, u"save_playcounts"_s, Conversion::None, {} };

  // Global shortcuts: the same IDs, except for the Last.fm ones which are for any scrobbler in Akousma.
  for (const QString &id : std::initializer_list<QString> {u"play"_s, u"pause"_s, u"play_pause"_s, u"stop"_s, u"stop_after"_s, u"next_track"_s, u"prev_track"_s, u"inc_volume"_s, u"dec_volume"_s, u"mute"_s, u"seek_forward"_s, u"seek_backward"_s, u"show_hide"_s, u"show_osd"_s, u"toggle_pretty_osd"_s, u"shuffle_mode"_s, u"repeat_mode"_s}) {
    m << SettingMapping { u"Shortcuts"_s, id, u"GlobalShortcuts"_s, id, Conversion::None, {} };
  }
  m << SettingMapping { u"Shortcuts"_s, u"toggle_last_fm_scrobbling"_s, u"GlobalShortcuts"_s, u"toggle_scrobbling"_s, Conversion::None, {} };
  m << SettingMapping { u"Shortcuts"_s, u"love_last_fm_scrobbling"_s, u"GlobalShortcuts"_s, u"love"_s, Conversion::None, {} };

  return m;

}

}  // namespace

PlayerImporter::Items PlayerImporter::Installation::available_items() const {

  Items items;
  if (has_settings) items |= Item::Settings;
  if (has_database()) items |= Item::Collection;
  if (has_covers()) items |= Item::Covers;
  return items;

}

QString PlayerImporter::PlayerName(const Player player) {

  switch (player) {
    case Player::Strawberry:
      return u"Strawberry"_s;
    case Player::Clementine:
      return u"Clementine"_s;
  }
  return QString();

}

QString PlayerImporter::PlayerToString(const Player player) {
  return PlayerName(player).toLower();
}

std::optional<PlayerImporter::Player> PlayerImporter::PlayerFromString(const QString &name) {

  const QString lower = name.trimmed().toLower();
  if (lower == "strawberry"_L1) return Player::Strawberry;
  if (lower == "clementine"_L1) return Player::Clementine;
  return std::nullopt;

}

PlayerImporter::Items PlayerImporter::ItemsFromString(const QString &items, bool *ok) {

  Items ret;
  bool valid = true;
  const QStringList parts = items.split(u',', Qt::SkipEmptyParts);
  for (const QString &part : parts) {
    const QString item = part.trimmed().toLower();
    if (item == "all"_L1) ret |= Item::All;
    else if (item == "settings"_L1) ret |= Item::Settings;
    else if (item == "collection"_L1 || item == "library"_L1) ret |= Item::Collection;
    else if (item == "covers"_L1) ret |= Item::Covers;
    else valid = false;
  }
  if (ok) *ok = valid && ret != Item::None;
  return ret;

}

QString PlayerImporter::ItemsToString(const Items items) {

  QStringList list;
  if (items & Item::Settings) list << u"settings"_s;
  if (items & Item::Collection) list << u"collection"_s;
  if (items & Item::Covers) list << u"covers"_s;
  return list.join(u',');

}

QString PlayerImporter::AkousmaDataDir() {
  return StandardPaths::WritableLocation(StandardPaths::StandardLocation::AppLocalDataLocation);
}

QString PlayerImporter::AkousmaDatabaseFile() {
  // The same location Database uses.
  return QDir::fromNativeSeparators(QDir::toNativeSeparators(AkousmaDataDir()).replace(u"Akousma"_s, u"akousma"_s)) + u"/akousma.db"_s;
}

bool PlayerImporter::AkousmaHasData() {

  Settings s;
  const QStringList keys = s.allKeys();
  for (const QString &key : keys) {
    if (!key.startsWith(QLatin1String(kSettingsGroup) + u'/')) return true;
  }

  return QFile::exists(AkousmaDatabaseFile());

}

void PlayerImporter::AddCoverDirs(Installation *installation, const QStringList &dir_names) {

  for (const QString &dir_name : dir_names) {
    const QString path = installation->data_dir + u'/' + dir_name;
    QDir dir(path);
    if (dir.exists() && !dir.isEmpty(QDir::Files | QDir::NoDotAndDotDot)) {
      installation->cover_dirs << path;
    }
  }

}

std::optional<PlayerImporter::Installation> PlayerImporter::DetectStrawberry(const QString &name, const QString &settings_file, const QString &data_dir, const QString &cache_dir) {

  Installation installation;
  installation.player = Player::Strawberry;
  installation.name = name;
  installation.data_dir = data_dir;
  installation.cache_dir = cache_dir;

  if (settings_file.isEmpty()) {
    // Windows registry, HKEY_CURRENT_USER\Software\Strawberry\Strawberry
    installation.settings_org = u"Strawberry"_s;
    installation.settings_app = u"Strawberry"_s;
    QSettings s(QSettings::NativeFormat, QSettings::UserScope, installation.settings_org, installation.settings_app);
    installation.has_settings = !s.allKeys().isEmpty();
    installation.settings_location = QDir::toNativeSeparators(s.fileName());
  }
  else {
    installation.settings_file = settings_file;
    installation.config_dir = QFileInfo(settings_file).absolutePath();
    installation.has_settings = QFileInfo(settings_file).isFile();
    installation.settings_location = QDir::toNativeSeparators(settings_file);
  }

  // Strawberry lower cases its database directory on Windows and macOS, the file systems there are case insensitive.
  const QStringList database_files = QStringList() << data_dir + u"/strawberry.db"_s << data_dir.toLower() + u"/strawberry.db"_s;
  for (const QString &database_file : database_files) {
    if (QFileInfo(database_file).isFile()) {
      installation.database_file = database_file;
      break;
    }
  }

  AddCoverDirs(&installation, StrawberryCoverDirNames());

  if (!installation.IsValid()) return std::nullopt;

  return installation;

}

std::optional<PlayerImporter::Installation> PlayerImporter::DetectClementine(const QString &name, const QString &settings_file, const QString &config_dir) {

  Installation installation;
  installation.player = Player::Clementine;
  installation.name = name;
  installation.data_dir = config_dir;
  installation.config_dir = config_dir;

  if (settings_file.isEmpty()) {
    // Windows registry, HKEY_CURRENT_USER\Software\Clementine\Clementine
    installation.settings_org = u"Clementine"_s;
    installation.settings_app = u"Clementine"_s;
    QSettings s(QSettings::NativeFormat, QSettings::UserScope, installation.settings_org, installation.settings_app);
    installation.has_settings = !s.allKeys().isEmpty();
    installation.settings_location = QDir::toNativeSeparators(s.fileName());
  }
  else {
    installation.settings_file = settings_file;
    installation.has_settings = QFileInfo(settings_file).isFile();
    installation.settings_location = QDir::toNativeSeparators(settings_file);
  }

  const QString database_file = config_dir + u"/clementine.db"_s;
  if (QFileInfo(database_file).isFile()) {
    installation.database_file = database_file;
  }

  AddCoverDirs(&installation, QStringList() << QLatin1String(kClementineCoverDir));

  if (!installation.IsValid()) return std::nullopt;

  return installation;

}

QList<PlayerImporter::Installation> PlayerImporter::Detect() {

  QList<PlayerImporter::Installation> installations;
  const QString home = QDir::homePath();

  const auto add = [&installations](const std::optional<Installation> &installation) {
    if (installation.has_value()) installations << installation.value();
  };

#if defined(Q_OS_WIN)

  // Strawberry: settings in the registry, collection and covers in %LOCALAPPDATA%\Strawberry\Strawberry.
  const QString local_app_data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  add(DetectStrawberry(u"Strawberry"_s, QString(), local_app_data + u"/Strawberry/Strawberry"_s, local_app_data + u"/Strawberry/Strawberry/cache"_s));

  // Clementine: settings in the registry, the database and covers in %LOCALAPPDATA%\Clementine (1.4) or %USERPROFILE%\.config\Clementine (1.3 and older).
  std::optional<Installation> clementine = DetectClementine(u"Clementine"_s, QString(), local_app_data + u"/Clementine"_s);
  if (!clementine.has_value() || !clementine->has_database()) {
    std::optional<Installation> clementine_old = DetectClementine(u"Clementine"_s, QString(), home + u"/.config/Clementine"_s);
    if (clementine_old.has_value() && (clementine_old->has_database() || !clementine.has_value())) clementine = clementine_old;
  }
  add(clementine);

#elif defined(Q_OS_MACOS)

  const QString preferences = home + u"/Library/Preferences"_s;
  const QString application_support = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  const QString caches = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
  add(DetectStrawberry(u"Strawberry"_s, preferences + u"/org.strawberrymusicplayer.Strawberry.plist"_s, application_support + u"/Strawberry/Strawberry"_s, caches + u"/Strawberry/Strawberry"_s));
  add(DetectClementine(u"Clementine"_s, preferences + u"/org.clementine-player.Clementine.plist"_s, application_support + u"/Clementine"_s));

#else

  const auto xdg_dir = [&home](const char *variable, const QString &fallback) {
    const QString value = qEnvironmentVariable(variable);
    return value.startsWith(u'/') ? value : home + fallback;
  };
  const QString config_home = xdg_dir("XDG_CONFIG_HOME", u"/.config"_s);
  const QString data_home = xdg_dir("XDG_DATA_HOME", u"/.local/share"_s);
  const QString cache_home = xdg_dir("XDG_CACHE_HOME", u"/.cache"_s);

  add(DetectStrawberry(u"Strawberry"_s, config_home + u"/strawberry/strawberry.conf"_s, data_home + u"/strawberry/strawberry"_s, cache_home + u"/strawberry/strawberry"_s));
  const QString strawberry_flatpak = home + u"/.var/app/org.strawberrymusicplayer.strawberry"_s;
  add(DetectStrawberry(u"Strawberry (Flatpak)"_s, strawberry_flatpak + u"/config/strawberry/strawberry.conf"_s, strawberry_flatpak + u"/data/strawberry/strawberry"_s, strawberry_flatpak + u"/cache/strawberry/strawberry"_s));

  add(DetectClementine(u"Clementine"_s, config_home + u"/Clementine/Clementine.conf"_s, config_home + u"/Clementine"_s));
  const QString clementine_flatpak = home + u"/.var/app/org.clementine_player.Clementine/config/Clementine"_s;
  add(DetectClementine(u"Clementine (Flatpak)"_s, clementine_flatpak + u"/Clementine.conf"_s, clementine_flatpak));

#endif

  return installations;

}

std::optional<PlayerImporter::Installation> PlayerImporter::Detect(const Player player) {

  const QList<Installation> installations = Detect();
  for (const Installation &installation : installations) {
    if (installation.player == player) return installation;
  }

  return std::nullopt;

}

QSettings *PlayerImporter::OpenSettings(const Installation &installation) {

  if (installation.settings_file.isEmpty()) {
    return new QSettings(QSettings::NativeFormat, QSettings::UserScope, installation.settings_org, installation.settings_app);
  }

#ifdef Q_OS_MACOS
  return new QSettings(installation.settings_file, QSettings::NativeFormat);
#else
  return new QSettings(installation.settings_file, QSettings::IniFormat);
#endif

}

bool PlayerImporter::BackupSettings(const QString &backup_dir, QString *error) {

  Settings s;
  const QStringList keys = s.allKeys();
  if (keys.isEmpty()) return true;

  if (!QDir().mkpath(backup_dir)) {
    *error = tr("Could not create the backup directory %1.").arg(QDir::toNativeSeparators(backup_dir));
    return false;
  }

  // A copy of the settings file itself where there is one, and the values as INI file, which also covers the Windows registry.
  if (QFileInfo(s.fileName()).isFile()) {
    QFile::copy(s.fileName(), backup_dir + u'/' + QFileInfo(s.fileName()).fileName());
  }

  QSettings backup(backup_dir + u"/settings.ini"_s, QSettings::IniFormat);
  for (const QString &key : keys) {
    backup.setValue(key, s.value(key));
  }
  backup.sync();
  if (backup.status() != QSettings::NoError) {
    *error = tr("Could not write the settings backup to %1.").arg(QDir::toNativeSeparators(backup.fileName()));
    return false;
  }

  return true;

}

bool PlayerImporter::BackupDatabase(const QString &backup_dir, QString *error) {

  const QString database_file = AkousmaDatabaseFile();
  if (!QFile::exists(database_file)) return true;

  if (!QDir().mkpath(backup_dir)) {
    *error = tr("Could not create the backup directory %1.").arg(QDir::toNativeSeparators(backup_dir));
    return false;
  }

  if (!QFile::copy(database_file, backup_dir + u"/akousma.db"_s)) {
    *error = tr("Could not back up the collection database %1.").arg(QDir::toNativeSeparators(database_file));
    return false;
  }

  return true;

}

PlayerImporter::Result PlayerImporter::Import(const Installation &installation, const Items items) {

  Result result;

  const Items available = installation.available_items();
  const Items todo = items & available;
  if (todo == Item::None) {
    result.success = false;
    result.errors << tr("There is nothing to import from %1.").arg(installation.name);
    return result;
  }

  qLog(Info) << "Importing" << ItemsToString(todo) << "from" << installation.name;

  result.backup_dir = AkousmaDataDir() + u"/import-backup-"_s + QDateTime::currentDateTime().toString(u"yyyyMMdd-HHmmss"_s);

  QString error;
  if (todo & Item::Settings && !BackupSettings(result.backup_dir, &error)) {
    result.success = false;
    result.errors << error;
    return result;
  }
  if (todo & Item::Collection && !BackupDatabase(result.backup_dir, &error)) {
    result.success = false;
    result.errors << error;
    return result;
  }
  if (QDir(result.backup_dir).exists()) {
    result.messages << tr("The previous Akousma settings and collection were backed up to %1.").arg(QDir::toNativeSeparators(result.backup_dir));
  }
  else {
    result.backup_dir.clear();
  }

  // Covers first, so the cover paths in the collection can be pointed at the copied files.
  bool covers_copied = false;
  if (todo & Item::Covers) {
    covers_copied = ImportCovers(installation, &result);
    if (covers_copied) result.imported |= Item::Covers;
  }

  if (todo & Item::Collection) {
    const bool success = installation.player == Player::Strawberry ? ImportStrawberryDatabase(installation, covers_copied, &result) : ImportClementineDatabase(installation, covers_copied, &result);
    if (success) result.imported |= Item::Collection;
  }

  if (todo & Item::Settings) {
    const bool success = installation.player == Player::Strawberry ? ImportStrawberrySettings(installation, &result) : ImportClementineSettings(installation, &result);
    if (success) result.imported |= Item::Settings;
  }

  result.success = result.errors.isEmpty();

  {
    Settings s;
    s.beginGroup(kSettingsGroup);
    s.setValue(kFirstRunPromptDone, true);
    s.setValue(kLastImport, QStringLiteral("%1 %2 %3").arg(QDateTime::currentDateTime().toString(Qt::ISODate), PlayerToString(installation.player), ItemsToString(result.imported)));
    s.endGroup();
  }

  for (const QString &message : std::as_const(result.messages)) {
    qLog(Info) << message;
  }
  for (const QString &import_error : std::as_const(result.errors)) {
    qLog(Error) << import_error;
  }

  return result;

}

bool PlayerImporter::ImportStrawberrySettings(const Installation &installation, Result *result) {

  ScopedPtr<QSettings> source(OpenSettings(installation));
  const QStringList keys = source->allKeys();
  if (keys.isEmpty()) {
    result->errors << tr("Could not read the Strawberry settings from %1.").arg(installation.settings_location);
    return false;
  }

  // Strawberry and Akousma use the same settings, only paths into Strawberry's own directories have to point to Akousma's.
  PathPairs dirs;
  dirs << std::make_pair(installation.data_dir, AkousmaDataDir());
  dirs << std::make_pair(installation.data_dir.toLower(), QDir::fromNativeSeparators(QDir::toNativeSeparators(AkousmaDataDir()).replace(u"Akousma"_s, u"akousma"_s)));
  if (!installation.cache_dir.isEmpty()) {
    dirs << std::make_pair(installation.cache_dir, StandardPaths::WritableLocation(StandardPaths::StandardLocation::CacheLocation));
  }
  if (!installation.config_dir.isEmpty()) {
    Settings akousma_settings;
    dirs << std::make_pair(installation.config_dir, QFileInfo(akousma_settings.fileName()).absolutePath());
  }

  Settings s;
  s.clear();
  int rewritten = 0;
  for (const QString &key : keys) {
    if (key.startsWith(QLatin1String(kSettingsGroup) + u'/')) continue;
    bool changed = false;
    const QVariant value = RewriteSettingValue(source->value(key), dirs, &changed);
    if (changed) {
      ++rewritten;
      qLog(Debug) << "Rewrote the path in setting" << key;
    }
    s.setValue(key, value);
  }
  s.sync();

  if (s.status() != QSettings::NoError) {
    result->errors << tr("Could not write the Akousma settings to %1.").arg(QDir::toNativeSeparators(s.fileName()));
    return false;
  }

  result->messages << tr("Imported %1 settings from %2.").arg(keys.count()).arg(installation.settings_location);
  if (rewritten > 0) {
    result->messages << tr("Changed %1 paths in the settings from Strawberry's directories to Akousma's.").arg(rewritten);
  }
  result->messages << tr("Passwords Strawberry kept in the system keyring or credential store are not imported, enter them again in the settings.");

  return true;

}

QString PlayerImporter::ClementineLanguage(const QString &language) {

  if (language.isEmpty()) return QString();

  // Clementine uses language codes like "de" or "pt_BR", Akousma's translations are named like akousma_de_DE.qm.
  QStringList catalogs;
  for (const QString &path : std::initializer_list<QString> {u":/i18n"_s, QStringLiteral(TRANSLATIONS_DIR), QCoreApplication::applicationDirPath()}) {
    catalogs << QDir(path).entryList(QStringList() << u"akousma_*.qm"_s, QDir::Files, QDir::Name);
  }

  const QString exact = u"akousma_"_s + language + u".qm"_s;
  if (catalogs.contains(exact)) return language;
  for (const QString &catalog : std::as_const(catalogs)) {
    if (catalog.startsWith(u"akousma_"_s + language + u'_')) {
      return catalog.mid(8, catalog.length() - 8 - 3);
    }
  }

  return QString();

}

bool PlayerImporter::ImportClementineSettings(const Installation &installation, Result *result) {

  ScopedPtr<QSettings> source(OpenSettings(installation));
  if (source->allKeys().isEmpty()) {
    result->errors << tr("Could not read the Clementine settings from %1.").arg(installation.settings_location);
    return false;
  }

  Settings s;
  s.clear();

  int imported = 0;
  const QList<SettingMapping> mappings = ClementineSettingMappings();
  for (const SettingMapping &mapping : mappings) {
    const QString source_key = mapping.source_group + u'/' + mapping.source_key;
    QVariant value = source->value(source_key);
    if (!value.isValid()) {
      if (!mapping.clementine_default.isValid()) continue;
      value = mapping.clementine_default;
    }
    else {
      ++imported;
    }

    s.beginGroup(mapping.group);
    switch (mapping.conversion) {
      case Conversion::None:
        s.setValue(mapping.key, value);
        break;
      case Conversion::Invert:
        s.setValue(mapping.key, !value.toBool());
        break;
      case Conversion::PercentToFraction:
        s.setValue(mapping.key, value.toDouble() / 100.0);
        break;
      case Conversion::Language:{
        const QString language = ClementineLanguage(value.toString());
        if (!language.isEmpty()) s.setValue(mapping.key, language);
        break;
      }
      case Conversion::MonoToChannels:
        if (value.toBool()) {
          s.setValue(mapping.key, true);
          s.setValue(u"channels"_s, 1);
        }
        break;
    }
    s.endGroup();
  }
  s.sync();

  if (s.status() != QSettings::NoError) {
    result->errors << tr("Could not write the Akousma settings to %1.").arg(QDir::toNativeSeparators(s.fileName()));
    return false;
  }

  result->messages << tr("Imported %1 settings from %2. Clementine settings without an equivalent in Akousma, such as Last.fm, internet services, appearance and the equalizer, are not imported.").arg(imported).arg(installation.settings_location);

  return true;

}

bool PlayerImporter::ReplaceDatabaseFile(const QString &new_file, Result *result) {

  const QString database_file = AkousmaDatabaseFile();
  for (const QString &suffix : std::initializer_list<QString> {QString(), u"-wal"_s, u"-shm"_s, u"-journal"_s}) {
    if (QFile::exists(database_file + suffix) && !QFile::remove(database_file + suffix)) {
      result->errors << tr("Could not replace the collection database %1.").arg(QDir::toNativeSeparators(database_file + suffix));
      QFile::remove(new_file);
      return false;
    }
  }

  if (!QFile::rename(new_file, database_file)) {
    if (!QFile::copy(new_file, database_file)) {
      result->errors << tr("Could not write the collection database %1.").arg(QDir::toNativeSeparators(database_file));
      QFile::remove(new_file);
      return false;
    }
    QFile::remove(new_file);
  }

  return true;

}

int PlayerImporter::RewriteCoverUrls(QSqlDatabase &db, const QString &old_dir, const QString &new_dir, Result *result) {

  int rewritten = 0;

  const QStringList tables = db.tables(QSql::Tables);
  for (const QString &table : tables) {
    const QSet<QString> columns = TableColumns(db, u"main"_s, table);
    for (const QString &column : std::initializer_list<QString> {u"art_automatic"_s, u"art_manual"_s}) {
      if (!columns.contains(column)) continue;
      QList<std::pair<qint64, QString>> updates;
      {
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("SELECT ROWID, %1 FROM %2 WHERE %1 LIKE 'file:%'").arg(column, table))) {
          result->errors << QueryError(q);
          return rewritten;
        }
        while (q.next()) {
          const QUrl url = QUrl::fromEncoded(q.value(1).toString().toUtf8());
          if (!url.isLocalFile()) continue;
          QString path = url.toLocalFile();
          if (RewritePath(&path, old_dir, new_dir)) {
            updates << std::make_pair(q.value(0).toLongLong(), QString::fromUtf8(QUrl::fromLocalFile(path).toEncoded()));
          }
        }
      }
      QSqlQuery update(db);
      update.prepare(QStringLiteral("UPDATE %1 SET %2 = :url WHERE ROWID = :rowid").arg(table, column));
      for (const std::pair<qint64, QString> &u : std::as_const(updates)) {
        update.bindValue(u":url"_s, u.second);
        update.bindValue(u":rowid"_s, u.first);
        if (!update.exec()) {
          result->errors << QueryError(update);
          return rewritten;
        }
        ++rewritten;
      }
    }
  }

  return rewritten;

}

bool PlayerImporter::ImportStrawberryDatabase(const Installation &installation, const bool covers, Result *result) {

  const QString database_file = AkousmaDatabaseFile();
  if (!QDir().mkpath(QFileInfo(database_file).absolutePath())) {
    result->errors << tr("Could not create the directory for the collection database %1.").arg(QDir::toNativeSeparators(database_file));
    return false;
  }

  const QString new_file = database_file + u".import"_s;
  QFile::remove(new_file);

  int songs = 0;
  int playlists = 0;
  int covers_rewritten = 0;
  bool success = true;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, QLatin1String(kConnectionName));
    db.setConnectOptions(u"QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=30000"_s);
    db.setDatabaseName(installation.database_file);
    if (db.open()) {
      QSqlQuery q(db);
      int schema_version = 0;
      if (q.exec(u"SELECT version FROM schema_version"_s) && q.next()) {
        schema_version = q.value(0).toInt();
      }
      q.finish();
      if (schema_version > Database::kSchemaVersion) {
        result->errors << tr("The Strawberry collection database has schema version %1, which is newer than the version %2 this Akousma supports. Update Akousma and import again.").arg(schema_version).arg(Database::kSchemaVersion);
        success = false;
      }
      // VACUUM INTO makes a consistent copy, also when Strawberry is running and has changes in its write ahead log.
      else if (!q.exec(u"VACUUM INTO "_s + SqlString(new_file))) {
        qLog(Warning) << "VACUUM INTO failed, copying the file instead:" << q.lastError().text();
        q.finish();
        db.close();
        QFile::remove(new_file);
        if (!QFile::copy(installation.database_file, new_file)) {
          result->errors << tr("Could not copy the Strawberry collection database %1.").arg(QDir::toNativeSeparators(installation.database_file));
          success = false;
        }
      }
      db.close();
    }
    else {
      result->errors << tr("Could not open the Strawberry collection database %1: %2").arg(QDir::toNativeSeparators(installation.database_file), db.lastError().text());
      success = false;
    }
  }
  QSqlDatabase::removeDatabase(QLatin1String(kConnectionName));

  if (success) {
    QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, QLatin1String(kConnectionName));
    db.setDatabaseName(new_file);
    if (db.open()) {
      songs = CountRows(db, u"songs"_s);
      playlists = CountRows(db, u"playlists"_s);
      if (covers) {
        db.transaction();
        covers_rewritten = RewriteCoverUrls(db, installation.data_dir, AkousmaDataDir(), result);
        if (installation.data_dir.toLower() != installation.data_dir) {
          covers_rewritten += RewriteCoverUrls(db, installation.data_dir.toLower(), AkousmaDataDir(), result);
        }
        db.commit();
      }
      db.close();
    }
    else {
      result->errors << tr("Could not open the copied collection database: %1").arg(db.lastError().text());
      success = false;
    }
  }
  QSqlDatabase::removeDatabase(QLatin1String(kConnectionName));

  if (!success) {
    QFile::remove(new_file);
    return false;
  }

  if (!ReplaceDatabaseFile(new_file, result)) return false;

  result->messages << tr("Imported the Strawberry collection with %1 songs and %2 playlists, including play counts, ratings and the collection folders.").arg(songs).arg(playlists);
  if (covers_rewritten > 0) {
    result->messages << tr("Pointed %1 album covers in the collection to Akousma's cover cache.").arg(covers_rewritten);
  }

  return true;

}

bool PlayerImporter::ImportClementineDatabase(const Installation &installation, const bool covers, Result *result) {

  const QString database_file = AkousmaDatabaseFile();
  if (!QDir().mkpath(QFileInfo(database_file).absolutePath())) {
    result->errors << tr("Could not create the directory for the collection database %1.").arg(QDir::toNativeSeparators(database_file));
    return false;
  }

  const QString new_file = database_file + u".import"_s;
  QFile::remove(new_file);

  // Create an empty Akousma database with the current schema.
  {
    Database database(nullptr, nullptr, new_file);
    database.Close();
  }

  QStringList errors;
  int songs = 0;
  int playlists = 0;
  int playlist_items = 0;
  int covers_converted = 0;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, QLatin1String(kConnectionName));
    db.setConnectOptions(u"QSQLITE_BUSY_TIMEOUT=30000"_s);
    db.setDatabaseName(new_file);
    if (db.open()) {
      QSqlQuery q(db);
      if (q.exec(u"ATTACH DATABASE "_s + SqlString(installation.database_file) + u" AS clementine"_s)) {
        q.finish();
        const QSet<QString> song_columns = TableColumns(db, u"clementine"_s, u"songs"_s);
        const QSet<QString> playlist_columns = TableColumns(db, u"clementine"_s, u"playlists"_s);
        const QSet<QString> item_columns = TableColumns(db, u"clementine"_s, u"playlist_items"_s);
        if (!song_columns.contains(u"filename"_s) || !song_columns.contains(u"directory"_s)) {
          errors << tr("%1 is not a Clementine collection database.").arg(QDir::toNativeSeparators(installation.database_file));
        }
        else {
          db.transaction();
          const auto exec = [&db, &errors](const QString &sql) {
            if (!errors.isEmpty()) return;
            QSqlQuery query(db);
            if (!query.exec(sql)) errors << QueryError(query);
          };

          // Keep the ROWIDs, the songs refer to the directories and the playlist items to the songs by ROWID.
          exec(u"INSERT INTO directories (ROWID, path, subdirs) SELECT ROWID, path, subdirs FROM clementine.directories"_s);
          exec(u"INSERT INTO subdirectories (directory_id, path, mtime) SELECT directory, path, mtime FROM clementine.subdirectories"_s);

          QStringList destination;
          QStringList expressions;
          SongColumnExpressions(song_columns, u"c"_s, &destination, &expressions);
          exec(QStringLiteral("INSERT OR IGNORE INTO songs (ROWID, source, artist_id, album_id, song_id, %1) SELECT c.ROWID, 2, '', '', '', %2 FROM clementine.songs AS c WHERE c.unavailable = 0").arg(destination.join(u", "_s), expressions.join(u", "_s)));

          // Smart playlists from Clementine use a different generator format, only normal playlists are imported.
          QStringList playlist_destination = QStringList() << u"name"_s;
          QStringList playlist_expressions = QStringList() << u"COALESCE(p.name, '')"_s;
          const QList<std::pair<QString, QString>> optional_playlist_columns = QList<std::pair<QString, QString>>() << std::make_pair(u"last_played"_s, u"-1"_s) << std::make_pair(u"ui_order"_s, u"0"_s) << std::make_pair(u"special_type"_s, QString()) << std::make_pair(u"ui_path"_s, QString()) << std::make_pair(u"is_favorite"_s, u"0"_s);
          for (const std::pair<QString, QString> &column : optional_playlist_columns) {
            if (!playlist_columns.contains(column.first)) continue;
            playlist_destination << column.first;
            playlist_expressions << (column.second.isEmpty() ? u"p."_s + column.first : QStringLiteral("COALESCE(p.%1, %2)").arg(column.first, column.second));
          }
          const QString dynamic_filter = playlist_columns.contains(u"dynamic_playlist_type"_s) ? u" WHERE p.dynamic_playlist_type IS NULL"_s : QString();
          exec(QStringLiteral("INSERT INTO playlists (ROWID, %1) SELECT p.ROWID, %2 FROM clementine.playlists AS p%3").arg(playlist_destination.join(u", "_s), playlist_expressions.join(u", "_s), dynamic_filter));

          // Collection songs, local files and streams. Items from Clementine's internet services can not be played by Akousma.
          QStringList item_destination;
          QStringList item_expressions;
          SongColumnExpressions(item_columns, u"i"_s, &item_destination, &item_expressions);
          exec(QStringLiteral("INSERT INTO playlist_items (playlist, type, source, collection_id, %1) "
                              "SELECT i.playlist, "
                              "CASE i.type WHEN 'Library' THEN 2 WHEN 'File' THEN 1 ELSE 5 END, "
                              "CASE i.type WHEN 'Library' THEN 2 WHEN 'File' THEN 1 ELSE 5 END, "
                              "CASE i.type WHEN 'Library' THEN i.library_id ELSE NULL END, "
                              "%2 FROM clementine.playlist_items AS i "
                              "WHERE i.type IN ('Library', 'File', 'Stream') AND i.playlist IN (SELECT ROWID FROM main.playlists) "
                              "ORDER BY i.ROWID").arg(item_destination.join(u", "_s), item_expressions.join(u", "_s)));

          if (errors.isEmpty()) {
            const QString old_cover_dir = covers ? installation.data_dir + u'/' + QLatin1String(kClementineCoverDir) : QString();
            const QString new_cover_dir = covers ? AkousmaDataDir() + u'/' + QLatin1String(kAkousmaCollectionCoverDir) : QString();
            covers_converted += ConvertClementineCovers(db, u"songs"_s, old_cover_dir, new_cover_dir, &errors);
            covers_converted += ConvertClementineCovers(db, u"playlist_items"_s, old_cover_dir, new_cover_dir, &errors);
          }

          if (errors.isEmpty()) {
            db.commit();
            songs = CountRows(db, u"songs"_s);
            playlists = CountRows(db, u"playlists"_s);
            playlist_items = CountRows(db, u"playlist_items"_s);
          }
          else {
            db.rollback();
          }
        }
        QSqlQuery detach(db);
        detach.exec(u"DETACH DATABASE clementine"_s);
      }
      else {
        errors << tr("Could not open the Clementine collection database %1: %2").arg(QDir::toNativeSeparators(installation.database_file), q.lastError().text());
      }
      db.close();
    }
    else {
      errors << tr("Could not create the collection database: %1").arg(db.lastError().text());
    }
  }
  QSqlDatabase::removeDatabase(QLatin1String(kConnectionName));

  if (!errors.isEmpty()) {
    result->errors << errors;
    QFile::remove(new_file);
    return false;
  }

  if (!ReplaceDatabaseFile(new_file, result)) return false;

  result->messages << tr("Imported the Clementine library with %1 songs, %2 playlists and %3 playlist entries, including play counts, ratings and the library folders.").arg(songs).arg(playlists).arg(playlist_items);
  if (covers_converted > 0) {
    result->messages << tr("Converted the album covers of %1 songs and playlist entries.").arg(covers_converted);
  }

  return true;

}

int PlayerImporter::CopyDirectory(const QString &source, const QString &destination, QString *error) {

  if (!QDir().mkpath(destination)) {
    *error = tr("Could not create the directory %1.").arg(QDir::toNativeSeparators(destination));
    return -1;
  }

  int copied = 0;
  QDirIterator it(source, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString source_file = it.next();
    const QString destination_file = destination + source_file.mid(source.length());
    // Covers are named after a hash of the album, an existing file is the same cover.
    if (QFile::exists(destination_file)) continue;
    QDir().mkpath(QFileInfo(destination_file).absolutePath());
    if (QFile::copy(source_file, destination_file)) {
      ++copied;
    }
    else {
      qLog(Warning) << "Could not copy" << source_file << "to" << destination_file;
    }
  }

  return copied;

}

bool PlayerImporter::ImportCovers(const Installation &installation, Result *result) {

  int copied = 0;
  for (const QString &source : installation.cover_dirs) {
    const QString dir_name = QFileInfo(source).fileName();
    const QString destination = AkousmaDataDir() + u'/' + (installation.player == Player::Clementine ? QLatin1String(kAkousmaCollectionCoverDir) : dir_name);
    QString error;
    const int count = CopyDirectory(source, destination, &error);
    if (count < 0) {
      result->errors << error;
      return false;
    }
    copied += count;
  }

  result->messages << tr("Copied %1 album covers from %2.").arg(copied).arg(installation.name);

  return true;

}

void PlayerImporter::SetPendingImport(const Installation &installation, const Items items) {

  Settings s;
  s.beginGroup(kSettingsGroup);
  s.setValue(kPendingPlayer, PlayerToString(installation.player));
  s.setValue(kPendingName, installation.name);
  s.setValue(kPendingItems, ItemsToString(items));
  s.endGroup();
  s.sync();

}

std::optional<PlayerImporter::Installation> PlayerImporter::TakePendingImport(Items *items) {

  Settings s;
  s.beginGroup(kSettingsGroup);
  const std::optional<Player> player = PlayerFromString(s.value(kPendingPlayer).toString());
  const QString name = s.value(kPendingName).toString();
  bool ok = false;
  *items = ItemsFromString(s.value(kPendingItems).toString(), &ok);
  s.remove(kPendingPlayer);
  s.remove(kPendingName);
  s.remove(kPendingItems);
  s.endGroup();
  s.sync();

  if (!player.has_value() || !ok) return std::nullopt;

  // The installation the user picked, or another one of the same player when it is gone.
  const QList<Installation> installations = Detect();
  for (const Installation &installation : installations) {
    if (installation.player == player.value() && installation.name == name) return installation;
  }
  for (const Installation &installation : installations) {
    if (installation.player == player.value()) return installation;
  }

  return std::nullopt;

}

std::optional<PlayerImporter::Installation> PlayerImporter::FindInstallation(const QString &player_option, QString *error) {

  if (player_option.compare("auto"_L1, Qt::CaseInsensitive) == 0) {
    const QList<Installation> installations = Detect();
    if (installations.isEmpty()) {
      *error = tr("No Strawberry or Clementine settings or collection were found.");
      return std::nullopt;
    }
    return installations.first();
  }

  const std::optional<Player> player = PlayerFromString(player_option);
  if (!player.has_value()) {
    *error = tr("Unknown player \"%1\", use strawberry, clementine or auto.").arg(player_option);
    return std::nullopt;
  }

  const std::optional<Installation> installation = Detect(player.value());
  if (!installation.has_value()) {
    *error = tr("No %1 settings or collection were found.").arg(PlayerName(player.value()));
  }

  return installation;

}

int PlayerImporter::ImportQuiet(const QString &player_option, const QString &items_option) {

  bool ok = true;
  const Items items = items_option.isEmpty() ? Items(Item::All) : ItemsFromString(items_option, &ok);
  if (!ok) {
    qLog(Error) << "Invalid --import-items" << items_option << "- use a comma separated list of settings, collection, covers or all.";
    return 2;
  }

  QString error;
  const std::optional<Installation> installation = FindInstallation(player_option, &error);
  if (!installation.has_value()) {
    // Nothing to import is not an error for the installer, which offers the import without knowing what is installed.
    qLog(Info) << error;
    SetFirstRunPromptDone();
    return 0;
  }

  const Result result = Import(installation.value(), items);

  return result.success ? 0 : 1;

}

bool PlayerImporter::FirstRunPromptDone() {

  Settings s;
  s.beginGroup(kSettingsGroup);
  const bool done = s.value(kFirstRunPromptDone, false).toBool();
  s.endGroup();
  return done;

}

void PlayerImporter::SetFirstRunPromptDone() {

  Settings s;
  s.beginGroup(kSettingsGroup);
  s.setValue(kFirstRunPromptDone, true);
  s.endGroup();

}

void PlayerImporter::SetRestartRequested(const bool restart) {
  sRestartRequested = restart;
}

bool PlayerImporter::RestartRequested() {
  return sRestartRequested;
}
