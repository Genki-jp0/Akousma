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

#include <vector>

#include <QCoreApplication>
#include <QSize>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QColor>
#include <QResizeEvent>

#include "analyzer/blockanalyzer.h"

namespace {

class TestBlockAnalyzer : public BlockAnalyzer {
 public:
  explicit TestBlockAnalyzer() : BlockAnalyzer(nullptr) {}

  void SetSize(const QSize size) {
    const QSize old_size = this->size();
    resize(size);
    QResizeEvent e(size, old_size);
    QCoreApplication::sendEvent(this, &e);
  }

  // One frame, drawn on a transparent image.
  QImage Frame(const float level) {
    QImage image(size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    analyze(p, std::vector<float>(256, level), true);
    p.end();
    return image;
  }
};

QPalette PaletteWithWindowAlpha(const int alpha) {

  QPalette palette;
  for (const QPalette::ColorGroup group : { QPalette::Active, QPalette::Inactive, QPalette::Disabled }) {
    palette.setColor(group, QPalette::Window, QColor(40, 40, 40, alpha));
    palette.setColor(group, QPalette::Highlight, QColor(48, 140, 198));
  }
  return palette;

}

int MinAlpha(const QImage &image) {

  int min_alpha = 255;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      min_alpha = qMin(min_alpha, qAlpha(image.pixel(x, y)));
    }
  }
  return min_alpha;

}

}  // namespace

// Regression test: the bars, fade bars and background were only drawn with the palette the analyzer was resized with, so after
// the Windows window backdrop was turned off (translucent Window color -> opaque one) the analyzer kept drawing translucent
// blocks, which show black on a window without a backdrop.
TEST(BlockAnalyzerTest, PaletteChangeRebuildsCachedPixmaps) {

  TestBlockAnalyzer analyzer;
  analyzer.setPalette(PaletteWithWindowAlpha(48));
  analyzer.SetSize(QSize(200, 40));

  EXPECT_LT(MinAlpha(analyzer.Frame(0.5F)), 255);

  analyzer.setPalette(PaletteWithWindowAlpha(255));
  EXPECT_EQ(MinAlpha(analyzer.Frame(0.5F)), 255);
  EXPECT_EQ(MinAlpha(analyzer.Frame(0.0F)), 255);

  // And back to translucent: the background follows the new Window color.
  analyzer.setPalette(PaletteWithWindowAlpha(48));
  EXPECT_EQ(qAlpha(analyzer.Frame(0.0F).pixel(0, 0)), 48);

}

// Regression test: each frame was drawn over the previous one, with a translucent Window color the old frames showed through
// and the background got more opaque with every frame.
TEST(BlockAnalyzerTest, TranslucentBackgroundReplacesPreviousFrame) {

  TestBlockAnalyzer analyzer;
  analyzer.setPalette(PaletteWithWindowAlpha(48));
  analyzer.SetSize(QSize(200, 40));

  const int first_alpha = qAlpha(analyzer.Frame(0.0F).pixel(0, 0));
  int last_alpha = first_alpha;
  for (int i = 0; i < 10; ++i) {
    last_alpha = qAlpha(analyzer.Frame(0.0F).pixel(0, 0));
  }

  EXPECT_EQ(first_alpha, 48);
  EXPECT_EQ(last_alpha, first_alpha);

}
