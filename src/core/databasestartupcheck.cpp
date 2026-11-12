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

#include <QAbstractButton>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QList>
#include <QLocale>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QStringList>

#include "includes/shared_ptr.h"
#include "logging.h"
#include "database.h"
#include "databasehealth.h"
#include "databasestartupcheck.h"

using namespace Qt::Literals::StringLiterals;

DatabaseStartupCheck::Problem DatabaseStartupCheck::Check(const QString &filename) {

  Problem problem;
  problem.filename = filename;
  problem.backup_filename = DatabaseHealth::BackupFilename(filename);

  {
    // Opening the database creates the schema for a new one and runs the schema updates.
    // The Application opens it again later, there is nothing left to do then.
    Database database(SharedPtr<TaskManager>(), nullptr, filename);
    problem.probe.state = database.open_state();
    problem.probe.error = database.open_error();
    database.Close();
  }

  if (DatabaseHealth::IsUsable(problem.probe.state) && problem.probe.state != DatabaseHealth::State::TooNew) {
    return problem;
  }

  // The version for the description, read without changing anything.
  problem.probe.schema_version = DatabaseHealth::ProbeFile(filename).schema_version;

  problem.backup_usable = DatabaseHealth::IsUsableBackup(DatabaseHealth::ProbeFile(problem.backup_filename));
  problem.actions = DatabaseHealth::Actions(problem.probe.state, problem.backup_usable);

  return problem;

}

bool DatabaseStartupCheck::Run(const QString &filename, const ChooseFunction &choose, const MessageFunction &message) {

  for (;;) {
    const Problem problem = Check(filename);
    if (problem.actions.isEmpty()) return true;

    qLog(Error) << "The collection database" << filename << "can't be used:" << DatabaseHealth::StateDescription(problem.probe.state, problem.probe.schema_version) << problem.probe.error;

    DatabaseHealth::Action action = choose(problem);
    if (!problem.actions.contains(action)) action = DatabaseHealth::Action::Quit;

    switch (action) {
      case DatabaseHealth::Action::Quit:
        qLog(Info) << "Quitting, the collection database was left as it is";
        return false;
      case DatabaseHealth::Action::Retry:
        break;
      case DatabaseHealth::Action::OpenAnyway:
        qLog(Warning) << "Opening the collection database" << filename << "with schema version" << problem.probe.schema_version << "anyway";
        return true;
      case DatabaseHealth::Action::RestoreBackup: {
        QString moved_to;
        QString error;
        if (DatabaseHealth::RestoreBackup(filename, problem.backup_filename, &moved_to, &error)) {
          if (moved_to.isEmpty()) {
            message(tr("The collection database was restored from the backup %1.").arg(QDir::toNativeSeparators(problem.backup_filename)), false);
          }
          else {
            message(tr("The collection database was renamed to %1 and replaced by a copy of the backup %2.").arg(QDir::toNativeSeparators(moved_to), QDir::toNativeSeparators(problem.backup_filename)), false);
          }
        }
        else {
          message(tr("Could not restore the backup: %1").arg(error), true);
        }
        break;
      }
      case DatabaseHealth::Action::MoveAside: {
        QString moved_to;
        QString error;
        if (DatabaseHealth::MoveAside(filename, &moved_to, &error)) {
          message(tr("The collection database was renamed to %1. Akousma starts with a new, empty collection; add your music folders again in the settings. Your music files are not changed.").arg(QDir::toNativeSeparators(moved_to)), false);
        }
        else {
          message(tr("Could not move the collection database aside: %1").arg(error), true);
        }
        break;
      }
    }
    // Check the database again: after a retry, a restored backup or with a new database.
  }

}

QString DatabaseStartupCheck::ActionText(const DatabaseHealth::Action action) {

  switch (action) {
    case DatabaseHealth::Action::Quit:
      return tr("&Quit");
    case DatabaseHealth::Action::Retry:
      return tr("&Try again");
    case DatabaseHealth::Action::OpenAnyway:
      return tr("&Open anyway");
    case DatabaseHealth::Action::RestoreBackup:
      return tr("&Restore the backup");
    case DatabaseHealth::Action::MoveAside:
      return tr("&Start with a new collection");
  }

  return QString();

}

QString DatabaseStartupCheck::ProblemText(const Problem &problem) {

  QStringList paragraphs;
  paragraphs << DatabaseHealth::StateDescription(problem.probe.state, problem.probe.schema_version);
  paragraphs << tr("Database: %1").arg(QDir::toNativeSeparators(problem.filename));

  for (const DatabaseHealth::Action action : problem.actions) {
    switch (action) {
      case DatabaseHealth::Action::Quit:
        paragraphs << tr("Quit leaves the database as it is, so you can copy it or look at it first.");
        break;
      case DatabaseHealth::Action::Retry:
        paragraphs << tr("Try again after closing other programs that use the database or freeing disk space.");
        break;
      case DatabaseHealth::Action::OpenAnyway:
        paragraphs << tr("Open anyway uses the database as it is. Parts of the collection may not work, and the newer Akousma may not be able to use it afterwards.");
        break;
      case DatabaseHealth::Action::RestoreBackup: {
        const QDateTime modified = QFileInfo(problem.backup_filename).lastModified();
        paragraphs << tr("Restore the backup renames the database and puts a copy of the automatic backup from %1 in its place. Changes to the collection since then are lost, the renamed database is kept.").arg(QLocale().toString(modified, QLocale::ShortFormat));
        break;
      }
      case DatabaseHealth::Action::MoveAside:
        paragraphs << tr("Start with a new collection renames the database, so it is kept, and creates a new, empty one. Your music files are not changed.");
        break;
    }
  }

  return paragraphs.join(u"\n\n"_s);

}

DatabaseHealth::Action DatabaseStartupCheck::Ask(const Problem &problem) {

  const bool too_new = problem.probe.state == DatabaseHealth::State::TooNew;
  QMessageBox box(too_new ? QMessageBox::Warning : QMessageBox::Critical, QGuiApplication::applicationDisplayName(), too_new ? tr("The collection database was written by a newer version of Akousma.") : tr("Akousma can't use its collection database."));
  box.setInformativeText(ProblemText(problem));
  if (!problem.probe.error.isEmpty()) {
    box.setDetailedText(problem.probe.error);
  }

  QMap<QAbstractButton*, DatabaseHealth::Action> buttons;
  for (const DatabaseHealth::Action action : problem.actions) {
    QMessageBox::ButtonRole role = QMessageBox::ActionRole;
    if (action == DatabaseHealth::Action::Quit) role = QMessageBox::RejectRole;
    else if (action == DatabaseHealth::Action::Retry || action == DatabaseHealth::Action::OpenAnyway) role = QMessageBox::AcceptRole;
    QPushButton *button = box.addButton(ActionText(action), role);
    buttons.insert(button, action);
    if (action == DatabaseHealth::Action::Quit) {
      box.setDefaultButton(button);
      box.setEscapeButton(button);
    }
  }

  box.exec();

  return buttons.value(box.clickedButton(), DatabaseHealth::Action::Quit);

}

void DatabaseStartupCheck::ShowMessage(const QString &message, const bool error) {

  if (error) {
    QMessageBox::warning(nullptr, QGuiApplication::applicationDisplayName(), message);
  }
  else {
    QMessageBox::information(nullptr, QGuiApplication::applicationDisplayName(), message);
  }

}

bool DatabaseStartupCheck::Run() {
  return Run(Database::DefaultFilename(), &DatabaseStartupCheck::Ask, &DatabaseStartupCheck::ShowMessage);
}
