/*
 * Strawberry Music Player
 * Copyright 2026, Jonas Kvinge <jonas@jkvinge.net>
 * Copyright 2026, Genki (genki_jp0) <genki_jp0@users.noreply.github.com>
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

#include <memory>

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
#include <QSizePolicy>
#include <QSlider>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTimeLine>

#include "moodbar/moodbarproxystyle.h"

#ifdef HAVE_WAVEFORM
#  include "waveform/waveformproxystyle.h"
#endif

namespace {

// Moodbar data with the colors changing along the moodbar, reversed for the other moodbar, so the moodbars look different.
QByteArray MoodbarData(const bool reversed) {

  QByteArray data;
  constexpr int kSamples = 1000;
  for (int i = 0; i < kSamples; ++i) {
    const int value = (reversed ? kSamples - 1 - i : i) * 255 / (kSamples - 1);
    data.append(static_cast<char>(value));
    data.append(static_cast<char>(255 - value));
    data.append(static_cast<char>((value * 3) % 256));
  }
  return data;

}

class MoodbarProxyStyleTest : public ::testing::Test {
 protected:
  void SetUp() override {
    slider_ = std::make_unique<QSlider>(Qt::Horizontal);
    slider_->resize(400, 30);
    slider_->show();
    style_ = std::make_unique<MoodbarProxyStyle>(&*slider_);
    style_->SetShowMoodbar(true);
    timeline_ = style_->findChild<QTimeLine*>();
    ASSERT_NE(nullptr, timeline_);
  }

  void TearDown() override {
    // The style owns the fade timeline, delete it while the slider is still alive.
    style_.reset();
    slider_.reset();
  }

  // Moves the fade to the given time and keeps it there, the image is grabbed at the same point of the fade for each case.
  QImage GrabAt(const int msec) {
    if (timeline_->state() == QTimeLine::State::Running) timeline_->setPaused(true);
    timeline_->setCurrentTime(msec);
    return slider_->grab().toImage();
  }

  // Shows the first moodbar, fades it out partly, then sets the next moodbar, like a quick track change, and returns the image while fading.
  QImage QuickTrackChange(const QByteArray &first_data, const QByteArray &next_data) {

    style_->SetMoodbarData(first_data);
    // The fade is finished when the moodbar is drawn at the end of the fade.
    GrabAt(timeline_->duration());

    style_->SetMoodbarData(QByteArray());
    GrabAt(600);

    style_->SetMoodbarData(next_data);
    return GrabAt(600);

  }

  std::unique_ptr<QSlider> slider_;
  std::unique_ptr<MoodbarProxyStyle> style_;
  QTimeLine *timeline_ = nullptr;
};

// Regression test: on a quick track change, the fade continued with the moodbar of the previous song, and then jumped to the new one.
TEST_F(MoodbarProxyStyleTest, QuickTrackChangeFadesToNewMoodbar) {

  const QImage image = QuickTrackChange(MoodbarData(false), MoodbarData(true));

  // The same fade, where the previous moodbar is the same as the new one.
  TearDown();
  SetUp();
  const QImage expected_image = QuickTrackChange(MoodbarData(true), MoodbarData(true));

  // The other moodbar looks different while fading at the same point.
  TearDown();
  SetUp();
  const QImage previous_moodbar_image = QuickTrackChange(MoodbarData(false), MoodbarData(false));
  ASSERT_NE(expected_image, previous_moodbar_image);

  EXPECT_EQ(expected_image, image);

}

}  // namespace

// Regression test: the rendered moodbar kept the colors of the palette it was first drawn with, so after the Windows window
// backdrop was turned off (translucent Window color -> opaque one) the margin around it was drawn with the old color.
TEST(MoodbarProxyStyleSliderTest, PaletteChangeRendersNewPixmap) {

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

#ifdef HAVE_WAVEFORM
// Regression test: the moodbar and the waveform proxy styles share the seek slider and both set its vertical size policy.
// In moodbar mode the hidden waveform proxy reset it to Fixed (on Preferences OK/Apply, or when it got data), so the moodbar was drawn as a thin strip.
TEST(MoodbarProxyStyleSliderTest, HiddenWaveformProxyKeepsMoodbarExpanded) {

  QStandardPaths::setTestModeEnabled(true);

  QSlider slider(Qt::Horizontal);
  MoodbarProxyStyle *moodbar_style = new MoodbarProxyStyle(&slider);
  WaveformProxyStyle *waveform_style = new WaveformProxyStyle(&slider);
  slider.setStyle(moodbar_style);

  moodbar_style->SetShowMoodbar(true);
  moodbar_style->SetMoodbarData(QByteArray(3 * 1000, '\x80'));
  ASSERT_EQ(slider.sizePolicy().verticalPolicy(), QSizePolicy::MinimumExpanding);

  waveform_style->ReloadSettings();
  EXPECT_EQ(slider.sizePolicy().verticalPolicy(), QSizePolicy::MinimumExpanding);

  waveform_style->SetWaveformData(QByteArray());
  EXPECT_EQ(slider.sizePolicy().verticalPolicy(), QSizePolicy::MinimumExpanding);

  // Switching the seekbar to the normal slider still gives it back its standard height.
  moodbar_style->SetShowMoodbar(false);
  EXPECT_EQ(slider.sizePolicy().verticalPolicy(), QSizePolicy::Fixed);

  delete waveform_style;
  delete moodbar_style;

}
#endif
