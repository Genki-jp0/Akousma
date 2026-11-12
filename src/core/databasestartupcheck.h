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

#ifndef DATABASESTARTUPCHECK_H
#define DATABASESTARTUPCHECK_H

#include <functional>

#include <QtGlobal>
#include <QCoreApplication>
#include <QList>
#include <QString>

#include "databasehealth.h"

// Opens the collection database at start-up, before the Application is created, and deals with a database that can't be used.
// Opening it creates or updates the schema, so a failed schema update is found here too.
// Instead of aborting, the user is told what is wrong and can quit, try again, restore the automatic backup or start with a new collection.
// The database is never deleted: restoring the backup and starting with a new collection first rename it.
class DatabaseStartupCheck {
  Q_DECLARE_TR_FUNCTIONS(DatabaseStartupCheck)

 public:
  struct Problem {
    QString filename;
    QString backup_filename;
    DatabaseHealth::Probe probe;
    bool backup_usable = false;
    QList<DatabaseHealth::Action> actions;
  };

  // Asks the user what to do about a database that can't be used. Returns one of problem.actions.
  using ChooseFunction = std::function<DatabaseHealth::Action(const Problem &problem)>;
  // Tells the user what was done, or why it failed.
  using MessageFunction = std::function<void(const QString &message, const bool error)>;

  // Returns false when Akousma should quit.
  static bool Run(const QString &filename, const ChooseFunction &choose, const MessageFunction &message);

  // Run() for Akousma's collection database with message boxes, for main(). Needs a QApplication.
  static bool Run();

  // What the dialog says about a problem, without the buttons.
  static QString ProblemText(const Problem &problem);
  static QString ActionText(const DatabaseHealth::Action action);

 private:
  static Problem Check(const QString &filename);
  static DatabaseHealth::Action Ask(const Problem &problem);
  static void ShowMessage(const QString &message, const bool error);
};

#endif  // DATABASESTARTUPCHECK_H
