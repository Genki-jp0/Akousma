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
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTime>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "includes/shared_ptr.h"
#include "core/database.h"
#include "core/databasehealth.h"
#include "core/databasestartupcheck.h"

using namespace Qt::Literals::StringLiterals;

// clazy:excludeall=non-pod-global-static,returning-void-expression

namespace {

using State = DatabaseHealth::State;
using Action = DatabaseHealth::Action;

// Not an SQLite database: the header doesn't start with "SQLite format 3".
QByteArray GarbageData() {
  QByteArray data;
  while (data.size() < 8192) data += "This is not an SQLite database.\n";
  return data;
}

bool WriteFile(const QString &filename, const QByteArray &data) {
  QFile file(filename);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  return file.write(data) == data.size();
}

QByteArray ReadFile(const QString &filename) {
  QFile file(filename);
  if (!file.open(QIODevice::ReadOnly)) return QByteArray();
  return file.readAll();
}

// Creates an SQLite database with the given tables, and a schema_version table with the version unless it is -1.
bool CreateDatabase(const QString &filename, const int schema_version, const QStringList &tables) {

  bool ok = true;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, u"databasehealth_test"_s);
    db.setDatabaseName(filename);
    ok = db.open();
    if (ok && schema_version != -1) {
      QSqlQuery q(db);
      ok = q.exec(u"CREATE TABLE schema_version (version INTEGER NOT NULL)"_s) && q.exec(QStringLiteral("INSERT INTO schema_version (version) VALUES (%1)").arg(schema_version));
    }
    for (const QString &table : tables) {
      if (!ok) break;
      QSqlQuery q(db);
      ok = q.exec(QStringLiteral("CREATE TABLE %1 (title TEXT)").arg(table));
    }
    db.close();
  }
  QSqlDatabase::removeDatabase(u"databasehealth_test"_s);

  return ok;

}

int ReadVersion(const QString &filename) {

  int version = -1;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, u"databasehealth_test_version"_s);
    db.setDatabaseName(filename);
    if (db.open()) {
      DatabaseHealth::ReadSchemaVersion(db, &version);
    }
    db.close();
  }
  QSqlDatabase::removeDatabase(u"databasehealth_test_version"_s);

  return version;

}

DatabaseHealth::Facts ReadableFacts(const int table_count, const bool has_version, const int version) {

  DatabaseHealth::Facts facts;
  facts.exists = true;
  facts.opened = true;
  facts.catalogue_readable = true;
  facts.table_count = table_count;
  facts.schema_version_readable = has_version;
  facts.schema_version = version;
  return facts;

}

State OpenWithDatabase(const QString &filename, QString *error = nullptr) {

  Database database(SharedPtr<TaskManager>(), nullptr, filename);
  const State state = database.open_state();
  if (error) *error = database.open_error();
  database.Close();
  return state;

}

}  // namespace

TEST(DatabaseHealthTest, ClassifyTellsNoTablesFromCannotRead) {

  DatabaseHealth::Facts facts;
  facts.exists = false;
  EXPECT_EQ(State::Missing, DatabaseHealth::Classify(facts, 10, 30));

  facts.exists = true;
  facts.opened = false;
  EXPECT_EQ(State::CannotOpen, DatabaseHealth::Classify(facts, 10, 30));

  // Opened, but the catalogue could not be read: this is not a new database.
  facts.opened = true;
  facts.catalogue_readable = false;
  EXPECT_EQ(State::Unreadable, DatabaseHealth::Classify(facts, 10, 30));

  EXPECT_EQ(State::Empty, DatabaseHealth::Classify(ReadableFacts(0, false, 0), 10, 30));
  EXPECT_EQ(State::NoSchemaVersion, DatabaseHealth::Classify(ReadableFacts(3, false, 0), 10, 30));
  EXPECT_EQ(State::TooOld, DatabaseHealth::Classify(ReadableFacts(3, true, 9), 10, 30));
  EXPECT_EQ(State::NeedsUpgrade, DatabaseHealth::Classify(ReadableFacts(3, true, 10), 10, 30));
  EXPECT_EQ(State::NeedsUpgrade, DatabaseHealth::Classify(ReadableFacts(3, true, 29), 10, 30));
  EXPECT_EQ(State::Ok, DatabaseHealth::Classify(ReadableFacts(3, true, 30), 10, 30));
  EXPECT_EQ(State::TooNew, DatabaseHealth::Classify(ReadableFacts(3, true, 31), 10, 30));

  DatabaseHealth::Facts damaged = ReadableFacts(3, true, 30);
  damaged.integrity_checked = true;
  damaged.integrity_ok = false;
  EXPECT_EQ(State::Corrupt, DatabaseHealth::Classify(damaged, 10, 30));
  damaged.integrity_ok = true;
  EXPECT_EQ(State::Ok, DatabaseHealth::Classify(damaged, 10, 30));

}

TEST(DatabaseHealthTest, UsableStates) {

  for (const State state : {State::Missing, State::Empty, State::Ok, State::NeedsUpgrade, State::TooNew}) {
    EXPECT_TRUE(DatabaseHealth::IsUsable(state));
    EXPECT_TRUE(DatabaseHealth::Actions(state, true).isEmpty() == (state != State::TooNew));
  }
  for (const State state : {State::TooOld, State::NoSchemaVersion, State::Unreadable, State::Corrupt, State::CannotOpen, State::UpgradeFailed}) {
    EXPECT_FALSE(DatabaseHealth::IsUsable(state));
    EXPECT_FALSE(DatabaseHealth::Actions(state, true).isEmpty());
  }

}

TEST(DatabaseHealthTest, ActionsOfferQuitFirstAndTheBackupOnlyWhenUsable) {

  for (const State state : {State::TooOld, State::NoSchemaVersion, State::Unreadable, State::Corrupt, State::CannotOpen, State::UpgradeFailed, State::TooNew}) {
    const QList<Action> with_backup = DatabaseHealth::Actions(state, true);
    const QList<Action> without_backup = DatabaseHealth::Actions(state, false);
    ASSERT_FALSE(with_backup.isEmpty());
    EXPECT_EQ(Action::Quit, with_backup.first());
    EXPECT_EQ(Action::Quit, without_backup.first());
    EXPECT_FALSE(without_backup.contains(Action::RestoreBackup));
    EXPECT_EQ(state == State::TooNew, with_backup.contains(Action::OpenAnyway));
  }

  EXPECT_EQ(QList<Action>() << Action::Quit << Action::Retry << Action::RestoreBackup << Action::MoveAside, DatabaseHealth::Actions(State::Unreadable, true));
  EXPECT_EQ(QList<Action>() << Action::Quit << Action::Retry << Action::MoveAside, DatabaseHealth::Actions(State::UpgradeFailed, false));
  EXPECT_EQ(QList<Action>() << Action::Quit << Action::MoveAside, DatabaseHealth::Actions(State::TooOld, false));
  // A file SQLite can't open at all is not moved aside, the cause is usually the folder.
  EXPECT_EQ(QList<Action>() << Action::Quit << Action::Retry, DatabaseHealth::Actions(State::CannotOpen, true));

}

TEST(DatabaseHealthTest, ProbeFile) {

  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());

  const QString missing = dir.filePath(u"missing.db"_s);
  EXPECT_EQ(State::Missing, DatabaseHealth::ProbeFile(missing).state);
  // Probing doesn't create the file.
  EXPECT_FALSE(QFileInfo::exists(missing));

  const QString empty = dir.filePath(u"empty.db"_s);
  ASSERT_TRUE(WriteFile(empty, QByteArray()));
  EXPECT_EQ(State::Empty, DatabaseHealth::ProbeFile(empty).state);

  const QString garbage = dir.filePath(u"garbage.db"_s);
  ASSERT_TRUE(WriteFile(garbage, GarbageData()));
  const DatabaseHealth::Probe garbage_probe = DatabaseHealth::ProbeFile(garbage);
  EXPECT_EQ(State::Unreadable, garbage_probe.state);
  EXPECT_FALSE(garbage_probe.error.isEmpty());
  EXPECT_EQ(State::Unreadable, DatabaseHealth::ProbeFile(garbage, true).state);

  const QString no_version = dir.filePath(u"noversion.db"_s);
  ASSERT_TRUE(CreateDatabase(no_version, -1, QStringList() << u"songs"_s));
  EXPECT_EQ(State::NoSchemaVersion, DatabaseHealth::ProbeFile(no_version).state);

  const QString too_old = dir.filePath(u"tooold.db"_s);
  ASSERT_TRUE(CreateDatabase(too_old, Database::kMinSupportedSchemaVersion - 1, QStringList() << u"songs"_s));
  const DatabaseHealth::Probe too_old_probe = DatabaseHealth::ProbeFile(too_old);
  EXPECT_EQ(State::TooOld, too_old_probe.state);
  EXPECT_EQ(Database::kMinSupportedSchemaVersion - 1, too_old_probe.schema_version);

  const QString older = dir.filePath(u"older.db"_s);
  ASSERT_TRUE(CreateDatabase(older, Database::kSchemaVersion - 1, QStringList() << u"songs"_s));
  EXPECT_EQ(State::NeedsUpgrade, DatabaseHealth::ProbeFile(older).state);

  const QString current = dir.filePath(u"current.db"_s);
  ASSERT_TRUE(CreateDatabase(current, Database::kSchemaVersion, QStringList() << u"songs"_s));
  const DatabaseHealth::Probe current_probe = DatabaseHealth::ProbeFile(current, true);
  EXPECT_EQ(State::Ok, current_probe.state);
  EXPECT_TRUE(current_probe.tables.contains(u"songs"_s));
  EXPECT_TRUE(current_probe.error.isEmpty());

  const QString too_new = dir.filePath(u"toonew.db"_s);
  ASSERT_TRUE(CreateDatabase(too_new, Database::kSchemaVersion + 1, QStringList() << u"songs"_s));
  EXPECT_EQ(State::TooNew, DatabaseHealth::ProbeFile(too_new).state);

}

TEST(DatabaseHealthTest, ImportProblem) {

  const QStringList required = QStringList() << u"songs"_s << u"playlists"_s;

  DatabaseHealth::Probe probe;
  probe.state = State::Ok;
  probe.schema_version = Database::kSchemaVersion;
  probe.tables = QStringList() << u"schema_version"_s << u"songs"_s << u"playlists"_s;
  EXPECT_TRUE(DatabaseHealth::ImportProblem(probe, required).isEmpty());

  probe.state = State::NeedsUpgrade;
  EXPECT_TRUE(DatabaseHealth::ImportProblem(probe, required).isEmpty());

  probe.tables.removeAll(u"playlists"_s);
  EXPECT_TRUE(DatabaseHealth::ImportProblem(probe, required).contains(u"playlists"_s));

  // A file of 0 bytes is an empty database, it must not replace the collection.
  for (const State state : {State::Missing, State::Empty, State::TooNew, State::TooOld, State::NoSchemaVersion, State::Unreadable, State::Corrupt, State::CannotOpen}) {
    probe.state = state;
    EXPECT_FALSE(DatabaseHealth::ImportProblem(probe, QStringList()).isEmpty());
  }

}

TEST(DatabaseHealthTest, MoveAsideFilename) {

  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());

  const QString filename = dir.filePath(u"akousma.db"_s);
  const QDateTime time(QDate(2026, 11, 10), QTime(15, 30, 5));
  const QString expected = filename + u".unusable-20261110-153005"_s;
  EXPECT_EQ(expected, DatabaseHealth::MoveAsideFilename(filename, time));

  // An existing file, or only its journal, is never overwritten.
  ASSERT_TRUE(WriteFile(expected, "x"));
  EXPECT_EQ(expected + u"-2"_s, DatabaseHealth::MoveAsideFilename(filename, time));
  ASSERT_TRUE(WriteFile(expected + u"-2-journal"_s, "x"));
  EXPECT_EQ(expected + u"-3"_s, DatabaseHealth::MoveAsideFilename(filename, time));

}

TEST(DatabaseHealthTest, MoveAsideKeepsTheDatabaseAndItsJournal) {

  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());

  const QString filename = dir.filePath(u"akousma.db"_s);
  ASSERT_TRUE(WriteFile(filename, GarbageData()));
  ASSERT_TRUE(WriteFile(filename + u"-journal"_s, "journal"));

  QString moved_to;
  QString error;
  ASSERT_TRUE(DatabaseHealth::MoveAside(filename, &moved_to, &error)) << error.toStdString();
  EXPECT_FALSE(QFileInfo::exists(filename));
  EXPECT_FALSE(QFileInfo::exists(filename + u"-journal"_s));
  EXPECT_TRUE(moved_to.startsWith(filename + u".unusable-"_s));
  EXPECT_EQ(GarbageData(), ReadFile(moved_to));
  EXPECT_EQ(QByteArray("journal"), ReadFile(moved_to + u"-journal"_s));

  // Nothing to move.
  EXPECT_FALSE(DatabaseHealth::MoveAside(filename, &moved_to, &error));
  EXPECT_FALSE(error.isEmpty());

}

TEST(DatabaseHealthTest, RestoreBackupKeepsTheBackupAndTheOldDatabase) {

  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());

  const QString filename = dir.filePath(u"akousma.db"_s);
  const QString backup = DatabaseHealth::BackupFilename(filename);
  ASSERT_TRUE(WriteFile(filename, GarbageData()));
  ASSERT_TRUE(CreateDatabase(backup, Database::kSchemaVersion, QStringList() << u"songs"_s));
  const QByteArray backup_data = ReadFile(backup);

  QString moved_to;
  QString error;
  ASSERT_TRUE(DatabaseHealth::RestoreBackup(filename, backup, &moved_to, &error)) << error.toStdString();
  EXPECT_EQ(backup_data, ReadFile(filename));
  EXPECT_EQ(backup_data, ReadFile(backup));
  EXPECT_EQ(GarbageData(), ReadFile(moved_to));

  EXPECT_FALSE(DatabaseHealth::RestoreBackup(filename, dir.filePath(u"nobackup.db"_s), &moved_to, &error));

}

// These used to end in qFatal() and abort the process.
TEST(DatabaseHealthTest, DatabaseReportsAnUnusableFileInsteadOfAborting) {

  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());

  const QString garbage = dir.filePath(u"garbage.db"_s);
  ASSERT_TRUE(WriteFile(garbage, GarbageData()));
  QString error;
  EXPECT_EQ(State::Unreadable, OpenWithDatabase(garbage, &error));
  EXPECT_FALSE(error.isEmpty());
  // The file is left as it was, no schema was written into it.
  EXPECT_EQ(GarbageData(), ReadFile(garbage));

  const QString too_old = dir.filePath(u"tooold.db"_s);
  ASSERT_TRUE(CreateDatabase(too_old, Database::kMinSupportedSchemaVersion - 1, QStringList() << u"songs"_s));
  EXPECT_EQ(State::TooOld, OpenWithDatabase(too_old));

  const QString no_version = dir.filePath(u"noversion.db"_s);
  ASSERT_TRUE(CreateDatabase(no_version, -1, QStringList() << u"songs"_s));
  EXPECT_EQ(State::NoSchemaVersion, OpenWithDatabase(no_version));

  // The last schema update fails because the tables it updates don't exist. It is rolled back, the version stays.
  const QString broken = dir.filePath(u"broken.db"_s);
  ASSERT_TRUE(CreateDatabase(broken, Database::kSchemaVersion - 1, QStringList() << u"songs"_s));
  EXPECT_EQ(State::UpgradeFailed, OpenWithDatabase(broken, &error));
  EXPECT_FALSE(error.isEmpty());
  EXPECT_EQ(Database::kSchemaVersion - 1, ReadVersion(broken));

  const QString too_new = dir.filePath(u"toonew.db"_s);
  ASSERT_TRUE(CreateDatabase(too_new, Database::kSchemaVersion + 1, QStringList() << u"songs"_s));
  EXPECT_EQ(State::TooNew, OpenWithDatabase(too_new));

  const QString fresh = dir.filePath(u"new.db"_s);
  EXPECT_EQ(State::Ok, OpenWithDatabase(fresh));
  EXPECT_EQ(Database::kSchemaVersion, ReadVersion(fresh));

  // A file of 0 bytes gets the schema, as before.
  const QString empty = dir.filePath(u"empty.db"_s);
  ASSERT_TRUE(WriteFile(empty, QByteArray()));
  EXPECT_EQ(State::Ok, OpenWithDatabase(empty));

}

TEST(DatabaseStartupCheckTest, QuitLeavesTheDatabaseAlone) {

  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString filename = dir.filePath(u"akousma.db"_s);
  ASSERT_TRUE(WriteFile(filename, GarbageData()));

  int asked = 0;
  const bool start = DatabaseStartupCheck::Run(filename, [&asked](const DatabaseStartupCheck::Problem &problem) {
    ++asked;
    EXPECT_EQ(State::Unreadable, problem.probe.state);
    EXPECT_FALSE(problem.backup_usable);
    EXPECT_FALSE(DatabaseStartupCheck::ProblemText(problem).isEmpty());
    return Action::Quit;
  }, [](const QString&, const bool) {});

  EXPECT_FALSE(start);
  EXPECT_EQ(1, asked);
  EXPECT_EQ(GarbageData(), ReadFile(filename));

}

TEST(DatabaseStartupCheckTest, StartWithANewCollection) {

  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString filename = dir.filePath(u"akousma.db"_s);
  ASSERT_TRUE(WriteFile(filename, GarbageData()));

  int asked = 0;
  QStringList messages;
  const bool start = DatabaseStartupCheck::Run(filename, [&asked](const DatabaseStartupCheck::Problem &problem) {
    ++asked;
    EXPECT_TRUE(problem.actions.contains(Action::MoveAside));
    return Action::MoveAside;
  }, [&messages](const QString &message, const bool error) {
    EXPECT_FALSE(error);
    messages << message;
  });

  EXPECT_TRUE(start);
  EXPECT_EQ(1, asked);
  EXPECT_EQ(1, messages.count());
  EXPECT_EQ(State::Ok, DatabaseHealth::ProbeFile(filename).state);

  // The old file is kept next to the new one.
  int kept = 0;
  const QStringList entries = QDir(dir.path()).entryList(QStringList() << u"akousma.db.unusable-*"_s, QDir::Files);
  for (const QString &entry : entries) {
    if (ReadFile(dir.filePath(entry)) == GarbageData()) ++kept;
  }
  EXPECT_EQ(1, kept);

}

TEST(DatabaseStartupCheckTest, RestoreTheBackup) {

  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString filename = dir.filePath(u"akousma.db"_s);
  const QString backup = DatabaseHealth::BackupFilename(filename);

  // A real backup, written by Database.
  ASSERT_EQ(State::Ok, OpenWithDatabase(backup));
  ASSERT_TRUE(WriteFile(filename, GarbageData()));

  const bool start = DatabaseStartupCheck::Run(filename, [](const DatabaseStartupCheck::Problem &problem) {
    EXPECT_TRUE(problem.backup_usable);
    EXPECT_TRUE(problem.actions.contains(Action::RestoreBackup));
    return Action::RestoreBackup;
  }, [](const QString&, const bool error) { EXPECT_FALSE(error); });

  EXPECT_TRUE(start);
  EXPECT_EQ(State::Ok, DatabaseHealth::ProbeFile(filename).state);
  EXPECT_TRUE(QFileInfo::exists(backup));

}

TEST(DatabaseStartupCheckTest, NewerDatabaseCanBeOpenedAnyway) {

  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString filename = dir.filePath(u"akousma.db"_s);
  ASSERT_TRUE(CreateDatabase(filename, Database::kSchemaVersion + 1, QStringList() << u"songs"_s));

  const bool start = DatabaseStartupCheck::Run(filename, [](const DatabaseStartupCheck::Problem &problem) {
    EXPECT_EQ(State::TooNew, problem.probe.state);
    EXPECT_EQ(Database::kSchemaVersion + 1, problem.probe.schema_version);
    return Action::OpenAnyway;
  }, [](const QString&, const bool) {});

  EXPECT_TRUE(start);
  EXPECT_EQ(Database::kSchemaVersion + 1, ReadVersion(filename));

}

TEST(DatabaseStartupCheckTest, UsableDatabaseAsksNothing) {

  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString filename = dir.filePath(u"akousma.db"_s);

  const bool start = DatabaseStartupCheck::Run(filename, [](const DatabaseStartupCheck::Problem&) {
    ADD_FAILURE() << "Asked about a new database";
    return Action::Quit;
  }, [](const QString&, const bool) { ADD_FAILURE() << "Message about a new database"; });

  EXPECT_TRUE(start);
  EXPECT_EQ(State::Ok, DatabaseHealth::ProbeFile(filename).state);

}
