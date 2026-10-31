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

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QColor>
#include <QRect>
#include <QSize>
#include <QSlider>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleOptionSlider>

#include "moodbar/moodbarproxystyle.h"

// Regression test: the rendered moodbar kept the colors of the palette it was first drawn with, so after the Windows window
// backdrop was turned off (translucent Window color -> opaque one) the margin around it was drawn with the old color.
TEST(MoodbarProxyStyleTest, PaletteChangeRendersNewPixmap) {

  QStandardPaths::setTestModeEnabled(true);

  QSlider slider(Qt::Horizontal);
  slider.resize(200, 40);

  auto set_window_color = [&slider](const QColor &color) {
    QPalette palette = slider.palette();
    for (const QPalette::ColorGroup group : { QPalette::Active, QPalette::Inactive, QPalette::Disabled }) {
      palette.setColor(group, QPalette::Window, color);
    }
    slider.setPalette(palette);
  };
  set_window_color(QColor(40, 40, 40, 48));

  MoodbarProxyStyle *style = new MoodbarProxyStyle(&slider);
  style->SetMoodbarData(QByteArray(3 * 1000, '\x80'));
  style->SetShowMoodbar(true);

  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 1500) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }

  auto paint_image = [&]() {
    QStyleOptionSlider opt;
    opt.initFrom(&slider);
    opt.rect = QRect(QPoint(0, 0), slider.size());
    opt.minimum = 0;
    opt.maximum = 200;
    opt.sliderValue = 0;
    opt.orientation = Qt::Horizontal;

    QImage image(slider.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    style->drawComplexControl(QStyle::CC_Slider, &opt, &painter, &slider);
    painter.end();
    return image;
  };

  // A pixel of the margin around the moodbar, away from the position arrow.
  const QPoint margin(150, 1);
  paint_image();

  set_window_color(QColor(200, 200, 200));
  EXPECT_EQ(paint_image().pixelColor(margin), QColor(200, 200, 200));

  delete style;

}
