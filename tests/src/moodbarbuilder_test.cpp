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

#include "config.h"

#include "gtest_include.h"

#include <QList>
#include <QByteArray>

#include "moodbar/moodbarbuilder.h"

namespace {

constexpr int kBands = 128;
constexpr int kRate = 44100;

// Regression test: without frames, like for a track shorter than a frame, the moodbar was uninitialized data, which was cached and saved.
TEST(MoodbarBuilderTest, NoFramesGivesEmptyData) {

  MoodbarBuilder builder;
  builder.Init(kBands, kRate);
  EXPECT_TRUE(builder.Finish(1000).isEmpty());

}

TEST(MoodbarBuilderTest, FramesGiveDataForWidth) {

  MoodbarBuilder builder;
  builder.Init(kBands, kRate);

  QList<double> magnitudes(kBands);
  for (int frame = 0; frame < 10; ++frame) {
    for (int band = 0; band < kBands; ++band) {
      magnitudes[band] = static_cast<double>((band + frame) % 7);
    }
    builder.AddFrame(magnitudes.constData(), kBands);
  }

  EXPECT_EQ(3000, builder.Finish(1000).size());

}

// Regression test: the averaged color values (0 - 255) were converted from double to char, which is signed, so every value above 127 was out of range, which is undefined behavior.
// Run with -fsanitize=float-cast-overflow (not part of GCC's -fsanitize=undefined) to see it.
TEST(MoodbarBuilderTest, BrightFramesGiveFullByteValues) {

  MoodbarBuilder builder;
  builder.Init(kBands, kRate);

  QList<double> magnitudes(kBands);
  for (int frame = 0; frame < 100; ++frame) {
    for (int band = 0; band < kBands; ++band) {
      magnitudes[band] = static_cast<double>(frame);
    }
    builder.AddFrame(magnitudes.constData(), kBands);
  }

  const QByteArray data = builder.Finish(100);
  ASSERT_EQ(300, data.size());
  int maximum = 0;
  for (const char c : data) {
    maximum = qMax(maximum, static_cast<int>(static_cast<unsigned char>(c)));
  }
  EXPECT_GT(maximum, 127);

}

}  // namespace
