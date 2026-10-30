/*
   Strawberry Music Player
   This file was part of Amarok.
   Copyright 2003-2004, Max Howell <max.howell@methylblue.com>
   Copyright 2009-2012, David Sansome <me@davidsansome.com>
   Copyright 2010, 2012, 2014, John Maguire <john.maguire@gmail.com>
   Copyright 2017, Santiago Gil

   Strawberry is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Strawberry is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ANALYZERBASE_H
#define ANALYZERBASE_H

#include "config.h"

#ifdef __FreeBSD__
#  include <sys/types.h>
#endif

#include <vector>

#include <QWidget>
#include <QBasicTimer>
#include <QElapsedTimer>
#include <QString>
#include <QPainter>

#include "includes/shared_ptr.h"
#include "analyzer/fht.h"
#include "engine/enginebase.h"

class QHideEvent;
class QShowEvent;
class QPaintEvent;
class QTimerEvent;
class VsyncTicker;

class AnalyzerBase : public QWidget {
  Q_OBJECT

 public:
  ~AnalyzerBase() override;

  int timeout() const { return timeout_; }

  void set_engine(SharedPtr<EngineBase> engine) { engine_ = engine; }

  void ChangeTimeout(const int timeout);

  // Vsync mode: instead of a fixed timer, a new frame is started every time the monitor refreshes (Windows, through the DWM).
  // Other platforms keep using the timer, which the AnalyzerContainer then sets to the refresh rate of the monitor.
  void SetVsync(const bool vsync);

  // Start a new frame: used by the timer and by the vsync ticker
  void Tick();

  virtual void framerateChanged() {}

 protected:
  using Scope = std::vector<float>;
  explicit AnalyzerBase(QWidget *parent, const uint scope_size = 7);

  void hideEvent(QHideEvent *e) override;
  void showEvent(QShowEvent *e) override;
  void paintEvent(QPaintEvent *e) override;
  void timerEvent(QTimerEvent *e) override;

  void StartTicking();
  void StopTicking();
  void MeasureFrameInterval();

  int resizeExponent(int exp);
  int resizeForBands(const int bands);
  virtual void init() {}
  virtual void transform(Scope &scope);
  virtual void analyze(QPainter &p, const Scope &s, const bool new_frame) = 0;
  virtual void demo(QPainter &p);

  void interpolate(const Scope &in_scope, Scope &out_scope);
  void initSin(Scope &v, const uint size = 6000);

 protected:
  QBasicTimer timer_;
  FHT *fht_;
  SharedPtr<EngineBase> engine_;
  Scope lastscope_;

  bool new_frame_;
  bool is_playing_;
  int timeout_;

 private:
  bool vsync_;
  VsyncTicker *vsync_ticker_;

  // In vsync mode the real time between two frames is measured, so that the animation speed follows the monitor when its refresh rate changes
  // or when frames get dropped, instead of staying at the rate that was asked for
  QElapsedTimer frame_clock_;
  double last_frame_ms_;
  double frame_interval_ms_;  // moving average
  int frame_samples_;
};

#endif  // ANALYZERBASE_H
