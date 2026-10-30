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

#ifndef SEEKBARSETTINGS_H
#define SEEKBARSETTINGS_H

#include "config.h"

#include <QVariant>
#include <QString>

#include "core/settings.h"

namespace SeekbarSettings {

constexpr char kSettingsGroup[] = "Seekbar";

enum class Mode {
  Normal = 0,
  Moodbar = 1,
  Waveform = 2
};

constexpr char kMode[] = "mode";

// The moodbar is shown on the seekbar by default when Strawberry is built with it.
#ifdef HAVE_MOODBAR
constexpr Mode kDefaultMode = Mode::Moodbar;
#else
constexpr Mode kDefaultMode = Mode::Normal;
#endif

// Set when the default mode was applied once to an existing configuration.
constexpr char kModeDefaultApplied[] = "mode_default_applied";

// Returns the seekbar mode, the single setting used by the seekbar's right-click menu, the moodbar settings page and the moodbar and waveform controllers.
// The mode used to be saved on every startup, so the normal seekbar saved by an earlier version doesn't mean it was chosen, it is changed to the default once.
inline Mode LoadMode() {

  Settings s;
  s.beginGroup(QLatin1String(kSettingsGroup));
  int mode = s.value(QLatin1String(kMode), static_cast<int>(kDefaultMode)).toInt();
  if (!s.value(QLatin1String(kModeDefaultApplied), false).toBool()) {
    if (mode == static_cast<int>(Mode::Normal) && kDefaultMode != Mode::Normal) {
      mode = static_cast<int>(kDefaultMode);
      s.setValue(QLatin1String(kMode), mode);
    }
    s.setValue(QLatin1String(kModeDefaultApplied), true);
  }
  s.endGroup();

  switch (static_cast<Mode>(mode)) {
    case Mode::Normal:
    case Mode::Moodbar:
    case Mode::Waveform:
      return static_cast<Mode>(mode);
  }

  return kDefaultMode;

}

// Saves the seekbar mode chosen by the user.
inline void SaveMode(const Mode mode) {

  Settings s;
  s.beginGroup(QLatin1String(kSettingsGroup));
  s.setValue(QLatin1String(kMode), static_cast<int>(mode));
  s.setValue(QLatin1String(kModeDefaultApplied), true);
  s.endGroup();

}

}  // namespace SeekbarSettings

#endif  // SEEKBARSETTINGS_H
