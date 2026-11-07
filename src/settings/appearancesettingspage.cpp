/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2012, David Sansome <me@davidsansome.com>
 * Copyright 2018-2026, Jonas Kvinge <jonas@jkvinge.net>
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

#include "config.h"

#include <utility>

#include <QApplication>
#include <QGuiApplication>
#include <QWidget>
#include <QStyleFactory>
#include <QStyleHints>
#include <QVariant>
#include <QMap>
#include <QString>
#include <QColor>
#include <QPalette>
#include <QColorDialog>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPointer>
#include <QRadioButton>
#include <QSlider>
#include <QBoxLayout>
#include <QFormLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QSettings>
#include <QMessageBox>
#include <QInputDialog>
#include <QSize>
#include <QPair>
#include <QList>
#include <QFont>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QTimer>
#include <QSignalBlocker>
#include <QDir>
#include <QUrl>
#include <QDesktopServices>

#include "appearancesettingspage.h"
#include "constants/appearancesettings.h"
#include "constants/filefilterconstants.h"
#include "utilities/styleutils.h"
#include "core/iconloader.h"
#include "core/appicon.h"
#include "core/stylehelper.h"
#include "core/settings.h"
#include "core/appearance.h"
#include "core/settingstransfer.h"
#include "widgets/fancytabwidget.h"
#include "tabbarthemes.h"
#include "settingspage.h"
#include "settingsdialog.h"
#include "ui_appearancesettingspage.h"
#ifdef Q_OS_WIN32
#  include <QStandardItemModel>
#  include <QStandardItem>
#  include "core/mainwindowbackdrop.h"
#endif

using namespace Qt::Literals::StringLiterals;
using namespace AppearanceSettings;

namespace {
// Size of the sidebar background previews in the Tabbar background dropdown.
constexpr int kTabBarPreviewWidth = 150;
constexpr int kTabBarPreviewHeight = 48;

// The icons shown for each icon set in its dropdown.
constexpr int kIconSetPreviewIconSize = 20;
constexpr int kIconSetPreviewSpacing = 4;
constexpr const char16_t *kIconSetPreviewIcons[] = { u"media-playback-start", u"media-skip-forward", u"media-playlist-shuffle", u"folder-sound", u"edit-find", u"configure", u"document-save" };
// The size of each style's icon in the Application icon dropdown.
constexpr int kAppIconPreviewSize = 32;
// Where PlayingWidget keeps its album cover mode (0 small, 1 large).
constexpr char kPlayingWidgetSettingsGroup[] = "PlayingWidget";
constexpr char kPlayingWidgetMode[] = "mode";
// Item data of the user's themes in the Tabbar background dropdown, their name is in kTabBarThemeNameRole.
constexpr int kTabBarThemeItem = -1;
constexpr int kTabBarThemeNameRole = Qt::UserRole + 1;
constexpr int kTabBarBadgeSize = 20;

// A checkerboard like a transparency grid, the preview of the custom image before an image is chosen.
void DrawCheckerboard(QPainter *p, const QRect &rect) {
  constexpr int kSquare = 6;
  p->fillRect(rect, QColor(204, 204, 204));
  for (int y = rect.top(); y < rect.bottom(); y += kSquare) {
    for (int x = rect.left() + (((y - rect.top()) / kSquare) % 2) * kSquare; x < rect.right(); x += kSquare * 2) {
      p->fillRect(QRect(x, y, kSquare, kSquare), QColor(240, 240, 240));
    }
  }
}

// The badge of the custom image preview: the edit icon of the chosen icon set, or the built-in one. No badge without one.
void DrawEditBadge(QPainter *p, const QRect &rect, const QPixmap &icon) {
  if (!icon.isNull()) p->drawPixmap(rect, icon);
}

}  // namespace

AppearanceSettingsPage::AppearanceSettingsPage(SettingsDialog *dialog, SharedPtr<Appearance> appearance, QWidget *parent)
    : SettingsPage(dialog, parent),
      ui_(new Ui_AppearanceSettingsPage),
      appearance_(appearance),
      original_style_(QApplication::style() ? QApplication::style()->objectName() : QString()),
      system_palette_(QApplication::palette()),
      original_dark_mode_(false),
      original_use_custom_color_set_(false),
      background_image_type_(BackgroundImageType::Default) {

  ui_->setupUi(this);
  setWindowIcon(IconLoader::Load(u"view-media-visualization"_s, true, 0, 32));

  ui_->combobox_style->addItem(u"default"_s, u"default"_s);
  const QStringList styles = QStyleFactory::keys();
  for (const QString &style : styles) {
    ui_->combobox_style->addItem(style, style);
  }

  ui_->combobox_window_backdrop->setItemData(0, static_cast<int>(WindowBackdrop::None));
  ui_->combobox_window_backdrop->setItemData(1, static_cast<int>(WindowBackdrop::Mica));
  ui_->combobox_window_backdrop->setItemData(2, static_cast<int>(WindowBackdrop::MicaAlt));
  ui_->combobox_window_backdrop->setItemData(3, static_cast<int>(WindowBackdrop::Acrylic));
#ifdef Q_OS_WIN32
  // Mica and Mica Alt need Windows 11 22H2, Acrylic has a fallback for Windows 10.
  if (QStandardItemModel *backdrop_model = qobject_cast<QStandardItemModel*>(ui_->combobox_window_backdrop->model())) {
    for (int i = 0; i < backdrop_model->rowCount(); ++i) {
      const WindowBackdrop backdrop = static_cast<WindowBackdrop>(ui_->combobox_window_backdrop->itemData(i).toInt());
      if (!MainWindowBackdrop::IsAvailable(backdrop)) {
        QStandardItem *item = backdrop_model->item(i);
        item->setEnabled(false);
        item->setToolTip(tr("Needs Windows 11 22H2 or later"));
      }
    }
  }
  // The lists stay opaque by default, so their text keeps ClearType.
  QObject::connect(ui_->combobox_window_backdrop, &QComboBox::currentIndexChanged, this, [this]() {
    ui_->checkbox_window_backdrop_translucent_content->setEnabled(ui_->combobox_window_backdrop->currentData().toInt() != static_cast<int>(WindowBackdrop::None));
  });
#else
  ui_->widget_window_backdrop->hide();
#endif

  CreateColorSelectors();

  ui_->combobox_background_image_position->setItemData(0, static_cast<int>(BackgroundImagePosition::UpperLeft));
  ui_->combobox_background_image_position->setItemData(1, static_cast<int>(BackgroundImagePosition::UpperRight));
  ui_->combobox_background_image_position->setItemData(2, static_cast<int>(BackgroundImagePosition::Middle));
  ui_->combobox_background_image_position->setItemData(3, static_cast<int>(BackgroundImagePosition::BottomLeft));
  ui_->combobox_background_image_position->setItemData(4, static_cast<int>(BackgroundImagePosition::BottomRight));

  QObject::connect(ui_->checkbox_dark_mode, &QCheckBox::toggled, this, &AppearanceSettingsPage::DarkModeToggled);

  QObject::connect(ui_->use_custom_color_set, &QRadioButton::toggled, this, &AppearanceSettingsPage::UseCustomColorSetOptionChanged);
  QObject::connect(ui_->use_custom_color_set, &QRadioButton::toggled, ui_->widget_custom_colors, &QWidget::setEnabled);
  QObject::connect(ui_->button_dark_colors, &QPushButton::pressed, this, &AppearanceSettingsPage::SetDarkColors);
  QObject::connect(ui_->button_reset_colors, &QPushButton::pressed, this, &AppearanceSettingsPage::ResetToDefaultColors);

  QObject::connect(ui_->select_tabbar_color, &QPushButton::pressed, this, &AppearanceSettingsPage::TabBarSelectBGColor);
  QObject::connect(ui_->tabbar_system_color, &QRadioButton::toggled, this, &AppearanceSettingsPage::TabBarSystemColor);
  // Layout
  ui_->combobox_player_layout->addItem(tr("Akousma (Clementine sizes, repeat and shuffle beside the playback buttons)"), static_cast<int>(PlayerLayout::Akousma));
  ui_->combobox_player_layout->addItem(tr("Strawberry (original layout)"), static_cast<int>(PlayerLayout::Strawberry));
  ui_->combobox_player_layout->addItem(tr("Custom"), static_cast<int>(PlayerLayout::Custom));
  QObject::connect(ui_->combobox_player_layout, QOverload<int>::of(&QComboBox::activated), this, &AppearanceSettingsPage::PlayerLayoutChosen);

  // Tabbar background: a dropdown with a preview of each background, like the moodbar styles.
  ui_->tabbar_background->setIconSize(QSize(kTabBarPreviewWidth, kTabBarPreviewHeight));
  ui_->tabbar_background->addItem(tr("Gradient"), static_cast<int>(TabBarBackground::Gradient));
  ui_->tabbar_background->addItem(tr("Clementine"), static_cast<int>(TabBarBackground::Clementine));
  ui_->tabbar_background->addItem(tr("Strawberry"), static_cast<int>(TabBarBackground::Strawberry));
  ui_->tabbar_background->addItem(tr("Custom"), static_cast<int>(TabBarBackground::CustomImage));
  QObject::connect(ui_->tabbar_background, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AppearanceSettingsPage::TabBarBackgroundChanged);
  // Choosing one of the user's themes sets the options on this page to the theme's.
  QObject::connect(ui_->tabbar_background, QOverload<int>::of(&QComboBox::activated), this, &AppearanceSettingsPage::TabBarThemeChosen);
  QObject::connect(ui_->button_tabbar_theme_save, &QPushButton::clicked, this, &AppearanceSettingsPage::TabBarThemeSave);
  QObject::connect(ui_->button_tabbar_theme_rename, &QPushButton::clicked, this, &AppearanceSettingsPage::TabBarThemeRename);
  QObject::connect(ui_->button_tabbar_theme_delete, &QPushButton::clicked, this, &AppearanceSettingsPage::TabBarThemeDelete);
  QObject::connect(ui_->tabbar_custom_color, &QRadioButton::toggled, this, &AppearanceSettingsPage::UpdateTabBarThemeButtons);
  // The previews follow the colors and options chosen on this page.
  QObject::connect(ui_->tabbar_system_color, &QRadioButton::toggled, this, &AppearanceSettingsPage::UpdateTabBarBackgroundPreviews);
  QObject::connect(ui_->tabbar_gradient, &QCheckBox::toggled, this, &AppearanceSettingsPage::UpdateTabBarBackgroundPreviews);
  QObject::connect(ui_->tabbar_gradient_direction, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AppearanceSettingsPage::UpdateTabBarBackgroundPreviews);
  QObject::connect(ui_->tabbar_background_image, &QLineEdit::editingFinished, this, &AppearanceSettingsPage::UpdateTabBarBackgroundPreviews);
  QObject::connect(ui_->tabbar_background_image_opacity, &QSlider::sliderReleased, this, &AppearanceSettingsPage::UpdateTabBarBackgroundPreviews);
  QObject::connect(ui_->tabbar_background_image_tint, &QCheckBox::toggled, this, &AppearanceSettingsPage::UpdateTabBarBackgroundPreviews);
  QObject::connect(ui_->tabbar_gradient_custom_colors, &QCheckBox::toggled, this, &AppearanceSettingsPage::TabBarBackgroundChanged);
  QObject::connect(ui_->tabbar_background_image_stretch, &QCheckBox::toggled, this, &AppearanceSettingsPage::TabBarBackgroundChanged);
  QObject::connect(ui_->tabbar_background_image_keep_aspect_ratio, &QCheckBox::toggled, this, &AppearanceSettingsPage::TabBarBackgroundChanged);
  QObject::connect(ui_->tabbar_background_image_tile, &QCheckBox::toggled, this, &AppearanceSettingsPage::TabBarBackgroundChanged);
  QObject::connect(ui_->tabbar_background_image_blur, &QSlider::valueChanged, this, [this](const int value) { ui_->tabbar_background_image_blur_radius->setText(QStringLiteral("%1px").arg(value)); });
  QObject::connect(ui_->tabbar_background_image_opacity, &QSlider::valueChanged, this, [this](const int value) { ui_->tabbar_background_image_opacity_level->setText(QStringLiteral("%1%").arg(value)); });
  QObject::connect(ui_->select_tabbar_gradient_start_color, &QPushButton::pressed, this, [this]() { TabBarSelectGradientColor(true); });
  QObject::connect(ui_->select_tabbar_gradient_end_color, &QPushButton::pressed, this, [this]() { TabBarSelectGradientColor(false); });
  QObject::connect(ui_->select_tabbar_background_image, &QPushButton::pressed, this, &AppearanceSettingsPage::TabBarSelectBackgroundImage);

  QObject::connect(ui_->select_playlist_playing_song_color, &QPushButton::pressed, this, &AppearanceSettingsPage::PlaylistPlayingSongSelectColor);
  QObject::connect(ui_->playlist_playing_song_color_system, &QRadioButton::toggled, this, &AppearanceSettingsPage::PlaylistPlayingSongColorSystem);

  QObject::connect(ui_->use_default_background, &QRadioButton::toggled, ui_->widget_background_image_options, &AppearanceSettingsPage::setDisabled);
  QObject::connect(ui_->use_no_background, &QRadioButton::toggled, ui_->widget_background_image_options, &AppearanceSettingsPage::setDisabled);
  QObject::connect(ui_->use_album_cover_background, &QRadioButton::toggled, ui_->widget_background_image_options, &AppearanceSettingsPage::setEnabled);
  QObject::connect(ui_->use_strawbs_background, &QRadioButton::toggled, ui_->widget_background_image_options, &AppearanceSettingsPage::setDisabled);
  QObject::connect(ui_->use_custom_background_image, &QRadioButton::toggled, ui_->widget_background_image_options, &AppearanceSettingsPage::setEnabled);

  QObject::connect(ui_->select_background_image_filename_button, &QPushButton::pressed, this, &AppearanceSettingsPage::SelectBackgroundImage);
  QObject::connect(ui_->use_custom_background_image, &QRadioButton::toggled, ui_->background_image_filename, &AppearanceSettingsPage::setEnabled);
  QObject::connect(ui_->use_custom_background_image, &QRadioButton::toggled, ui_->select_background_image_filename_button, &AppearanceSettingsPage::setEnabled);

  QObject::connect(ui_->checkbox_background_image_stretch, &QCheckBox::toggled, ui_->checkbox_background_image_do_not_cut, &AppearanceSettingsPage::setEnabled);
  QObject::connect(ui_->checkbox_background_image_stretch, &QCheckBox::toggled, ui_->checkbox_background_image_keep_aspect_ratio, &AppearanceSettingsPage::setEnabled);
  QObject::connect(ui_->checkbox_background_image_stretch, &QCheckBox::toggled, ui_->spinbox_background_image_maxsize, &AppearanceSettingsPage::setDisabled);

  QObject::connect(ui_->checkbox_background_image_keep_aspect_ratio, &QCheckBox::toggled, ui_->checkbox_background_image_do_not_cut, &AppearanceSettingsPage::setEnabled);

  QObject::connect(ui_->slider_background_image_blur, &QSlider::valueChanged, this, &AppearanceSettingsPage::BackgroundImageBlurLevelChanged);
  QObject::connect(ui_->slider_background_image_opacity, &QSlider::valueChanged, this, &AppearanceSettingsPage::BackgroundImageOpacityLevelChanged);

  // Icon set: a dropdown with a few icons of each set.
  constexpr int preview_icon_count = static_cast<int>(sizeof(kIconSetPreviewIcons) / sizeof(kIconSetPreviewIcons[0]));
  ui_->combobox_icon_set->setIconSize(QSize(preview_icon_count * (kIconSetPreviewIconSize + kIconSetPreviewSpacing) - kIconSetPreviewSpacing, kIconSetPreviewIconSize));
  QObject::connect(ui_->combobox_icon_set, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AppearanceSettingsPage::IconSetChanged);
  QObject::connect(ui_->button_icon_set_folder, &QPushButton::clicked, this, &AppearanceSettingsPage::OpenIconSetFolder);
  // "Follow system icon theme" shows the desktop's theme now.
  if (IconThemeWatcher *watcher = IconLoader::ThemeWatcher()) {
    QObject::connect(watcher, &IconThemeWatcher::IconThemeChanged, this, [this]() { UpdateIconSetItems(SelectedIconSet()); });
  }

  // Application icon: a dropdown with each style's icon.
  ui_->combobox_app_icon->setIconSize(QSize(kAppIconPreviewSize, kAppIconPreviewSize));
  const QList<AppIcon::Style> app_icon_styles = AppIcon::Styles();
  for (const AppIcon::Style &style : app_icon_styles) {
    ui_->combobox_app_icon->addItem(AppIcon::StyleIcon(style.key), style.name, style.key);
  }
  // The icon set's pomegranate instead: the checkbox shows the one of the chosen set.
  ui_->checkbox_app_icon_from_iconset->setIconSize(QSize(kAppIconPreviewSize, kAppIconPreviewSize));
  QObject::connect(ui_->checkbox_app_icon_from_iconset, &QCheckBox::toggled, this, &AppearanceSettingsPage::UpdateAppIconControls);

  AppearanceSettingsPage::Load();

}

AppearanceSettingsPage::~AppearanceSettingsPage() = default;

void AppearanceSettingsPage::Load() {

  // Disconnect while loading so intermediate combobox signals don't trigger StyleChanged before colors are ready.
  QObject::disconnect(ui_->combobox_style, &QComboBox::currentIndexChanged, this, &AppearanceSettingsPage::StyleChanged);

  Settings s;
  s.beginGroup(kSettingsGroup);

  // Style
  original_style_ = QApplication::style() ? QApplication::style()->objectName() : QString();
  ComboBoxLoadFromSettings(s, ui_->combobox_style, QLatin1String(kStyle), u"default"_s);

  UpdateIconSetItems(IconLoader::IconSetFromSettings());

  ui_->combobox_app_icon->setCurrentIndex(qMax(0, ui_->combobox_app_icon->findData(AppIcon::StyleFromSettings())));
  ui_->checkbox_app_icon_from_iconset->setChecked(AppIcon::FromIconSetFromSettings());
  UpdateAppIconControls();

#ifdef Q_OS_WIN32
  {
    const WindowBackdrop backdrop = MainWindowBackdrop::LoadSetting();
    int backdrop_index = ui_->combobox_window_backdrop->findData(static_cast<int>(MainWindowBackdrop::IsAvailable(backdrop) ? backdrop : WindowBackdrop::None));
    if (backdrop_index < 0) backdrop_index = 0;
    ui_->combobox_window_backdrop->setCurrentIndex(backdrop_index);
    ui_->checkbox_window_backdrop_translucent_content->setChecked(MainWindowBackdrop::LoadTranslucentContentSetting());
    ui_->checkbox_window_backdrop_translucent_content->setEnabled(ui_->combobox_window_backdrop->currentData().toInt() != static_cast<int>(WindowBackdrop::None));
  }
#endif

  original_dark_mode_ = s.value(kDarkMode, kDefaultDarkMode).toBool();
  ui_->checkbox_dark_mode->setChecked(original_dark_mode_);

  // Colors
  const QPalette palette = QApplication::palette();

  // Keep in mind originals colors, in case the user clicks on Cancel, to be able to restore colors
  original_use_custom_color_set_ = s.value(kUseCustomColorSet, kDefaultUseCustomColorSet).toBool();

  current_colors_.clear();
  for (const Appearance::ColorRole &color_role : Appearance::ColorRoles()) {
    const QVariant value = s.value(color_role.settings_key);
    QColor color = value.isValid() ? value.value<QColor>() : QColor();
    if (!color.isValid()) {
      color = palette.color(color_role.role);
    }
    current_colors_.insert(color_role.role, color);
  }

  original_colors_ = current_colors_;

  UpdateColorSelectorsColors();

  // Tabbar background color
  bool tabbar_system_color = s.value(kTabBarSystemColor, kDefaultTabBarSystemColor).toBool();
  ui_->tabbar_gradient->setChecked(s.value(kTabBarGradient, kDefaultTabBarGradient).toBool());
  ui_->tabbar_system_color->setChecked(tabbar_system_color);
  ui_->tabbar_custom_color->setChecked(!tabbar_system_color);

  current_tabbar_bg_color_ = s.value(kTabBarColor, FancyTabWidget::DefaultTabbarBgColor()).value<QColor>();

  UpdateColorSelectorColor(ui_->select_tabbar_color, current_tabbar_bg_color_);
  TabBarSystemColor(ui_->tabbar_system_color->isChecked());

  // Tabbar background
  tabbar_themes_ = TabBarThemes::Load();
  UpdateTabBarThemeItems();
  SetTabBarBackground(s.value(kTabBarBackground, static_cast<int>(kDefaultTabBarBackground)).toInt());
  {
    const int theme_index = ui_->tabbar_background->findData(s.value(kTabBarTheme).toString(), kTabBarThemeNameRole, Qt::MatchFixedString);
    if (theme_index >= 0) ui_->tabbar_background->setCurrentIndex(theme_index);
  }
  ui_->tabbar_gradient_custom_colors->setChecked(s.value(kTabBarGradientCustomColors, kDefaultTabBarGradientCustomColors).toBool());
  current_tabbar_gradient_start_color_ = s.value(kTabBarGradientStartColor, current_tabbar_bg_color_.lighter(117)).value<QColor>();
  current_tabbar_gradient_end_color_ = s.value(kTabBarGradientEndColor, StyleHelper::shadowColor(false).darker(109)).value<QColor>();
  UpdateColorSelectorColor(ui_->select_tabbar_gradient_start_color, current_tabbar_gradient_start_color_);
  UpdateColorSelectorColor(ui_->select_tabbar_gradient_end_color, current_tabbar_gradient_end_color_);
  ui_->tabbar_gradient_direction->setCurrentIndex(qBound(0, s.value(kTabBarGradientDirection, static_cast<int>(kDefaultTabBarGradientDirection)).toInt(), ui_->tabbar_gradient_direction->count() - 1));
  ui_->tabbar_background_image->setText(s.value(kTabBarImage).toString());
  // The positions are BackgroundImagePosition, from 1.
  ui_->tabbar_background_image_position->setCurrentIndex(qBound(0, s.value(kTabBarImagePosition, kDefaultTabBarImagePosition).toInt() - 1, ui_->tabbar_background_image_position->count() - 1));
  ui_->tabbar_background_image_maxsize->setValue(s.value(kTabBarImageMaxSize, kDefaultTabBarImageMaxSize).toInt());
  ui_->tabbar_background_image_stretch->setChecked(s.value(kTabBarImageStretch, kDefaultTabBarImageStretch).toBool());
  ui_->tabbar_background_image_keep_aspect_ratio->setChecked(s.value(kTabBarImageKeepAspectRatio, kDefaultTabBarImageKeepAspectRatio).toBool());
  ui_->tabbar_background_image_do_not_cut->setChecked(s.value(kTabBarImageDoNotCut, kDefaultTabBarImageDoNotCut).toBool());
  ui_->tabbar_background_image_tile->setChecked(s.value(kTabBarImageTile, kDefaultTabBarImageTile).toBool());
  ui_->tabbar_background_image_blur->setValue(s.value(kTabBarImageBlurRadius, kDefaultTabBarImageBlurRadius).toInt());
  ui_->tabbar_background_image_blur_radius->setText(QStringLiteral("%1px").arg(ui_->tabbar_background_image_blur->value()));
  ui_->tabbar_background_image_opacity->setValue(s.value(kTabBarImageOpacity, kDefaultTabBarImageOpacity).toInt());
  ui_->tabbar_background_image_opacity_level->setText(QStringLiteral("%1%").arg(ui_->tabbar_background_image_opacity->value()));
  ui_->tabbar_background_image_tint->setChecked(s.value(kTabBarImageTint, kDefaultTabBarImageTint).toBool());
  TabBarBackgroundChanged();
  UpdateTabBarBackgroundPreviews();

  // Currently playing song color
  current_playlist_playing_song_color_ = s.value(kPlaylistPlayingSongColor).value<QColor>();
  if (current_playlist_playing_song_color_.isValid()) {
    ui_->playlist_playing_song_color_custom->setChecked(true);
  }
  else {
    ui_->playlist_playing_song_color_system->setChecked(true);
    current_playlist_playing_song_color_ = StyleHelper::highlightColor();
  }
  UpdateColorSelectorColor(ui_->select_playlist_playing_song_color, current_playlist_playing_song_color_);
  PlaylistPlayingSongColorSystem(ui_->playlist_playing_song_color_system->isChecked());

  // Playlist background image
  {
    const int v = s.value(kBackgroundImageType, static_cast<int>(kDefaultBackgroundImageType)).toInt();
    background_image_type_ = (v >= static_cast<int>(BackgroundImageType::Default) && v <= static_cast<int>(BackgroundImageType::Strawbs)) ? static_cast<BackgroundImageType>(v) : kDefaultBackgroundImageType;
  }
  background_image_filename_ = s.value(kBackgroundImageFilename).toString();

  ui_->use_system_color_set->setChecked(!original_use_custom_color_set_);
  ui_->use_custom_color_set->setChecked(original_use_custom_color_set_);
  ui_->widget_custom_colors->setEnabled(original_use_custom_color_set_);

  switch (background_image_type_) {
    case BackgroundImageType::Default:
      ui_->use_default_background->setChecked(true);
      break;
    case BackgroundImageType::None:
      ui_->use_no_background->setChecked(true);
      break;
    case BackgroundImageType::Custom:
      ui_->use_custom_background_image->setChecked(true);
      break;
    case BackgroundImageType::Album:
      ui_->use_album_cover_background->setChecked(true);
      break;
    case BackgroundImageType::Strawbs:
      ui_->use_strawbs_background->setChecked(true);
      break;
  }
  ui_->background_image_filename->setText(background_image_filename_);

  const int background_image_position = s.value(kBackgroundImagePosition, static_cast<int>(kDefaultBackgroundImagePosition)).toInt();
  int background_image_position_index = ui_->combobox_background_image_position->findData(background_image_position);
  if (background_image_position_index < 0) {
    background_image_position_index = ui_->combobox_background_image_position->findData(static_cast<int>(kDefaultBackgroundImagePosition));
  }
  ui_->combobox_background_image_position->setCurrentIndex(background_image_position_index);

  ui_->spinbox_background_image_maxsize->setValue(s.value(kBackgroundImageMaxSize, kDefaultBackgroundImageMaxSize).toInt());
  ui_->checkbox_background_image_stretch->setChecked(s.value(kBackgroundImageStretch, kDefaultBackgroundImageStretch).toBool());
  ui_->checkbox_background_image_do_not_cut->setChecked(s.value(kBackgroundImageDoNotCut, kDefaultBackgroundImageDoNotCut).toBool());
  ui_->checkbox_background_image_keep_aspect_ratio->setChecked(s.value(kBackgroundImageKeepAspectRatio, kDefaultBackgroundImageKeepAspectRatio).toBool());
  ui_->slider_background_image_blur->setValue(s.value(kBackgroundImageBlurRadius, kDefaultBackgroundImageBlurRadius).toInt());
  ui_->slider_background_image_opacity->setValue(s.value(kBackgroundImageOpacityLevel, kDefaultBackgroundImageOpacityLevel).toInt());

  ui_->checkbox_background_image_keep_aspect_ratio->setEnabled(ui_->checkbox_background_image_stretch->isChecked());
  ui_->checkbox_background_image_do_not_cut->setEnabled(ui_->checkbox_background_image_stretch->isChecked() && ui_->checkbox_background_image_keep_aspect_ratio->isChecked());

  // Button sizes
  ui_->spinbox_icon_size_tabbar_small_mode->setValue(s.value(kIconSizeTabbarSmallMode, kDefaultIconSizeTabbarSmallMode).toInt());
  ui_->spinbox_icon_size_tabbar_large_mode->setValue(s.value(kIconSizeTabbarLargeMode, kDefaultIconSizeTabbarLargeMode).toInt());
  ui_->spinbox_icon_size_play_control_buttons->setValue(s.value(kIconSizePlayControlButtons, kDefaultIconSizePlayControlButtons).toInt());
  ui_->spinbox_icon_size_playlist_buttons->setValue(s.value(kIconSizePlaylistButtons, kDefaultIconSizePlaylistButtons).toInt());
  ui_->spinbox_icon_size_left_panel_buttons->setValue(s.value(kIconSizeLeftPanelButtons, kDefaultIconSizeLeftPanelButtons).toInt());
  ui_->spinbox_icon_size_configure_buttons->setValue(s.value(kIconSizeConfigureButtons, kDefaultIconSizeConfigureButtons).toInt());

  // Layout
  ui_->checkbox_sequence_beside_play_controls->setChecked(s.value(kSequenceBesidePlayControls, kDefaultSequenceBesidePlayControls).toBool());
  ui_->spinbox_player_controls_height->setValue(s.value(kPlayerControlsHeight, kDefaultPlayerControlsHeight).toInt());
  {
    Settings playing_widget_settings;
    playing_widget_settings.beginGroup(QLatin1String(kPlayingWidgetSettingsGroup));
    original_small_playing_cover_ = playing_widget_settings.value(QLatin1String(kPlayingWidgetMode), kPlayerLayoutAkousma.small_playing_cover ? 0 : 1).toInt() == 0;
    playing_widget_settings.endGroup();
  }
  ui_->checkbox_small_playing_cover->setChecked(original_small_playing_cover_);
  PlayerLayout player_layout = PlayerLayout::Custom;
  if (s.contains(kPlayerLayout)) {
    player_layout = static_cast<PlayerLayout>(s.value(kPlayerLayout).toInt());
  }
  // Not chosen yet (e.g. settings imported from Strawberry): the layout whose values are set.
  else if (PlayerLayoutMatches(kPlayerLayoutAkousma)) {
    player_layout = PlayerLayout::Akousma;
  }
  else if (PlayerLayoutMatches(kPlayerLayoutStrawberry)) {
    player_layout = PlayerLayout::Strawberry;
  }
  ui_->combobox_player_layout->setCurrentIndex(qMax(0, ui_->combobox_player_layout->findData(static_cast<int>(player_layout))));
  UpdatePlayerLayoutControls();

  original_tabbar_bg_color_ = current_tabbar_bg_color_;
  original_background_image_filename_ = background_image_filename_;
  original_playlist_playing_song_color_ = current_playlist_playing_song_color_;

  s.endGroup();

  Init(ui_->layout_appearancesettingspage->parentWidget());

  if (!Settings().childGroups().contains(QLatin1String(kSettingsGroup))) set_changed();

  QObject::connect(ui_->combobox_style, &QComboBox::currentIndexChanged, this, &AppearanceSettingsPage::StyleChanged);
  InitStyle(ui_->combobox_style->currentIndex());

}

void AppearanceSettingsPage::Save() {

  Settings s;
  s.beginGroup(kSettingsGroup);

  // Style
  s.setValue(kStyle, ui_->combobox_style->currentText());

  QString selected_style = ui_->combobox_style->currentData().toString();
  if (selected_style.compare(u"default"_s, Qt::CaseInsensitive) == 0) {
    selected_style = appearance_->default_style();
  }

  // Icon set. system_icons is kept up to date for older versions reading these settings.
  const QString icon_set = ui_->combobox_icon_set->currentData().toString().isEmpty() ? QLatin1String(IconLoader::kIconSetBuiltIn) : ui_->combobox_icon_set->currentData().toString();
  s.setValue(kIconSet, icon_set);
  s.setValue(kSystemThemeIcons, icon_set == QLatin1String(IconLoader::kIconSetSystem));

  // The application icon changes when the settings are applied (MainWindow::ReloadSettings).
  s.setValue(kAppIcon, AppIcon::ValidStyle(ui_->combobox_app_icon->currentData().toString()));
  // Also the icon set's pomegranate, right away, though the set's other icons wait for the restart.
  s.setValue(kAppIconFromIconSet, ui_->checkbox_app_icon_from_iconset->isChecked());
  // The icons are set when the windows are made, so another icon set needs a restart. Ask after all pages are saved.
  if (icon_set != IconLoader::CurrentIconSet() && icon_set != restart_asked_icon_set_) {
    restart_asked_icon_set_ = icon_set;
    const QString icon_set_name = ui_->combobox_icon_set->currentText();
    QPointer<SettingsDialog> settings_dialog = dialog();
    QTimer::singleShot(0, settings_dialog, [settings_dialog, icon_set_name]() {
      if (!settings_dialog) return;
      QWidget *parent = settings_dialog->isVisible() ? static_cast<QWidget*>(settings_dialog) : settings_dialog->parentWidget();
      if (SettingsTransfer::AskRestart(parent, tr("Icon set"), tr("Akousma uses the icon set \"%1\" from the next start.").arg(icon_set_name))) {
        settings_dialog->RequestRestart();
      }
    });
  }

  s.setValue(kDarkMode, Utilities::StyleHasDarkModeSupport(selected_style) ? ui_->checkbox_dark_mode->isChecked() : false);

#ifdef Q_OS_WIN32
  s.setValue(kWindowBackdrop, ui_->combobox_window_backdrop->currentData().toInt());
  s.setValue(kWindowBackdropTranslucentContent, ui_->checkbox_window_backdrop_translucent_content->isChecked());
#endif

  // Colors
  const bool use_custom_color_set = Utilities::StyleHasCustomPaletteColorsSupport(selected_style) && ui_->use_custom_color_set->isChecked();
  s.setValue(kUseCustomColorSet, use_custom_color_set);
  if (use_custom_color_set) {
    for (const Appearance::ColorRole &color_role : Appearance::ColorRoles()) {
      s.setValue(color_role.settings_key, current_colors_.value(color_role.role));
    }
  }
  else {
    QApplication::setPalette(system_palette_);
    for (const Appearance::ColorRole &color_role : Appearance::ColorRoles()) {
      s.remove(color_role.settings_key);
    }
  }

  // Tabbar background color
  s.setValue(kTabBarSystemColor, ui_->tabbar_system_color->isChecked());
  s.setValue(kTabBarGradient, ui_->tabbar_gradient->isChecked());
  s.setValue(kTabBarColor, current_tabbar_bg_color_);

  // Tabbar background
  s.setValue(kTabBarBackground, CurrentTabBarBackground());
  if (CurrentTabBarTheme() >= 0) s.setValue(kTabBarTheme, tabbar_themes_[CurrentTabBarTheme()].name);
  else s.remove(kTabBarTheme);
  s.setValue(kTabBarGradientCustomColors, ui_->tabbar_gradient_custom_colors->isChecked());
  s.setValue(kTabBarGradientStartColor, current_tabbar_gradient_start_color_);
  s.setValue(kTabBarGradientEndColor, current_tabbar_gradient_end_color_);
  s.setValue(kTabBarGradientDirection, ui_->tabbar_gradient_direction->currentIndex());
  s.setValue(kTabBarImage, ui_->tabbar_background_image->text());
  s.setValue(kTabBarImagePosition, ui_->tabbar_background_image_position->currentIndex() + 1);
  s.setValue(kTabBarImageMaxSize, ui_->tabbar_background_image_maxsize->value());
  s.setValue(kTabBarImageStretch, ui_->tabbar_background_image_stretch->isChecked());
  s.setValue(kTabBarImageKeepAspectRatio, ui_->tabbar_background_image_keep_aspect_ratio->isChecked());
  s.setValue(kTabBarImageDoNotCut, ui_->tabbar_background_image_do_not_cut->isChecked());
  s.setValue(kTabBarImageTile, ui_->tabbar_background_image_tile->isChecked());
  s.setValue(kTabBarImageBlurRadius, ui_->tabbar_background_image_blur->value());
  s.setValue(kTabBarImageOpacity, ui_->tabbar_background_image_opacity->value());
  s.setValue(kTabBarImageTint, ui_->tabbar_background_image_tint->isChecked());

  // Currently playing song color
  if (ui_->playlist_playing_song_color_system->isChecked()) {
    s.setValue(kPlaylistPlayingSongColor, QColor());
  }
  else {
    s.setValue(kPlaylistPlayingSongColor, current_playlist_playing_song_color_);
  }

  // Playlist background image
  background_image_filename_ = ui_->background_image_filename->text();
  if (ui_->use_default_background->isChecked()) {
    background_image_type_ = BackgroundImageType::Default;
  }
  else if (ui_->use_no_background->isChecked()) {
    background_image_type_ = BackgroundImageType::None;
  }
  else if (ui_->use_album_cover_background->isChecked()) {
    background_image_type_ = BackgroundImageType::Album;
  }
  else if (ui_->use_strawbs_background->isChecked()) {
    background_image_type_ = BackgroundImageType::Strawbs;
  }
  else if (ui_->use_custom_background_image->isChecked()) {
    background_image_type_ = BackgroundImageType::Custom;
  }
  s.setValue(kBackgroundImageType, static_cast<int>(background_image_type_));

  if (background_image_type_ == BackgroundImageType::Custom) {
    s.setValue(kBackgroundImageFilename, background_image_filename_);
  }
  else {
    s.remove(kBackgroundImageFilename);
  }

  s.setValue(kBackgroundImageMaxSize, ui_->spinbox_background_image_maxsize->value());
  s.setValue(kBackgroundImagePosition, ui_->combobox_background_image_position->currentData().toInt());
  s.setValue(kBackgroundImageStretch, ui_->checkbox_background_image_stretch->isChecked());
  s.setValue(kBackgroundImageDoNotCut, ui_->checkbox_background_image_do_not_cut->isChecked());
  s.setValue(kBackgroundImageKeepAspectRatio, ui_->checkbox_background_image_keep_aspect_ratio->isChecked());

  s.setValue(kBackgroundImageBlurRadius, ui_->slider_background_image_blur->value());
  s.setValue(kBackgroundImageOpacityLevel, ui_->slider_background_image_opacity->value());

  // Button sizes
  s.setValue(kIconSizeTabbarSmallMode, ui_->spinbox_icon_size_tabbar_small_mode->value());
  s.setValue(kIconSizeTabbarLargeMode, ui_->spinbox_icon_size_tabbar_large_mode->value());
  s.setValue(kIconSizePlayControlButtons, ui_->spinbox_icon_size_play_control_buttons->value());
  s.setValue(kIconSizePlaylistButtons, ui_->spinbox_icon_size_playlist_buttons->value());
  s.setValue(kIconSizeLeftPanelButtons, ui_->spinbox_icon_size_left_panel_buttons->value());
  s.setValue(kIconSizeConfigureButtons, ui_->spinbox_icon_size_configure_buttons->value());

  // Layout
  s.setValue(kPlayerLayout, ui_->combobox_player_layout->currentData().toInt());
  s.setValue(kSequenceBesidePlayControls, ui_->checkbox_sequence_beside_play_controls->isChecked());
  s.setValue(kPlayerControlsHeight, ui_->spinbox_player_controls_height->value());
  // Only when changed here, the album cover's context menu changes it too.
  if (ui_->checkbox_small_playing_cover->isChecked() != original_small_playing_cover_) {
    Settings playing_widget_settings;
    playing_widget_settings.beginGroup(QLatin1String(kPlayingWidgetSettingsGroup));
    playing_widget_settings.setValue(QLatin1String(kPlayingWidgetMode), ui_->checkbox_small_playing_cover->isChecked() ? 0 : 1);
    playing_widget_settings.endGroup();
    original_small_playing_cover_ = ui_->checkbox_small_playing_cover->isChecked();
  }

  s.endGroup();

  appearance_->set_system_palette(system_palette_);

}

void AppearanceSettingsPage::Cancel() {

  const QString current_style = QApplication::style() ? QApplication::style()->objectName() : QString();
  if (original_style_.compare(current_style, Qt::CaseInsensitive) != 0) {
    ApplyStyle(current_style, original_style_);
  }

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
  QGuiApplication::styleHints()->setColorScheme(original_dark_mode_ ? Qt::ColorScheme::Dark : Qt::ColorScheme::Unknown);
#endif

  if (original_use_custom_color_set_) {
    Appearance::SetCustomPaletteColors(original_colors_);
  }
  else {
    QApplication::setPalette(system_palette_);
  }

  background_image_filename_ = original_background_image_filename_;
  current_tabbar_bg_color_ = original_tabbar_bg_color_;
  current_playlist_playing_song_color_ = original_playlist_playing_song_color_;

}

void AppearanceSettingsPage::InitStyle(const int index) {

  SetStyle(index, false);

}

void AppearanceSettingsPage::StyleChanged(const int index) {

  SetStyle(index, true);

}

void AppearanceSettingsPage::SetStyle(const int index, const bool apply) {

  const QString style_name = ui_->combobox_style->itemData(index).toString();
  SetStyle(style_name, apply);

}

void AppearanceSettingsPage::SetStyle(const QString &new_style, const bool apply) {

  QString current_style = QApplication::style() ? QApplication::style()->objectName() : QString();

  bool style_changed = false;
  if (apply) {
    style_changed = ApplyStyle(current_style, new_style);
    current_style = QApplication::style() ? QApplication::style()->objectName() : QString();
  }

  const QString style_for_caps = apply ? current_style : (new_style.compare("default"_L1, Qt::CaseInsensitive) == 0 ? appearance_->default_style() : new_style);
  const bool custom_palette_colors_support = Utilities::StyleHasCustomPaletteColorsSupport(style_for_caps);
  const bool dark_mode_support = Utilities::StyleHasDarkModeSupport(style_for_caps);

  if (!dark_mode_support && ui_->checkbox_dark_mode->isChecked()) {
    ui_->checkbox_dark_mode->setChecked(false);
  }

  if (!custom_palette_colors_support && !ui_->use_system_color_set->isChecked()) {
    ui_->use_system_color_set->setChecked(true);
  }

  ui_->checkbox_dark_mode->setEnabled(dark_mode_support);
  ui_->groupbox_colors->setEnabled(custom_palette_colors_support);

  if (style_changed) {
    QApplication::setPalette(QPalette());
    system_palette_ = QApplication::palette();
    if (ui_->use_custom_color_set->isChecked()) {
      ApplyCustomColors();
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QGuiApplication::styleHints()->setColorScheme(dark_mode_support && ui_->checkbox_dark_mode->isChecked() ? Qt::ColorScheme::Dark : Qt::ColorScheme::Unknown);
#endif
  }

}

bool AppearanceSettingsPage::ApplyStyle(const QString &current_style, const QString &new_style) {

  QString effective_new_style = new_style;
  if (new_style.compare("default"_L1, Qt::CaseInsensitive) == 0) {
    effective_new_style = appearance_->default_style();
  }
  if (current_style.compare(effective_new_style, Qt::CaseInsensitive) != 0) {
    qLog(Debug) << "Changing style from" << current_style << "to" << effective_new_style;
    if (QApplication::setStyle(effective_new_style)) {
      qLog(Debug) << "Style is set to" << effective_new_style;
      return true;
    }
    else {
      qLog(Warning) << "Could not set style" << effective_new_style;
    }
  }

  return false;

}

void AppearanceSettingsPage::DarkModeToggled(const bool checked) {

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
  QGuiApplication::styleHints()->setColorScheme(checked ? Qt::ColorScheme::Dark : Qt::ColorScheme::Unknown);
#else
  Q_UNUSED(checked)
#endif

}

QString AppearanceSettingsPage::ColorRoleLabel(const QPalette::ColorRole color_role) {

  switch (color_role) {
    case QPalette::Window:          return tr("Window");
    case QPalette::WindowText:      return tr("Window text");
    case QPalette::Base:            return tr("Base");
    case QPalette::AlternateBase:   return tr("Alternate base");
    case QPalette::ToolTipBase:     return tr("Tooltip base");
    case QPalette::ToolTipText:     return tr("Tooltip text");
    case QPalette::PlaceholderText: return tr("Placeholder text");
    case QPalette::Text:            return tr("Text");
    case QPalette::Button:          return tr("Button");
    case QPalette::ButtonText:      return tr("Button text");
    case QPalette::BrightText:      return tr("Bright text");
    default: return QString();
  }

}

void AppearanceSettingsPage::CreateColorSelectors() {

  QFormLayout *layout = ui_->layout_custom_colors;
  for (const Appearance::ColorRole &color_role : Appearance::ColorRoles()) {
    QPushButton *button = new QPushButton(ui_->widget_custom_colors);
    button->setToolTip(tr("Select color"));
    const QPalette::ColorRole color_role_role = color_role.role;
    QObject::connect(button, &QPushButton::pressed, this, [this, color_role_role]() { SelectColor(color_role_role); });
    layout->addRow(ColorRoleLabel(color_role_role) + u':', button);
    color_selectors_.insert(color_role_role, button);
  }

}

void AppearanceSettingsPage::SelectColor(const QPalette::ColorRole role) {

  const QColor color_selected = QColorDialog::getColor(current_colors_.value(role));
  if (!color_selected.isValid()) return;

  current_colors_[role] = color_selected;

  const auto it = color_selectors_.constFind(role);
  if (it != color_selectors_.constEnd() && it.value()) {
    UpdateColorSelectorColor(it.value(), color_selected);
  }

  ApplyCustomColors();

  set_changed();

}

void AppearanceSettingsPage::ApplyCustomColors() {
  Appearance::SetCustomPaletteColors(current_colors_);
}

void AppearanceSettingsPage::SetDarkColors() {

  // Switch to a custom color set and fill it with colors suitable for a dark theme.
  ui_->use_custom_color_set->setChecked(true);

  const QMap<QPalette::ColorRole, QColor> &dark_colors = Appearance::DarkColors();
  for (const Appearance::ColorRole &color_role : Appearance::ColorRoles()) {
    current_colors_[color_role.role] = dark_colors.value(color_role.role);
  }

  UpdateColorSelectorsColors();
  ApplyCustomColors();

  set_changed();

}

void AppearanceSettingsPage::UseCustomColorSetOptionChanged(const bool checked) {

  if (checked) {
    ApplyCustomColors();
  }
  else {
    QApplication::setPalette(system_palette_);
  }

}

void AppearanceSettingsPage::ResetToDefaultColors() {

  // Reset the custom color set back to the system default colors.
  const QPalette &p = system_palette_;
  for (const Appearance::ColorRole &color_role : Appearance::ColorRoles()) {
    current_colors_[color_role.role] = p.color(color_role.role);
  }

  UpdateColorSelectorsColors();

  if (ui_->use_custom_color_set->isChecked()) {
    ApplyCustomColors();
  }

  set_changed();

}

void AppearanceSettingsPage::UpdateColorSelectorsColors() const {

  for (auto it = color_selectors_.constBegin(); it != color_selectors_.constEnd(); ++it) {
    if (it.value()) {
      UpdateColorSelectorColor(it.value(), current_colors_.value(it.key()));
    }
  }

}

void AppearanceSettingsPage::UpdateColorSelectorColor(QWidget *color_selector, const QColor &color) {

  const QString css = QStringLiteral("background-color: rgb(%1, %2, %3); color: rgb(255, 255, 255); border: 1px dotted black;").arg(color.red()).arg(color.green()).arg(color.blue());
  if (color_selector->styleSheet() != css) {
    color_selector->setStyleSheet(css);
  }

}

void AppearanceSettingsPage::SelectBackgroundImage() {

  QString selected_filename = QFileDialog::getOpenFileName(this, tr("Select background image"), background_image_filename_, tr(kLoadImageFileFilter) + u";;"_s + tr(kAllFilesFilterSpec));
  if (selected_filename.isEmpty()) return;
  background_image_filename_ = selected_filename;
  ui_->background_image_filename->setText(background_image_filename_);

}

void AppearanceSettingsPage::BackgroundImageBlurLevelChanged(const int value) {
  ui_->label_background_image_blur_radius->setText(QStringLiteral("%1px").arg(value));
}

void AppearanceSettingsPage::BackgroundImageOpacityLevelChanged(const int percent) {
  ui_->label_background_image_opacity->setText(QStringLiteral("%1%").arg(percent));
}

void AppearanceSettingsPage::TabBarSystemColor(const bool checked) {

  if (checked) {
    current_tabbar_bg_color_ = FancyTabWidget::DefaultTabbarBgColor();
    UpdateColorSelectorColor(ui_->select_tabbar_color, current_tabbar_bg_color_);
  }
  ui_->layout_tabbar_color->setEnabled(!checked);
  ui_->select_tabbar_color->setEnabled(!checked);

}

void AppearanceSettingsPage::TabBarBackgroundChanged() {

  const TabBarBackground background = static_cast<TabBarBackground>(CurrentTabBarBackground());
  const bool custom_image = background == TabBarBackground::CustomImage;
  UpdateTabBarThemeButtons();
  ui_->select_tabbar_gradient_start_color->setEnabled(ui_->tabbar_gradient_custom_colors->isChecked());
  ui_->select_tabbar_gradient_end_color->setEnabled(ui_->tabbar_gradient_custom_colors->isChecked());
  ui_->tabbar_background_image->setEnabled(custom_image);
  ui_->select_tabbar_background_image->setEnabled(custom_image);
  // Same dependencies as the playlist background image options.
  ui_->widget_tabbar_background_image_options->setEnabled(custom_image);
  const bool tile = ui_->tabbar_background_image_tile->isChecked();
  ui_->tabbar_background_image_stretch->setEnabled(!tile);
  ui_->tabbar_background_image_keep_aspect_ratio->setEnabled(!tile && ui_->tabbar_background_image_stretch->isChecked());
  ui_->tabbar_background_image_do_not_cut->setEnabled(!tile && ui_->tabbar_background_image_stretch->isChecked() && ui_->tabbar_background_image_keep_aspect_ratio->isChecked());
  ui_->tabbar_background_image_maxsize->setEnabled(!ui_->tabbar_background_image_stretch->isChecked() || tile);
  ui_->tabbar_background_image_position->setEnabled(!tile);

}

// A layout was chosen in the dropdown: Akousma and Strawberry set their values, Custom starts from the values of the layout chosen before.
void AppearanceSettingsPage::PlayerLayoutChosen() {

  const PlayerLayout player_layout = static_cast<PlayerLayout>(ui_->combobox_player_layout->currentData().toInt());
  if (player_layout != PlayerLayout::Custom) {
    const PlayerLayoutValues &values = player_layout == PlayerLayout::Strawberry ? kPlayerLayoutStrawberry : kPlayerLayoutAkousma;
    ui_->spinbox_icon_size_tabbar_small_mode->setValue(values.icon_size_tabbar_small_mode);
    ui_->spinbox_icon_size_tabbar_large_mode->setValue(values.icon_size_tabbar_large_mode);
    ui_->spinbox_icon_size_play_control_buttons->setValue(values.icon_size_play_control_buttons);
    ui_->spinbox_icon_size_playlist_buttons->setValue(values.icon_size_playlist_buttons);
    ui_->spinbox_icon_size_left_panel_buttons->setValue(values.icon_size_left_panel_buttons);
    ui_->spinbox_icon_size_configure_buttons->setValue(values.icon_size_configure_buttons);
    ui_->checkbox_sequence_beside_play_controls->setChecked(values.sequence_beside_play_controls);
    ui_->checkbox_small_playing_cover->setChecked(values.small_playing_cover);
    ui_->spinbox_player_controls_height->setValue(values.player_controls_height);
    SetTabBarBackground(static_cast<int>(values.tabbar_background));
  }
  UpdatePlayerLayoutControls();

  set_changed();

}

void AppearanceSettingsPage::UpdatePlayerLayoutControls() {

  const PlayerLayout player_layout = static_cast<PlayerLayout>(ui_->combobox_player_layout->currentData().toInt());
  const bool custom = player_layout == PlayerLayout::Custom;
  ui_->widget_player_layout_custom->setEnabled(custom);
  ui_->groupbox_icon_sizes->setEnabled(custom);

  switch (player_layout) {
    case PlayerLayout::Akousma:
      ui_->label_player_layout_description->setText(tr("Clementine's sizes: %1 px playback buttons with repeat and shuffle beside them in the same size, a small album cover with the song details in the sidebar and the plain gradient sidebar background.").arg(kPlayerLayoutAkousma.icon_size_play_control_buttons));
      break;
    case PlayerLayout::Strawberry:
      ui_->label_player_layout_description->setText(tr("Strawberry's original layout: %1 px playback buttons, repeat and shuffle in the status bar, a large album cover in the sidebar and the strawberry sidebar background.").arg(kPlayerLayoutStrawberry.icon_size_play_control_buttons));
      break;
    case PlayerLayout::Custom:
      ui_->label_player_layout_description->setText(tr("Set the sizes and positions below and the icon sizes yourself. The sidebar background is chosen under Tabbar background."));
      break;
  }

}

bool AppearanceSettingsPage::PlayerLayoutMatches(const PlayerLayoutValues &values) const {

  return ui_->spinbox_icon_size_tabbar_small_mode->value() == values.icon_size_tabbar_small_mode &&
         ui_->spinbox_icon_size_tabbar_large_mode->value() == values.icon_size_tabbar_large_mode &&
         ui_->spinbox_icon_size_play_control_buttons->value() == values.icon_size_play_control_buttons &&
         ui_->spinbox_icon_size_playlist_buttons->value() == values.icon_size_playlist_buttons &&
         ui_->spinbox_icon_size_left_panel_buttons->value() == values.icon_size_left_panel_buttons &&
         ui_->spinbox_icon_size_configure_buttons->value() == values.icon_size_configure_buttons &&
         ui_->checkbox_sequence_beside_play_controls->isChecked() == values.sequence_beside_play_controls &&
         ui_->spinbox_player_controls_height->value() == values.player_controls_height;

}

void AppearanceSettingsPage::SetTabBarBackground(const int background) {

  // The removed album cover background and unknown values are the plain gradient.
  int idx = ui_->tabbar_background->findData(static_cast<int>(TabBarBackgroundFromSetting(background)));
  if (idx < 0) idx = ui_->tabbar_background->findData(static_cast<int>(kDefaultTabBarBackground));
  ui_->tabbar_background->setCurrentIndex(idx);

}

// Renders a preview of each background into the dropdown: the background as the sidebar draws it, with two tabs.
void AppearanceSettingsPage::UpdateTabBarBackgroundPreviews() {

  const QSize preview_size(kTabBarPreviewWidth, kTabBarPreviewHeight);

  FancyTabWidget::BackgroundOptions options;
  options.system_color = ui_->tabbar_system_color->isChecked();
  options.color = options.system_color ? FancyTabWidget::DefaultTabbarBgColor() : current_tabbar_bg_color_;
  options.gradient = ui_->tabbar_gradient->isChecked();
  options.gradient_custom_colors = ui_->tabbar_gradient_custom_colors->isChecked();
  options.gradient_start_color = current_tabbar_gradient_start_color_;
  options.gradient_end_color = current_tabbar_gradient_end_color_;
  options.gradient_direction = ui_->tabbar_gradient_direction->currentIndex();
  options.image_position = static_cast<int>(BackgroundImagePosition::Middle);
  options.image_stretch = true;
  options.image_keep_aspect_ratio = true;
  options.image_opacity = ui_->tabbar_background_image_opacity->value();
  options.image_tint = ui_->tabbar_background_image_tint->isChecked();

  // The icons of the icon set chosen on this page.
  const IconLoader::IconSet icon_set = SelectedIconSetEntry();
  const qreal device_pixel_ratio = devicePixelRatioF();
  const QList<QPair<QIcon, QString>> tabs = { qMakePair(QIcon(IconLoader::IconSetPixmap(icon_set, u"library-music"_s, qRound(24 * device_pixel_ratio))), tr("Collection")), qMakePair(QIcon(IconLoader::IconSetPixmap(icon_set, u"view-media-playlist"_s, qRound(24 * device_pixel_ratio))), tr("Playlists")) };
  const QPixmap edit_badge = IconLoader::IconSetPixmap(icon_set, u"document-edit"_s, qRound(kTabBarBadgeSize * device_pixel_ratio));
  const int tab_width = kTabBarPreviewWidth / static_cast<int>(tabs.count());
  constexpr int kIconSize = 24;

  QFont font(this->font());
  font.setBold(true);
  font.setPointSizeF(font.pointSizeF() * 0.85);

  const FancyTabWidget::BackgroundOptions page_options = options;
  for (int i = 0; i < ui_->tabbar_background->count(); ++i) {
    const QVariant item_data = ui_->tabbar_background->itemData(i);
    if (!item_data.isValid()) continue;  // Separator
    bool custom_badge = false;
    bool checkerboard = false;
    options = page_options;
    if (item_data.toInt() == kTabBarThemeItem) {
      // A saved theme: its own look, without the badge.
      const qsizetype theme_index = TabBarThemes::IndexOf(tabbar_themes_, ui_->tabbar_background->itemData(i, kTabBarThemeNameRole).toString());
      if (theme_index < 0) continue;
      options = ThemeBackgroundOptions(tabbar_themes_[theme_index]);
    }
    else {
      options.mode = item_data.toInt();
      options.graphics = FancyTabWidget::BackgroundGraphics(options.mode);
      options.image = QPixmap();
      if (static_cast<TabBarBackground>(options.mode) == TabBarBackground::CustomImage) {
        // Custom is always recognisable: the chosen image, or a checkerboard before one is chosen, with an edit badge.
        custom_badge = true;
        if (!ui_->tabbar_background_image->text().isEmpty()) options.image = QPixmap(ui_->tabbar_background_image->text());
        checkerboard = options.image.isNull();
      }
    }
    QPixmap pixmap = FancyTabWidget::RenderBackground(preview_size, options);
    QPainter p(&pixmap);
    if (checkerboard) DrawCheckerboard(&p, pixmap.rect());
    p.setFont(font);
    for (int t = 0; t < tabs.count(); ++t) {
      const QRect tab_rect(t * tab_width, 0, tab_width, kTabBarPreviewHeight);
      tabs[t].first.paint(&p, QRect(tab_rect.left() + (tab_width - kIconSize) / 2, 3, kIconSize, kIconSize));
      const QRect text_rect = tab_rect.adjusted(0, 0, 0, -3);
      // Drop shadow and text, as the sidebar draws them.
      p.setPen(QColor(0, 0, 0, 110));
      p.drawText(text_rect.translated(0, 1), Qt::AlignHCenter | Qt::AlignBottom, tabs[t].second);
      p.setPen(checkerboard ? QColor(60, 60, 60) : StyleHelper::panelTextColor());
      p.drawText(text_rect, Qt::AlignHCenter | Qt::AlignBottom, tabs[t].second);
    }
    if (custom_badge) {
      DrawEditBadge(&p, QRect(pixmap.width() - kTabBarBadgeSize - 4, 4, kTabBarBadgeSize, kTabBarBadgeSize), edit_badge);
    }
    p.end();
    ui_->tabbar_background->setItemData(i, pixmap, Qt::DecorationRole);
  }

}

int AppearanceSettingsPage::CurrentTabBarTheme() const {

  if (ui_->tabbar_background->currentData().toInt() != kTabBarThemeItem) return -1;
  return static_cast<int>(TabBarThemes::IndexOf(tabbar_themes_, ui_->tabbar_background->currentData(kTabBarThemeNameRole).toString()));

}

// The background drawn for the current dropdown entry, for a saved theme the background it is based on.
int AppearanceSettingsPage::CurrentTabBarBackground() const {

  const int theme_index = CurrentTabBarTheme();
  if (theme_index >= 0) return tabbar_themes_[theme_index].background;
  const int item_data = ui_->tabbar_background->currentData().toInt();
  return item_data == kTabBarThemeItem ? static_cast<int>(kDefaultTabBarBackground) : item_data;

}

// The user's themes after the built-in backgrounds, behind a separator.
void AppearanceSettingsPage::UpdateTabBarThemeItems() {

  const QString current_theme = CurrentTabBarTheme() >= 0 ? tabbar_themes_[CurrentTabBarTheme()].name : QString();
  const int current_background = ui_->tabbar_background->currentData().toInt();

  ui_->tabbar_background->blockSignals(true);
  while (ui_->tabbar_background->count() > 0) {
    const QVariant item_data = ui_->tabbar_background->itemData(ui_->tabbar_background->count() - 1);
    if (item_data.isValid() && item_data.toInt() != kTabBarThemeItem) break;
    ui_->tabbar_background->removeItem(ui_->tabbar_background->count() - 1);
  }
  if (!tabbar_themes_.isEmpty()) {
    ui_->tabbar_background->insertSeparator(ui_->tabbar_background->count());
    for (const TabBarThemes::Theme &theme : std::as_const(tabbar_themes_)) {
      ui_->tabbar_background->addItem(theme.name, kTabBarThemeItem);
      ui_->tabbar_background->setItemData(ui_->tabbar_background->count() - 1, theme.name, kTabBarThemeNameRole);
    }
  }
  int idx = current_theme.isEmpty() ? -1 : ui_->tabbar_background->findData(current_theme, kTabBarThemeNameRole, Qt::MatchFixedString);
  if (idx < 0 && current_background != kTabBarThemeItem) idx = ui_->tabbar_background->findData(current_background);
  if (idx < 0) idx = ui_->tabbar_background->findData(static_cast<int>(kDefaultTabBarBackground));
  ui_->tabbar_background->setCurrentIndex(idx);
  ui_->tabbar_background->blockSignals(false);

  UpdateTabBarBackgroundPreviews();
  TabBarBackgroundChanged();

}

void AppearanceSettingsPage::UpdateTabBarThemeButtons() {

  // Saving makes sense for what the built-in backgrounds don't have: a custom image, a custom color or custom gradient colors, or a theme.
  const bool theme = CurrentTabBarTheme() >= 0;
  const bool custom = theme || CurrentTabBarBackground() == static_cast<int>(TabBarBackground::CustomImage) || ui_->tabbar_custom_color->isChecked() || ui_->tabbar_gradient_custom_colors->isChecked();
  ui_->button_tabbar_theme_save->setEnabled(custom);
  ui_->button_tabbar_theme_rename->setEnabled(theme);
  ui_->button_tabbar_theme_delete->setEnabled(theme);

}

FancyTabWidget::BackgroundOptions AppearanceSettingsPage::ThemeBackgroundOptions(const TabBarThemes::Theme &theme) {

  FancyTabWidget::BackgroundOptions options;
  options.mode = theme.background;
  options.system_color = theme.system_color;
  options.color = theme.system_color || !theme.color.isValid() ? FancyTabWidget::DefaultTabbarBgColor() : theme.color;
  options.gradient = theme.gradient;
  options.gradient_custom_colors = theme.gradient_custom_colors;
  options.gradient_start_color = theme.gradient_start_color;
  options.gradient_end_color = theme.gradient_end_color;
  options.gradient_direction = theme.gradient_direction;
  options.graphics = FancyTabWidget::BackgroundGraphics(theme.background);
  if (theme.background == static_cast<int>(TabBarBackground::CustomImage) && !theme.image.isEmpty()) options.image = QPixmap(theme.image);
  options.image_position = theme.image_position;
  options.image_max_size = theme.image_max_size;
  options.image_stretch = theme.image_stretch;
  options.image_keep_aspect_ratio = theme.image_keep_aspect_ratio;
  options.image_do_not_cut = theme.image_do_not_cut;
  options.image_tile = theme.image_tile;
  options.image_opacity = theme.image_opacity;
  options.image_tint = theme.image_tint;
  return options;

}

// A saved theme was chosen: the options on this page become the theme's, saving the page applies them.
void AppearanceSettingsPage::TabBarThemeChosen() {

  const int theme_index = CurrentTabBarTheme();
  if (theme_index < 0) return;
  const TabBarThemes::Theme &theme = tabbar_themes_[theme_index];

  ui_->tabbar_system_color->setChecked(theme.system_color);
  ui_->tabbar_custom_color->setChecked(!theme.system_color);
  if (!theme.system_color && theme.color.isValid()) {
    current_tabbar_bg_color_ = theme.color;
    UpdateColorSelectorColor(ui_->select_tabbar_color, current_tabbar_bg_color_);
  }
  ui_->tabbar_gradient->setChecked(theme.gradient);
  ui_->tabbar_gradient_custom_colors->setChecked(theme.gradient_custom_colors);
  if (theme.gradient_start_color.isValid()) current_tabbar_gradient_start_color_ = theme.gradient_start_color;
  if (theme.gradient_end_color.isValid()) current_tabbar_gradient_end_color_ = theme.gradient_end_color;
  UpdateColorSelectorColor(ui_->select_tabbar_gradient_start_color, current_tabbar_gradient_start_color_);
  UpdateColorSelectorColor(ui_->select_tabbar_gradient_end_color, current_tabbar_gradient_end_color_);
  ui_->tabbar_gradient_direction->setCurrentIndex(qBound(0, theme.gradient_direction, ui_->tabbar_gradient_direction->count() - 1));
  ui_->tabbar_background_image->setText(theme.image);
  ui_->tabbar_background_image_position->setCurrentIndex(qBound(0, theme.image_position - 1, ui_->tabbar_background_image_position->count() - 1));
  ui_->tabbar_background_image_maxsize->setValue(theme.image_max_size);
  ui_->tabbar_background_image_stretch->setChecked(theme.image_stretch);
  ui_->tabbar_background_image_keep_aspect_ratio->setChecked(theme.image_keep_aspect_ratio);
  ui_->tabbar_background_image_do_not_cut->setChecked(theme.image_do_not_cut);
  ui_->tabbar_background_image_tile->setChecked(theme.image_tile);
  ui_->tabbar_background_image_blur->setValue(theme.image_blur_radius);
  ui_->tabbar_background_image_opacity->setValue(theme.image_opacity);
  ui_->tabbar_background_image_tint->setChecked(theme.image_tint);

  TabBarBackgroundChanged();
  UpdateTabBarBackgroundPreviews();
  set_changed();

}

// Asks for a theme name: not empty, not the name of a built-in background or of another theme than except_index. Empty when cancelled.
QString AppearanceSettingsPage::AskTabBarThemeName(const QString &title, const QString &initial_name, const qsizetype except_index, bool *overwrite) {

  if (overwrite) *overwrite = false;
  QString name = initial_name;
  Q_FOREVER {
    bool ok = false;
    name = QInputDialog::getText(this, title, tr("Theme name:"), QLineEdit::Normal, name, &ok).trimmed();
    if (!ok) return QString();
    if (name.isEmpty()) {
      QMessageBox::warning(this, title, tr("The theme needs a name."));
      continue;
    }
    bool built_in = TabBarThemes::IsBuiltInName(name);
    for (int i = 0; i < ui_->tabbar_background->count() && !built_in; ++i) {
      const QVariant item_data = ui_->tabbar_background->itemData(i);
      if (item_data.isValid() && item_data.toInt() != kTabBarThemeItem && ui_->tabbar_background->itemText(i).compare(name, Qt::CaseInsensitive) == 0) built_in = true;
    }
    if (built_in) {
      QMessageBox::warning(this, title, tr("\"%1\" is the name of a built-in background, choose another name.").arg(name));
      continue;
    }
    const qsizetype existing = TabBarThemes::IndexOf(tabbar_themes_, name);
    if (existing >= 0 && existing != except_index) {
      if (!overwrite) {
        QMessageBox::warning(this, title, tr("There is a theme called \"%1\" already, choose another name.").arg(name));
        continue;
      }
      if (QMessageBox::question(this, title, tr("There is a theme called \"%1\" already. Replace it?").arg(name), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) continue;
      *overwrite = true;
    }
    return name;
  }

}

void AppearanceSettingsPage::TabBarThemeSave() {

  const QString title = tr("Save sidebar theme");
  const int current_theme = CurrentTabBarTheme();
  bool overwrite = false;
  const QString name = AskTabBarThemeName(title, current_theme >= 0 ? tabbar_themes_[current_theme].name : QString(), -1, &overwrite);
  if (name.isEmpty()) return;

  TabBarThemes::Theme theme;
  theme.name = name;
  theme.background = CurrentTabBarBackground();
  theme.system_color = ui_->tabbar_system_color->isChecked();
  theme.color = current_tabbar_bg_color_;
  theme.gradient = ui_->tabbar_gradient->isChecked();
  theme.gradient_custom_colors = ui_->tabbar_gradient_custom_colors->isChecked();
  theme.gradient_start_color = current_tabbar_gradient_start_color_;
  theme.gradient_end_color = current_tabbar_gradient_end_color_;
  theme.gradient_direction = ui_->tabbar_gradient_direction->currentIndex();
  theme.image_position = ui_->tabbar_background_image_position->currentIndex() + 1;
  theme.image_max_size = ui_->tabbar_background_image_maxsize->value();
  theme.image_stretch = ui_->tabbar_background_image_stretch->isChecked();
  theme.image_keep_aspect_ratio = ui_->tabbar_background_image_keep_aspect_ratio->isChecked();
  theme.image_do_not_cut = ui_->tabbar_background_image_do_not_cut->isChecked();
  theme.image_tile = ui_->tabbar_background_image_tile->isChecked();
  theme.image_blur_radius = ui_->tabbar_background_image_blur->value();
  theme.image_opacity = ui_->tabbar_background_image_opacity->value();
  theme.image_tint = ui_->tabbar_background_image_tint->isChecked();
  // A copy of the image, so the theme keeps working when the original is moved.
  if (theme.background == static_cast<int>(TabBarBackground::CustomImage)) {
    theme.image = TabBarThemes::CopyImage(name, ui_->tabbar_background_image->text());
  }

  const qsizetype existing = TabBarThemes::IndexOf(tabbar_themes_, name);
  if (existing >= 0) {
    if (tabbar_themes_[existing].image != theme.image) TabBarThemes::RemoveImage(tabbar_themes_[existing].image);
    tabbar_themes_[existing] = theme;
  }
  else {
    tabbar_themes_ << theme;
  }
  Q_UNUSED(overwrite);
  TabBarThemes::Save(tabbar_themes_);

  UpdateTabBarThemeItems();
  ui_->tabbar_background->setCurrentIndex(ui_->tabbar_background->findData(name, kTabBarThemeNameRole, Qt::MatchFixedString));
  TabBarThemeChosen();

}

void AppearanceSettingsPage::TabBarThemeRename() {

  const int theme_index = CurrentTabBarTheme();
  if (theme_index < 0) return;

  const QString name = AskTabBarThemeName(tr("Rename sidebar theme"), tabbar_themes_[theme_index].name, theme_index, nullptr);
  if (name.isEmpty() || name == tabbar_themes_[theme_index].name) return;

  tabbar_themes_[theme_index].name = name;
  TabBarThemes::Save(tabbar_themes_);
  UpdateTabBarThemeItems();
  ui_->tabbar_background->setCurrentIndex(ui_->tabbar_background->findData(name, kTabBarThemeNameRole, Qt::MatchFixedString));
  set_changed();

}

void AppearanceSettingsPage::TabBarThemeDelete() {

  const int theme_index = CurrentTabBarTheme();
  if (theme_index < 0) return;

  const TabBarThemes::Theme theme = tabbar_themes_[theme_index];
  if (QMessageBox::question(this, tr("Delete sidebar theme"), tr("Delete the sidebar theme \"%1\"?").arg(theme.name), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;

  TabBarThemes::RemoveImage(theme.image);
  tabbar_themes_.removeAt(theme_index);
  TabBarThemes::Save(tabbar_themes_);

  // The look stays, as the background the theme was based on; a removed image copy is replaced by the plain gradient.
  ui_->tabbar_background->setCurrentIndex(ui_->tabbar_background->findData(theme.background == static_cast<int>(TabBarBackground::CustomImage) ? static_cast<int>(TabBarBackground::Gradient) : theme.background));
  if (theme.background == static_cast<int>(TabBarBackground::CustomImage)) ui_->tabbar_background_image->clear();
  UpdateTabBarThemeItems();
  set_changed();

}

void AppearanceSettingsPage::TabBarSelectGradientColor(const bool start) {

  QColor &color = start ? current_tabbar_gradient_start_color_ : current_tabbar_gradient_end_color_;
  const QColor color_selected = QColorDialog::getColor(color);
  if (!color_selected.isValid()) return;
  color = color_selected;
  UpdateColorSelectorColor(start ? ui_->select_tabbar_gradient_start_color : ui_->select_tabbar_gradient_end_color, color);
  UpdateTabBarBackgroundPreviews();

  set_changed();

}

void AppearanceSettingsPage::TabBarSelectBackgroundImage() {

  const QString selected_filename = QFileDialog::getOpenFileName(this, tr("Select tabbar background image"), ui_->tabbar_background_image->text(), tr(kLoadImageFileFilter) + u";;"_s + tr(kAllFilesFilterSpec));
  if (selected_filename.isEmpty()) return;
  ui_->tabbar_background_image->setText(selected_filename);
  UpdateTabBarBackgroundPreviews();

  set_changed();

}

void AppearanceSettingsPage::TabBarSelectBGColor() {

  if (ui_->tabbar_system_color->isChecked()) return;

  QColor color_selected = QColorDialog::getColor(current_tabbar_bg_color_);
  if (!color_selected.isValid()) return;
  current_tabbar_bg_color_ = color_selected;
  UpdateColorSelectorColor(ui_->select_tabbar_color, current_tabbar_bg_color_);
  UpdateTabBarBackgroundPreviews();

  set_changed();

}

void AppearanceSettingsPage::PlaylistPlayingSongColorSystem(const bool checked) {

  if (checked) {
    current_playlist_playing_song_color_ = StyleHelper::highlightColor();
    UpdateColorSelectorColor(ui_->select_playlist_playing_song_color, current_playlist_playing_song_color_);
  }
  ui_->layout_playlist_playing_song_color_custom->setEnabled(!checked);
  ui_->select_playlist_playing_song_color->setEnabled(!checked);

}

void AppearanceSettingsPage::PlaylistPlayingSongSelectColor() {

  if (ui_->playlist_playing_song_color_system->isChecked()) return;

  QColor color_selected = QColorDialog::getColor(current_playlist_playing_song_color_);
  if (!color_selected.isValid()) return;
  current_playlist_playing_song_color_ = color_selected;
  UpdateColorSelectorColor(ui_->select_playlist_playing_song_color, current_playlist_playing_song_color_);

  set_changed();

}

void AppearanceSettingsPage::UpdateIconSetItems(const QString &id) {

  const QSignalBlocker blocker(ui_->combobox_icon_set);
  ui_->combobox_icon_set->clear();
  icon_sets_ = IconLoader::IconSets();

  const QSize icon_size = ui_->combobox_icon_set->iconSize();
  const qreal device_pixel_ratio = devicePixelRatioF();
  const int pixmap_icon_size = qRound(kIconSetPreviewIconSize * device_pixel_ratio);

  int current_index = 0;
  for (const IconLoader::IconSet &icon_set : std::as_const(icon_sets_)) {
    QPixmap preview(icon_size * device_pixel_ratio);
    preview.setDevicePixelRatio(device_pixel_ratio);
    preview.fill(Qt::transparent);
    {
      QPainter p(&preview);
      p.setRenderHint(QPainter::SmoothPixmapTransform);
      int x = 0;
      for (const char16_t *name : kIconSetPreviewIcons) {
        const QPixmap icon = IconLoader::IconSetPixmap(icon_set, QString::fromUtf16(name), pixmap_icon_size);
        p.drawPixmap(QRect(x, 0, kIconSetPreviewIconSize, kIconSetPreviewIconSize), icon);
        x += kIconSetPreviewIconSize + kIconSetPreviewSpacing;
      }
    }
    QString text = icon_set.name;
    if (icon_set.type == IconLoader::IconSetType::UserTheme) {
      text = tr("%1 (your icon theme)").arg(icon_set.name);
    }
    else if (icon_set.type == IconLoader::IconSetType::SystemTheme) {
      text = tr("%1 (installed icon theme)").arg(icon_set.name);
    }
    ui_->combobox_icon_set->addItem(QIcon(preview), text, icon_set.id);
    if (icon_set.id == id) current_index = ui_->combobox_icon_set->count() - 1;
  }
  ui_->combobox_icon_set->setCurrentIndex(current_index);
  IconSetChanged();

}

IconLoader::IconSet AppearanceSettingsPage::SelectedIconSetEntry() const {

  const int index = ui_->combobox_icon_set->currentIndex();
  if (index >= 0 && index < icon_sets_.count()) return icon_sets_.at(index);
  return IconLoader::FindIconSet(IconLoader::CurrentIconSet());

}

QString AppearanceSettingsPage::SelectedIconSet() const {

  const QString id = ui_->combobox_icon_set->currentData().toString();
  return id.isEmpty() ? IconLoader::CurrentIconSet() : id;

}

void AppearanceSettingsPage::IconSetChanged() {

  const int index = ui_->combobox_icon_set->currentIndex();
  if (index < 0 || index >= icon_sets_.count()) {
    ui_->label_icon_set_info->clear();
    return;
  }

  const IconLoader::IconSet &icon_set = icon_sets_.at(index);
  QString info;
  switch (icon_set.type) {
    case IconLoader::IconSetType::BuiltIn:
      info = tr("Akousma's own icons.");
      break;
    case IconLoader::IconSetType::System:
      info = tr("The icon theme of your desktop, also when you change it there. Icons it doesn't have come from Akousma's own icons.");
      break;
    case IconLoader::IconSetType::SystemTheme:
      info = tr("An icon theme installed on this computer, in %1. Icons it doesn't have come from Akousma's own icons.").arg(QDir::toNativeSeparators(icon_set.path));
      break;
    case IconLoader::IconSetType::Bundled:
      info = tr("%1 of %2 icons from %3, the others are Akousma's own: logos and ratings always are. Licence: %4. Source: %5").arg(icon_set.icons).arg(IconLoader::ThemableIconCount()).arg(icon_set.name, icon_set.licence, icon_set.source);
      break;
    case IconLoader::IconSetType::UserTheme:
      info = tr("Your icon theme in %1. Icons it doesn't have come from Akousma's own icons.").arg(QDir::toNativeSeparators(icon_set.path));
      break;
  }
  if (icon_set.id != IconLoader::CurrentIconSet()) {
    info += u' ' + tr("Akousma restarts to use it.");
  }
  ui_->label_icon_set_info->setText(info);

  // The sidebar previews show the icons of the chosen set.
  UpdateTabBarBackgroundPreviews();

  UpdateAppIconControls();

}

void AppearanceSettingsPage::UpdateAppIconControls() {

  // The chosen icon set's pomegranate, if it has one. The style is used without one, so it can be chosen then.
  const QString bundled_id = AppIcon::IconSetWithIcon(SelectedIconSet());
  ui_->checkbox_app_icon_from_iconset->setIcon(bundled_id.isEmpty() ? QIcon() : AppIcon::IconSetIcon(bundled_id));
  ui_->combobox_app_icon->setEnabled(!ui_->checkbox_app_icon_from_iconset->isChecked() || bundled_id.isEmpty());

}

void AppearanceSettingsPage::OpenIconSetFolder() {

  const QString dir = IconLoader::UserIconsDir();
  if (!QDir().mkpath(dir)) {
    QMessageBox::warning(this, tr("Icon set"), tr("Could not create the folder %1.").arg(QDir::toNativeSeparators(dir)));
    return;
  }
  QMessageBox::information(this, tr("Icon set"), tr("Put an icon theme folder (a freedesktop icon theme with its index.theme file, PNG icons work everywhere, SVG icons need the Qt SVG plugin) into %1 and it is listed as an icon set the next time you open the settings.").arg(QDir::toNativeSeparators(dir)));
  QDesktopServices::openUrl(QUrl::fromLocalFile(dir));

}
