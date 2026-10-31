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

#include <optional>

#include <QtGlobal>
#include <QByteArray>
#include <QDataStream>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "smartplaylists/playlistgenerator.h"
#include "smartplaylists/playlistquerygenerator.h"
#include "smartplaylists/smartplaylistsearch.h"
#include "smartplaylists/smartplaylistsearchterm.h"
#include "clementinesmartplaylist.h"

using namespace Qt::Literals::StringLiterals;

namespace {

// Clementine's smart_playlists::SearchTerm as Clementine serializes it.
struct ClementineTerm {
  quint8 field = 0;
  quint8 op = 0;
  QVariant value;
  QVariant second_value;
  quint8 date = 0;
};

// Clementine is built with Qt 4 (1.3) or Qt 5 (1.4). The values in a search are integers, doubles and strings, which Qt 4, 5 and 6 serialize the same way.
constexpr QDataStream::Version kClementineStreamVersion = QDataStream::Qt_5_0;

// More conditions than any smart playlist has, guards against reading garbage as a list size.
constexpr quint32 kMaxTerms = 10000;

QString DescribeTerm(const ClementineTerm &term, const std::optional<SmartPlaylistSearchTerm::Field> field) {

  const QString field_name = ClementineSmartPlaylist::ClementineFieldName(term.field);
  const SmartPlaylistSearchTerm::Type type = field.has_value() ? SmartPlaylistSearchTerm::TypeOf(field.value()) : SmartPlaylistSearchTerm::Type::Number;
  const SmartPlaylistSearchTerm::Operator op = static_cast<SmartPlaylistSearchTerm::Operator>(term.op);
  const QString op_text = SmartPlaylistSearchTerm::OperatorText(type, op);

  if (op == SmartPlaylistSearchTerm::Operator::Empty || op == SmartPlaylistSearchTerm::Operator::NotEmpty) {
    return field_name + u' ' + op_text;
  }

  QString value = term.value.toString();
  if (type == SmartPlaylistSearchTerm::Type::Date) {
    const QString date_name = SmartPlaylistSearchTerm::DateName(static_cast<SmartPlaylistSearchTerm::DateType>(term.date), false);
    if (op == SmartPlaylistSearchTerm::Operator::NumericDate || op == SmartPlaylistSearchTerm::Operator::NumericDateNot) {
      value = term.value.toString() + u' ' + date_name;
    }
    else if (op == SmartPlaylistSearchTerm::Operator::RelativeDate) {
      value = term.value.toString() + u'-' + term.second_value.toString() + u' ' + date_name;
    }
    else {
      value = QDateTime::fromSecsSinceEpoch(term.value.toLongLong()).date().toString(Qt::ISODate);
    }
  }
  else if (type == SmartPlaylistSearchTerm::Type::Text) {
    value = u'"' + value + u'"';
  }

  return field_name + u' ' + op_text + u' ' + value;

}

}  // namespace

std::optional<int> ClementineSmartPlaylist::GeneratorType(const QString &clementine_type) {

  // Clementine's Generator::Create() only knows "Query".
  if (clementine_type == "Query"_L1) return static_cast<int>(PlaylistGenerator::Type::Query);
  return std::nullopt;

}

std::optional<SmartPlaylistSearchTerm::Field> ClementineSmartPlaylist::MapField(const int clementine_field) {

  using Field = SmartPlaylistSearchTerm::Field;

  switch (static_cast<ClementineField>(clementine_field)) {
    case ClementineField::Title:        return Field::Title;
    case ClementineField::Artist:       return Field::Artist;
    case ClementineField::Album:        return Field::Album;
    case ClementineField::AlbumArtist:  return Field::AlbumArtist;
    case ClementineField::Composer:     return Field::Composer;
    case ClementineField::Length:       return Field::Length;
    case ClementineField::Track:        return Field::Track;
    case ClementineField::Disc:         return Field::Disc;
    case ClementineField::Year:         return Field::Year;
    case ClementineField::Genre:        return Field::Genre;
    case ClementineField::BPM:          return Field::BPM;
    case ClementineField::Bitrate:      return Field::Bitrate;
    case ClementineField::Samplerate:   return Field::Samplerate;
    case ClementineField::Filesize:     return Field::Filesize;
    case ClementineField::DateCreated:  return Field::DateCreated;
    case ClementineField::DateModified: return Field::DateModified;
    case ClementineField::Rating:       return Field::Rating;
    case ClementineField::PlayCount:    return Field::PlayCount;
    case ClementineField::SkipCount:    return Field::SkipCount;
    case ClementineField::LastPlayed:   return Field::LastPlayed;
    case ClementineField::Comment:      return Field::Comment;
    case ClementineField::Filepath:     return Field::Filepath;
    case ClementineField::Performer:    return Field::Performer;
    case ClementineField::Grouping:     return Field::Grouping;
    case ClementineField::OriginalYear: return Field::OriginalYear;
    case ClementineField::Score:        // Clementine's own play count / skip count / rating based score, Akousma doesn't have it.
    case ClementineField::FieldCount:
      break;
  }

  return std::nullopt;

}

QString ClementineSmartPlaylist::ClementineFieldName(const int clementine_field) {

  if (clementine_field == static_cast<int>(ClementineField::Score)) return tr("Score");

  const std::optional<SmartPlaylistSearchTerm::Field> field = MapField(clementine_field);
  if (field.has_value()) return SmartPlaylistSearchTerm::FieldName(field.value());

  return tr("Unknown field %1").arg(clementine_field);

}

ClementineSmartPlaylist::Result ClementineSmartPlaylist::Convert(const QString &name, const QByteArray &clementine_data) {

  Result result;

  // smart_playlists::Search: terms, sort type, sort field, limit, search type; then QueryGenerator's dynamic flag.
  QDataStream s(clementine_data);
  s.setVersion(kClementineStreamVersion);

  quint32 term_count = 0;
  s >> term_count;
  if (s.status() != QDataStream::Ok || term_count > kMaxTerms) {
    result.error = tr("The smart playlist \"%1\" could not be read.").arg(name);
    return result;
  }

  QList<ClementineTerm> clementine_terms;
  for (quint32 i = 0; i < term_count; ++i) {
    ClementineTerm term;
    s >> term.field >> term.op >> term.value >> term.second_value >> term.date;
    clementine_terms << term;
  }

  quint8 sort_type = 0;
  quint8 sort_field = 0;
  qint32 limit = 0;
  quint8 search_type = 0;
  s >> sort_type >> sort_field >> limit >> search_type;
  if (s.status() != QDataStream::Ok || sort_type > static_cast<quint8>(SmartPlaylistSearch::SortType::FieldDesc) || search_type > static_cast<quint8>(SmartPlaylistSearch::SearchType::All)) {
    result.error = tr("The smart playlist \"%1\" could not be read.").arg(name);
    return result;
  }

  // Smart playlists from Clementine versions before the dynamic flag was stored are not dynamic.
  bool dynamic = false;
  if (!s.atEnd()) {
    s >> dynamic;
    if (s.status() != QDataStream::Ok) dynamic = false;
  }

  SmartPlaylistSearch::TermList terms;
  for (const ClementineTerm &clementine_term : std::as_const(clementine_terms)) {
    const std::optional<SmartPlaylistSearchTerm::Field> field = MapField(clementine_term.field);
    if (!field.has_value()) {
      result.dropped << tr("Smart playlist \"%1\": dropped the condition \"%2\", Akousma has no %3 field.").arg(name, DescribeTerm(clementine_term, field), ClementineFieldName(clementine_term.field));
      continue;
    }
    const SmartPlaylistSearchTerm::Operator op = static_cast<SmartPlaylistSearchTerm::Operator>(clementine_term.op);
    if (!SmartPlaylistSearchTerm::OperatorsForType(SmartPlaylistSearchTerm::TypeOf(field.value())).contains(op)) {
      result.dropped << tr("Smart playlist \"%1\": dropped the condition \"%2\", Akousma doesn't support this comparison for %3.").arg(name, DescribeTerm(clementine_term, field), ClementineFieldName(clementine_term.field));
      continue;
    }
    SmartPlaylistSearchTerm term(field.value(), op, clementine_term.value);
    term.second_value_ = clementine_term.second_value;
    term.datetype_ = static_cast<SmartPlaylistSearchTerm::DateType>(clementine_term.date);
    terms << term;
  }

  if (terms.isEmpty() && !clementine_terms.isEmpty() && search_type != static_cast<quint8>(SmartPlaylistSearch::SearchType::All)) {
    // Without any condition left the smart playlist would match the whole collection.
    result.error = tr("Smart playlist \"%1\" was not imported: none of its conditions exist in Akousma.").arg(name);
    return result;
  }

  SmartPlaylistSearchTerm::Field akousma_sort_field = SmartPlaylistSearchTerm::Field::Title;
  const std::optional<SmartPlaylistSearchTerm::Field> mapped_sort_field = MapField(sort_field);
  if (mapped_sort_field.has_value()) {
    akousma_sort_field = mapped_sort_field.value();
  }
  else if (sort_type != static_cast<quint8>(SmartPlaylistSearch::SortType::Random)) {
    // Clementine's score combines the rating with the play and skip counts, the rating is the closest Akousma has. Akousma's own "Favourite tracks" sorts by rating as well.
    akousma_sort_field = SmartPlaylistSearchTerm::Field::Rating;
    result.dropped << tr("Smart playlist \"%1\": sorted by %2 instead of %3, Akousma has no %3 field.").arg(name, SmartPlaylistSearchTerm::FieldName(akousma_sort_field), ClementineFieldName(sort_field));
  }

  const SmartPlaylistSearch search(static_cast<SmartPlaylistSearch::SearchType>(search_type), terms, static_cast<SmartPlaylistSearch::SortType>(sort_type), akousma_sort_field, limit);
  const PlaylistQueryGenerator generator(name, search, dynamic);

  result.success = true;
  result.dynamic = dynamic;
  result.data = generator.Save();

  return result;

}
