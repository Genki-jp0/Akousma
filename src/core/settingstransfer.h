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

#ifndef SETTINGSTRANSFER_H
#define SETTINGSTRANSFER_H

#include <QtGlobal>
#include <QCoreApplication>
#include <QString>

class QWidget;

// Export, import and reset of Akousma's settings.
//
// An export is an INI file (the same on every system, also where the settings are in the Windows registry) with a header group
// [AkousmaSettingsExport] and all settings except passwords, login tokens and other secrets (PlayerImporter::IsSecretSettingKey).
// Import and reset change only the settings: the collection database, playlists, album covers and saved passwords (keyring and the
// secret setting keys) are kept; a reset also keeps the smart playlists (an import replaces them with the exported ones).
// Both back up the current settings first. Akousma must not be running for them, so the running application only marks them as
// pending and they are done at the next start, before anything reads the settings.
class SettingsTransfer {
  Q_DECLARE_TR_FUNCTIONS(SettingsTransfer)

 public:
  enum class Action {
    None,
    Reset,
    Import
  };

  struct Result {
    bool success = false;
    int count = 0;           // Settings exported, imported or reset.
    int secrets = 0;         // Secret settings left out of an export, or kept by an import or reset.
    QString backup_dir;      // Where the settings were backed up before an import or reset.
    QString version;         // The Akousma version an imported file was exported from.
    QString error;
  };

  static QString DefaultExportFileName();
  static Result Export(const QString &filename);
  // Whether filename is an Akousma settings export, version is the Akousma version it was exported from.
  static bool CheckExportFile(const QString &filename, QString *version, QString *error);
  static Result Import(const QString &filename);
  static Result Reset();

  // A reset or import chosen in the running application, done at the next start by RunPendingAction().
  static void SetPendingAction(const Action action, const QString &filename = QString());
  static Action PendingAction(QString *filename = nullptr);
  // Does the pending reset or import and shows the result. Returns false when there was none.
  static bool RunPendingAction();

  // The dialogs of Settings > Behavior and Tools > Settings: export to a file the user picks, import (asks to restart), reset with the
  // offer to export first (asks to restart). Import and reset return true when the user wants to restart now.
  static void ExportWithDialog(QWidget *parent);
  static bool ImportWithDialog(QWidget *parent);
  static bool ResetWithDialog(QWidget *parent);

 private:
  static QString PendingFileName();
  static QString NewBackupDir();
  static bool KeepOnReset(const QString &key);
  static bool AskRestart(QWidget *parent, const QString &title, const QString &text);
};

#endif  // SETTINGSTRANSFER_H
