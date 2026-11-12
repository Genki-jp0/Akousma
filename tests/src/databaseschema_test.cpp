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

#include <QList>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariant>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>

#include "includes/shared_ptr.h"
#include "core/database.h"
#include "core/databasehealth.h"

using namespace Qt::Literals::StringLiterals;

// clazy:excludeall=returning-void-expression

namespace {

constexpr char kConnectionName[] = "databaseschema_test";

DatabaseHealth::State OpenWithDatabase(const QString &filename) {

  Database database(SharedPtr<TaskManager>(), nullptr, filename);
  const DatabaseHealth::State state = database.open_state();
  database.Close();
  return state;

}

// Runs the statements on the database file, returns the first error.
QString Exec(const QString &filename, const QStringList &statements) {

  QString error;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, QLatin1String(kConnectionName));
    db.setDatabaseName(filename);
    if (!db.open()) {
      error = db.lastError().text();
    }
    for (const QString &statement : statements) {
      if (!error.isEmpty()) break;
      QSqlQuery q(db);
      if (!q.exec(statement)) error = q.lastError().text() + u": "_s + statement;
    }
    db.close();
  }
  QSqlDatabase::removeDatabase(QLatin1String(kConnectionName));

  return error;

}

QList<QVariantList> Select(const QString &filename, const QString &statement) {

  QList<QVariantList> rows;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, QLatin1String(kConnectionName));
    db.setDatabaseName(filename);
    if (db.open()) {
      QSqlQuery q(db);
      if (q.exec(statement)) {
        while (q.next()) {
          QVariantList row;
          for (int i = 0; i < q.record().count(); ++i) row << q.value(i);
          rows << row;
        }
      }
    }
    db.close();
  }
  QSqlDatabase::removeDatabase(QLatin1String(kConnectionName));

  return rows;

}

}  // namespace

// Schema 31 gives the collection songs an id that is never used again, because playlists refer to them by it.
TEST(DatabaseSchemaTest, SongIdsAreKeptAndNotReused) {

  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString filename = dir.filePath(u"akousma.db"_s);

  // Make a database of schema 30: the current one, with the songs table as it was before, without the rowid column.
  ASSERT_EQ(DatabaseHealth::State::Ok, OpenWithDatabase(filename));
  const QList<QVariantList> songs_sql = Select(filename, u"SELECT sql FROM sqlite_master WHERE type = 'table' AND name = 'songs'"_s);
  ASSERT_EQ(1, songs_sql.count());
  QString create_v30 = songs_sql.first().value(0).toString();
  ASSERT_TRUE(create_v30.contains(u"rowid INTEGER PRIMARY KEY AUTOINCREMENT,"_s));
  create_v30.replace(u"rowid INTEGER PRIMARY KEY AUTOINCREMENT,"_s, QString());
  create_v30.replace(u"CREATE TABLE songs ("_s, u"CREATE TABLE songs_v30 ("_s);
  ASSERT_TRUE(create_v30.startsWith(u"CREATE TABLE songs_v30 ("_s));
  ASSERT_EQ(QString(), Exec(filename, QStringList()
                                          << u"DROP VIEW duplicated_songs"_s
                                          << create_v30
                                          << u"DROP TABLE songs"_s
                                          << u"ALTER TABLE songs_v30 RENAME TO songs"_s
                                          << u"CREATE UNIQUE INDEX idx_songs_url_beginning_cue_path ON songs (url, beginning, IFNULL(cue_path, ''))"_s
                                          << u"UPDATE schema_version SET version = 30"_s));

  QStringList inserts;
  for (int i = 1; i <= 10; ++i) {
    inserts << QStringLiteral("INSERT INTO songs (title, url, source, directory_id) VALUES ('Song %1', 'file:///music/%1.flac', 2, 1)").arg(i);
  }
  inserts << u"INSERT INTO playlists (name) VALUES ('Playlist')"_s;
  // A playlist entry still refers to song 10, which is removed below.
  inserts << u"INSERT INTO playlist_items (playlist, type, source, collection_id) VALUES (1, 2, 2, 10)"_s;
  inserts << u"DELETE FROM songs WHERE ROWID IN (5, 9, 10)"_s;
  ASSERT_EQ(QString(), Exec(filename, inserts));

  const QList<QVariantList> before = Select(filename, u"SELECT ROWID, title FROM songs ORDER BY ROWID"_s);
  ASSERT_EQ(7, before.count());

  // Opening the database runs the schema update.
  ASSERT_EQ(DatabaseHealth::State::Ok, OpenWithDatabase(filename));
  EXPECT_EQ(Database::kSchemaVersion, DatabaseHealth::ProbeFile(filename).schema_version);
  EXPECT_EQ(DatabaseHealth::State::Ok, DatabaseHealth::ProbeFile(filename, true).state);

  // The songs keep their ids, the playlists still find them.
  EXPECT_EQ(before, Select(filename, u"SELECT ROWID, title FROM songs ORDER BY ROWID"_s));

  // New songs get ids that were never used, also not the one of song 10 the playlist entry still refers to.
  ASSERT_EQ(QString(), Exec(filename, QStringList() << u"INSERT INTO songs (title, url) VALUES ('New song', 'file:///music/new.flac')"_s));
  const QList<QVariantList> new_song = Select(filename, u"SELECT ROWID FROM songs WHERE title = 'New song'"_s);
  ASSERT_EQ(1, new_song.count());
  EXPECT_EQ(11, new_song.first().value(0).toInt());

  ASSERT_EQ(QString(), Exec(filename, QStringList() << u"DELETE FROM songs WHERE title = 'New song'"_s << u"INSERT INTO songs (title, url) VALUES ('Another song', 'file:///music/another.flac')"_s));
  const QList<QVariantList> another_song = Select(filename, u"SELECT ROWID FROM songs WHERE title = 'Another song'"_s);
  ASSERT_EQ(1, another_song.count());
  EXPECT_EQ(12, another_song.first().value(0).toInt());

  // The indexes and the view are back.
  const QList<QVariantList> objects = Select(filename, u"SELECT name FROM sqlite_master WHERE name IN ('idx_songs_url_beginning_cue_path', 'idx_title', 'duplicated_songs')"_s);
  EXPECT_EQ(3, objects.count());

}

// A new database has the same songs table as an updated one.
TEST(DatabaseSchemaTest, NewDatabaseHasSongIds) {

  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString filename = dir.filePath(u"akousma.db"_s);

  ASSERT_EQ(DatabaseHealth::State::Ok, OpenWithDatabase(filename));
  const QList<QVariantList> sql = Select(filename, u"SELECT sql FROM sqlite_master WHERE type = 'table' AND name = 'songs'"_s);
  ASSERT_EQ(1, sql.count());
  EXPECT_TRUE(sql.first().value(0).toString().contains(u"rowid INTEGER PRIMARY KEY AUTOINCREMENT"_s));

  ASSERT_EQ(QString(), Exec(filename, QStringList() << u"INSERT INTO songs (title, url) VALUES ('Song', 'file:///music/1.flac')"_s << u"DELETE FROM songs"_s << u"INSERT INTO songs (title, url) VALUES ('Song', 'file:///music/1.flac')"_s));
  const QList<QVariantList> ids = Select(filename, u"SELECT ROWID FROM songs"_s);
  ASSERT_EQ(1, ids.count());
  EXPECT_EQ(2, ids.first().value(0).toInt());

}
