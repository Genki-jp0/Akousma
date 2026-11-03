/*
 * Akousma
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

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QSize>

#include "moodbar/moodbarrenderer.h"

namespace {

ColorVector WhiteColors(const int count) {
  return ColorVector(count, QColor(Qt::white));
}

// Regression test: a moodbar one pixel high divided by rect.height() / 2, which is 0, and converted the resulting NaN to int, which is undefined behavior and drew black bars.
TEST(MoodbarRendererTest, OnePixelHighMoodbarIsDrawnInItsColors) {

  QImage image(QSize(8, 10), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  {
    QPainter p(&image);
    MoodbarRenderer::Render(WhiteColors(16), &p, QRect(0, 5, 8, 1));
  }

  for (int x = 0; x < image.width(); ++x) {
    const QColor color = image.pixelColor(x, 5);
    EXPECT_EQ(255, color.alpha()) << x;
    EXPECT_GT(color.value(), 200) << x;
  }

}

// Regression test: a moodbar without height drew a row of pixels above its rectangle.
TEST(MoodbarRendererTest, EmptyRectDrawsNothing) {

  QImage image(QSize(8, 10), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  {
    QPainter p(&image);
    MoodbarRenderer::Render(WhiteColors(16), &p, QRect(0, 5, 8, 0));
  }

  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      EXPECT_EQ(0, qAlpha(image.pixel(x, y))) << x << "," << y;
    }
  }

  EXPECT_TRUE(MoodbarRenderer::RenderToImage(WhiteColors(16), QSize(8, 0)).isNull());

}

TEST(MoodbarRendererTest, NormalMoodbarFillsItsRect) {

  const QImage image = MoodbarRenderer::RenderToImage(WhiteColors(16), QSize(8, 6));
  ASSERT_FALSE(image.isNull());
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      EXPECT_EQ(255, qAlpha(image.pixel(x, y))) << x << "," << y;
    }
  }

}

}  // namespace
