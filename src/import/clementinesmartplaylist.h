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

#ifndef CLEMENTINESMARTPLAYLIST_H
#define CLEMENTINESMARTPLAYLIST_H

#include <optional>

#include <QtGlobal>
#include <QCoreApplication>
#include <QByteArray>
#include <QString>
#include <QStringList>

#include "smartplaylists/smartplaylistsearchterm.h"

// Converts a Clementine smart playlist to Akousma's format.
// Clementine stores a smart playlist as generator type "Query" and the QDataStream serialization of its search (smart_playlists::Search) followed by the dynamic flag.
// Akousma (like Strawberry) stores generator type 1 (PlaylistGenerator::Type::Query) and the serialization of SmartPlaylistSearch followed by the dynamic flag.
// Both use the same layout, operators, search types, sort types and date types, only the field numbers differ.
// Conditions on fields Akousma doesn't have (Clementine's "Score") or with operators Akousma doesn't offer for the field are dropped and reported.
class ClementineSmartPlaylist {
  Q_DECLARE_TR_FUNCTIONS(ClementineSmartPlaylist)

 public:
  // Clementine's smart_playlists::SearchTerm::Field, these values are persisted by Clementine.
  enum class ClementineField {
    Title = 0,
    Artist,
    Album,
    AlbumArtist,
    Composer,
    Length,
    Track,
    Disc,
    Year,
    Genre,
    BPM,
    Bitrate,
    Samplerate,
    Filesize,
    DateCreated,
    DateModified,
    Rating,
    Score,
    PlayCount,
    SkipCount,
    LastPlayed,
    Comment,
    Filepath,
    Performer,
    Grouping,
    OriginalYear,
    FieldCount
  };

  struct Result {
    bool success = false;
    QByteArray data;       // The search for Akousma's PlaylistQueryGenerator::Load().
    bool dynamic = false;
    QStringList dropped;   // One line per dropped condition or changed sort order, for the import summary.
    QString error;         // Why the smart playlist was not converted.
  };

  // Akousma's generator type for Clementine's "Query" generator, or nullopt for unknown generator types.
  static std::optional<int> GeneratorType(const QString &clementine_type);

  // The Akousma field for a Clementine field, or nullopt when Akousma doesn't have it.
  static std::optional<SmartPlaylistSearchTerm::Field> MapField(const int clementine_field);

  // English name of a Clementine field, for the import summary.
  static QString ClementineFieldName(const int clementine_field);

  // Converts the data of a Clementine "Query" smart playlist. The name is only used in the messages.
  static Result Convert(const QString &name, const QByteArray &clementine_data);
};

#endif  // CLEMENTINESMARTPLAYLIST_H
