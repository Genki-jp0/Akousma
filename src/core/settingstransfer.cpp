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

#include <utility>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QSet>
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
#include "constants/appearancesettings.h"
#include "settings/tabbarthemes.h"

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
constexpr char kPendingAcceptedConnectionChanges[] = "accepted_connection_changes";

// The import's own state (pending import, first start offer) belongs to this installation, it is neither exported nor reset.
constexpr char kImportSettingsGroup[] = "Import/";

// The user's smart playlists are kept by a reset, like the playlists in the collection database.
constexpr char kSmartPlaylistsSettingsGroup[] = "SerializedSmartPlaylists/";

// The user's saved sidebar themes: kept by a reset; an export also carries their images.
bool IsTabBarThemeKey(const QString &key) {
  return key.startsWith(QLatin1String(TabBarThemes::kSettingsGroup) + u'/');
}

bool IsTabBarThemeImageKey(const QString &key) {
  return IsTabBarThemeKey(key) && key.endsWith(u'/' + QLatin1String(TabBarThemes::kImageKey));
}

bool IsTabBarThemeImageDataKey(const QString &key) {
  return IsTabBarThemeKey(key) && key.endsWith(u'/' + QLatin1String(TabBarThemes::kImageDataKey));
}

bool IsInstallationState(const QString &key) {
  return key.startsWith(QLatin1String(kImportSettingsGroup));
}

// The settings groups an import may write: the groups Akousma itself uses (the kSettingsGroup constants and the groups of the services,
// providers, dialogs and widgets). A group that isn't here is refused, a new group has to be added here to be imported.
// Not here: the export header, Credentials (secrets, never exported) and Import (this installation's import state).
const QSet<QString> &KnownSettingsGroups() {
  static const QSet<QString> groups = QSet<QString>() << u"AcoustID"_s
                                                      << u"AlbumCoverExport"_s
                                                      << u"Analyzer"_s
                                                      << u"Appearance"_s
                                                      << u"Backend"_s
                                                      << u"Behaviour"_s
                                                      << u"Collection"_s
                                                      << u"Context"_s
                                                      << u"CoverManager"_s
                                                      << u"Covers"_s
                                                      << u"CredentialsManager"_s
                                                      << u"DiscordRPC"_s
                                                      << u"Discogs"_s
                                                      << u"EditTagDialog"_s
                                                      << u"Equalizer"_s
                                                      << u"FileView"_s
                                                      << u"GeniusLyrics"_s
                                                      << u"GlobalShortcuts"_s
                                                      << u"Jellyfin"_s
                                                      << u"LastFM"_s
                                                      << u"ListenBrainz"_s
                                                      << u"Lyrics"_s
                                                      << u"LyricsEditor"_s
                                                      << u"MainWindow"_s
                                                      << u"Moodbar"_s
                                                      << u"Musixmatch"_s
                                                      << u"NetworkProxy"_s
                                                      << u"OSD"_s
                                                      << u"OSDPretty"_s
                                                      << u"OpenTidal"_s
                                                      << u"OrganizeDialog"_s
                                                      << u"Player"_s
                                                      << u"PlayingWidget"_s
                                                      << u"Playlist"_s
                                                      << u"PlaylistSaveOptionsDialog"_s
                                                      << u"PlaylistSequence"_s
                                                      << u"Plex"_s
                                                      << u"Qobuz"_s
                                                      << u"RadioBrowser"_s
                                                      << u"RadioParadise"_s
                                                      << u"SavedGroupings"_s
                                                      << u"Scrobbler"_s
                                                      << u"Seekbar"_s
                                                      << u"SerializedSmartPlaylists"_s
                                                      << u"SettingsDialog"_s
                                                      << u"SomaFM"_s
                                                      << u"Spotify"_s
                                                      << u"Subsonic"_s
                                                      << u"TabBarThemes"_s
                                                      << u"Tidal"_s
                                                      << u"Transcoder"_s
                                                      << u"UpdateChecker"_s
                                                      << u"Waveform"_s;
  return groups;
}

// The saved groupings of a collection view: SavedGroupingManager::GetSavedGroupingsSettingsGroup(), SavedGroupings_<view's group>.
constexpr char kSavedGroupingsPrefix[] = "SavedGroupings_";

// Settings that decide where Akousma sends the saved passwords, tokens and logins, as whom, or how: the server addresses, the user
// names, the authentication method, the certificate checks, the OAuth client and API credential choice and the credentials backend.
// An import changes them only when the user accepts each change.
// Keys renamed by SettingsMigration are listed with both spellings: a file exported before the rename is imported with the old
// spelling, and SettingsMigration renames it at the next start.
const QSet<QString> &ConnectionSettingKeys() {
  static const QSet<QString> keys = QSet<QString>() << u"Jellyfin/url"_s
                                                    << u"Jellyfin/username"_s
                                                    << u"Jellyfin/verify_certificate"_s
                                                    << u"Jellyfin/verifycertificate"_s
                                                    << u"Jellyfin/http_url_accepted"_s
                                                    << u"Subsonic/url"_s
                                                    << u"Subsonic/username"_s
                                                    << u"Subsonic/verify_certificate"_s
                                                    << u"Subsonic/verifycertificate"_s
                                                    << u"Subsonic/auth_method"_s
                                                    << u"Subsonic/authmethod"_s
                                                    << u"Plex/server_url"_s
                                                    << u"Plex/server_name"_s
                                                    << u"Plex/server_machine_identifier"_s
                                                    << u"Plex/verify_certificate"_s
                                                    << u"Plex/verifycertificate"_s
                                                    << u"Tidal/oauth"_s
                                                    << u"Tidal/username"_s
                                                    << u"Tidal/client_id"_s
                                                    << u"Tidal/use_custom_client_id"_s
                                                    << u"Spotify/client_id"_s
                                                    << u"Spotify/use_custom_api_credentials"_s
                                                    << u"Qobuz/app_id"_s
                                                    << u"Qobuz/username"_s
                                                    << u"Qobuz/user_id"_s
                                                    << u"Qobuz/credentials_id"_s
                                                    << u"LastFM/username"_s
                                                    << u"LastFM/client_id"_s
                                                    << u"LastFM/use_custom_api_credentials"_s
                                                    << u"ListenBrainz/client_id"_s
                                                    << u"ListenBrainz/use_custom_api_credentials"_s
                                                    << u"GeniusLyrics/client_id"_s
                                                    << u"GeniusLyrics/use_custom_api_credentials"_s
                                                    << u"Musixmatch/client_id"_s
                                                    << u"Musixmatch/use_custom_api_credentials"_s
                                                    << u"Discogs/client_id"_s
                                                    << u"Discogs/use_custom_api_credentials"_s
                                                    << u"OpenTidal/client_id"_s
                                                    << u"OpenTidal/use_custom_api_credentials"_s
                                                    << u"AcoustID/use_custom_api_key"_s
                                                    << u"Backend/strict_ssl"_s
                                                    << u"CredentialsManager/backend"_s;
  return keys;
}

// The whole group: the proxy gets all traffic and the saved proxy password.
constexpr char kNetworkProxyGroup[] = "NetworkProxy/";

// Whether two setting values are the same. The settings and an INI file don't always give the same type for a value (an INI file has
// no integers or booleans, only their text), so values that both have a text are compared as text.
bool SameSettingValue(const QVariant &a, const QVariant &b) {

  if (a.isValid() != b.isValid()) return false;
  if (!a.isValid()) return true;
  if (a.typeId() == b.typeId()) return a == b;
  if (a.canConvert<QString>() && b.canConvert<QString>() && a.typeId() != QMetaType::QStringList && b.typeId() != QMetaType::QStringList) {
    return a.toString() == b.toString();
  }
  return false;

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

bool SettingsTransfer::IsKnownSettingsGroup(const QString &key) {

  const qsizetype slash = key.indexOf(u'/');
  if (slash <= 0) return false;
  const QString group = key.left(slash);
  return KnownSettingsGroups().contains(group) || (group.startsWith(QLatin1String(kSavedGroupingsPrefix)) && group.length() > static_cast<qsizetype>(qstrlen(kSavedGroupingsPrefix)));

}

bool SettingsTransfer::IsConnectionSettingKey(const QString &key) {
  return key.startsWith(QLatin1String(kNetworkProxyGroup)) || ConnectionSettingKeys().contains(key);
}

QVariantMap SettingsTransfer::SettingsValues(const QSettings &settings) {

  QVariantMap values;
  const QStringList keys = settings.allKeys();
  for (const QString &key : keys) {
    values.insert(key, settings.value(key));
  }
  return values;

}

QString SettingsTransfer::ConnectionChangeId(const QString &key, const QVariant &imported) {

  QByteArray data;
  {
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << key << imported.isValid();
    if (imported.isValid()) stream << imported;
  }
  return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());

}

SettingsTransfer::ImportPlan SettingsTransfer::PlanImport(const QVariantMap &current, const QVariantMap &imported, const QStringList &accepted_connection_changes) {

  ImportPlan plan;

  // Connection settings: compared with the current value, a change is only made when the user accepted it. The file not having one
  // that is set now is a change too, it would go back to the default.
  QSet<QString> connection_keys;
  for (auto it = current.constBegin(); it != current.constEnd(); ++it) {
    if (IsConnectionSettingKey(it.key()) && !KeepOnReset(it.key())) connection_keys << it.key();
  }
  for (auto it = imported.constBegin(); it != imported.constEnd(); ++it) {
    if (IsConnectionSettingKey(it.key()) && !KeepOnReset(it.key())) connection_keys << it.key();
  }
  QStringList connection_keys_sorted(connection_keys.cbegin(), connection_keys.cend());
  connection_keys_sorted.sort();
  QSet<QString> held_back_keys;
  for (const QString &key : std::as_const(connection_keys_sorted)) {
    const QVariant current_value = current.value(key);
    const QVariant imported_value = imported.value(key);
    if (SameSettingValue(current_value, imported_value)) continue;
    ConnectionChange change;
    change.key = key;
    change.current = current_value;
    change.imported = imported_value;
    change.id = ConnectionChangeId(key, imported_value);
    plan.connection_changes << change;
    if (!accepted_connection_changes.contains(change.id)) {
      plan.held_back << change;
      held_back_keys << key;
    }
  }

  // The current settings: replaced, except the saved passwords and logins, this installation's import state, groups the import can't
  // write (they would be lost) and the held back connection settings.
  for (auto it = current.constBegin(); it != current.constEnd(); ++it) {
    const QString &key = it.key();
    if (KeepOnReset(key)) {
      if (PlayerImporter::IsSecretSettingKey(key)) ++plan.secrets;
      continue;
    }
    if (!IsKnownSettingsGroup(key) || held_back_keys.contains(key)) continue;
    plan.remove << key;
  }

  // The settings in the file: only known groups, never secrets, the export header or the import state.
  QSet<QString> refused_groups;
  for (auto it = imported.constBegin(); it != imported.constEnd(); ++it) {
    const QString &key = it.key();
    if (key.startsWith(QLatin1String(kExportGroup) + u'/') || KeepOnReset(key)) continue;
    if (!IsKnownSettingsGroup(key)) {
      plan.refused_keys << key;
      refused_groups << (key.contains(u'/') ? key.left(key.indexOf(u'/')) : key);
      continue;
    }
    if (held_back_keys.contains(key)) continue;
    plan.write.insert(key, it.value());
  }
  plan.refused_groups = QStringList(refused_groups.cbegin(), refused_groups.cend());
  plan.refused_groups.sort();

  return plan;

}

QString SettingsTransfer::SettingValueText(const QVariant &value) {

  if (!value.isValid()) return tr("(not set)");
  QString text;
  if (value.typeId() == QMetaType::QStringList) {
    text = value.toStringList().join(u", "_s);
  }
  else if (value.canConvert<QString>()) {
    text = value.toString();
  }
  else {
    return tr("(%1 value)").arg(QString::fromLatin1(value.typeName()));
  }
  if (text.isEmpty()) return tr("(empty)");
  constexpr qsizetype kMaxLength = 120;
  if (text.length() > kMaxLength) text = text.left(kMaxLength) + QChar(0x2026);
  return text;

}

QString SettingsTransfer::ConnectionChangesText(const QList<ConnectionChange> &changes) {

  QStringList lines;
  for (const ConnectionChange &change : changes) {
    lines << tr("%1: %2 \u2192 %3").arg(change.key, SettingValueText(change.current), SettingValueText(change.imported));
  }
  return lines.join(u'\n');

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
  out.setValue(QLatin1String(kExportBasedOn), QLatin1String(AKOUSMA_BASE_STRAWBERRY_DISPLAY));
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
    // The image of a saved sidebar theme goes into the file too, so the theme works on another computer.
    if (IsTabBarThemeImageKey(key)) {
      QFile image_file(s.value(key).toString());
      if (!image_file.fileName().isEmpty() && image_file.open(QIODevice::ReadOnly)) {
        out.setValue(key.left(key.length() - static_cast<qsizetype>(qstrlen(TabBarThemes::kImageKey))) + QLatin1String(TabBarThemes::kImageDataKey), image_file.readAll());
        image_file.close();
      }
    }
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

SettingsTransfer::Result SettingsTransfer::Import(const QString &filename, const QStringList &accepted_connection_changes) {

  Result result;
  if (!CheckExportFile(filename, &result.version, &result.error)) return result;

  result.backup_dir = NewBackupDir();
  if (!PlayerImporter::BackupSettings(result.backup_dir, &result.error)) return result;

  Settings s;
  QSettings in(filename, QSettings::IniFormat);
  const ImportPlan plan = PlanImport(SettingsValues(s), SettingsValues(in), accepted_connection_changes);
  result.secrets = plan.secrets;
  result.refused = plan.refused_groups;
  for (const ConnectionChange &change : plan.held_back) {
    result.held_back << change.key;
  }

  // The settings are replaced, except the saved passwords and logins, this installation's import state, groups this version doesn't
  // know and the connection settings the user didn't accept.
  for (const QString &key : plan.remove) {
    s.remove(key);
  }

  QStringList theme_image_data_keys;
  for (auto it = plan.write.constBegin(); it != plan.write.constEnd(); ++it) {
    const QString &key = it.key();
    if (IsTabBarThemeImageDataKey(key)) {
      theme_image_data_keys << key;
      continue;
    }
    // The album cover sidebar background was removed, an older export that has it gets the plain gradient.
    if (key == QLatin1String(AppearanceSettings::kSettingsGroup) + u'/' + QLatin1String(AppearanceSettings::kTabBarBackground)) {
      s.setValue(key, static_cast<int>(AppearanceSettings::TabBarBackgroundFromSetting(it.value().toInt())));
      ++result.count;
      continue;
    }
    s.setValue(key, it.value());
    ++result.count;
  }

  // The images of the saved sidebar themes: written to the theme image folder, the themes point to them there.
  for (const QString &data_key : std::as_const(theme_image_data_keys)) {
    const QString image_key = data_key.left(data_key.length() - static_cast<qsizetype>(qstrlen(TabBarThemes::kImageDataKey))) + QLatin1String(TabBarThemes::kImageKey);
    const QString exported_image = in.value(image_key).toString();
    const QString filename_only = QFileInfo(exported_image).fileName().isEmpty() ? u"theme.img"_s : QFileInfo(exported_image).fileName();
    QString image = TabBarThemes::ImageDir() + u'/' + filename_only;
    if (QFileInfo::exists(image)) {
      // The same file from this computer, or another one with the same name: keep both.
      QFile existing(image);
      if (existing.open(QIODevice::ReadOnly) && existing.readAll() == in.value(data_key).toByteArray()) {
        s.setValue(image_key, image);
        continue;
      }
      image = TabBarThemes::ImageDir() + u'/' + QDateTime::currentDateTime().toString(u"yyyyMMdd-HHmmss-"_s) + filename_only;
    }
    QDir().mkpath(TabBarThemes::ImageDir());
    QFile image_file(image);
    if (image_file.open(QIODevice::WriteOnly)) {
      image_file.write(in.value(data_key).toByteArray());
      image_file.close();
      s.setValue(image_key, image);
    }
  }
  s.sync();

  if (s.status() != QSettings::NoError) {
    result.error = tr("Could not write the settings.");
    return result;
  }

  if (!result.held_back.isEmpty()) {
    qLog(Warning) << "Settings import from" << filename << "kept the current connection settings" << result.held_back << ", the changes were not accepted";
  }
  if (!result.refused.isEmpty()) {
    qLog(Warning) << "Settings import from" << filename << "refused unknown settings groups" << result.refused;
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
    if (KeepOnReset(key) || key.startsWith(QLatin1String(kSmartPlaylistsSettingsGroup)) || IsTabBarThemeKey(key)) {
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

void SettingsTransfer::SetPendingAction(const Action action, const QString &filename, const QStringList &accepted_connection_changes) {

  const QString pending_file = PendingFileName();
  if (action == Action::None) {
    QFile::remove(pending_file);
    return;
  }

  QDir().mkpath(QFileInfo(pending_file).absolutePath());
  QSettings pending(pending_file, QSettings::IniFormat);
  pending.setValue(QLatin1String(kPendingAction), action == Action::Reset ? u"reset"_s : u"import"_s);
  pending.setValue(QLatin1String(kPendingFilename), filename);
  pending.setValue(QLatin1String(kPendingAcceptedConnectionChanges), accepted_connection_changes);
  pending.sync();

}

SettingsTransfer::Action SettingsTransfer::PendingAction(QString *filename, QStringList *accepted_connection_changes) {

  const QString pending_file = PendingFileName();
  if (!QFile::exists(pending_file)) return Action::None;

  QSettings pending(pending_file, QSettings::IniFormat);
  const QString action = pending.value(QLatin1String(kPendingAction)).toString();
  if (filename) *filename = pending.value(QLatin1String(kPendingFilename)).toString();
  if (accepted_connection_changes) *accepted_connection_changes = pending.value(QLatin1String(kPendingAcceptedConnectionChanges)).toStringList();
  if (action == "reset"_L1) return Action::Reset;
  if (action == "import"_L1) return Action::Import;
  return Action::None;

}

bool SettingsTransfer::RunPendingAction() {

  QString filename;
  QStringList accepted_connection_changes;
  const Action action = PendingAction(&filename, &accepted_connection_changes);
  if (action == Action::None) {
    QFile::remove(PendingFileName());
    return false;
  }
  // Done once, also when it fails.
  SetPendingAction(Action::None);

  const Result result = action == Action::Reset ? Reset() : Import(filename, accepted_connection_changes);
  const QString title = action == Action::Reset ? tr("Reset all settings") : tr("Import settings");
  if (result.success) {
    QString text = action == Action::Reset ? tr("All settings were reset to the defaults.") : tr("The settings from %1 were imported.").arg(QDir::toNativeSeparators(filename));
    if (!result.held_back.isEmpty()) {
      text += u"\n\n"_s + tr("These connection settings were not imported, the current ones were kept: %1").arg(result.held_back.join(u", "_s));
    }
    if (!result.refused.isEmpty()) {
      text += u"\n\n"_s + tr("These parts of the file are unknown to this version of Akousma and were not imported: %1").arg(result.refused.join(u", "_s));
    }
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

  // What the import would do with the current settings, nothing accepted yet.
  ImportPlan plan;
  {
    Settings s;
    QSettings in(filename, QSettings::IniFormat);
    plan = PlanImport(SettingsValues(s), SettingsValues(in));
  }

  QString text = tr("Replace the current settings with the settings in %1 (exported from Akousma %2)?").arg(QDir::toNativeSeparators(filename), version) + u"\n\n"_s +
                 tr("The current settings are backed up first. The collection, playlists, album covers and saved passwords are not changed.");
  if (!plan.refused_groups.isEmpty()) {
    text += u"\n\n"_s + tr("These parts of the file are unknown to this version of Akousma and are not imported: %1").arg(plan.refused_groups.join(u", "_s));
  }
  if (QMessageBox::question(parent, title, text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return false;

  // Connection settings: the saved passwords and logins would be sent to what the file says, the user has to see and accept the changes.
  QStringList accepted_connection_changes;
  if (!plan.connection_changes.isEmpty()) {
    QMessageBox box(QMessageBox::Warning, title, tr("The file changes how Akousma connects to servers and logs in."), QMessageBox::NoButton, parent);
    box.setInformativeText(tr("Your saved passwords and logins would be sent to the servers in the file. Only import these changes if you trust the file and know the servers:") + u"\n\n"_s +
                           ConnectionChangesText(plan.connection_changes) + u"\n\n"_s +
                           tr("If you keep your connection settings, everything else in the file is imported."));
    QPushButton *button_keep = box.addButton(tr("&Keep my connection settings"), QMessageBox::AcceptRole);
    QPushButton *button_import = box.addButton(tr("&Import the connection settings too"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(button_keep);
    box.setEscapeButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == button_import) {
      for (const ConnectionChange &change : std::as_const(plan.connection_changes)) {
        accepted_connection_changes << change.id;
      }
    }
    else if (box.clickedButton() != button_keep) {
      return false;
    }
  }

  SetPendingAction(Action::Import, filename, accepted_connection_changes);
  return AskRestart(parent, title, tr("The settings are imported when Akousma starts again."));

}

bool SettingsTransfer::ResetWithDialog(QWidget *parent) {

  const QString title = tr("Reset all settings");
  QMessageBox box(QMessageBox::Warning, title, tr("Reset all settings to the defaults?"), QMessageBox::Yes | QMessageBox::No, parent);
  box.setInformativeText(tr("All settings, including the layout, are set back to Akousma's defaults. The current settings are backed up first.") + u"\n\n"_s +
                         tr("The collection, playlists, smart playlists, album covers, your saved sidebar themes and saved passwords and logins are not changed."));
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
