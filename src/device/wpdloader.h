/*
 * Akousma
 * Copyright 2026, Genki (genki_jp0) <86833542+Genki-jp0@users.noreply.github.com>
 * Based on Strawberry's MTP device support, which was part of Clementine:
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2019-2026, Jonas Kvinge <jonas@jkvinge.net>
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

#ifndef WPDLOADER_H
#define WPDLOADER_H

#include "config.h"

#include <QObject>
#include <QString>
#include <QUrl>

#include "includes/scoped_ptr.h"
#include "includes/shared_ptr.h"

class QThread;
class TaskManager;
class TagReaderClient;
class CollectionBackend;
class WpdConnection;

// Windows only: reads the songs of a WPD (MTP) device into the device's collection, in its own thread.
class WpdLoader : public QObject {
  Q_OBJECT

 public:
  explicit WpdLoader(const QUrl &url, const SharedPtr<TaskManager> task_manager, const SharedPtr<TagReaderClient> tagreader_client, const SharedPtr<CollectionBackend> backend, QObject *parent = nullptr);
  ~WpdLoader() override;

  void Abort() { abort_ = true; }

 public Q_SLOTS:
  void LoadDatabase();

 Q_SIGNALS:
  void Error(const QString &message);
  void TaskStarted(const int task_id);
  void LoadFinished(const bool success, WpdConnection *connection);

 private:
  bool TryLoad();

 private:
  QUrl url_;
  SharedPtr<TaskManager> task_manager_;
  SharedPtr<TagReaderClient> tagreader_client_;
  SharedPtr<CollectionBackend> backend_;
  ScopedPtr<WpdConnection> connection_;
  QThread *original_thread_;
  bool abort_;
};

#endif  // WPDLOADER_H
