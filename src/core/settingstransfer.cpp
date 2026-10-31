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

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QSettings>
#include <QStandardPaths>
#include <QWidget>
#include <QMessageBox>
#include <QFileDialog>
#include <QCheckBox>
#include <QPushButton>

#include "version.h"
#include "settingstransfer.h"
#include "core/logging.h"
#include "core/settings.h"
#include "import/playerimporter.h"

using namespace Qt::Literals::StringLiterals;

namespace {

// Header of an export file.
constexpr char kExportGroup[] = "AkousmaSettingsExport";
constexpr char kExportApplication[] = "application";
constexpr char kExportVersion[] = "version";
constexpr char kExportBasedOn[] = "based_on";
constexpr char kExportDate[] = "exported";
constexpr char kExportFormat[] = "format";
constexpr int kExportFormatVersion = 1;

// The pending reset or import, in a file next to the collection database: the settings themselves are what is reset or replaced.
constexpr char kPendingFile[] = "/pending-settings-change.ini";
constexpr char kPendingAction[] = "action";
constexpr char kPendingFilename[] = "file";

// The import's own state (pending import, first start offer) belongs to this installation, it is neither exported nor reset.
constexpr char kImportSettingsGroup[] = "Import/";

// The user's smart playlists are kept by a reset, like the playlists in the collection database.
constexpr char kSmartPlaylistsSettingsGroup[] = "SerializedSmartPlaylists/";

bool IsInstallationState(const QString &key) {
  return key.startsWith(QLatin1String(kImportSettingsGroup));
}

}  // namespace

QString SettingsTransfer::DefaultExportFileName() {

  QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  if (dir.isEmpty() || !QDir(dir).exists()) dir = QDir::homePath();
  return dir + u"/akousma-settings-"_s + QDateTime::currentDateTime().toString(u"yyyyMMdd-HHmmss"_s) + u".ini"_s;

}

QString SettingsTransfer::PendingFileName() {
  return PlayerImporter::AkousmaDataDir() + QLatin1String(kPendingFile);
}

QString SettingsTransfer::NewBackupDir() {

  const QString base = PlayerImporter::AkousmaDataDir() + u"/settings-backup-"_s + QDateTime::currentDateTime().toString(u"yyyyMMdd-HHmmss"_s);
  QString dir = base;
  for (int i = 2; QFileInfo::exists(dir); ++i) {
    dir = base + u'-' + QString::number(i);
  }
  return dir;

}

bool SettingsTransfer::KeepOnReset(const QString &key) {
  return PlayerImporter::IsSecretSettingKey(key) || IsInstallationState(key);
}

SettingsTransfer::Result SettingsTransfer::Export(const QString &filename) {

  Result result;

  if (filename.isEmpty()) {
    result.error = tr("No file name given.");
    return result;
  }

  const QFileInfo fileinfo(filename);
  if (!fileinfo.absoluteDir().exists() && !QDir().mkpath(fileinfo.absolutePath())) {
    result.error = tr("Could not create the folder %1.").arg(QDir::toNativeSeparators(fileinfo.absolutePath()));
    return result;
  }
  if (fileinfo.exists() && !QFile::remove(filename)) {
    result.error = tr("Could not replace %1.").arg(QDir::toNativeSeparators(filename));
    return result;
  }

  Settings s;
  QSettings out(filename, QSettings::IniFormat);
  out.beginGroup(QLatin1String(kExportGroup));
  out.setValue(QLatin1String(kExportApplication), u"Akousma"_s);
  out.setValue(QLatin1String(kExportVersion), QLatin1String(AKOUSMA_VERSION_DISPLAY));
  out.setValue(QLatin1String(kExportBasedOn), u"Strawberry %1 (%2)"_s.arg(QLatin1String(AKOUSMA_BASE_STRAWBERRY_VERSION), QLatin1String(AKOUSMA_BASE_STRAWBERRY_COMMIT)));
  out.setValue(QLatin1String(kExportDate), QDateTime::currentDateTime().toString(Qt::ISODate));
  out.setValue(QLatin1String(kExportFormat), kExportFormatVersion);
  out.endGroup();

  const QStringList keys = s.allKeys();
  for (const QString &key : keys) {
    if (IsInstallationState(key)) continue;
    // No passwords, login tokens or other secrets: they stay in the keyring and in this installation's settings.
    if (PlayerImporter::IsSecretSettingKey(key)) {
      ++result.secrets;
      continue;
    }
    out.setValue(key, s.value(key));
    ++result.count;
  }
  out.sync();

  if (out.status() != QSettings::NoError) {
    result.error = tr("Could not write %1.").arg(QDir::toNativeSeparators(filename));
    return result;
  }

  qLog(Info) << "Exported" << result.count << "settings to" << filename << "(" << result.secrets << "passwords and logins left out )";
  result.success = true;
  return result;

}

bool SettingsTransfer::CheckExportFile(const QString &filename, QString *version, QString *error) {

  if (!QFileInfo(filename).isFile()) {
    *error = tr("%1 does not exist.").arg(QDir::toNativeSeparators(filename));
    return false;
  }

  QSettings in(filename, QSettings::IniFormat);
  if (in.status() != QSettings::NoError) {
    *error = tr("%1 could not be read.").arg(QDir::toNativeSeparators(filename));
    return false;
  }
  in.beginGroup(QLatin1String(kExportGroup));
  const QString application = in.value(QLatin1String(kExportApplication)).toString();
  const int format = in.value(QLatin1String(kExportFormat), 0).toInt();
  if (version) *version = in.value(QLatin1String(kExportVersion)).toString();
  in.endGroup();

  if (application != "Akousma"_L1) {
    *error = tr("%1 is not an Akousma settings export.").arg(QDir::toNativeSeparators(filename));
    return false;
  }
  if (format < 1 || format > kExportFormatVersion) {
    *error = tr("%1 was exported by a newer Akousma, it can't be imported.").arg(QDir::toNativeSeparators(filename));
    return false;
  }

  return true;

}

SettingsTransfer::Result SettingsTransfer::Import(const QString &filename) {

  Result result;
  if (!CheckExportFile(filename, &result.version, &result.error)) return result;

  result.backup_dir = NewBackupDir();
  if (!PlayerImporter::BackupSettings(result.backup_dir, &result.error)) return result;

  Settings s;
  // The settings are replaced, except the saved passwords and logins and this installation's import state.
  const QStringList keys = s.allKeys();
  for (const QString &key : keys) {
    if (KeepOnReset(key)) {
      if (PlayerImporter::IsSecretSettingKey(key)) ++result.secrets;
      continue;
    }
    s.remove(key);
  }

  QSettings in(filename, QSettings::IniFormat);
  const QStringList import_keys = in.allKeys();
  for (const QString &key : import_keys) {
    if (key.startsWith(QLatin1String(kExportGroup) + u'/') || KeepOnReset(key)) continue;
    s.setValue(key, in.value(key));
    ++result.count;
  }
  s.sync();

  if (s.status() != QSettings::NoError) {
    result.error = tr("Could not write the settings.");
    return result;
  }

  qLog(Info) << "Imported" << result.count << "settings from" << filename << ", backup in" << result.backup_dir;
  result.success = true;
  return result;

}

SettingsTransfer::Result SettingsTransfer::Reset() {

  Result result;

  result.backup_dir = NewBackupDir();
  if (!PlayerImporter::BackupSettings(result.backup_dir, &result.error)) return result;

  Settings s;
  const QStringList keys = s.allKeys();
  for (const QString &key : keys) {
    if (KeepOnReset(key) || key.startsWith(QLatin1String(kSmartPlaylistsSettingsGroup))) {
      if (PlayerImporter::IsSecretSettingKey(key)) ++result.secrets;
      continue;
    }
    s.remove(key);
    ++result.count;
  }
  s.sync();

  if (s.status() != QSettings::NoError) {
    result.error = tr("Could not write the settings.");
    return result;
  }

  qLog(Info) << "Reset" << result.count << "settings to the defaults, backup in" << result.backup_dir;
  result.success = true;
  return result;

}

void SettingsTransfer::SetPendingAction(const Action action, const QString &filename) {

  const QString pending_file = PendingFileName();
  if (action == Action::None) {
    QFile::remove(pending_file);
    return;
  }

  QDir().mkpath(QFileInfo(pending_file).absolutePath());
  QSettings pending(pending_file, QSettings::IniFormat);
  pending.setValue(QLatin1String(kPendingAction), action == Action::Reset ? u"reset"_s : u"import"_s);
  pending.setValue(QLatin1String(kPendingFilename), filename);
  pending.sync();

}

SettingsTransfer::Action SettingsTransfer::PendingAction(QString *filename) {

  const QString pending_file = PendingFileName();
  if (!QFile::exists(pending_file)) return Action::None;

  QSettings pending(pending_file, QSettings::IniFormat);
  const QString action = pending.value(QLatin1String(kPendingAction)).toString();
  if (filename) *filename = pending.value(QLatin1String(kPendingFilename)).toString();
  if (action == "reset"_L1) return Action::Reset;
  if (action == "import"_L1) return Action::Import;
  return Action::None;

}

bool SettingsTransfer::RunPendingAction() {

  QString filename;
  const Action action = PendingAction(&filename);
  if (action == Action::None) {
    QFile::remove(PendingFileName());
    return false;
  }
  // Done once, also when it fails.
  SetPendingAction(Action::None);

  const Result result = action == Action::Reset ? Reset() : Import(filename);
  const QString title = action == Action::Reset ? tr("Reset all settings") : tr("Import settings");
  if (result.success) {
    const QString text = action == Action::Reset ? tr("All settings were reset to the defaults.") : tr("The settings from %1 were imported.").arg(QDir::toNativeSeparators(filename));
    QMessageBox::information(nullptr, title, text + u"\n\n"_s + tr("The previous settings were backed up to %1.").arg(QDir::toNativeSeparators(result.backup_dir)));
  }
  else {
    QMessageBox::warning(nullptr, title, result.error + u"\n\n"_s + tr("The settings were not changed."));
  }

  return true;

}

bool SettingsTransfer::AskRestart(QWidget *parent, const QString &title, const QString &text) {

  QMessageBox box(QMessageBox::Question, title, text + u"\n\n"_s + tr("Akousma has to restart for this. Restart Akousma now?"), QMessageBox::NoButton, parent);
  QPushButton *button_restart = box.addButton(tr("&Restart now"), QMessageBox::AcceptRole);
  box.addButton(tr("&Later"), QMessageBox::RejectRole);
  box.setDefaultButton(button_restart);
  box.exec();
  if (box.clickedButton() == button_restart) return true;

  QMessageBox::information(parent, title, tr("This is done the next time Akousma starts."));
  return false;

}

void SettingsTransfer::ExportWithDialog(QWidget *parent) {

  const QString filename = QFileDialog::getSaveFileName(parent, tr("Export settings"), DefaultExportFileName(), tr("Akousma settings (*.ini)"));
  if (filename.isEmpty()) return;

  const Result result = Export(filename);
  if (result.success) {
    QMessageBox::information(parent, tr("Export settings"), tr("%1 settings were exported to %2.").arg(result.count).arg(QDir::toNativeSeparators(filename)) + u"\n\n"_s + tr("Passwords and logins are not in the file, they stay in the keyring and in this installation."));
  }
  else {
    QMessageBox::warning(parent, tr("Export settings"), result.error);
  }

}

bool SettingsTransfer::ImportWithDialog(QWidget *parent) {

  const QString title = tr("Import settings");
  const QString filename = QFileDialog::getOpenFileName(parent, title, QFileInfo(DefaultExportFileName()).absolutePath(), tr("Akousma settings (*.ini)"));
  if (filename.isEmpty()) return false;

  QString version;
  QString error;
  if (!CheckExportFile(filename, &version, &error)) {
    QMessageBox::warning(parent, title, error);
    return false;
  }

  const QString text = tr("Replace the current settings with the settings in %1 (exported from Akousma %2)?").arg(QDir::toNativeSeparators(filename), version) + u"\n\n"_s +
                       tr("The current settings are backed up first. The collection, playlists, album covers and saved passwords are not changed.");
  if (QMessageBox::question(parent, title, text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return false;

  SetPendingAction(Action::Import, filename);
  return AskRestart(parent, title, tr("The settings are imported when Akousma starts again."));

}

bool SettingsTransfer::ResetWithDialog(QWidget *parent) {

  const QString title = tr("Reset all settings");
  QMessageBox box(QMessageBox::Warning, title, tr("Reset all settings to the defaults?"), QMessageBox::Yes | QMessageBox::No, parent);
  box.setInformativeText(tr("All settings, including the layout, are set back to Akousma's defaults. The current settings are backed up first.") + u"\n\n"_s +
                         tr("The collection, playlists, smart playlists, album covers and saved passwords and logins are not changed."));
  box.setDefaultButton(QMessageBox::No);
  QCheckBox *checkbox_export = new QCheckBox(tr("Export my current settings first"), &box);
  checkbox_export->setChecked(true);
  box.setCheckBox(checkbox_export);
  if (box.exec() != QMessageBox::Yes) return false;

  if (checkbox_export->isChecked()) {
    // Cancelling the export cancels the reset.
    const QString filename = QFileDialog::getSaveFileName(parent, tr("Export settings"), DefaultExportFileName(), tr("Akousma settings (*.ini)"));
    if (filename.isEmpty()) return false;
    const Result result = Export(filename);
    if (!result.success) {
      QMessageBox::warning(parent, title, result.error + u"\n\n"_s + tr("The settings were not reset."));
      return false;
    }
    QMessageBox::information(parent, title, tr("Your current settings were exported to %1.").arg(QDir::toNativeSeparators(filename)));
  }

  SetPendingAction(Action::Reset);
  return AskRestart(parent, title, tr("The settings are reset when Akousma starts again."));

}
