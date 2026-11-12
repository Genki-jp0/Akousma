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

#ifndef DATABASEHEALTH_H
#define DATABASEHEALTH_H

#include <QtGlobal>
#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QStringList>

class QDateTime;
class QSqlDatabase;

// Finds out whether a collection database can be used, and what can be done about one that can't.
// Used by Database when it opens the collection, by the start-up check that offers to move a damaged database aside,
// and by the player importer before it installs another player's database as Akousma's collection.
// Nothing here deletes a database file: a database that can't be used is renamed and kept.
class DatabaseHealth {
  Q_DECLARE_TR_FUNCTIONS(DatabaseHealth)

 public:
  enum class State {
    // No file. Opening it creates a new database.
    Missing,
    // An SQLite database without tables (also a file of 0 bytes). Opening it creates the schema.
    Empty,
    // The current schema version.
    Ok,
    // An older schema version that is updated when the database is opened.
    NeedsUpgrade,
    // A schema version newer than this Akousma knows, written by a newer Akousma.
    TooNew,
    // A schema version older than the oldest one Akousma can update.
    TooOld,
    // Tables, but no readable schema version: not a collection database of Akousma or Strawberry.
    NoSchemaVersion,
    // SQLite could not read the file: it is not an SQLite database, it is damaged, or it is locked by another program.
    Unreadable,
    // The integrity check found errors. Only checked when asked for.
    Corrupt,
    // SQLite could not open the file at all, for example because of its permissions.
    CannotOpen,
    // Creating the schema or one of the schema updates failed, for example because the disk is full. Set by Database.
    UpgradeFailed
  };

  // The facts Classify() decides on, collected by ProbeConnection() and ProbeFile().
  struct Facts {
    bool exists = true;
    bool opened = false;
    bool catalogue_readable = false;
    int table_count = 0;
    bool schema_version_readable = false;
    int schema_version = 0;
    bool integrity_checked = false;
    bool integrity_ok = false;
  };

  struct Probe {
    State state = State::Missing;
    // The schema version, or -1 when there is none.
    int schema_version = -1;
    QStringList tables;
    // The SQLite error or the integrity check messages, for the details.
    QString error;
  };

  // What the user can do about a database that can't be used, in the order they are offered.
  enum class Action {
    // Quit Akousma and leave the database as it is.
    Quit,
    // Try to open the database again, for example after another program stopped using it or disk space was freed.
    Retry,
    // Open the database although it was written by a newer Akousma.
    OpenAnyway,
    // Move the database aside and put a copy of its automatic backup in its place.
    RestoreBackup,
    // Move the database aside and start with a new, empty collection.
    MoveAside
  };

  static State Classify(const Facts &facts, const int min_schema_version, const int max_schema_version);

  // Inspects a database connection that is open. With integrity_check, also runs PRAGMA quick_check, which reads the whole file.
  static Probe ProbeConnection(QSqlDatabase &db, const bool integrity_check = false, const int min_schema_version = -1, const int max_schema_version = -1);

  // Opens a database file read-only and inspects it. A file that doesn't exist is not created.
  static Probe ProbeFile(const QString &filename, const bool integrity_check = false);

  // Reads the version from the schema_version table, returns false when there is no such table or no row.
  static bool ReadSchemaVersion(QSqlDatabase &db, int *schema_version, QString *error = nullptr);

  // True for a database Database can open and use, maybe after creating or updating the schema.
  static bool IsUsable(const State state);

  // True for a backup that can replace the collection database.
  static bool IsUsableBackup(const Probe &backup);

  static QList<Action> Actions(const State state, const bool backup_usable);

  // The name a database that can't be used is renamed to: "<filename>.unusable-<date>-<time>", with a number added when that exists.
  static QString MoveAsideFilename(const QString &filename, const QDateTime &time);

  // Renames the database and its journal files (-journal, -wal and -shm) to MoveAsideFilename(), so the next start creates a new database.
  // Puts everything back and returns false when one of the files can't be renamed.
  static bool MoveAside(const QString &filename, QString *moved_to, QString *error);

  // Moves the database aside, then copies the backup into its place. The backup is kept.
  static bool RestoreBackup(const QString &filename, const QString &backup_filename, QString *moved_to, QString *error);

  // Why a database can't be imported as Akousma's collection, or an empty string when it can.
  static QString ImportProblem(const Probe &probe, const QStringList &required_tables);

  // A sentence for the user about a database in this state.
  static QString StateDescription(const State state, const int schema_version);

  static QString BackupFilename(const QString &filename);

  static const QStringList kJournalSuffixes;
};

#endif  // DATABASEHEALTH_H
