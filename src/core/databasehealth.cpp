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

#include <atomic>
#include <utility>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "logging.h"
#include "database.h"
#include "databasehealth.h"

using namespace Qt::Literals::StringLiterals;

const QStringList DatabaseHealth::kJournalSuffixes = QStringList() << u"-journal"_s << u"-wal"_s << u"-shm"_s;

namespace {

std::atomic<int> sNextProbeConnectionId(1);

}  // namespace

DatabaseHealth::State DatabaseHealth::Classify(const Facts &facts, const int min_schema_version, const int max_schema_version) {

  if (!facts.exists) return State::Missing;
  if (!facts.opened) return State::CannotOpen;
  // QSqlDatabase::tables() returns an empty list also when the catalogue can't be read, which made a damaged file look like a new database.
  if (!facts.catalogue_readable) return State::Unreadable;
  if (facts.integrity_checked && !facts.integrity_ok) return State::Corrupt;
  if (facts.table_count == 0) return State::Empty;
  if (!facts.schema_version_readable) return State::NoSchemaVersion;
  if (facts.schema_version < min_schema_version) return State::TooOld;
  if (facts.schema_version > max_schema_version) return State::TooNew;
  if (facts.schema_version < max_schema_version) return State::NeedsUpgrade;
  return State::Ok;

}

bool DatabaseHealth::ReadSchemaVersion(QSqlDatabase &db, int *schema_version, QString *error) {

  QSqlQuery q(db);
  if (!q.exec(u"SELECT version FROM schema_version"_s)) {
    if (error) *error = q.lastError().text();
    return false;
  }
  if (!q.next()) {
    if (error) *error = q.lastError().isValid() ? q.lastError().text() : tr("The schema_version table is empty.");
    return false;
  }
  bool ok = false;
  const int version = q.value(0).toInt(&ok);
  if (!ok) {
    if (error) *error = tr("The schema version is not a number.");
    return false;
  }
  *schema_version = version;
  return true;

}

DatabaseHealth::Probe DatabaseHealth::ProbeConnection(QSqlDatabase &db, const bool integrity_check, const int min_schema_version, const int max_schema_version) {

  Probe probe;
  Facts facts;
  facts.opened = db.isOpen();

  QStringList errors;

  if (facts.opened) {
    QSqlQuery q(db);
    if (q.exec(u"SELECT name FROM sqlite_master WHERE type = 'table'"_s)) {
      while (q.next()) {
        probe.tables << q.value(0).toString();
      }
      // A damaged page can also make a step fail after the first rows.
      facts.catalogue_readable = !q.lastError().isValid();
      if (!facts.catalogue_readable) errors << q.lastError().text();
    }
    else {
      errors << q.lastError().text();
    }
  }
  else {
    errors << db.lastError().text();
  }
  facts.table_count = static_cast<int>(probe.tables.count());

  if (facts.catalogue_readable && integrity_check) {
    facts.integrity_checked = true;
    QSqlQuery q(db);
    // Ask for 10 error messages at most.
    if (q.exec(u"PRAGMA quick_check(10)"_s)) {
      QStringList messages;
      while (q.next()) {
        messages << q.value(0).toString();
      }
      facts.integrity_ok = !q.lastError().isValid() && messages == QStringList(u"ok"_s);
      if (!facts.integrity_ok) {
        errors << messages;
        if (q.lastError().isValid()) errors << q.lastError().text();
      }
    }
    else {
      errors << q.lastError().text();
    }
  }

  if (facts.catalogue_readable && facts.table_count > 0) {
    QString error;
    facts.schema_version_readable = ReadSchemaVersion(db, &facts.schema_version, &error);
    if (facts.schema_version_readable) {
      probe.schema_version = facts.schema_version;
    }
    else {
      errors << error;
    }
  }

  probe.state = Classify(facts, min_schema_version == -1 ? Database::kMinSupportedSchemaVersion : min_schema_version, max_schema_version == -1 ? Database::kSchemaVersion : max_schema_version);
  if (!IsUsable(probe.state) || probe.state == State::TooNew) {
    errors.removeAll(QString());
    probe.error = errors.join(u'\n');
  }

  return probe;

}

DatabaseHealth::Probe DatabaseHealth::ProbeFile(const QString &filename, const bool integrity_check) {

  if (!QFileInfo(filename).isFile()) {
    Probe probe;
    probe.state = QFileInfo::exists(filename) ? State::CannotOpen : State::Missing;
    if (probe.state == State::CannotOpen) probe.error = tr("%1 is not a file.").arg(QDir::toNativeSeparators(filename));
    return probe;
  }

  const QString connection_name = QStringLiteral("databasehealth_%1_thread_%2").arg(sNextProbeConnectionId++).arg(reinterpret_cast<quint64>(QThread::currentThread()));

  Probe probe;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, connection_name);
    db.setConnectOptions(u"QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=30000"_s);
    db.setDatabaseName(filename);
    db.open();
    probe = ProbeConnection(db, integrity_check);
    db.close();
  }
  QSqlDatabase::removeDatabase(connection_name);

  return probe;

}

bool DatabaseHealth::IsUsable(const State state) {

  switch (state) {
    case State::Missing:
    case State::Empty:
    case State::Ok:
    case State::NeedsUpgrade:
    case State::TooNew:
      return true;
    case State::TooOld:
    case State::NoSchemaVersion:
    case State::Unreadable:
    case State::Corrupt:
    case State::CannotOpen:
    case State::UpgradeFailed:
      return false;
  }

  return false;

}

bool DatabaseHealth::IsUsableBackup(const Probe &backup) {

  // A backup written by a newer Akousma would only bring the same question back.
  return backup.state == State::Ok || backup.state == State::NeedsUpgrade;

}

QList<DatabaseHealth::Action> DatabaseHealth::Actions(const State state, const bool backup_usable) {

  QList<Action> actions;
  switch (state) {
    case State::Missing:
    case State::Empty:
    case State::Ok:
    case State::NeedsUpgrade:
      break;
    case State::CannotOpen:
      // The file may be fine: the directory or the file permissions are wrong, or the disk is not mounted. Moving it aside would not help.
      actions << Action::Quit << Action::Retry;
      break;
    case State::TooNew:
      actions << Action::Quit << Action::OpenAnyway;
      if (backup_usable) actions << Action::RestoreBackup;
      actions << Action::MoveAside;
      break;
    case State::TooOld:
    case State::NoSchemaVersion:
      // Retrying would find the same file.
      actions << Action::Quit;
      if (backup_usable) actions << Action::RestoreBackup;
      actions << Action::MoveAside;
      break;
    case State::Unreadable:
    case State::Corrupt:
    case State::UpgradeFailed:
      // Another program can hold a lock on the file, and a schema update can fail because the disk is full: these can work on the next try.
      actions << Action::Quit << Action::Retry;
      if (backup_usable) actions << Action::RestoreBackup;
      actions << Action::MoveAside;
      break;
  }

  return actions;

}

QString DatabaseHealth::BackupFilename(const QString &filename) {
  // The name Database::BackupFile() writes the backup to.
  return filename + u".bak"_s;
}

QString DatabaseHealth::MoveAsideFilename(const QString &filename, const QDateTime &time) {

  const QString base = filename + u".unusable-"_s + time.toString(u"yyyyMMdd-HHmmss"_s);

  const auto taken = [](const QString &name) {
    if (QFileInfo::exists(name)) return true;
    for (const QString &suffix : kJournalSuffixes) {
      if (QFileInfo::exists(name + suffix)) return true;
    }
    return false;
  };

  QString name = base;
  for (int i = 2; taken(name); ++i) {
    name = base + u'-' + QString::number(i);
  }

  return name;

}

bool DatabaseHealth::MoveAside(const QString &filename, QString *moved_to, QString *error) {

  const QString destination = MoveAsideFilename(filename, QDateTime::currentDateTime());

  // The journal is renamed with the database, so SQLite can still use it to roll back an interrupted transaction in the renamed file,
  // and a stale journal is not applied to the new database.
  QList<std::pair<QString, QString>> moved;
  const QStringList suffixes = QStringList() << QString() << kJournalSuffixes;
  for (const QString &suffix : suffixes) {
    const QString source = filename + suffix;
    if (!QFileInfo::exists(source)) continue;
    QFile file(source);
    if (!file.rename(destination + suffix)) {
      if (error) *error = tr("Could not rename %1 to %2: %3").arg(QDir::toNativeSeparators(source), QDir::toNativeSeparators(destination + suffix), file.errorString());
      qLog(Error) << "Could not rename" << source << "to" << destination + suffix << file.errorString();
      // Put back what was renamed, so the database and its journal stay together.
      for (const std::pair<QString, QString> &rename : std::as_const(moved)) {
        if (!QFile::rename(rename.second, rename.first)) {
          qLog(Error) << "Could not rename" << rename.second << "back to" << rename.first;
        }
      }
      return false;
    }
    moved << std::make_pair(source, destination + suffix);
  }

  if (moved.isEmpty()) {
    if (error) *error = tr("%1 does not exist.").arg(QDir::toNativeSeparators(filename));
    return false;
  }

  qLog(Info) << "Moved the collection database" << filename << "to" << destination;
  if (moved_to) *moved_to = destination;

  return true;

}

bool DatabaseHealth::RestoreBackup(const QString &filename, const QString &backup_filename, QString *moved_to, QString *error) {

  if (!QFileInfo(backup_filename).isFile()) {
    if (error) *error = tr("The backup %1 does not exist.").arg(QDir::toNativeSeparators(backup_filename));
    return false;
  }

  bool exists = QFileInfo::exists(filename);
  for (const QString &suffix : kJournalSuffixes) {
    exists = exists || QFileInfo::exists(filename + suffix);
  }
  if (exists && !MoveAside(filename, moved_to, error)) {
    return false;
  }

  // QFile::copy() writes to a temporary file and renames it, so a failed copy leaves no half written database.
  QFile backup(backup_filename);
  if (!backup.copy(filename)) {
    if (error) *error = tr("Could not copy the backup %1 to %2: %3").arg(QDir::toNativeSeparators(backup_filename), QDir::toNativeSeparators(filename), backup.errorString());
    qLog(Error) << "Could not copy" << backup_filename << "to" << filename << backup.errorString();
    return false;
  }

  qLog(Info) << "Restored the collection database" << filename << "from" << backup_filename;

  return true;

}

QString DatabaseHealth::StateDescription(const State state, const int schema_version) {

  switch (state) {
    case State::Missing:
      return tr("The database does not exist yet.");
    case State::Empty:
      return tr("The database is empty.");
    case State::Ok:
      return tr("The database can be used.");
    case State::NeedsUpgrade:
      return tr("The database has schema version %1 and is updated to version %2 when it is opened.").arg(schema_version).arg(Database::kSchemaVersion);
    case State::TooNew:
      return tr("The database has schema version %1, which is newer than the version %2 this Akousma supports. It was probably written by a newer Akousma.").arg(schema_version).arg(Database::kSchemaVersion);
    case State::TooOld:
      return tr("The database has schema version %1, which is older than the oldest version %2 Akousma can update.").arg(schema_version).arg(Database::kMinSupportedSchemaVersion);
    case State::NoSchemaVersion:
      return tr("The database has no schema version, so it is not a collection database of Akousma or Strawberry.");
    case State::Unreadable:
      return tr("SQLite could not read the database: it is damaged, it is not an SQLite database, or another program has locked it.");
    case State::Corrupt:
      return tr("The database is damaged: the integrity check found errors.");
    case State::CannotOpen:
      return tr("The database could not be opened.");
    case State::UpgradeFailed:
      return tr("The database could not be updated to the current schema version %1, for example because the disk is full or the folder is read-only. The failed update was rolled back.").arg(Database::kSchemaVersion);
  }

  return QString();

}

QString DatabaseHealth::ImportProblem(const Probe &probe, const QStringList &required_tables) {

  switch (probe.state) {
    case State::Ok:
    case State::NeedsUpgrade: {
      QStringList missing;
      for (const QString &table : required_tables) {
        if (!probe.tables.contains(table)) missing << table;
      }
      if (!missing.isEmpty()) {
        return tr("These tables are missing in the database: %1.").arg(missing.join(u", "_s));
      }
      return QString();
    }
    case State::Missing:
      return tr("The database does not exist.");
    case State::Empty:
      return tr("The database has no tables.");
    case State::TooNew:
      return tr("The database has schema version %1, which is newer than the version %2 this Akousma supports. Update Akousma and import again.").arg(probe.schema_version).arg(Database::kSchemaVersion);
    case State::TooOld:
    case State::NoSchemaVersion:
    case State::Unreadable:
    case State::Corrupt:
    case State::CannotOpen:
    case State::UpgradeFailed:
      break;
  }

  QString problem = StateDescription(probe.state, probe.schema_version);
  if (!probe.error.isEmpty()) {
    problem += u' ' + tr("SQLite reported: %1").arg(probe.error);
  }
  return problem;

}
