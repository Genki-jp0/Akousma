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

#ifndef URLRECORDER_H
#define URLRECORDER_H

#include <QObject>
#include <QList>
#include <QUrl>

// Records the URLs QDesktopServices::openUrl() is asked to open, instead of starting the web browser.
// Usage: QDesktopServices::setUrlHandler(u"https"_s, &recorder, "OpenUrl"); and unsetUrlHandler() when done.
class UrlRecorder : public QObject {
  Q_OBJECT

 public:
  explicit UrlRecorder(QObject *parent = nullptr) : QObject(parent) {}

  const QList<QUrl> &urls() const { return urls_; }

 public Q_SLOTS:
  void OpenUrl(const QUrl &url) { urls_ << url; }

 private:
  QList<QUrl> urls_;
};

#endif  // URLRECORDER_H
