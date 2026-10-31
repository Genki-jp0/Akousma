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

#ifndef PLAYERIMPORTER_H
#define PLAYERIMPORTER_H

#include <optional>

#include <QtGlobal>
#include <QCoreApplication>
#include <QFlags>
#include <QList>
#include <QString>
#include <QStringList>

class QSettings;
class QSqlDatabase;

// Imports the settings, the collection database and the album cover caches of Strawberry or Clementine into Akousma.
// The import replaces Akousma's settings and collection, so it only runs while Akousma has no database connections open: at startup, before the Application is created.
// Existing Akousma settings and the collection database are backed up first.
class PlayerImporter {
  Q_DECLARE_TR_FUNCTIONS(PlayerImporter)

 public:
  enum class Player {
    Strawberry,
    Clementine
  };

  enum class Item {
    None = 0x0,
    Settings = 0x1,
    Collection = 0x2,
    Covers = 0x4,
    All = Settings | Collection | Covers
  };
  Q_DECLARE_FLAGS(Items, Item)

  // An installation of Strawberry or Clementine found for the current user.
  struct Installation {
    Player player = Player::Strawberry;
    QString name;              // Shown to the user, for example "Strawberry" or "Strawberry (Flatpak)".
    QString settings_file;     // Settings file (INI on Linux, plist on macOS), empty when the settings are in the Windows registry.
    QString settings_org;      // Organization and application for settings in the Windows registry.
    QString settings_app;
    QString settings_location; // Where the settings are, for display.
    bool has_settings = false;
    QString data_dir;          // Strawberry's application data directory, Clementine's configuration directory.
    QString cache_dir;         // Strawberry's cache directory, used to rewrite paths in the settings.
    QString config_dir;        // Directory holding the settings file, used to rewrite paths in the settings.
    QString database_file;
    QStringList cover_dirs;    // Album cover cache directories that exist.

    bool has_database() const { return !database_file.isEmpty(); }
    bool has_covers() const { return !cover_dirs.isEmpty(); }
    bool IsValid() const { return has_settings || has_database() || has_covers(); }
    Items available_items() const;
  };

  struct Result {
    bool success = true;
    Items imported;
    QStringList messages;
    QStringList errors;
    QString backup_dir;
  };

  // Strawberry and Clementine installations found for the current user, Strawberry first.
  static QList<Installation> Detect();
  static std::optional<Installation> Detect(const Player player);

  static QString PlayerName(const Player player);
  static std::optional<Player> PlayerFromString(const QString &name);
  static QString PlayerToString(const Player player);

  // Parses a comma separated list of "settings", "collection", "covers" or "all".
  static Items ItemsFromString(const QString &items, bool *ok = nullptr);
  static QString ItemsToString(const Items items);

  // Where Akousma keeps its collection database and album covers.
  static QString AkousmaDataDir();
  static QString AkousmaDatabaseFile();

  // Whether Akousma has settings or a collection already, the first start prompt is only shown when it has neither.
  static bool AkousmaHasData();

  // Runs the import, see the class comment for when this is safe.
  static Result Import(const Installation &installation, const Items items);

  // An import chosen in the running application, done the next time Akousma starts.
  static void SetPendingImport(const Installation &installation, const Items items);
  static std::optional<Installation> TakePendingImport(Items *items);

  // The installation for the --import-from command line option: strawberry, clementine or auto (Strawberry, else Clementine).
  static std::optional<Installation> FindInstallation(const QString &player_option, QString *error);

  // Runs the import for --import-from with --import-quiet: no user interface, the result is written to the log.
  // Returns the exit code for the process.
  static int ImportQuiet(const QString &player_option, const QString &items_option);

  // The first start prompt is shown once.
  static bool FirstRunPromptDone();
  static void SetFirstRunPromptDone();

  // Set when Akousma should start again after it exits, to run a pending import.
  static void SetRestartRequested(const bool restart);
  static bool RestartRequested();

 private:
  static std::optional<Installation> DetectStrawberry(const QString &name, const QString &settings_file, const QString &data_dir, const QString &cache_dir);
  static std::optional<Installation> DetectClementine(const QString &name, const QString &settings_file, const QString &config_dir);
  static void AddCoverDirs(Installation *installation, const QStringList &dir_names);

  static bool BackupSettings(const QString &backup_dir, QString *error);
  static bool BackupDatabase(const QString &backup_dir, QString *error);

  static bool ImportStrawberrySettings(const Installation &installation, Result *result);
  static bool ImportClementineSettings(const Installation &installation, Result *result);
  static bool ImportStrawberryDatabase(const Installation &installation, const bool covers, Result *result);
  static bool ImportClementineDatabase(const Installation &installation, const bool covers, Result *result);
  static bool ImportCovers(const Installation &installation, Result *result);

  static QSettings *OpenSettings(const Installation &installation);
  static QString ClementineLanguage(const QString &language);
  static bool ReplaceDatabaseFile(const QString &new_file, Result *result);
  static int RewriteCoverUrls(QSqlDatabase &db, const QString &old_dir, const QString &new_dir, Result *result);
  static int CopyDirectory(const QString &source, const QString &destination, QString *error);
};

Q_DECLARE_OPERATORS_FOR_FLAGS(PlayerImporter::Items)

#endif  // PLAYERIMPORTER_H
