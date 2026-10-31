/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2018-2026, Jonas Kvinge <jonas@jkvinge.net>
 *
 * Strawberry is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Strawberry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"
#include "version.h"

#include <QtGlobal>

#include <cstdlib>
#include <ctime>

#ifdef Q_OS_UNIX
#  include <unistd.h>
#endif

#ifdef Q_OS_MACOS
#  include <sys/resource.h>
#  include <sys/sysctl.h>
#endif

#ifdef Q_OS_WIN32
#  include <windows.h>
#  include <iostream>
#endif  // Q_OS_WIN32

#include <glib.h>

#include <utility>
#include <iostream>
#include <memory>

#include <QObject>
#include <QApplication>
#include <QCoreApplication>
#include <QSysInfo>
#include <QLibraryInfo>
#include <QFileDevice>
#include <QIODevice>
#include <QByteArray>
#include <QNetworkProxy>
#include <QFile>
#include <QDir>
#include <QString>
#include <QSettings>
#include <QLoggingCategory>
#include <QStyle>
#include <QStyleHints>
#include <QMessageBox>
#include <QProcess>
#include <QElapsedTimer>
#ifdef HAVE_TRANSLATIONS
#  include <QTranslator>
#endif

#include "main.h"

#include "includes/scoped_ptr.h"
#include "includes/shared_ptr.h"

#include "core/logging.h"
#include "core/standardpaths.h"
#include "core/settings.h"

#include "utilities/envutils.h"
#include "utilities/styleutils.h"

#include <kdsingleapplication.h>

#ifdef Q_OS_UNIX
  #include "core/unixsignalwatcher.h"
#endif

#ifdef HAVE_QTSPARKLE
#  include <qtsparkle-qt6/Updater>
#endif  // HAVE_QTSPARKLE

#ifdef Q_OS_MACOS
#  include "utilities/macosutils.h"
#  include "core/mac_startup.h"
#endif

#ifdef HAVE_MPRIS2
#  include "mpris2/mpris2.h"
#endif

#ifdef HAVE_DISCORD_RPC
#  include "discord/discordrichpresence.h"
#endif

#include "core/iconloader.h"
#include "core/commandlineoptions.h"
#include "core/networkproxyfactory.h"

#include "core/application.h"
#include "core/metatypes.h"
#include "core/mainwindow.h"

#ifdef Q_OS_MACOS
#  include "systemtrayicon/macsystemtrayicon.h"
#else
#  include "systemtrayicon/qtsystemtrayicon.h"
#endif

#ifdef HAVE_TRANSLATIONS
#  include "core/translations.h"
#endif

#include "constants/behavioursettings.h"
#include "constants/appearancesettings.h"

#if defined(Q_OS_MACOS)
#  include "osd/osdmac.h"
#elif defined(HAVE_DBUS)
#  include "osd/osddbus.h"
#else
#  include "osd/osdbase.h"
#endif

#include "engine/gststartup.h"

#include "import/playerimporter.h"
#include "core/settingstransfer.h"
#include "import/importdialog.h"

using namespace Qt::Literals::StringLiterals;
using std::as_const;
using std::make_unique;
using std::make_shared;

int main(int argc, char *argv[]) {

#ifdef Q_OS_MACOS
  // Do Mac specific startup to get media keys working.
  // This must go before QApplication initialization.
  mac::MacMain();
#endif

  QCoreApplication::setApplicationName(u"Akousma"_s);
  QCoreApplication::setOrganizationName(u"Akousma"_s);
  QCoreApplication::setApplicationVersion(QStringLiteral(AKOUSMA_VERSION));
  QCoreApplication::setOrganizationDomain(u"genki_jp0.github.io"_s);

  // This makes us show up nicely in gnome-volume-control
  g_set_application_name("Akousma");
  g_setenv("PULSE_PROP_application.icon_name", "akousma", TRUE);
  g_setenv("PULSE_PROP_media.role", "music", TRUE);

  RegisterMetaTypes();

  // Initialize logging.  Log levels are set after the commandline options are parsed below.
  logging::Init();
  g_log_set_default_handler(reinterpret_cast<GLogFunc>(&logging::GLog), nullptr);

  CommandlineOptions options(argc, argv);
  {
    // Only start a core application now, so we can check if there's another instance without requiring an X server.
    // This MUST be done before parsing the commandline options so QTextCodec gets the right system locale for filenames.
    QCoreApplication core_app(argc, argv);
    // Parse commandline options - need to do this before starting the full QApplication, so it works without an X server
    if (!options.Parse()) return 1;
    logging::SetLevels(options.log_levels());
    if (!options.export_settings().isEmpty()) {
      // Reading the settings is safe while Akousma runs.
      const SettingsTransfer::Result result = SettingsTransfer::Export(options.export_settings());
      if (!result.success) {
        std::cerr << result.error.toLocal8Bit().constData() << std::endl;
        return 1;
      }
      std::cout << QObject::tr("%1 settings exported to %2, %3 passwords and logins left out.").arg(result.count).arg(options.export_settings()).arg(result.secrets).toLocal8Bit().constData() << std::endl;
      return 0;
    }
    ScopedPtr<KDSingleApplication> single_app = make_unique<KDSingleApplication>(QCoreApplication::applicationName().toLower(), KDSingleApplication::Option::IncludeUsernameInSocketName);
    if (options.restarted()) {
      // Akousma started itself again, for example to import from Strawberry or Clementine. Wait until the previous instance has exited.
      QElapsedTimer timer;
      timer.start();
      while (!single_app->isPrimaryInstance() && timer.elapsed() < 30000) {
        single_app.reset();
        QThread::msleep(250);
        single_app = make_unique<KDSingleApplication>(QCoreApplication::applicationName().toLower(), KDSingleApplication::Option::IncludeUsernameInSocketName);
      }
    }
    if (!single_app->isPrimaryInstance()) {
      if (!options.import_from().isEmpty()) {
        qLog(Error) << "Akousma is already running, close it to import from" << options.import_from();
        return 1;
      }
      if (!options.import_settings().isEmpty() || options.reset_settings()) {
        std::cerr << "Akousma is already running, close it to import or reset the settings." << std::endl;
        return 1;
      }
      if (options.is_empty()) {
        qLog(Info) << "Akousma is already running - activating existing window (1)";
      }
      if (!single_app->sendMessage(options.Serialize())) {
        qLog(Error) << "Could not send message to primary instance.";
      }
      return 0;
    }
    if (!options.import_settings().isEmpty() || options.reset_settings()) {
      // Import or reset the settings and exit. The collection, playlists, covers and saved passwords are not changed.
      const SettingsTransfer::Result result = options.reset_settings() ? SettingsTransfer::Reset() : SettingsTransfer::Import(options.import_settings());
      if (!result.success) {
        std::cerr << result.error.toLocal8Bit().constData() << std::endl;
        return 1;
      }
      std::cout << (options.reset_settings() ? QObject::tr("All settings were reset to the defaults.") : QObject::tr("%1 settings imported from %2.").arg(result.count).arg(options.import_settings())).toLocal8Bit().constData() << std::endl;
      std::cout << QObject::tr("The previous settings were backed up to %1.").arg(QDir::toNativeSeparators(result.backup_dir)).toLocal8Bit().constData() << std::endl;
      return 0;
    }
    if (options.import_quiet() && !options.import_from().isEmpty()) {
      // Import without a user interface and exit, used by the Windows installer.
      Q_INIT_RESOURCE(data);
#if defined(HAVE_TRANSLATIONS) && !defined(INSTALL_TRANSLATIONS)
      Q_INIT_RESOURCE(translations);
#endif
      return PlayerImporter::ImportQuiet(options.import_from(), options.import_items(), options.import_passwords());
    }
  }

#ifdef Q_OS_MACOS
  // Must happen after QCoreApplication::setOrganizationName().
  Utilities::SetEnv("XDG_CONFIG_HOME", StandardPaths::WritableLocation(StandardPaths::StandardLocation::AppConfigLocation));
#endif

  // Output the version, so when people attach log output to bug reports they don't have to tell us which version they're using.
  qLog(Info) << "Akousma" << AKOUSMA_VERSION_DISPLAY << "based on Strawberry" << AKOUSMA_BASE_STRAWBERRY_VERSION << AKOUSMA_BASE_STRAWBERRY_COMMIT << "Qt" << QLibraryInfo::version().toString();
  qLog(Info) << QStringLiteral("%1 %2 - (%3 %4) [%5]").arg(QSysInfo::prettyProductName(), QSysInfo::productVersion(), QSysInfo::kernelType(), QSysInfo::kernelVersion(), QSysInfo::currentCpuArchitecture());

  // Seed the random number generators.
  time_t t = time(nullptr);
  srand(static_cast<uint>(t));

#ifdef Q_OS_MACOS
  Utilities::IncreaseFDLimit();
#endif

  QGuiApplication::setApplicationDisplayName(u"Akousma"_s);
  QGuiApplication::setDesktopFileName(u"io.github.genki_jp0.Akousma"_s);
  QGuiApplication::setQuitOnLastWindowClosed(false);

  QApplication a(argc, argv);

#ifdef Q_OS_LINUX
  if (Utilities::IsWSL()) {
    const QString message = u"Akousma is not supported when running under the Windows Subsystem for Linux (WSL). Please use the native Windows version instead."_s;
    qLog(Error) << message;
    QMessageBox::critical(nullptr, u"Unsupported environment"_s, message);
    return 1;
  }
#endif

  KDSingleApplication single_app(QCoreApplication::applicationName().toLower(), KDSingleApplication::Option::IncludeUsernameInSocketName);
  if (!single_app.isPrimaryInstance()) {
    if (options.is_empty()) {
      qLog(Info) << "Akousma is already running - activating existing window (2)";
    }
    if (!single_app.sendMessage(options.Serialize())) {
      qLog(Error) << "Could not send message to primary instance.";
    }
    return 0;
  }

  QThread::currentThread()->setObjectName(u"Main"_s);

  // A settings reset or import chosen in the running application before it restarted, done before anything reads the settings.
  SettingsTransfer::RunPendingAction();

  // Whether this is the first start of Akousma, checked before anything writes settings.
  const bool first_start = !PlayerImporter::AkousmaHasData();

  if (QGuiApplication::platformName() != "wayland"_L1) {
    QGuiApplication::setWindowIcon(IconLoader::Load(u"akousma"_s));
  }

#if defined(USE_BUNDLE)
  qLog(Debug) << "Looking for resources in" << QCoreApplication::libraryPaths();
#endif

  GstStartup::Initialize();

  // Gnome on Ubuntu has menu icons disabled by default.  I think that's a bad idea, and makes some menus in Strawberry look confusing.
  QCoreApplication::setAttribute(Qt::AA_DontShowIconsInMenus, false);

  const QString default_style = QApplication::style() ? QApplication::style()->objectName() : QString();
  {
    Settings s;
    s.beginGroup(AppearanceSettings::kSettingsGroup);
    const QString style_name = s.value(AppearanceSettings::kStyle).toString();
    const bool dark_mode = s.value(AppearanceSettings::kDarkMode, false).toBool();
    s.endGroup();
    if (!style_name.isEmpty() && style_name.compare("default"_L1, Qt::CaseInsensitive) != 0) {
      if (!QApplication::setStyle(style_name)) {
        qLog(Error) << "Could not set style" << style_name << "- falling back to default style" << default_style;
        if (!QApplication::setStyle(default_style)) {
          qLog(Error) << "Could not set default style" << default_style;
        }
      }
    }
    if (dark_mode && QApplication::style()) {
      const QString current_style = QApplication::style() ? QApplication::style()->objectName() : QString();
      const bool dark_mode_supported = Utilities::StyleHasDarkModeSupport(current_style);
      if (dark_mode_supported) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif
      }
    }
    if (QApplication::style()) {
      qLog(Debug) << "Style:" << QApplication::style()->objectName();
    }
  }

  // Resources
  Q_INIT_RESOURCE(data);
  Q_INIT_RESOURCE(icons);
#if defined(HAVE_TRANSLATIONS) && !defined(INSTALL_TRANSLATIONS)
  Q_INIT_RESOURCE(translations);
#endif

  IconLoader::Init();

#ifdef HAVE_TRANSLATIONS

  QStringList languages;

  // Load language from command line options
  if (!options.language().isEmpty()) {
    languages << options.language();
  }

  // Load language from settings
  if (languages.isEmpty()) {
    Settings s;
    s.beginGroup(BehaviourSettings::kSettingsGroup);
    const QString language = s.value(BehaviourSettings::kLanguage).toString();
    s.endGroup();
    if (!language.isEmpty()) {
      languages << language;
    }
  }

  // Use system UI languages
  if (languages.isEmpty()) {
#  if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    languages = QLocale::system().uiLanguages(QLocale::TagSeparator::Underscore);
#  else
    const QStringList system_languages = QLocale::system().uiLanguages();
    for (const QString &language : system_languages) {
      QString language_underscore = language;
      language_underscore = language_underscore.replace(u'-', u'_');
      languages << language_underscore;
    }
#  endif
  }

  if (languages.isEmpty()) {
    languages << QLocale::system().name();
  }

  ScopedPtr<Translations> translations = make_unique<Translations>();

#  if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
  QStringList qt_translation_paths = QLibraryInfo::paths(QLibraryInfo::TranslationsPath);
#  else
  QStringList qt_translation_paths = QStringList() << QLibraryInfo::path(QLibraryInfo::TranslationsPath);
#  endif
  qt_translation_paths.removeDuplicates();
  for (const QString &language : as_const(languages)) {
    bool translation_loaded = false;
    for (const QString &translation_path : as_const(qt_translation_paths)) {
      if (translations->LoadTranslation(u"qt"_s, translation_path, language)) {
        translation_loaded = true;
        break;
      }
    }
    if (translation_loaded) {
      break;
    }
  }

  QStringList translation_paths = QStringList() << u":/i18n"_s
                                                << QStringLiteral(TRANSLATIONS_DIR)
                                                << QCoreApplication::applicationDirPath()
                                                << QDir::currentPath();
  translation_paths.removeDuplicates();
  for (const QString &language : as_const(languages)) {
    bool language_loaded = false;
    for (const QString &translation_path : as_const(translation_paths)) {
      if (translations->LoadTranslation(u"akousma"_s, translation_path, language)) {
        language_loaded = true;
        break;
      }
    }
    if (language_loaded) {
      break;
    }
  }

#  ifdef HAVE_QTSPARKLE
  for (const QString &language : as_const(languages)) {
    if (qtsparkle::LoadTranslations(language)) {
      break;
    }
  }
#  endif  // HAVE_QTSPARKLE

#endif  // HAVE_TRANSLATIONS

  // Import from Strawberry or Clementine: an import chosen before Akousma restarted, one requested with --import-from, or the offer on the first start.
  // This has to be done before the collection database is opened.
  if (ImportDialog::ImportAtStartup(options, first_start)) {
    // Imported settings like the style and language are read before this, start again to use them.
    QProcess::startDetached(QCoreApplication::applicationFilePath(), QStringList() << u"--restarted"_s);
    return 0;
  }

  Application app;

  // Network proxy
  QNetworkProxyFactory::setApplicationProxyFactory(NetworkProxyFactory::Instance());

  // Create the tray icon and OSD
  SharedPtr<SystemTrayIcon> tray_icon = make_shared<SystemTrayIcon>();

#if defined(Q_OS_MACOS)
  OSDMac osd(tray_icon, &app);
#elif defined(HAVE_DBUS)
  OSDDBus osd(tray_icon, &app);
#else
  OSDBase osd(tray_icon, &app);
#endif

#ifdef HAVE_MPRIS2
  Mpris2 mpris2(app.player(), app.playlist_manager(), app.current_albumcover_loader());
#endif
#ifdef HAVE_DISCORD_RPC
  DiscordRichPresence discord_rich_presence(app.player(), app.playlist_manager());
#endif

  // Window
  MainWindow w(&app,
               tray_icon,
               &osd,
#ifdef HAVE_DISCORD_RPC
               &discord_rich_presence,
#endif
               options,
               default_style);

#ifdef Q_OS_UNIX
  UnixSignalWatcher unix_signal_watcher;
  unix_signal_watcher.WatchForSignal(SIGTERM);
  QObject::connect(&unix_signal_watcher, &UnixSignalWatcher::UnixSignal, &w, &MainWindow::Exit);
#endif

#if QT_CONFIG(sessionmanager)
  QObject::connect(&a, &QApplication::commitDataRequest, &w, &MainWindow::CommitData, Qt::DirectConnection);
#endif

#ifdef Q_OS_MACOS
  mac::EnableFullScreen(w);
#endif  // Q_OS_MACOS

#ifdef HAVE_MPRIS2
  QObject::connect(&mpris2, &Mpris2::RaiseMainWindow, &w, &MainWindow::Raise);
  QObject::connect(&mpris2, &Mpris2::ExitApplication, &w, &MainWindow::Exit);
#endif
  QObject::connect(&single_app, &KDSingleApplication::messageReceived, &w, QOverload<const QByteArray&>::of(&MainWindow::CommandlineOptionsReceived));

  int ret = QCoreApplication::exec();

  if (PlayerImporter::RestartRequested()) {
    // The import dialog asked to restart, the import is done at the start.
    QProcess::startDetached(QCoreApplication::applicationFilePath(), QStringList() << u"--restarted"_s);
  }

#if defined(__MINGW32__) && !defined(HAVE_WINPTHREADS)
  // Workaround crash on exit with the GCC win32 threading model (not needed with winpthreads).
  TerminateProcess(GetCurrentProcess(), 0);
#endif

  return ret;

}
