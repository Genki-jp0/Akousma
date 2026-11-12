/*
 * Strawberry Music Player
 * Copyright 2026, Jonas Kvinge <jonas@jkvinge.net>
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

#ifndef CREDENTIALSMANAGER_H
#define CREDENTIALSMANAGER_H

#include "config.h"

#include <QObject>
#include <QList>
#include <QQueue>
#include <QString>

#include "includes/scoped_ptr.h"
#include "credentialsreply.h"
#include "credentialsresult.h"
#include "credentialsidentity.h"

class CredentialsBackendInterface;

// Stores passwords securely in the system keyring.
// On Linux and *BSD KWallet, GNOME Keyring (Secret Service D-Bus API) and libsecret are supported, on Windows Windows Credential Manager, and on macOS Keychain.
// If no secure backend is available, passwords are stored obfuscated in the settings file as a last resort.
// Passwords are unique per service, the service name should be a short lowercase identifier, for example "subsonic".
// All requests are asynchronous and run one at a time in the order they were made.
// KWallet, GNOME Keyring and libsecret use asynchronous APIs, backends without an asynchronous API run in a thread pool with QtConcurrent.
// Requests return a reply which emits Finished in the calling thread, the calling thread must have an event loop.
// Requests can be made from any thread, and may show a password prompt from the system keyring.
class CredentialsManager : public QObject {
  Q_OBJECT

 public:
  explicit CredentialsManager(QObject *parent = nullptr);
  ~CredentialsManager() override;

  enum class BackendType {
    Auto,
    KWallet,
    GnomeKeyring,
    LibSecret,
    WindowsCredentialManager,
    Keychain,
    Settings
  };

  static constexpr char kSettingsGroup[] = "CredentialsManager";
  static constexpr char kBackend[] = "backend";
  static constexpr char kInsecureBackendNoticeShown[] = "insecure_backend_notice_shown";

  static QString BackendToString(const BackendType backend);
  static BackendType BackendFromString(const QString &backend);

  // Returns the backends compiled in for this platform, excluding Auto.
  static QList<BackendType> SupportedBackends();

  // Returns the automatically detected backends in order of preference.
  static QList<BackendType> AutoBackendTypes();

  // Creates a backend using the names of the identity in the keyring, the settings backend always uses Akousma's settings.
  // Returns nullptr when the backend is not supported on this platform.
  static ScopedPtr<CredentialsBackendInterface> CreateBackend(const BackendType backend_type, const CredentialsIdentity &identity = CredentialsIdentity::Akousma());

  // Name of the backend in use, empty until the first request has initialized it.
  QString backend_name() const;

  // True when the backend in use stores the passwords unencrypted in the settings file, false until the first request has initialized the backend.
  bool insecure_backend() const;

  // True when the settings file is selected in the settings as the backend, instead of being used because no secure backend is available.
  bool settings_backend_selected() const { return backend_type_ == BackendType::Settings; }

  // Returns true once after passwords started being stored in the settings file, so the user is told about it one time.
  // The notice is given again when a secure backend was used in the meantime.
  static bool TakeInsecureBackendNotice();

  // Re-reads the selected backend from the settings, the backend is initialized again before the next request.
  void ReloadSettings();

  // Finishes with Success and the password, NotFound if no password is stored for the service, or Error.
  [[nodiscard]] CredentialsReplyPtr ReadPasswordAsync(const QString &service);

  // Stores the password for the service, replacing any existing password.
  // Saving an empty password deletes the stored password.
  [[nodiscard]] CredentialsReplyPtr SavePasswordAsync(const QString &service, const QString &password);

  // Deletes the password for the service, finishes with Success if the password is deleted or did not exist.
  [[nodiscard]] CredentialsReplyPtr DeletePasswordAsync(const QString &service);

 Q_SIGNALS:
  // Emitted when the backend is initialized and stores the passwords unencrypted in the settings file, because no secure backend is available or because it is selected.
  void InsecureBackend();

 private:
  enum class RequestType {
    Read,
    Save,
    Delete
  };

  struct Request {
    RequestType type;
    CredentialsReplyPtr reply;
    QString password;
  };

  enum class BackendState {
    Uninitialized,
    Initializing,
    Ready
  };

  void AddRequest(const Request &request);
  void StartNextRequest();
  void RequestFinished(const Request &request, const CredentialsResult &credentials_result);

  void InitializeBackend();
  void CheckNextBackend();
  void BackendChecked(const bool available);
  void BackendReady();

 private:
  BackendType backend_type_;
  bool reset_backend_;
  BackendState backend_state_;
  ScopedPtr<CredentialsBackendInterface> backend_;

  QList<BackendType> candidate_backend_types_;
  ScopedPtr<CredentialsBackendInterface> candidate_backend_;
  bool candidate_backend_is_selected_;

  QQueue<Request> requests_;
  bool request_running_;
};

#endif  // CREDENTIALSMANAGER_H
