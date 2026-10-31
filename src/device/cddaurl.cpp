/*
 * Akousma
 * Copyright 2026, Genki (genki_jp0) <86833542+Genki-jp0@users.noreply.github.com>
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

#include "config.h"

#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QUrl>

#include "cddaurl.h"

using namespace Qt::Literals::StringLiterals;

namespace CDDAUrl {

QString UrlScheme() { return u"cdda"_s; }

QString NormalizeDevice(const QString &device) {

  // D:, d:, D:\, D:/, \\.\D:, \\?\D: and /D: (the path of cdda:///D:) are all drive D.
  static const QRegularExpression drive_letter_regex(uR"(^(?:\\\\[.?]\\|/)?([A-Za-z]):[\\/]?$)"_s);
  const QRegularExpressionMatch match = drive_letter_regex.match(device);
  if (match.hasMatch()) {
    return match.captured(1).toUpper() + u':';
  }

  return device;

}

QUrl DeviceUrl(const QString &device) {

  // cdda:// and the path: cdda:///dev/sr0, as always; a drive letter gets a slash, like a path: cdda:///D:.
  const QString normalized_device = NormalizeDevice(device);
  return QUrl(UrlScheme() + u"://"_s + (normalized_device.startsWith(u'/') ? normalized_device : u'/' + normalized_device));

}

QString DeviceFromUrl(const QUrl &url) {
  return NormalizeDevice(url.path());
}

QUrl TrackUrl(const QUrl &drive_url, const int track) {

  if (drive_url.isEmpty()) {
    return QUrl(QStringLiteral("cdda://%1a").arg(track));
  }

  return QUrl(QStringLiteral("cdda://%1/%2").arg(drive_url.path(), QString::number(track)));

}

bool ParseTrackUrl(const QUrl &url, QString &device, int &track) {

  device.clear();
  track = 0;

  if (url.scheme() != UrlScheme()) return false;

  bool ok = false;
  if (url.path().isEmpty()) {
    // cdda://3a or cdda://3: a track on the default drive.
    QString track_str = url.host();
    if (track_str.endsWith(u'a')) track_str.chop(1);
    track = track_str.toInt(&ok);
    return ok && track > 0;
  }

  QStringList path = url.path().split(u'/');
  track = path.takeLast().toInt(&ok);
  if (!ok || track <= 0) {
    track = 0;
    return false;
  }
  device = NormalizeDevice(path.join(u'/'));

  return true;

}

}  // namespace CDDAUrl
