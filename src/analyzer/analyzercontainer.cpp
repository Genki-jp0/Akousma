/*
   Strawberry Music Player
   This file was part of Clementine.
   Copyright 2010, David Sansome <me@davidsansome.com>

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

#include "config.h"

#include <chrono>

#include <QObject>
#include <QWidget>
#include <QVariant>
#include <QString>
#include <QTimer>
#include <QBoxLayout>
#include <QLayout>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QInputDialog>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QSettings>
#include <QtEvents>

#include "analyzercontainer.h"

#include "analyzerbase.h"
#include "blockanalyzer.h"
#include "boomanalyzer.h"
#include "turbineanalyzer.h"
#include "sonogramanalyzer.h"
#include "waverubberanalyzer.h"
#include "rainbowanalyzer.h"

#include "includes/shared_ptr.h"
#include "core/logging.h"
#include "core/settings.h"
#include "engine/enginebase.h"

using namespace std::chrono_literals;
using namespace Qt::Literals::StringLiterals;

namespace {
constexpr char kSettingsGroup[] = "Analyzer";
constexpr char kSettingsType[] = "type";
constexpr char kSettingsFramerate[] = "framerate";
constexpr char kSettingsVsync[] = "vsync";
constexpr int kLowFramerate = 20;
constexpr int kMediumFramerate = 25;
constexpr int kHighFramerate = 30;
constexpr int kSuperHighFramerate = 60;
constexpr int kMinFramerate = 1;
constexpr int kMaxFramerate = 1000;

// Timer interval in milliseconds for a framerate, rounded to the nearest millisecond and never below 1.
int TimeoutForFramerate(const int framerate) {
  return qMax(1, qRound(1000.0 / qBound(kMinFramerate, framerate, kMaxFramerate)));
}
}  // namespace

AnalyzerContainer::AnalyzerContainer(QWidget *parent)
    : QWidget(parent),
      current_framerate_(kMediumFramerate),
      vsync_(false),
      context_menu_(new QMenu(this)),
      context_menu_framerate_(new QMenu(tr("Framerate"), this)),
      vsync_action_(nullptr),
      custom_framerate_action_(nullptr),
      group_(new QActionGroup(this)),
      group_framerate_(new QActionGroup(this)),
      double_click_timer_(new QTimer(this)),
      ignore_next_click_(false),
      current_analyzer_(nullptr),
      engine_(nullptr) {

  QHBoxLayout *layout = new QHBoxLayout(this);
  setLayout(layout);
  layout->setContentsMargins(0, 0, 0, 0);

  // Init framerate sub-menu
  AddFramerate(tr("Low (%1 fps)").arg(kLowFramerate), kLowFramerate);
  AddFramerate(tr("Medium (%1 fps)").arg(kMediumFramerate), kMediumFramerate);
  AddFramerate(tr("High (%1 fps)").arg(kHighFramerate), kHighFramerate);
  AddFramerate(tr("Super high (%1 fps)").arg(kSuperHighFramerate), kSuperHighFramerate);

  // Has to come after the presets: UpdateFramerateActions() looks the presets up by their position in the group
  vsync_action_ = context_menu_framerate_->addAction(tr("Monitor refresh rate (vsync)"));
  vsync_action_->setCheckable(true);
  group_framerate_->addAction(vsync_action_);
  QObject::connect(vsync_action_, &QAction::triggered, this, &AnalyzerContainer::EnableVsync);

  custom_framerate_action_ = context_menu_framerate_->addAction(tr("Custom..."));
  custom_framerate_action_->setCheckable(true);
  group_framerate_->addAction(custom_framerate_action_);
  QObject::connect(custom_framerate_action_, &QAction::triggered, this, &AnalyzerContainer::ChooseCustomFramerate);

  context_menu_->addMenu(context_menu_framerate_);
  context_menu_->addSeparator();

  AddAnalyzerType<BlockAnalyzer>();
  AddAnalyzerType<BoomAnalyzer>();
  AddAnalyzerType<TurbineAnalyzer>();
  AddAnalyzerType<SonogramAnalyzer>();
  AddAnalyzerType<WaveRubberAnalyzer>();
  AddAnalyzerType<RainbowDashAnalyzer>();
  AddAnalyzerType<NyanCatAnalyzer>();

  disable_action_ = context_menu_->addAction(tr("No analyzer"), this, &AnalyzerContainer::DisableAnalyzer);
  disable_action_->setCheckable(true);
  group_->addAction(disable_action_);

  context_menu_->addSeparator();

  double_click_timer_->setSingleShot(true);
  double_click_timer_->setInterval(250ms);
  QObject::connect(double_click_timer_, &QTimer::timeout, this, &AnalyzerContainer::ShowPopupMenu);

  Load();

}

void AnalyzerContainer::showEvent(QShowEvent *e) {

  QWidget::showEvent(e);

  // Follow the monitor the window is on: it can be moved to one with another refresh rate
  QWindow *window_handle = window() ? window()->windowHandle() : nullptr;
  if (window_handle && window_handle != tracked_window_) {
    tracked_window_ = window_handle;
    QObject::connect(window_handle, &QWindow::screenChanged, this, &AnalyzerContainer::TrackScreen);
  }
  TrackScreen();

}

void AnalyzerContainer::TrackScreen() {

  QObject::disconnect(refresh_rate_connection_);
  refresh_rate_connection_ = QMetaObject::Connection();

  if (tracked_window_ && tracked_window_->screen()) {
    refresh_rate_connection_ = QObject::connect(tracked_window_->screen(), &QScreen::refreshRateChanged, this, [this]() { if (vsync_) ApplyFramerate(); });
  }

  if (vsync_) ApplyFramerate();

}

void AnalyzerContainer::mouseReleaseEvent(QMouseEvent *e) {

  if (e->button() == Qt::RightButton) {
    context_menu_->popup(e->globalPosition().toPoint());
  }

}

void AnalyzerContainer::ShowPopupMenu() {
  context_menu_->popup(last_click_pos_);
}

void AnalyzerContainer::wheelEvent(QWheelEvent *e) {
  Q_EMIT WheelEvent(e->angleDelta().y());
}

void AnalyzerContainer::SetEngine(SharedPtr<EngineBase> engine) {

  if (current_analyzer_) current_analyzer_->set_engine(engine);
  engine_ = engine;

}

void AnalyzerContainer::DisableAnalyzer() {

  delete current_analyzer_;
  current_analyzer_ = nullptr;

  Save();

}

void AnalyzerContainer::ChangeAnalyzer(const int id) {

  QObject *instance = analyzer_types_.at(id)->newInstance(Q_ARG(QWidget*, this));

  if (!instance) {
    qLog(Warning) << "Couldn't initialize a new" << analyzer_types_[id]->className();
    return;
  }

  delete current_analyzer_;
  current_analyzer_ = qobject_cast<AnalyzerBase*>(instance);
  current_analyzer_->set_engine(engine_);
  // Even if it is not supposed to happen, I don't want to get a dbz error
  current_framerate_ = current_framerate_ == 0 ? kMediumFramerate : current_framerate_;
  current_analyzer_->SetVsync(vsync_);
  current_analyzer_->ChangeTimeout(TimeoutForFramerate(EffectiveFramerate()));

  layout()->addWidget(current_analyzer_);

  Save();

}

// The user picked a fixed framerate, which also turns vsync off
void AnalyzerContainer::ChangeFramerate(int new_framerate) {

  // Even if it is not supposed to happen, I don't want to get a dbz error
  new_framerate = new_framerate <= 0 ? kMediumFramerate : qBound(kMinFramerate, new_framerate, kMaxFramerate);

  SaveVsync(false);
  SaveFramerate(new_framerate);
  ApplyFramerate();

}

void AnalyzerContainer::EnableVsync() {

  SaveVsync(true);
  ApplyFramerate();

}

// The refresh rate of the monitor the window is on, as a whole number of frames per second
int AnalyzerContainer::RefreshRate() const {

  const QWindow *window_handle = tracked_window_;
  const QScreen *screen = window_handle && window_handle->screen() ? window_handle->screen() : QGuiApplication::primaryScreen();
  const qreal refresh_rate = screen ? screen->refreshRate() : 0.0;
  if (refresh_rate < 1.0) return current_framerate_;
  return qBound(kMinFramerate, qRound(refresh_rate), kMaxFramerate);

}

int AnalyzerContainer::EffectiveFramerate() const {
  return vsync_ ? RefreshRate() : current_framerate_;
}

void AnalyzerContainer::ApplyFramerate() {

  if (current_analyzer_) {
    current_analyzer_->SetVsync(vsync_);
    current_analyzer_->ChangeTimeout(TimeoutForFramerate(EffectiveFramerate()));

    // notify the current analyzer that the framerate has changed
    current_analyzer_->framerateChanged();
  }

  UpdateFramerateActions();

}

void AnalyzerContainer::Load() {

  Settings s;
  s.beginGroup(kSettingsGroup);
  QString type = s.value(kSettingsType, u"BlockAnalyzer"_s).toString();
  current_framerate_ = s.value(kSettingsFramerate, kMediumFramerate).toInt();
  vsync_ = s.value(kSettingsVsync, false).toBool();
  s.endGroup();

  current_framerate_ = current_framerate_ <= 0 ? kMediumFramerate : qBound(kMinFramerate, current_framerate_, kMaxFramerate);

  // Analyzer
  if (type.isEmpty()) {
    DisableAnalyzer();
    disable_action_->setChecked(true);
  }
  else {
    for (int i = 0; i < analyzer_types_.count(); ++i) {
      if (type == QString::fromLatin1(analyzer_types_[i]->className())) {
        ChangeAnalyzer(i);
        QAction *action = actions_.value(i);
        action->setChecked(true);
        break;
      }
    }
    if (!current_analyzer_) {
      ChangeAnalyzer(0);
      QAction *action = actions_.value(0);
      action->setChecked(true);
    }
  }

  // Framerate: the saved value can be a custom one that is not in the preset list
  ApplyFramerate();

}

void AnalyzerContainer::ChooseCustomFramerate() {

  bool ok = false;
  const int framerate = QInputDialog::getInt(this, tr("Custom framerate"), tr("Framerate (fps):"), current_framerate_, kMinFramerate, kMaxFramerate, 1, &ok);
  if (ok) {
    ChangeFramerate(framerate);
  }
  else {
    // Cancelled: put the check mark back on the framerate that is still in use
    UpdateFramerateActions();
  }

}

void AnalyzerContainer::UpdateFramerateActions() {

  if (vsync_) {
    vsync_action_->setText(tr("Monitor refresh rate (%1 Hz)").arg(RefreshRate()));
    custom_framerate_action_->setText(tr("Custom..."));
    vsync_action_->setChecked(true);
    return;
  }

  vsync_action_->setText(tr("Monitor refresh rate (vsync)"));

  const QList<QAction*> actions = group_framerate_->actions();
  for (int i = 0; i < framerate_list_.count(); ++i) {
    if (current_framerate_ == framerate_list_.value(i)) {
      actions.value(i)->setChecked(true);
      custom_framerate_action_->setText(tr("Custom..."));
      return;
    }
  }

  custom_framerate_action_->setText(tr("Custom (%1 fps)...").arg(current_framerate_));
  custom_framerate_action_->setChecked(true);

}

void AnalyzerContainer::SaveVsync(const bool vsync) {

  vsync_ = vsync;
  Settings s;
  s.beginGroup(kSettingsGroup);
  s.setValue(kSettingsVsync, vsync_);
  s.endGroup();

}

void AnalyzerContainer::SaveFramerate(const int framerate) {

  // For now, framerate is common for all analyzers. Maybe each analyzer should have its own framerate?
  current_framerate_ = framerate;
  Settings s;
  s.beginGroup(kSettingsGroup);
  s.setValue(kSettingsFramerate, current_framerate_);
  s.endGroup();

}

void AnalyzerContainer::Save() {

  Settings s;
  s.beginGroup(kSettingsGroup);
  s.setValue(kSettingsType, current_analyzer_ ? QString::fromLatin1(current_analyzer_->metaObject()->className()) : QVariant());
  s.endGroup();

}

void AnalyzerContainer::AddFramerate(const QString &name, const int framerate) {

  QAction *action = context_menu_framerate_->addAction(name);
  group_framerate_->addAction(action);
  framerate_list_ << framerate;
  action->setCheckable(true);
  QObject::connect(action, &QAction::triggered, this, [this, framerate]() { ChangeFramerate(framerate); } );

}
