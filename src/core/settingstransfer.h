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
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QList>

class QSettings;
class QWidget;

// Export, import and reset of Akousma's settings.
//
// An export is an INI file (the same on every system, also where the settings are in the Windows registry) with a header group
// [AkousmaSettingsExport] and all settings except passwords, login tokens and other secrets (PlayerImporter::IsSecretSettingKey).
// Import and reset change only the settings: the collection database, playlists, album covers and saved passwords (keyring and the
// secret setting keys) are kept; a reset also keeps the smart playlists (an import replaces them with the exported ones).
// Both back up the current settings first. Akousma must not be running for them, so the running application only marks them as
// pending and they are done at the next start, before anything reads the settings.
//
// An import only writes groups this version of Akousma knows (IsKnownSettingsGroup), the rest of the file is refused. Connection
// settings (server addresses, user names, proxy, certificate checks and other login settings, IsConnectionSettingKey) decide where the
// saved passwords and logins are sent, so a file can't change them unless the user has seen and accepted each change: PlanImport()
// lists the changes, the import dialog asks, and the accepted changes are passed to Import(). The others keep the current value.
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
    QStringList held_back;   // Connection settings in an imported file that were not accepted, the current value was kept.
    QStringList refused;     // Groups in an imported file that this version doesn't know, not imported.
  };

  // A connection setting an import would change. imported is invalid when the file doesn't have the setting (it is removed, so the
  // default is used), current is invalid when the setting isn't set now.
  struct ConnectionChange {
    QString key;
    QVariant current;
    QVariant imported;
    QString id;  // Identifies the key and the imported value, for accepting the change.
  };

  // What an import does with the current settings and the settings in a file, see PlanImport().
  struct ImportPlan {
    QVariantMap write;                             // Settings to write: known groups, connection settings only when unchanged or accepted.
    QStringList remove;                            // Current settings to remove: not in the file, in a known group, not a held back connection setting.
    QList<ConnectionChange> connection_changes;    // All connection settings the file changes, accepted or not.
    QList<ConnectionChange> held_back;             // The connection changes that were not accepted: not written, the current value is kept.
    QStringList refused_keys;                      // Settings in groups this version doesn't know: not written.
    QStringList refused_groups;
    int secrets = 0;                               // Secret current settings, kept.
  };

  static QString DefaultExportFileName();
  static Result Export(const QString &filename);
  // Whether filename is an Akousma settings export, version is the Akousma version it was exported from.
  static bool CheckExportFile(const QString &filename, QString *version, QString *error);
  // Imports filename. accepted_connection_changes are the ConnectionChange::id values of the connection changes the user accepted,
  // the other connection settings in the file are held back.
  static Result Import(const QString &filename, const QStringList &accepted_connection_changes = QStringList());
  static Result Reset();

  // Decides what an import does, without changing anything. current are the current settings, imported the settings in the file
  // (both as from SettingsValues()), accepted_connection_changes as for Import().
  static ImportPlan PlanImport(const QVariantMap &current, const QVariantMap &imported, const QStringList &accepted_connection_changes = QStringList());
  static QVariantMap SettingsValues(const QSettings &settings);
  // Whether an import may write settings in the group of key (the part before the first /).
  static bool IsKnownSettingsGroup(const QString &key);
  // Whether key decides where saved passwords and logins are sent or how: changed by an import only when the user accepts it.
  static bool IsConnectionSettingKey(const QString &key);

  // A reset or import chosen in the running application, done at the next start by RunPendingAction().
  // accepted_connection_changes are passed to Import().
  static void SetPendingAction(const Action action, const QString &filename = QString(), const QStringList &accepted_connection_changes = QStringList());
  static Action PendingAction(QString *filename = nullptr, QStringList *accepted_connection_changes = nullptr);
  // Does the pending reset or import and shows the result. Returns false when there was none.
  static bool RunPendingAction();

  // The dialogs of Settings > Behavior and Tools > Settings: export to a file the user picks, import (asks to restart), reset with the
  // offer to export first (asks to restart). Import and reset return true when the user wants to restart now.
  static void ExportWithDialog(QWidget *parent);
  static bool ImportWithDialog(QWidget *parent);
  static bool ResetWithDialog(QWidget *parent);
  // Asks whether to restart now for a change that needs a restart. Returns true when the user wants to restart now.
  static bool AskRestart(QWidget *parent, const QString &title, const QString &text);

 private:
  static QString PendingFileName();
  static QString NewBackupDir();
  static bool KeepOnReset(const QString &key);
  static QString ConnectionChangeId(const QString &key, const QVariant &imported);
  static QString SettingValueText(const QVariant &value);
  static QString ConnectionChangesText(const QList<ConnectionChange> &changes);
};

#endif  // SETTINGSTRANSFER_H
