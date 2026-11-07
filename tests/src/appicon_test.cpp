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

#include "config.h"

#include "gtest_include.h"

#include <QCoreApplication>
#include <QApplication>
#include <QGuiApplication>
#include <QWidget>
#include <QWindow>
#include <QLabel>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QSize>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QStandardPaths>
#include <QDir>
#include <QFile>

#include "core/settings.h"
#include "core/iconloader.h"
#include "core/appicon.h"
#include "dialogs/aboutdialog.h"
#include "context/contextalbum.h"
#include "playlist/playlistview.h"

using namespace Qt::Literals::StringLiterals;

namespace {

QImage IconImage(const QIcon &icon, const int size) {
  return icon.pixmap(QSize(size, size)).toImage().convertToFormat(QImage::Format_ARGB32);
}

QImage WidgetImage(QWidget *widget) {
  return widget->grab().toImage().convertToFormat(QImage::Format_ARGB32);
}

// The bundled sets of the build, as in share/akousma/iconsets.
QStringList TestIconSets() {
  return QStringLiteral(AKOUSMA_TEST_ICONSETS).split(u',');
}

QString TestIconSetsDir() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/akousma/iconsets"_s;
}

void SetIconSetSettings(const QString &iconset, const bool from_iconset) {
  Settings s;
  s.setValue(u"Appearance/icon_set"_s, iconset);
  s.setValue(u"Appearance/app_icon_from_iconset"_s, from_iconset);
  s.sync();
}

bool IsGrey(const QImage &image) {
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const QRgb pixel = image.pixel(x, y);
      if (qAlpha(pixel) != 0 && (qRed(pixel) != qGreen(pixel) || qGreen(pixel) != qBlue(pixel))) return false;
    }
  }
  return true;
}

void SetStyleSetting(const QString &key) {
  Settings s;
  if (key.isEmpty()) {
    s.remove(u"Appearance/app_icon"_s);
  }
  else {
    s.setValue(u"Appearance/app_icon"_s, key);
  }
  s.sync();
}

class AppIconTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    QCoreApplication::setOrganizationName(u"Akousma"_s);
    QStandardPaths::setTestModeEnabled(true);
    const QString dir = TestIconSetsDir();
    QDir().mkpath(dir);
    const QStringList iconsets = TestIconSets();
    for (const QString &id : iconsets) {
      QFile::remove(dir + u'/' + id + u".rcc"_s);
      QFile::copy(QStringLiteral(AKOUSMA_TEST_ICONSETS_DIR) + u'/' + id + u".rcc"_s, dir + u'/' + id + u".rcc"_s);
    }
    IconLoader::Init();
  }
  static void TearDownTestSuite() {
    const QStringList iconsets = TestIconSets();
    for (const QString &id : iconsets) QFile::remove(TestIconSetsDir() + u'/' + id + u".rcc"_s);
  }
  void TearDown() override {
    SetStyleSetting(QString());
    {
      Settings s;
      s.remove(u"Appearance/icon_set"_s);
      s.remove(u"Appearance/app_icon_from_iconset"_s);
      s.sync();
    }
    AppIcon::ReloadSettings();
  }
};

}  // namespace

TEST_F(AppIconTest, Styles) {

  QStringList keys;
  const QList<AppIcon::Style> styles = AppIcon::Styles();
  for (const AppIcon::Style &style : styles) {
    keys << style.key;
    EXPECT_FALSE(style.name.isEmpty());
  }
  EXPECT_EQ(QStringList() << u"win11"_s << u"win10"_s << u"win81"_s << u"win8"_s << u"aero"_s, keys);
  EXPECT_EQ(QLatin1String(AppIcon::kDefaultStyle), keys.first());
  EXPECT_EQ(u"Fluent (Windows 11)"_s, styles.first().name);
  EXPECT_EQ(u"Aero (Windows Vista / 7)"_s, styles.last().name);

}

TEST_F(AppIconTest, UnknownStyleIsTheDefault) {

  EXPECT_EQ(u"aero"_s, AppIcon::ValidStyle(u"aero"_s));
  EXPECT_EQ(u"win81"_s, AppIcon::ValidStyle(u"win81"_s));
  EXPECT_EQ(u"win11"_s, AppIcon::ValidStyle(QString()));
  EXPECT_EQ(u"win11"_s, AppIcon::ValidStyle(u"winxp"_s));
  EXPECT_EQ(u"win11"_s, AppIcon::ValidStyle(u"Aero"_s));

  SetStyleSetting(QString());
  EXPECT_EQ(u"win11"_s, AppIcon::StyleFromSettings());
  SetStyleSetting(u"win8"_s);
  EXPECT_EQ(u"win8"_s, AppIcon::StyleFromSettings());
  SetStyleSetting(u"strawberry"_s);
  EXPECT_EQ(u"win11"_s, AppIcon::StyleFromSettings());

  // An unknown style draws the default icon.
  EXPECT_EQ(IconImage(AppIcon::StyleIcon(u"win11"_s), 32), IconImage(AppIcon::StyleIcon(u"strawberry"_s), 32));

}

// Each style has its own drawings, 16 to 256 px, from its .ico.
TEST_F(AppIconTest, StyleIcons) {

  const QList<QSize> sizes = QList<QSize>() << QSize(16, 16) << QSize(24, 24) << QSize(32, 32) << QSize(48, 48) << QSize(64, 64) << QSize(128, 128) << QSize(256, 256);
  QList<QImage> images;
  const QList<AppIcon::Style> styles = AppIcon::Styles();
  for (const AppIcon::Style &style : styles) {
    const QIcon icon = AppIcon::StyleIcon(style.key);
    ASSERT_FALSE(icon.isNull()) << style.key.toStdString();
    const QList<QSize> available_sizes = icon.availableSizes();
    for (const QSize &size : sizes) {
      EXPECT_TRUE(available_sizes.contains(size)) << style.key.toStdString() << " " << size.width();
    }
    const QImage image = IconImage(icon, 32);
    EXPECT_FALSE(images.contains(image)) << style.key.toStdString();
    images << image;
  }

}

// The grey icon is the colored one desaturated, with the same transparency.
TEST_F(AppIconTest, GreyIcon) {

  const QImage colored = IconImage(AppIcon::StyleIcon(u"win11"_s), 64);
  const QImage grey = IconImage(AppIcon::StyleIcon(u"win11"_s, true), 64);
  ASSERT_EQ(colored.size(), grey.size());
  bool has_color = false;
  for (int y = 0; y < grey.height(); ++y) {
    for (int x = 0; x < grey.width(); ++x) {
      const QRgb colored_pixel = colored.pixel(x, y);
      const QRgb grey_pixel = grey.pixel(x, y);
      EXPECT_EQ(qAlpha(colored_pixel), qAlpha(grey_pixel));
      if (qAlpha(grey_pixel) == 0) continue;
      EXPECT_EQ(qRed(grey_pixel), qGreen(grey_pixel));
      EXPECT_EQ(qGreen(grey_pixel), qBlue(grey_pixel));
      if (qRed(colored_pixel) != qGreen(colored_pixel)) has_color = true;
    }
  }
  EXPECT_TRUE(has_color);

  // Rec. 709 luma, like the grey icons rendered with ImageMagick before.
  QImage image(2, 1, QImage::Format_ARGB32);
  image.setPixel(0, 0, qRgba(226, 57, 80, 255));
  image.setPixel(1, 0, qRgba(204, 36, 71, 128));
  const QImage grey_image = AppIcon::Grey(image);
  EXPECT_EQ(qRgba(95, 95, 95, 255), grey_image.pixel(0, 0));
  EXPECT_EQ(qRgba(74, 74, 74, 128), grey_image.pixel(1, 0));

}

TEST_F(AppIconTest, Picture) {

  const QList<AppIcon::Style> styles = AppIcon::Styles();
  for (const AppIcon::Style &style : styles) {
    const QImage picture = AppIcon::StylePicture(style.key, 512);
    ASSERT_FALSE(picture.isNull()) << style.key.toStdString();
    EXPECT_EQ(QSize(512, 512), picture.size());
    // The same drawing as the 256 px icon.
    const QImage scaled = picture.scaled(256, 256, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
    const QImage icon = IconImage(AppIcon::StyleIcon(style.key), 256);
    ASSERT_EQ(scaled.size(), icon.size());
    qint64 difference = 0;
    for (int y = 0; y < icon.height(); ++y) {
      for (int x = 0; x < icon.width(); ++x) {
        const QRgb a = scaled.pixel(x, y);
        const QRgb b = icon.pixel(x, y);
        difference += qAbs(qRed(a) - qRed(b)) + qAbs(qGreen(a) - qGreen(b)) + qAbs(qBlue(a) - qBlue(b)) + qAbs(qAlpha(a) - qAlpha(b));
      }
    }
    EXPECT_LT(difference / (icon.width() * icon.height() * 4), 8) << style.key.toStdString();
  }
  // Up to 256 px the icon's own images.
  EXPECT_EQ(IconImage(AppIcon::StyleIcon(u"aero"_s), 32), AppIcon::StylePicture(u"aero"_s, 32));
  EXPECT_EQ(QSize(100, 100), AppIcon::StylePicture(u"aero"_s, 100).size());
  EXPECT_EQ(QSize(1024, 1024), AppIcon::StylePicture(u"aero"_s, 1024).size());
  const QImage grey = AppIcon::StylePicture(u"win10"_s, 128, true);
  const QRgb pixel = grey.pixel(64, 64);
  EXPECT_EQ(qRed(pixel), qBlue(pixel));
  EXPECT_TRUE(AppIcon::StylePicture(u"win10"_s, 0).isNull());

}

// Applying another style changes the application icon, the windows' icons and icons made before, and tells once.
TEST_F(AppIconTest, SwitchAtRuntime) {

  ASSERT_NE("wayland"_L1, QGuiApplication::platformName());

  SetStyleSetting(QString());
  AppIcon::ReloadSettings();
  ASSERT_EQ(u"win11"_s, AppIcon::CurrentStyle());
  QApplication::setWindowIcon(IconLoader::Load(u"akousma"_s));

  // A window without its own icon, and one given the application icon like MainWindow.
  QWidget plain_window;
  plain_window.show();
  QWidget window_with_icon;
  window_with_icon.setWindowIcon(IconLoader::Load(u"akousma"_s));
  window_with_icon.show();
  ASSERT_TRUE(plain_window.windowHandle());
  ASSERT_TRUE(window_with_icon.windowHandle());

  const QIcon icon_before = IconLoader::Load(u"akousma"_s);
  const QIcon grey_icon_before = IconLoader::Load(u"akousma-grey"_s);
  EXPECT_TRUE(AppIcon::IsAppIcon(icon_before));
  EXPECT_TRUE(AppIcon::IsAppIcon(grey_icon_before));
  EXPECT_FALSE(AppIcon::IsAppIcon(AppIcon::StyleIcon(u"win11"_s)));

  const QImage win11 = IconImage(AppIcon::StyleIcon(u"win11"_s), 32);
  const QImage aero = IconImage(AppIcon::StyleIcon(u"aero"_s), 32);
  ASSERT_NE(win11, aero);
  EXPECT_EQ(win11, IconImage(QApplication::windowIcon(), 32));
  EXPECT_EQ(win11, IconImage(plain_window.windowHandle()->icon(), 32));
  EXPECT_EQ(win11, IconImage(window_with_icon.windowHandle()->icon(), 32));

  int changed = 0;
  QObject::connect(AppIcon::Instance(), &AppIcon::Changed, AppIcon::Instance(), [&changed]() { ++changed; });

  SetStyleSetting(u"aero"_s);
  AppIcon::ReloadSettings();
  EXPECT_EQ(1, changed);
  EXPECT_EQ(u"aero"_s, AppIcon::CurrentStyle());
  EXPECT_EQ(aero, IconImage(QApplication::windowIcon(), 32));
  EXPECT_EQ(aero, IconImage(plain_window.windowIcon(), 32));
  EXPECT_EQ(aero, IconImage(plain_window.windowHandle()->icon(), 32));
  EXPECT_EQ(aero, IconImage(window_with_icon.windowHandle()->icon(), 32));
  EXPECT_EQ(aero, IconImage(icon_before, 32));
  EXPECT_EQ(IconImage(AppIcon::StyleIcon(u"aero"_s, true), 32), IconImage(grey_icon_before, 32));

  // The same style again: nothing to do.
  AppIcon::ReloadSettings();
  EXPECT_EQ(1, changed);

  // An unknown style in the settings: the default.
  SetStyleSetting(u"winxp"_s);
  AppIcon::ReloadSettings();
  EXPECT_EQ(2, changed);
  EXPECT_EQ(u"win11"_s, AppIcon::CurrentStyle());
  EXPECT_EQ(win11, IconImage(window_with_icon.windowHandle()->icon(), 32));

  QObject::disconnect(AppIcon::Instance(), &AppIcon::Changed, AppIcon::Instance(), nullptr);

}

// The About dialog, the context view's picture when nothing plays and the default playlist background follow the style.
TEST_F(AppIconTest, WidgetsFollow) {

  SetStyleSetting(QString());
  AppIcon::ReloadSettings();

  AboutDialog about_dialog;
  QLabel *label_icon = about_dialog.findChild<QLabel*>(u"label_icon"_s);
  ASSERT_TRUE(label_icon);
  const QImage about_before = label_icon->pixmap().toImage();
  EXPECT_FALSE(about_before.isNull());

  ContextAlbum context_album;
  context_album.resize(300, 300);
  const QImage context_before = WidgetImage(&context_album);

  // Not shown, it would need a playlist.
  PlaylistView playlist_view;
  playlist_view.resize(800, 700);
  const QImage playlist_before = WidgetImage(&playlist_view);

  SetStyleSetting(u"win10"_s);
  AppIcon::ReloadSettings();

  EXPECT_NE(about_before, label_icon->pixmap().toImage());
  EXPECT_EQ(IconImage(AppIcon::StyleIcon(u"win10"_s), 64), label_icon->pixmap().toImage().convertToFormat(QImage::Format_ARGB32));
  EXPECT_NE(context_before, WidgetImage(&context_album));
  EXPECT_NE(playlist_before, WidgetImage(&playlist_view));

  // Back to the first style, the same pictures as before (the context view's is scaled to its new size by then).
  SetStyleSetting(u"win11"_s);
  AppIcon::ReloadSettings();
  EXPECT_EQ(about_before, label_icon->pixmap().toImage());
  EXPECT_EQ(playlist_before, WidgetImage(&playlist_view));

}

// Each bundled set has the pomegranate in its own style, 16 to 256 px; the dark variants have the same as their light sets,
// like their other colored icons.
TEST_F(AppIconTest, IconSetIcons) {

  const QList<QSize> sizes = QList<QSize>() << QSize(16, 16) << QSize(22, 22) << QSize(24, 24) << QSize(32, 32) << QSize(48, 48) << QSize(64, 64) << QSize(128, 128) << QSize(256, 256);
  const QImage win11 = IconImage(AppIcon::StyleIcon(u"win11"_s), 32);
  QMap<QString, QImage> images;
  const QStringList iconsets = TestIconSets();
  ASSERT_EQ(8, iconsets.count());
  for (const QString &id : iconsets) {
    EXPECT_EQ(id, AppIcon::IconSetWithIcon(id));
    const QIcon icon = AppIcon::IconSetIcon(id);
    ASSERT_FALSE(icon.isNull()) << id.toStdString();
    const QList<QSize> available_sizes = icon.availableSizes();
    for (const QSize &size : sizes) {
      EXPECT_TRUE(available_sizes.contains(size)) << id.toStdString() << " " << size.width();
    }
    const QImage image = IconImage(icon, 32);
    EXPECT_NE(win11, image) << id.toStdString();
    images.insert(id, image);
    EXPECT_TRUE(IsGrey(IconImage(AppIcon::IconSetIcon(id, true), 32))) << id.toStdString();
    const QImage picture = AppIcon::IconSetPicture(id, 512);
    EXPECT_EQ(QSize(512, 512), picture.size()) << id.toStdString();
    EXPECT_EQ(QSize(100, 100), AppIcon::IconSetPicture(id, 100).size()) << id.toStdString();
  }
  EXPECT_EQ(images.value(u"breeze"_s), images.value(u"breeze-dark"_s));
  EXPECT_EQ(images.value(u"papirus"_s), images.value(u"papirus-dark"_s));
  const QStringList distinct = QStringList() << u"adwaita"_s << u"breeze"_s << u"elementary"_s << u"oxygen"_s << u"papirus"_s << u"tango"_s;
  for (const QString &a : distinct) {
    for (const QString &b : distinct) {
      if (a != b) {
        EXPECT_NE(images.value(a), images.value(b)) << a.toStdString() << " " << b.toStdString();
      }
    }
  }

  // No pomegranate: the built-in icons, an unknown set.
  EXPECT_TRUE(AppIcon::IconSetWithIcon(u"akousma"_s).isEmpty());
  EXPECT_TRUE(AppIcon::IconSetWithIcon(u"clementine"_s).isEmpty());
  EXPECT_TRUE(AppIcon::IconSetIcon(u"clementine"_s).isNull());
  EXPECT_TRUE(AppIcon::IconSetPicture(u"breeze"_s, 0).isNull());

}

// An installed icon theme or one of the user's own gets the pomegranate of the bundled set made from that theme.
TEST_F(AppIconTest, IconThemes) {

  EXPECT_EQ(u"papirus"_s, IconLoader::BundledIconSetForTheme(u"Papirus"_s));
  EXPECT_EQ(u"papirus-dark"_s, IconLoader::BundledIconSetForTheme(u"papirus-dark"_s));
  EXPECT_TRUE(IconLoader::BundledIconSetForTheme(u"Papirus-Light"_s).isEmpty());
  EXPECT_EQ(u"breeze"_s, AppIcon::IconSetWithIcon(u"systemtheme:breeze"_s));
  EXPECT_EQ(u"breeze-dark"_s, AppIcon::IconSetWithIcon(u"systemtheme:breeze-dark"_s));
  EXPECT_EQ(u"adwaita"_s, AppIcon::IconSetWithIcon(u"systemtheme:Adwaita"_s));
  EXPECT_EQ(u"tango"_s, AppIcon::IconSetWithIcon(u"theme:Tango"_s));
  EXPECT_TRUE(AppIcon::IconSetWithIcon(u"theme:MyIcons"_s).isEmpty());
  EXPECT_TRUE(AppIcon::IconSetWithIcon(u"systemtheme:hicolor"_s).isEmpty());

}

// "Use the icon set's Akousma icon" is off by default; on, the application icon follows the icon set right away, and is the chosen
// style when the set has no pomegranate.
TEST_F(AppIconTest, FromIconSet) {

  ASSERT_NE("wayland"_L1, QGuiApplication::platformName());

  EXPECT_FALSE(AppIcon::FromIconSetFromSettings());
  SetIconSetSettings(u"breeze"_s, false);
  AppIcon::ReloadSettings();
  EXPECT_TRUE(AppIcon::CurrentIconSet().isEmpty());

  QApplication::setWindowIcon(IconLoader::Load(u"akousma"_s));
  QWidget window;
  window.setWindowIcon(IconLoader::Load(u"akousma"_s));
  window.show();
  ASSERT_TRUE(window.windowHandle());
  AboutDialog about_dialog;
  QLabel *label_icon = about_dialog.findChild<QLabel*>(u"label_icon"_s);
  ASSERT_TRUE(label_icon);
  const QIcon icon_before = IconLoader::Load(u"akousma"_s);
  const QIcon grey_icon_before = IconLoader::Load(u"akousma-grey"_s);

  const QImage win11 = IconImage(AppIcon::StyleIcon(u"win11"_s), 32);
  const QImage aero = IconImage(AppIcon::StyleIcon(u"aero"_s), 32);
  const QImage breeze = IconImage(AppIcon::IconSetIcon(u"breeze"_s), 32);
  const QImage oxygen = IconImage(AppIcon::IconSetIcon(u"oxygen"_s), 32);
  EXPECT_EQ(win11, IconImage(window.windowHandle()->icon(), 32));

  int changed = 0;
  QObject::connect(AppIcon::Instance(), &AppIcon::Changed, AppIcon::Instance(), [&changed]() { ++changed; });

  SetIconSetSettings(u"breeze"_s, true);
  AppIcon::ReloadSettings();
  EXPECT_EQ(1, changed);
  EXPECT_EQ(u"breeze"_s, AppIcon::CurrentIconSet());
  EXPECT_EQ(u"win11"_s, AppIcon::CurrentStyle());
  EXPECT_EQ(breeze, IconImage(QApplication::windowIcon(), 32));
  EXPECT_EQ(breeze, IconImage(window.windowHandle()->icon(), 32));
  EXPECT_EQ(breeze, IconImage(icon_before, 32));
  EXPECT_EQ(IconImage(AppIcon::IconSetIcon(u"breeze"_s, true), 32), IconImage(grey_icon_before, 32));
  EXPECT_EQ(IconImage(AppIcon::IconSetIcon(u"breeze"_s), 64), label_icon->pixmap().toImage().convertToFormat(QImage::Format_ARGB32));
  EXPECT_EQ(AppIcon::IconSetPicture(u"breeze"_s, 512), AppIcon::Picture(512));

  // Another icon set: its pomegranate right away, before the restart that brings its other icons.
  SetIconSetSettings(u"oxygen"_s, true);
  AppIcon::ReloadSettings();
  EXPECT_EQ(2, changed);
  EXPECT_EQ(oxygen, IconImage(window.windowHandle()->icon(), 32));

  // A set without a pomegranate: the chosen style.
  SetStyleSetting(u"aero"_s);
  SetIconSetSettings(u"akousma"_s, true);
  AppIcon::ReloadSettings();
  EXPECT_EQ(3, changed);
  EXPECT_TRUE(AppIcon::CurrentIconSet().isEmpty());
  EXPECT_EQ(aero, IconImage(window.windowHandle()->icon(), 32));

  // Off again: the style, whatever the set.
  SetIconSetSettings(u"breeze"_s, true);
  AppIcon::ReloadSettings();
  EXPECT_EQ(breeze, IconImage(window.windowHandle()->icon(), 32));
  SetIconSetSettings(u"breeze"_s, false);
  AppIcon::ReloadSettings();
  EXPECT_EQ(5, changed);
  EXPECT_EQ(aero, IconImage(window.windowHandle()->icon(), 32));
  // Nothing changed: nothing to do.
  AppIcon::ReloadSettings();
  EXPECT_EQ(5, changed);

  QObject::disconnect(AppIcon::Instance(), &AppIcon::Changed, AppIcon::Instance(), nullptr);

}
