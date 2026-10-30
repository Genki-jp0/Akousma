/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2018, Vikram Ambrose <ambroseworks@gmail.com>
 * Copyright 2018-2024, Jonas Kvinge <jonas@jkvinge.net>
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

#ifndef FANCYTABWIDGET_H
#define FANCYTABWIDGET_H

#include <QTabWidget>
#include <QHash>
#include <QString>
#include <QIcon>
#include <QPixmap>
#include <QImage>
#include <QColor>

class QMenu;
class QActionGroup;
class QContextMenuEvent;
class QPaintEvent;
class FancyTabData;

class FancyTabWidget : public QTabWidget {
  Q_OBJECT

 public:
  explicit FancyTabWidget(QWidget *parent = nullptr);
  ~FancyTabWidget() override;

  enum class Mode {
    None = 0,
    LargeSidebar,
    SmallSidebar,
    Tabs,
    IconOnlyTabs,
    PlainSidebar,
    IconsSidebar,
  };

  static const char *kTabMode;
  static const char *kCurrentTab;

  Mode mode() const { return mode_; }
  int iconsize_smallsidebar() const { return iconsize_smallsidebar_; }
  int iconsize_largesidebar() const { return iconsize_largesidebar_; }

  void AddTab(QWidget *widget_view, const QString &name, const QIcon &icon, const QString &label);

  void LoadSettings(const QString &settings_group);
  void SaveSettings(const QString &settings_group);
  void ReloadSettings();

  int InsertTab(const int preffered_index, FancyTabData *tab);
  int InsertTab(const int idx, QWidget *page, const QIcon &icon, const QString &label);

  bool EnableTab(QWidget *widget_view);
  bool DisableTab(QWidget *widget_view);

  void AddSpacer();
  void AddBottomWidget(QWidget *widget_view);
  void SetBackgroundPixmap(const QPixmap &pixmap);
  // The cover of the playing song, used for the album cover sidebar background.
  void SetAlbumCoverImage(const QImage &image);
  int IndexOfTab(QWidget *widget);

  static QColor DefaultTabbarBgColor();

 public Q_SLOTS:
  void SetMode(const FancyTabWidget::Mode mode);
  void SetCurrentIndex(int idx);

 private Q_SLOTS:
  void TabBarUpdateGeometry();
  void CurrentTabChangedSlot(const int idx);

 protected:
  void paintEvent(QPaintEvent *e) override;
  void contextMenuEvent(QContextMenuEvent *e) override;

 private:
  void addMenuItem(QActionGroup *group, const QString &text, Mode mode);

 Q_SIGNALS:
  void ModeChanged(const FancyTabWidget::Mode mode);
  void CurrentTabChanged(const int idx);

 private:
  QPixmap background_pixmap_;
  QMenu *menu_;
  Mode mode_;
  QWidget *bottom_widget_;

  QHash<QWidget*, FancyTabData*> tabs_;

  bool bg_color_system_;
  bool bg_gradient_;
  QColor bg_color_;

  // Sidebar background, see AppearanceSettings::TabBarBackground. The strawberry graphics set with SetBackgroundPixmap() are only used for the default background.
  QPixmap sprite_pixmap_;
  int bg_mode_ = 0;
  bool bg_gradient_custom_colors_ = false;
  QColor bg_gradient_start_color_;
  QColor bg_gradient_end_color_;
  int bg_gradient_direction_ = 0;
  QString bg_image_filename_;
  QPixmap bg_image_;
  QImage bg_album_cover_;
  int bg_image_position_ = 3;
  int bg_image_max_size_ = 0;
  bool bg_image_stretch_ = true;
  bool bg_image_keep_aspect_ratio_ = true;
  bool bg_image_do_not_cut_ = false;
  bool bg_image_tile_ = false;
  int bg_image_blur_radius_ = 0;
  int bg_image_opacity_ = 100;
  bool bg_image_tint_ = false;

  void UpdateBackgroundImage();
  int iconsize_smallsidebar_;
  int iconsize_largesidebar_;
};

#endif  // FANCYTABWIDGET_H
