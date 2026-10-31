/*
 * Strawberry Music Player
 * Copyright 2024, Jonas Kvinge <jonas@jkvinge.net>
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

#ifndef COVERSSETTINGS_H
#define COVERSSETTINGS_H

namespace CoversSettings {

constexpr char kSettingsGroup[] = "Covers";
constexpr char kProviders[] = "providers";
constexpr char kTypes[] = "types";
constexpr char kSaveType[] = "save_type";
constexpr char kSaveFilename[] = "save_filename";
constexpr char kSavePattern[] = "save_pattern";
constexpr char kSaveOverwrite[] = "save_overwrite";
constexpr char kSaveLowercase[] = "save_lowercase";
constexpr char kSaveReplaceSpaces[] = "save_replace_spaces";
// Also write covers set from Akousma into the music file's tags (ID3v2 APIC frame for MP3).
constexpr char kSaveEmbedAlso[] = "save_embed_also";
// Only use album directory images named like a cover (cover, front, folder, albumart, ...) as automatic covers.
constexpr char kAutomaticStrict[] = "automatic_strict";
// Set when the old default cover type order (with album directory images above embedded covers) was migrated.
constexpr char kTypesEmbeddedFirst[] = "types_embedded_first";
// Set when the collection was rescanned once, so the covers of songs already in it are read again with the new cover rules.
constexpr char kCollectionRescanned[] = "album_art_patch_rescanned";

constexpr char kDefaultSavePattern[] = "%albumartist-%album";
constexpr bool kDefaultSaveOverwrite = false;
constexpr bool kDefaultSaveLowercase = false;
constexpr bool kDefaultSaveReplaceSpaces = false;
constexpr bool kDefaultSaveEmbedAlso = true;
constexpr bool kDefaultAutomaticStrict = true;

}  // namespace CoversSettings

#endif  // COVERSSETTINGS_H
