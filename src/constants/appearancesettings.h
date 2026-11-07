/*
 * Strawberry Music Player
 * Copyright 2024-2026, Jonas Kvinge <jonas@jkvinge.net>
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

#ifndef APPEARANCESETTINGS_H
#define APPEARANCESETTINGS_H

namespace AppearanceSettings {

constexpr char kSettingsGroup[] = "Appearance";

enum class BackgroundImageType {
  Default,
  None,
  Custom,
  Album,
  Strawbs
};

enum class BackgroundImagePosition {
  UpperLeft = 1,
  UpperRight = 2,
  Middle = 3,
  BottomLeft = 4,
  BottomRight = 5
};

constexpr char kStyle[] = "style";
constexpr char kSystemThemeIcons[] = "system_icons";
// The icon set (Settings > Appearance > Icon set): "akousma" (built-in), "system" (Linux), a bundled set such as "breeze",
// or "theme:<folder>" for an icon theme in <data dir>/icons. Before it existed, system_icons=true meant "system".
constexpr char kIconSet[] = "icon_set";
// The style of the application icon (Settings > Appearance > Application icon), see core/appicon.h: "win11" (default), "win10", "win81", "win8"
// or "aero". Another value means the default.
constexpr char kAppIcon[] = "app_icon";
// Whether the application icon is the pomegranate of the icon set (Settings > Appearance > Use the icon set's Akousma icon), see
// core/appicon.h. Icon sets without one use the style of kAppIcon.
constexpr char kAppIconFromIconSet[] = "app_icon_from_iconset";
constexpr bool kDefaultAppIconFromIconSet = false;
constexpr char kDarkMode[] = "dark_mode";
constexpr char kUseCustomColorSet[] = "use-custom-color-set";

// Windows only: system backdrop behind the main window (Settings > Appearance > Window backdrop).
constexpr char kWindowBackdrop[] = "window_backdrop";
// Also show the backdrop through the lists (playlist, collection, context...). Text there then loses ClearType.
constexpr char kWindowBackdropTranslucentContent[] = "window_backdrop_translucent_content";

enum class WindowBackdrop {
  None = 0,
  Mica = 1,
  MicaAlt = 2,
  Acrylic = 3
};

// Per-QPalette-role custom colors (the "central roles" from https://doc.qt.io/qt-6/qpalette.html).
constexpr char kColorWindow[] = "color-window";
constexpr char kColorWindowText[] = "color-window-text";
constexpr char kColorBase[] = "color-base";
constexpr char kColorAlternateBase[] = "color-alternate-base";
constexpr char kColorToolTipBase[] = "color-tooltip-base";
constexpr char kColorToolTipText[] = "color-tooltip-text";
constexpr char kColorText[] = "color-text";
constexpr char kColorButton[] = "color-button";
constexpr char kColorButtonText[] = "color-button-text";
constexpr char kColorBrightText[] = "color-bright-text";
constexpr char kColorPlaceholderText[] = "color-placeholder-text";

constexpr char kTabBarSystemColor[] = "tab_system_color";
constexpr char kTabBarGradient[] = "tab_gradient";
constexpr char kTabBarColor[] = "tab_color";

// Background of the sidebar tabbar: the strawberry graphics over the color, only the color or gradient, a custom image or Clementine's sidebar
// (its gradient with its graphics, data/pictures/clementine-sidebar-background.png from Clementine, GPL-3.0-or-later).
// 3 was the album cover of the playing song, removed; it and unknown values are read as the plain gradient (TabBarBackgroundFromSetting).
enum class TabBarBackground {
  Strawberry = 0,
  Gradient = 1,
  CustomImage = 2,
  Clementine = 4
};

// The background a stored tab_background value stands for.
constexpr TabBarBackground TabBarBackgroundFromSetting(const int value) {
  switch (value) {
    case static_cast<int>(TabBarBackground::Strawberry):
      return TabBarBackground::Strawberry;
    case static_cast<int>(TabBarBackground::CustomImage):
      return TabBarBackground::CustomImage;
    case static_cast<int>(TabBarBackground::Clementine):
      return TabBarBackground::Clementine;
    default:
      return TabBarBackground::Gradient;
  }
}

enum class TabBarGradientDirection {
  Horizontal = 0,
  Vertical = 1,
  Diagonal = 2
};

constexpr char kTabBarBackground[] = "tab_background";
// The name of the user's sidebar theme chosen in the dropdown (settings/tabbarthemes.h), empty for a built-in background.
constexpr char kTabBarTheme[] = "tab_theme";
constexpr char kTabBarGradientCustomColors[] = "tab_gradient_custom_colors";
constexpr char kTabBarGradientStartColor[] = "tab_gradient_start_color";
constexpr char kTabBarGradientEndColor[] = "tab_gradient_end_color";
constexpr char kTabBarGradientDirection[] = "tab_gradient_direction";
constexpr char kTabBarImage[] = "tab_background_image";
// The image options are the same as for the playlist background image, plus tiling and tinting.
constexpr char kTabBarImagePosition[] = "tab_background_image_position";
constexpr char kTabBarImageMaxSize[] = "tab_background_image_max_size";
constexpr char kTabBarImageStretch[] = "tab_background_image_stretch";
constexpr char kTabBarImageKeepAspectRatio[] = "tab_background_image_keep_aspect_ratio";
constexpr char kTabBarImageDoNotCut[] = "tab_background_image_do_not_cut";
constexpr char kTabBarImageTile[] = "tab_background_image_tile";
constexpr char kTabBarImageBlurRadius[] = "tab_background_image_blur_radius";
constexpr char kTabBarImageOpacity[] = "tab_background_image_opacity";
constexpr char kTabBarImageTint[] = "tab_background_image_tint";

constexpr char kPlaylistPlayingSongColor[] = "playlist_playing_song_color";

constexpr char kBackgroundImageType[] = "background_image_type";
constexpr char kBackgroundImageFilename[] = "background_image_file";
constexpr char kBackgroundImagePosition[] = "background_image_position";
constexpr char kBackgroundImageMaxSize[] = "background_image_max_size";
constexpr char kBackgroundImageStretch[] = "background_image_stretch";
constexpr char kBackgroundImageDoNotCut[] = "background_image_do_not_cut";
constexpr char kBackgroundImageKeepAspectRatio[] = "background_image_keep_aspect_ratio";

constexpr char kBackgroundImageBlurRadius[] = "blur_radius";
constexpr char kBackgroundImageOpacityLevel[] = "opacity_level";

constexpr char kIconSizeTabbarSmallMode[] = "icon_size_tabbar_small_mode";
constexpr char kIconSizeTabbarLargeMode[] = "icon_size_tabbar_large_mode";
constexpr char kIconSizePlayControlButtons[] = "icon_size_play_control_buttons";
constexpr char kIconSizePlaylistButtons[] = "icon_size_playlist_buttons";
constexpr char kIconSizeLeftPanelButtons[] = "icon_size_left_panel_buttons";
constexpr char kIconSizeConfigureButtons[] = "icon_size_configure_buttons";

// Akousma: layout of the player (Settings > Appearance > Layout).
// Akousma uses Clementine's sizes and puts repeat and shuffle beside the playback buttons, Strawberry is upstream's original layout, Custom are the
// user's own values. The values of the presets are below; choosing a preset in the settings writes its values to the keys, so the rest of the
// program only reads the keys (icon sizes above, the ones below, PlayingWidget/mode and the tabbar background).
enum class PlayerLayout {
  Akousma = 0,
  Strawberry = 1,
  Custom = 2
};
constexpr char kPlayerLayout[] = "player_layout";
// Repeat and shuffle beside the playback buttons, in their size (else in the status bar, as in Strawberry).
constexpr char kSequenceBesidePlayControls[] = "sequence_beside_play_controls";
// Minimum height of the playback controls row (the analyzer).
constexpr char kPlayerControlsHeight[] = "player_controls_height";

constexpr bool kDefaultDarkMode = false;
constexpr bool kDefaultSystemIcons = false;
constexpr bool kDefaultUseCustomColorSet = false;
constexpr WindowBackdrop kDefaultWindowBackdrop = WindowBackdrop::None;
constexpr bool kDefaultWindowBackdropTranslucentContent = false;
constexpr bool kDefaultTabBarSystemColor = false;
constexpr bool kDefaultTabBarGradient = true;
constexpr TabBarBackground kDefaultTabBarBackground = TabBarBackground::Gradient;  // Akousma layout
constexpr bool kDefaultTabBarGradientCustomColors = false;
constexpr TabBarGradientDirection kDefaultTabBarGradientDirection = TabBarGradientDirection::Horizontal;
// By default the image fills the sidebar, keeping its aspect ratio and cutting the sides.
constexpr int kDefaultTabBarImagePosition = 3;  // BackgroundImagePosition::Middle
constexpr int kDefaultTabBarImageMaxSize = 0;
constexpr bool kDefaultTabBarImageStretch = true;
constexpr bool kDefaultTabBarImageKeepAspectRatio = true;
constexpr bool kDefaultTabBarImageDoNotCut = false;
constexpr bool kDefaultTabBarImageTile = false;
constexpr int kDefaultTabBarImageBlurRadius = 0;
constexpr int kDefaultTabBarImageOpacity = 100;
constexpr bool kDefaultTabBarImageTint = false;

constexpr BackgroundImageType kDefaultBackgroundImageType = BackgroundImageType::Default;
constexpr BackgroundImagePosition kDefaultBackgroundImagePosition = BackgroundImagePosition::BottomRight;
constexpr int kDefaultBackgroundImageMaxSize = 0;
constexpr bool kDefaultBackgroundImageStretch = false;
constexpr bool kDefaultBackgroundImageDoNotCut = true;
constexpr bool kDefaultBackgroundImageKeepAspectRatio = true;
constexpr int kDefaultBackgroundImageBlurRadius = 0;
constexpr int kDefaultBackgroundImageOpacityLevel = 40;

// What each layout sets. Akousma: Clementine's sizes (clementine-player/Clementine master, see the comments), Strawberry: upstream's defaults.
struct PlayerLayoutValues {
  int icon_size_tabbar_small_mode;
  int icon_size_tabbar_large_mode;
  int icon_size_play_control_buttons;
  int icon_size_playlist_buttons;
  int icon_size_left_panel_buttons;
  int icon_size_configure_buttons;
  bool sequence_beside_play_controls;
  bool small_playing_cover;
  int player_controls_height;
  TabBarBackground tabbar_background;
  int sidebar_width;  // Initial width of the left side (tabs and collection), before the user moves the splitter.
};

constexpr PlayerLayoutValues kPlayerLayoutAkousma {
  22,     // src/widgets/fancytabwidget.cpp: IconSize_SmallSidebar = QSize(22, 22)
  24,     // src/widgets/fancytabwidget.cpp: IconSize_LargeSidebar = QSize(24, 24)
  22,     // src/ui/mainwindow.ui: back_button, pause_play_button, stop_button, forward_button iconSize 22x22
  16,     // src/playlist/playlistcontainer.ui: playlist toolbar buttons iconSize 16x16
  16,     // src/playlist/playlistlistcontainer.ui, src/widgets/fileview.ui: buttons iconSize 16x16
  16,     // src/library/libraryfilterwidget.ui: options button iconSize 16x16
  true,   // Akousma: repeat and shuffle beside the playback buttons (Clementine: src/playlist/playlistsequence.ui, 22x22 in the status bar)
  true,   // src/widgets/nowplayingwidget.cpp: mode_(SmallSongDetails), s.value("mode", SmallSongDetails)
  36,     // src/ui/mainwindow.ui: analyzer minimumSize 0x36
  TabBarBackground::Gradient,  // Genki's choice: the plain gradient (Clementine's background, src/widgets/fancytabwidget.cpp, is an option)
  300     // src/ui/mainwindow.cpp: ui_->splitter->setSizes(QList<int>() << 300 << width() - 300)
};

constexpr PlayerLayoutValues kPlayerLayoutStrawberry {
  32, 40, 32, 20, 22, 20,
  false,  // In the status bar, between the playlist summary and the seek bar.
  false,  // Large album cover.
  36,
  TabBarBackground::Strawberry,
  20
};

constexpr PlayerLayout kDefaultPlayerLayout = PlayerLayout::Akousma;
constexpr bool kDefaultSequenceBesidePlayControls = kPlayerLayoutAkousma.sequence_beside_play_controls;
constexpr int kDefaultPlayerControlsHeight = kPlayerLayoutAkousma.player_controls_height;

constexpr int kDefaultIconSizeTabbarSmallMode = kPlayerLayoutAkousma.icon_size_tabbar_small_mode;
constexpr int kDefaultIconSizeTabbarLargeMode = kPlayerLayoutAkousma.icon_size_tabbar_large_mode;
constexpr int kDefaultIconSizePlayControlButtons = kPlayerLayoutAkousma.icon_size_play_control_buttons;
constexpr int kDefaultIconSizePlaylistButtons = kPlayerLayoutAkousma.icon_size_playlist_buttons;
constexpr int kDefaultIconSizeLeftPanelButtons = kPlayerLayoutAkousma.icon_size_left_panel_buttons;
constexpr int kDefaultIconSizeConfigureButtons = kPlayerLayoutAkousma.icon_size_configure_buttons;

}  // namespace AppearanceSettings

#endif  // APPEARANCESETTINGS_H
