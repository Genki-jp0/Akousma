/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2018, Vikram Ambrose <ambroseworks@gmail.com>
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

#include <utility>
#include <chrono>

#include <QtGlobal>
#include <QObject>
#include <QWidget>
#include <QTimer>
#include <QVariant>
#include <QString>
#include <QIcon>
#include <QPainter>
#include <QStylePainter>
#include <QColor>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QPixmap>
#include <QPixmapCache>
#include <QLayout>
#include <QContextMenuEvent>
#include <QPaintEvent>

#include "fancytabwidget.h"
#include "fancytabbar.h"
#include "fancytabdata.h"
#include "utilities/colorutils.h"
#include "core/stylehelper.h"
#include "core/settings.h"
#include "constants/appearancesettings.h"

using namespace std::chrono_literals;
using namespace Qt::Literals::StringLiterals;

namespace {
constexpr int IconSize_LargeSidebar = 40;
constexpr int IconSize_SmallSidebar = 32;
}  // namespace

const char *FancyTabWidget::kTabMode = "tab_mode";
const char *FancyTabWidget::kCurrentTab = "current_tab";

FancyTabWidget::FancyTabWidget(QWidget *parent)
    : QTabWidget(parent),
      menu_(nullptr),
      mode_(Mode::None),
      bottom_widget_(nullptr),
      bg_color_system_(true),
      bg_gradient_(true),
      iconsize_smallsidebar_(IconSize_SmallSidebar),
      iconsize_largesidebar_(IconSize_LargeSidebar) {

  FancyTabBar *tabBar = new FancyTabBar(this);
  setTabBar(tabBar);
  setTabPosition(QTabWidget::West);
  setMovable(true);
  setElideMode(Qt::ElideNone);
  setUsesScrollButtons(true);

  QObject::connect(tabBar, &FancyTabBar::currentChanged, this, &FancyTabWidget::CurrentTabChangedSlot);

}

FancyTabWidget::~FancyTabWidget() = default;

void FancyTabWidget::AddTab(QWidget *widget_view, const QString &name, const QIcon &icon, const QString &label) {

  FancyTabData *tab = new FancyTabData(widget_view, name, icon, label, static_cast<int>(tabs_.count()), this);
  tabs_.insert(widget_view, tab);

}

void FancyTabWidget::LoadSettings(const QString &settings_group) {

  Settings s;
  s.beginGroup(settings_group);
  QMultiMap<int, FancyTabData*> tabs;
  for (FancyTabData *tab : std::as_const(tabs_)) {
    int idx = s.value(u"tab_"_s + tab->name(), tab->index()).toInt();
    while (tabs.contains(idx)) { ++idx; }
    tabs.insert(idx, tab);
  }
  s.endGroup();

  for (QMultiMap<int, FancyTabData*>::iterator it = tabs.begin(); it != tabs.end(); ++it) {
    (void)InsertTab(it.key(), it.value());
  }

}

void FancyTabWidget::SaveSettings(const QString &settings_group) {

  Settings s;
  s.beginGroup(settings_group);

  if (mode_ != Mode::None) {
    s.setValue(kTabMode, static_cast<int>(mode_));
  }
  s.setValue(kCurrentTab, currentIndex());

  for (FancyTabData *tab : std::as_const(tabs_)) {
    QString k = u"tab_"_s + tab->name();
    int idx = QTabWidget::indexOf(tab->page());
    if (idx < 0) {
      if (s.contains(k)) s.remove(k);
    }
    else {
      s.setValue(k, idx);
    }
  }

  s.endGroup();

}

void FancyTabWidget::ReloadSettings() {

  Settings s;
  s.beginGroup(AppearanceSettings::kSettingsGroup);
  bg_color_system_ = s.value(AppearanceSettings::kTabBarSystemColor, AppearanceSettings::kDefaultTabBarSystemColor).toBool();
  bg_gradient_ = s.value(AppearanceSettings::kTabBarGradient, AppearanceSettings::kDefaultTabBarGradient).toBool();
  bg_color_ = DefaultTabbarBgColor();
  if (!bg_color_system_) {
    bg_color_ = s.value(AppearanceSettings::kTabBarColor, bg_color_).value<QColor>();
  }
  iconsize_smallsidebar_ = s.value(AppearanceSettings::kIconSizeTabbarSmallMode, IconSize_SmallSidebar).toInt();
  iconsize_largesidebar_ = s.value(AppearanceSettings::kIconSizeTabbarLargeMode, IconSize_LargeSidebar).toInt();
  bg_mode_ = s.value(AppearanceSettings::kTabBarBackground, static_cast<int>(AppearanceSettings::kDefaultTabBarBackground)).toInt();
  bg_gradient_custom_colors_ = s.value(AppearanceSettings::kTabBarGradientCustomColors, AppearanceSettings::kDefaultTabBarGradientCustomColors).toBool();
  bg_gradient_start_color_ = s.value(AppearanceSettings::kTabBarGradientStartColor, bg_color_.lighter(117)).value<QColor>();
  bg_gradient_end_color_ = s.value(AppearanceSettings::kTabBarGradientEndColor, StyleHelper::shadowColor(false).darker(109)).value<QColor>();
  bg_gradient_direction_ = s.value(AppearanceSettings::kTabBarGradientDirection, static_cast<int>(AppearanceSettings::kDefaultTabBarGradientDirection)).toInt();
  bg_image_filename_ = s.value(AppearanceSettings::kTabBarImage).toString();
  bg_image_position_ = s.value(AppearanceSettings::kTabBarImagePosition, AppearanceSettings::kDefaultTabBarImagePosition).toInt();
  bg_image_max_size_ = s.value(AppearanceSettings::kTabBarImageMaxSize, AppearanceSettings::kDefaultTabBarImageMaxSize).toInt();
  bg_image_stretch_ = s.value(AppearanceSettings::kTabBarImageStretch, AppearanceSettings::kDefaultTabBarImageStretch).toBool();
  bg_image_keep_aspect_ratio_ = s.value(AppearanceSettings::kTabBarImageKeepAspectRatio, AppearanceSettings::kDefaultTabBarImageKeepAspectRatio).toBool();
  bg_image_do_not_cut_ = s.value(AppearanceSettings::kTabBarImageDoNotCut, AppearanceSettings::kDefaultTabBarImageDoNotCut).toBool();
  bg_image_tile_ = s.value(AppearanceSettings::kTabBarImageTile, AppearanceSettings::kDefaultTabBarImageTile).toBool();
  bg_image_blur_radius_ = qBound(0, s.value(AppearanceSettings::kTabBarImageBlurRadius, AppearanceSettings::kDefaultTabBarImageBlurRadius).toInt(), 100);
  bg_image_opacity_ = qBound(0, s.value(AppearanceSettings::kTabBarImageOpacity, AppearanceSettings::kDefaultTabBarImageOpacity).toInt(), 100);
  bg_image_tint_ = s.value(AppearanceSettings::kTabBarImageTint, AppearanceSettings::kDefaultTabBarImageTint).toBool();
  s.endGroup();

  background_pixmap_ = bg_mode_ == static_cast<int>(AppearanceSettings::TabBarBackground::Strawberry) ? sprite_pixmap_ : QPixmap();
  UpdateBackgroundImage();

#ifndef Q_OS_MACOS
  if (mode() == Mode::LargeSidebar) {
    setIconSize(QSize(iconsize_largesidebar_, iconsize_largesidebar_));
  }
  else {
    setIconSize(QSize(iconsize_smallsidebar_, iconsize_smallsidebar_));
  }
#endif

  update();
  TabBarUpdateGeometry();

}

void FancyTabWidget::SetMode(const Mode mode) {

  const Mode previous_mode = mode_;

  mode_ = mode;

  if (mode == Mode::Tabs || mode == Mode::IconOnlyTabs) {
    setTabPosition(QTabWidget::North);
  }
  else {
    setTabPosition(QTabWidget::West);
  }

#ifndef Q_OS_MACOS
  if (mode_ == Mode::LargeSidebar || mode_ == Mode::IconsSidebar) {
    setIconSize(QSize(iconsize_largesidebar_, iconsize_largesidebar_));
  }
  else {
    setIconSize(QSize(iconsize_smallsidebar_, iconsize_smallsidebar_));
  }
#endif

  if ((previous_mode == Mode::IconOnlyTabs || previous_mode == Mode::IconsSidebar) && (mode != Mode::IconOnlyTabs && mode != Mode::IconsSidebar)) {
    for (int i = 0; i < count(); ++i) {
      tabBar()->setTabText(i, tabBar()->tabData(i).value<FancyTabData*>()->label());
      tabBar()->setTabToolTip(i, ""_L1);
    }
  }
  else if ((previous_mode != Mode::IconOnlyTabs && previous_mode != Mode::IconsSidebar) && (mode == Mode::IconOnlyTabs || mode == Mode::IconsSidebar)) {
    for (int i = 0; i < count(); ++i) {
      tabBar()->setTabText(i, ""_L1);
      tabBar()->setTabToolTip(i, tabBar()->tabData(i).value<FancyTabData*>()->label());
    }
  }

  tabBar()->updateGeometry();
  updateGeometry();

  // There appears to be a bug in QTabBar which causes tabSizeHint to be ignored thus the need for this second shot repaint
  QTimer::singleShot(1ms, this, &FancyTabWidget::TabBarUpdateGeometry);

  Q_EMIT ModeChanged(mode);

}

int FancyTabWidget::InsertTab(const int preffered_index, FancyTabData *tab) {

  const int actual_index = InsertTab(preffered_index, tab->page(), tab->icon(), QString());
  tabBar()->setTabData(actual_index, QVariant::fromValue<FancyTabData*>(tab));

  if (mode_ == Mode::IconOnlyTabs || mode_ == Mode::IconsSidebar) {
    tabBar()->setTabText(actual_index, ""_L1);
    tabBar()->setTabToolTip(actual_index, tab->label());
  }
  else {
    tabBar()->setTabText(actual_index, tab->label());
    tabBar()->setTabToolTip(actual_index, ""_L1);
  }

  return actual_index;

}

int FancyTabWidget::InsertTab(const int idx, QWidget *page, const QIcon &icon, const QString &label) {
  return QTabWidget::insertTab(idx, page, icon, label);
}

bool FancyTabWidget::EnableTab(QWidget *widget_view) {

  if (!tabs_.contains(widget_view)) return false;
  FancyTabData *tab = tabs_.value(widget_view);

  if (QTabWidget::indexOf(tab->page()) >= 0) return true;

  (void)InsertTab(count(), tab);

  return true;

}

bool FancyTabWidget::DisableTab(QWidget *widget_view) {

  if (!tabs_.contains(widget_view)) return false;
  FancyTabData *tab = tabs_.value(widget_view);

  int idx = QTabWidget::indexOf(tab->page());
  if (idx < 0) return false;

  removeTab(idx);

  return true;

}

void FancyTabWidget::AddSpacer() {

  QWidget *spacer = new QWidget(this);
  const int idx = insertTab(count(), spacer, QIcon(), QString());
  setTabEnabled(idx, false);

}

void FancyTabWidget::AddBottomWidget(QWidget *widget_view) {
  bottom_widget_ = widget_view;
}

void FancyTabWidget::SetAlbumCoverImage(const QImage &image) {

  bg_album_cover_ = image;
  if (bg_mode_ == static_cast<int>(AppearanceSettings::TabBarBackground::AlbumCover)) {
    UpdateBackgroundImage();
    update();
  }

}

// Loads the custom image or takes the album cover, and blurs it like the playlist background image, the opacity is applied when drawing.
void FancyTabWidget::UpdateBackgroundImage() {

  QImage image;
  if (bg_mode_ == static_cast<int>(AppearanceSettings::TabBarBackground::CustomImage) && !bg_image_filename_.isEmpty()) {
    image = QImage(bg_image_filename_);
  }
  else if (bg_mode_ == static_cast<int>(AppearanceSettings::TabBarBackground::AlbumCover)) {
    image = bg_album_cover_;
  }

  if (!image.isNull() && bg_image_blur_radius_ > 0) {
    QImage source = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage blurred(source.size(), QImage::Format_ARGB32_Premultiplied);
    blurred.fill(Qt::transparent);
    QPainter blur_painter(&blurred);
    qt_blurImage(&blur_painter, source, bg_image_blur_radius_, true, false);
    blur_painter.end();
    image = blurred;
  }

  bg_image_ = image.isNull() ? QPixmap() : QPixmap::fromImage(image);

}

void FancyTabWidget::SetBackgroundPixmap(const QPixmap &pixmap) {

  sprite_pixmap_ = pixmap;
  background_pixmap_ = bg_mode_ == static_cast<int>(AppearanceSettings::TabBarBackground::Strawberry) ? sprite_pixmap_ : QPixmap();
  update();

}

void FancyTabWidget::SetBackdropAlpha(const int alpha) {

  const int backdrop_alpha = qBound(0, alpha, 255);
  if (backdrop_alpha == backdrop_alpha_) return;
  backdrop_alpha_ = backdrop_alpha;
  update();

}

void FancyTabWidget::SetCurrentIndex(int idx) {

  Q_ASSERT(count() > 0);

  if (idx >= count() || idx < 0) idx = 0;

  QWidget *currentPage = widget(idx);
  QLayout *layout = currentPage ? currentPage->layout() : nullptr;
  if (bottom_widget_ && layout) layout->addWidget(bottom_widget_);
  QTabWidget::setCurrentIndex(idx);

}

void FancyTabWidget::CurrentTabChangedSlot(const int idx) {

  QWidget *currentPage = currentWidget();
  QLayout *layout = currentPage ? currentPage->layout() : nullptr;
  if (bottom_widget_ && layout) layout->addWidget(bottom_widget_);

  Q_EMIT CurrentTabChanged(idx);

}

int FancyTabWidget::IndexOfTab(QWidget *widget) {

  if (!tabs_.contains(widget)) return -1;
  QWidget *page = tabs_.value(widget)->page();
  return QTabWidget::indexOf(page);

}

void FancyTabWidget::paintEvent(QPaintEvent *pe) {

  if (mode() != Mode::LargeSidebar && mode() != Mode::SmallSidebar && mode() != Mode::IconsSidebar) {
    QTabWidget::paintEvent(pe);
    return;
  }

  QStylePainter painter(this);
  QRect backgroundRect = rect();
  backgroundRect.setWidth(tabBar()->width());

  QString key = QString::asprintf("mh_vertical %d %d %d %d %d", backgroundRect.width(), backgroundRect.height(), bg_color_.rgb(), (bg_gradient_ ? 1 : 0), (background_pixmap_.isNull() ? 0 : 1));
  key += QString::asprintf(" %d %d %u %u %d %lld %d %d %d %d %d %d %d %d", bg_mode_, (bg_gradient_custom_colors_ ? 1 : 0), bg_gradient_start_color_.rgba(), bg_gradient_end_color_.rgba(), bg_gradient_direction_, static_cast<long long>(bg_image_.cacheKey()), bg_image_position_, bg_image_max_size_, (bg_image_stretch_ ? 1 : 0), (bg_image_keep_aspect_ratio_ ? 1 : 0), (bg_image_do_not_cut_ ? 1 : 0), (bg_image_tile_ ? 1 : 0), bg_image_opacity_, (bg_image_tint_ ? 1 : 0));
  if (backdrop_alpha_ < 255) key += QString::asprintf(" backdrop %d", backdrop_alpha_);
  // The plain gradient background is always a gradient.
  const bool draw_gradient = bg_gradient_ || bg_mode_ == static_cast<int>(AppearanceSettings::TabBarBackground::Gradient);

  QPixmap pixmap;
  if (!QPixmapCache::find(key, &pixmap)) {

    pixmap = QPixmap(backgroundRect.size());
    // With a window backdrop the default color and gradient are translucent, a custom gradient and images are drawn as they are.
    const bool translucent = backdrop_alpha_ < 255;
    if (translucent) pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    if (!translucent) {
      p.fillRect(backgroundRect, bg_color_);
    }
    else if (!draw_gradient) {
      QColor color(bg_color_);
      color.setAlpha(color.alpha() * backdrop_alpha_ / 255);
      p.fillRect(backgroundRect, color);
    }

    // Draw the gradient fill.
    if (draw_gradient) {

      QRect rect(0, 0, backgroundRect.width(), backgroundRect.height());

      QColor shadow = StyleHelper::shadowColor(false);
      QLinearGradient grad(backgroundRect.topRight(), backgroundRect.topLeft());
      switch (static_cast<AppearanceSettings::TabBarGradientDirection>(bg_gradient_direction_)) {
        case AppearanceSettings::TabBarGradientDirection::Horizontal:
          break;
        case AppearanceSettings::TabBarGradientDirection::Vertical:
          grad = QLinearGradient(backgroundRect.topLeft(), backgroundRect.bottomLeft());
          break;
        case AppearanceSettings::TabBarGradientDirection::Diagonal:
          grad = QLinearGradient(backgroundRect.topRight(), backgroundRect.bottomLeft());
          break;
      }
      QColor gradient_start = bg_gradient_custom_colors_ ? bg_gradient_start_color_ : bg_color_.lighter(117);
      QColor gradient_end = bg_gradient_custom_colors_ ? bg_gradient_end_color_ : shadow.darker(109);
      if (translucent && !bg_gradient_custom_colors_) {
        gradient_start.setAlpha(gradient_start.alpha() * backdrop_alpha_ / 255);
        gradient_end.setAlpha(gradient_end.alpha() * backdrop_alpha_ / 255);
      }
      grad.setColorAt(0, gradient_start);
      grad.setColorAt(1, gradient_end);
      p.fillRect(rect, grad);

      QColor light(255, 255, 255, 80);
      p.setPen(light);
      p.drawLine(rect.topRight() - QPoint(1, 0), rect.bottomRight() - QPoint(1, 0));
      QColor dark(0, 0, 0, 90);
      p.setPen(dark);
      p.drawLine(rect.topLeft(), rect.bottomLeft());

    }

    // Draw the translucent png graphics over the gradient fill
    // Repeat it horizontally too, the sidebar with labels is wider than the graphics.
    if (!background_pixmap_.isNull()) {
      QRect pixmap_rect(background_pixmap_.rect());
      pixmap_rect.moveTo(backgroundRect.topLeft());

      while (pixmap_rect.top() < backgroundRect.bottom()) {
        pixmap_rect.moveLeft(backgroundRect.left());
        while (pixmap_rect.left() < backgroundRect.right()) {
          QRect source_rect(pixmap_rect.intersected(backgroundRect));
          source_rect.moveTo(0, 0);
          p.drawPixmap(pixmap_rect.topLeft(), background_pixmap_, source_rect);
          pixmap_rect.moveLeft(pixmap_rect.right() + 1);
        }
        pixmap_rect.moveTop(pixmap_rect.bottom() - 10);
      }
    }

    // Draw the custom image or the album cover over the color or gradient, with the same options as the playlist background image.
    if ((bg_mode_ == static_cast<int>(AppearanceSettings::TabBarBackground::CustomImage) || bg_mode_ == static_cast<int>(AppearanceSettings::TabBarBackground::AlbumCover)) && !bg_image_.isNull()) {
      const QRect image_rect(0, 0, backgroundRect.width(), backgroundRect.height());
      QPixmap image = bg_image_;
      if (!bg_image_stretch_ && bg_image_max_size_ > 0 && (image.width() > bg_image_max_size_ || image.height() > bg_image_max_size_)) {
        image = image.scaled(bg_image_max_size_, bg_image_max_size_, Qt::KeepAspectRatio, Qt::SmoothTransformation);
      }
      p.save();
      p.setClipRect(image_rect);
      p.setOpacity(static_cast<qreal>(bg_image_opacity_) / 100.0);
      if (bg_image_tile_) {
        p.drawTiledPixmap(image_rect, image);
      }
      else {
        if (bg_image_stretch_) {
          if (!bg_image_keep_aspect_ratio_) {
            image = image.scaled(image_rect.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
          }
          else {
            image = image.scaled(image_rect.size(), bg_image_do_not_cut_ ? Qt::KeepAspectRatio : Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
          }
        }
        else if (image.width() > image_rect.width() || image.height() > image_rect.height()) {
          image = image.scaled(image_rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        int x = 0;
        int y = 0;
        switch (static_cast<AppearanceSettings::BackgroundImagePosition>(bg_image_position_)) {
          case AppearanceSettings::BackgroundImagePosition::UpperLeft:
            break;
          case AppearanceSettings::BackgroundImagePosition::UpperRight:
            x = image_rect.width() - image.width();
            break;
          case AppearanceSettings::BackgroundImagePosition::BottomLeft:
            y = image_rect.height() - image.height();
            break;
          case AppearanceSettings::BackgroundImagePosition::BottomRight:
            x = image_rect.width() - image.width();
            y = image_rect.height() - image.height();
            break;
          case AppearanceSettings::BackgroundImagePosition::Middle:
          default:
            x = (image_rect.width() - image.width()) / 2;
            y = (image_rect.height() - image.height()) / 2;
            break;
        }
        p.drawPixmap(x, y, image);
      }
      if (bg_image_tint_) {
        // Tint the image with the tabbar color.
        p.setOpacity(1.0);
        QColor tint(bg_color_);
        tint.setAlpha(110);
        p.fillRect(image_rect, tint);
      }
      p.restore();
    }

    // Shadow effect of the background
    QColor light(255, 255, 255, 80);
    p.setPen(light);
    p.drawLine(backgroundRect.topRight() - QPoint(1, 0), backgroundRect.bottomRight() - QPoint(1, 0));
    QColor dark(0, 0, 0, 90);
    p.setPen(dark);
    p.drawLine(backgroundRect.topLeft(), backgroundRect.bottomLeft());

    p.setPen(StyleHelper::borderColor());
    p.drawLine(backgroundRect.topRight(), backgroundRect.bottomRight());

    p.end();

    QPixmapCache::insert(key, pixmap);

  }

  painter.drawPixmap(backgroundRect.topLeft(), pixmap);

}

void FancyTabWidget::TabBarUpdateGeometry() {
  tabBar()->updateGeometry();
}

void FancyTabWidget::addMenuItem(QActionGroup *group, const QString &text, Mode mode) {

  QAction *action = group->addAction(text);
  action->setCheckable(true);
  QObject::connect(action, &QAction::triggered, this, [this, mode]() { SetMode(mode); });

  if (mode == mode_) action->setChecked(true);

}

void FancyTabWidget::contextMenuEvent(QContextMenuEvent *e) {

  if (!QRect(mapToGlobal(pos()), tabBar()->size()).contains(e->globalPos())) {
    QTabWidget::contextMenuEvent(e);
    return;
  }

  if (!menu_) {
    menu_ = new QMenu(this);
    QActionGroup *group = new QActionGroup(this);
    addMenuItem(group, tr("Large sidebar"), Mode::LargeSidebar);
    addMenuItem(group, tr("Icons sidebar"), Mode::IconsSidebar);
    addMenuItem(group, tr("Small sidebar"), Mode::SmallSidebar);
    addMenuItem(group, tr("Plain sidebar"), Mode::PlainSidebar);
    addMenuItem(group, tr("Tabs on top"), Mode::Tabs);
    addMenuItem(group, tr("Icons on top"), Mode::IconOnlyTabs);
    menu_->addActions(group->actions());
  }

  menu_->popup(e->globalPos());

}

QColor FancyTabWidget::DefaultTabbarBgColor() {

  QColor color = StyleHelper::highlightColor();
  if (Utilities::IsColorDark(color)) {
    color = color.lighter(130);
  }
  return color;

}
