/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2018-2021, Jonas Kvinge <jonas@jkvinge.net>
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

#ifndef DEVICEPROPERTIES_H
#define DEVICEPROPERTIES_H

#include "config.h"

#include <memory>

#include <QObject>
#include <QDialog>
#include <QFuture>
#include <QAbstractItemModel>
#include <QList>
#include <QPair>
#include <QString>

#include "includes/shared_ptr.h"
#include "core/song.h"

class QWidget;
class QTimer;

class DeviceManager;
class ConnectedDevice;
class Ui_DeviceProperties;

class DeviceProperties : public QDialog {
  Q_OBJECT

 public:
  explicit DeviceProperties(QWidget *parent = nullptr);
  ~DeviceProperties() override;

  void Init(const SharedPtr<DeviceManager> device_manager);
  void ShowDevice(const QModelIndex &idx);

 public Q_SLOTS:
  void accept() override;

 private:
  void UpdateHardwareInfo();
  void AddHardwareInfo(const int row, const QString &key, const QString &value);
  // Shows the transcode settings of the device too when reset is true (when the dialog is opened), not when the device
  // only changed, so what the user picked stays.
  void UpdateFormats(const bool reset);
  void ShowFormats(const bool success, const QList<Song::FileType> &formats, const QString &error_text);

 private Q_SLOTS:
  void DeviceDataChanged(const QModelIndex &top_left, const QModelIndex &bottom_right);
  void ModelChanged();
  void OpenDevice();
  void UpdateFormatsTimeout();

 private:
  Ui_DeviceProperties *ui_;

  SharedPtr<DeviceManager> device_manager_;
  QPersistentModelIndex index_;

  bool updating_formats_;
  QList<Song::FileType> supported_formats_;
  // The device the formats were asked of; they are asked again only for another device, or when the dialog is opened.
  std::weak_ptr<ConnectedDevice> formats_device_;
  // Answers to earlier questions (timed out, or for another device) are ignored.
  int formats_generation_;
  QTimer *formats_timer_;
  QString formats_text_;
  QList<QPair<QString, QString>> hardware_info_;
};

#endif  // DEVICEPROPERTIES_H
