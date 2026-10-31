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

#include <memory>
#include <optional>

#include <QObject>
#include <QEventLoop>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "includes/scoped_ptr.h"
#include "core/logging.h"
#include "credentialsmanager/credentialsmanager.h"
#include "credentialsmanager/credentialsbackendinterface.h"
#include "credentialsmanager/credentialsidentity.h"
#include "credentialsmanager/credentialsreply.h"
#include "credentialsmanager/credentialsresult.h"
#include "passwordcopier.h"

using namespace Qt::Literals::StringLiterals;

namespace {

// Opening a keyring can show a password prompt (KWallet waits 5 minutes for it), give up a little later.
constexpr int kTimeout = 330000;

// State shared with a backend callback, which may still come after the wait gave up.
template<typename T>
struct WaitState {
  QEventLoop loop;
  std::optional<T> value;
};

template<typename T>
std::optional<T> Wait(const std::shared_ptr<WaitState<T>> &state) {

  if (!state->value.has_value()) {
    QTimer::singleShot(kTimeout, &state->loop, &QEventLoop::quit);
    state->loop.exec();
  }

  return state->value;

}

}  // namespace

PasswordCopier::Result PasswordCopier::Copy(const QStringList &services, const ReadFunction &read, const SaveFunction &save) {

  Result result;

  for (const QString &service : services) {
    const CredentialsResult read_result = read(service);
    switch (read_result.status) {
      case CredentialsResult::Status::NotFound:
        result.not_found << service;
        continue;
      case CredentialsResult::Status::Error:
        result.failed << service + u": "_s + read_result.error;
        continue;
      case CredentialsResult::Status::Success:
        break;
    }
    if (read_result.password.isEmpty()) {
      result.not_found << service;
      continue;
    }
    const CredentialsResult save_result = save(service, read_result.password);
    if (save_result.success()) {
      result.copied << service;
    }
    else {
      result.failed << service + u": "_s + save_result.error;
    }
  }

  return result;

}

ScopedPtr<CredentialsBackendInterface> PasswordCopier::FindBackend(const CredentialsManager::BackendType selected_backend, const CredentialsIdentity &identity) {

  QList<CredentialsManager::BackendType> backend_types;
  if (selected_backend != CredentialsManager::BackendType::Auto) {
    backend_types << selected_backend;
  }
  const QList<CredentialsManager::BackendType> auto_backend_types = CredentialsManager::AutoBackendTypes();
  for (const CredentialsManager::BackendType backend_type : auto_backend_types) {
    if (!backend_types.contains(backend_type)) backend_types << backend_type;
  }

  for (const CredentialsManager::BackendType backend_type : std::as_const(backend_types)) {
    // The settings backend is the fallback, the passwords are then in the other application's settings file.
    if (backend_type == CredentialsManager::BackendType::Settings) return nullptr;
    ScopedPtr<CredentialsBackendInterface> backend = CredentialsManager::CreateBackend(backend_type, identity);
    if (!backend) continue;
    if (CheckAvailable(&*backend)) {
      qLog(Debug) << "Found credentials backend" << backend->name() << "for" << identity.label;
      return backend;
    }
  }

  return nullptr;

}

bool PasswordCopier::CheckAvailable(CredentialsBackendInterface *backend) {

  std::shared_ptr<WaitState<bool>> state = std::make_shared<WaitState<bool>>();
  backend->CheckAvailable([state](const bool available) {
    state->value = available;
    state->loop.quit();
  });

  return Wait(state).value_or(false);

}

CredentialsResult PasswordCopier::ReadPassword(CredentialsBackendInterface *backend, const QString &service) {

  std::shared_ptr<WaitState<CredentialsResult>> state = std::make_shared<WaitState<CredentialsResult>>();
  backend->ReadPassword(service, [state](const CredentialsResult &credentials_result) {
    state->value = credentials_result;
    state->loop.quit();
  });

  return Wait(state).value_or(CredentialsResult::Error(u"Timed out reading the password."_s));

}

CredentialsResult PasswordCopier::SavePassword(CredentialsManager *credentials_manager, const QString &service, const QString &password) {

  std::shared_ptr<WaitState<CredentialsResult>> state = std::make_shared<WaitState<CredentialsResult>>();
  CredentialsReplyPtr reply = credentials_manager->SavePasswordAsync(service, password);
  if (reply->finished()) return reply->result();
  // The reply is kept by this function, the lambda only needs it while it emits Finished.
  CredentialsReply *reply_ptr = &*reply;
  QObject::connect(reply_ptr, &CredentialsReply::Finished, &state->loop, [state, reply_ptr]() {
    state->value = reply_ptr->result();
    state->loop.quit();
  });

  return Wait(state).value_or(CredentialsResult::Error(u"Timed out saving the password."_s));

}
