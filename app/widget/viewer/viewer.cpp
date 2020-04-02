/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2019 Olive Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "viewer.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QLabel>
#include <QResizeEvent>
#include <QScreen>
#include <QVBoxLayout>
#include <QtMath>

#include "audio/audiomanager.h"
#include "common/timecodefunctions.h"
#include "config/config.h"
#include "project/item/sequence/sequence.h"
#include "project/project.h"
#include "render/pixelformat.h"
#include "widget/menu/menu.h"

ViewerWidget::ViewerWidget(QWidget* parent)
    : TimeBasedWidget(false, true, parent),
      playback_speed_(0),
      frame_cache_job_time_(0),
      color_menu_enabled_(true),
      divider_(Config::Current()["DefaultViewerDivider"].toInt()),
      override_color_manager_(nullptr),
      time_changed_from_timer_(false) {
  // Set up main layout
  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setMargin(0);

  // Set up stacked widget to allow switching away from the viewer widget
  stack_ = new QStackedWidget();
  layout->addWidget(stack_);

  // Create main OpenGL-based view and sizer
  sizer_ = new ViewerSizer();
  stack_->addWidget(sizer_);

  ViewerGLWidget* main_widget = new ViewerGLWidget();
  connect(main_widget, &ViewerGLWidget::customContextMenuRequested, this, &ViewerWidget::ShowContextMenu);
  connect(main_widget, &ViewerGLWidget::CursorColor, this, &ViewerWidget::CursorColor);
  connect(sizer_, &ViewerSizer::RequestMatrix, main_widget, &ViewerGLWidget::SetMatrix);
  sizer_->SetWidget(main_widget);
  gl_widgets_.append(main_widget);

  // Create waveform view when audio is connected and video isn't
  waveform_view_ = new AudioWaveformView();
  stack_->addWidget(waveform_view_);

  // Create time ruler
  layout->addWidget(ruler());

  // Create scrollbar
  layout->addWidget(scrollbar());
  connect(scrollbar(), &QScrollBar::valueChanged, ruler(), &TimeRuler::SetScroll);
  connect(scrollbar(), &QScrollBar::valueChanged, waveform_view_, &AudioWaveformView::SetScroll);

  // Create lower controls
  controls_ = new PlaybackControls();
  controls_->SetTimecodeEnabled(true);
  controls_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  connect(controls_, &PlaybackControls::PlayClicked, this, &ViewerWidget::Play);
  connect(controls_, &PlaybackControls::PauseClicked, this, &ViewerWidget::Pause);
  connect(controls_, &PlaybackControls::PrevFrameClicked, this, &ViewerWidget::PrevFrame);
  connect(controls_, &PlaybackControls::NextFrameClicked, this, &ViewerWidget::NextFrame);
  connect(controls_, &PlaybackControls::BeginClicked, this, &ViewerWidget::GoToStart);
  connect(controls_, &PlaybackControls::EndClicked, this, &ViewerWidget::GoToEnd);
  connect(controls_, &PlaybackControls::TimeChanged, this, &ViewerWidget::SetTimeAndSignal);
  layout->addWidget(controls_);

  // FIXME: Magic number
  SetScale(48.0);

  // Start background renderers
  video_renderer_ = new OpenGLBackend(this);
  connect(video_renderer_, &VideoRenderBackend::CachedTimeReady, this, &ViewerWidget::RendererCachedTime);
  connect(video_renderer_, &VideoRenderBackend::CachedTimeReady, ruler(), &TimeRuler::CacheTimeReady);
  connect(video_renderer_, &VideoRenderBackend::RangeInvalidated, ruler(), &TimeRuler::CacheInvalidatedRange);
  audio_renderer_ = new AudioBackend(this);

  waveform_view_->SetBackend(audio_renderer_);
  connect(waveform_view_, &AudioWaveformView::TimeChanged, this, &ViewerWidget::SetTimeAndSignal);

  connect(PixelFormat::instance(), &PixelFormat::FormatChanged, this, &ViewerWidget::UpdateRendererParameters);

  SetAutoMaxScrollBar(true);
}

void ViewerWidget::TimeChangedEvent(const int64_t& i) {
  if (!time_changed_from_timer_) {
    Pause();
  }

  controls_->SetTime(i);
  waveform_view_->SetTime(i);

  if (GetConnectedNode() && last_time_ != i) {
    rational time_set = Timecode::timestamp_to_time(i, timebase());

    UpdateTextureFromNode(time_set);

    PushScrubbedAudio();
  }

  last_time_ = i;
}

void ViewerWidget::ConnectNodeInternal(ViewerOutput* n) {
  if (!n->video_params().time_base().isNull()) {
    SetTimebase(n->video_params().time_base());
  } else if (n->audio_params().sample_rate() > 0) {
    SetTimebase(n->audio_params().time_base());
  } else {
    SetTimebase(rational());
  }

  connect(n, &ViewerOutput::TimebaseChanged, this, &ViewerWidget::SetTimebase);
  connect(n, &ViewerOutput::SizeChanged, this, &ViewerWidget::SizeChangedSlot);
  connect(n, &ViewerOutput::LengthChanged, this, &ViewerWidget::LengthChangedSlot);
  connect(n, &ViewerOutput::VideoParamsChanged, this, &ViewerWidget::UpdateRendererParameters);
  connect(n, &ViewerOutput::VisibleInvalidated, this, &ViewerWidget::InvalidateVisible);
  connect(n, &ViewerOutput::VideoGraphChanged, this, &ViewerWidget::UpdateStack);
  connect(n, &ViewerOutput::AudioGraphChanged, this, &ViewerWidget::UpdateStack);

  SizeChangedSlot(n->video_params().width(), n->video_params().height());
  LengthChangedSlot(n->Length());

  ColorManager* using_manager;
  if (override_color_manager_) {
    using_manager = override_color_manager_;
  } else if (n->parent()) {
    using_manager = static_cast<Sequence*>(n->parent())->project()->color_manager();
  } else {
    qWarning() << "Failed to find a suitable color manager for the connected viewer node";
    using_manager = nullptr;
  }

  foreach (ViewerGLWidget* glw, gl_widgets_) { glw->ConnectColorManager(using_manager); }

  divider_ = CalculateDivider();

  UpdateRendererParameters();

  UpdateStack();

  if (GetConnectedTimelinePoints()) {
    waveform_view_->ConnectTimelinePoints(GetConnectedTimelinePoints());
  }
}

void ViewerWidget::DisconnectNodeInternal(ViewerOutput* n) {
  Pause();

  SetTimebase(rational());

  disconnect(n, &ViewerOutput::TimebaseChanged, this, &ViewerWidget::SetTimebase);
  disconnect(n, &ViewerOutput::SizeChanged, this, &ViewerWidget::SizeChangedSlot);
  disconnect(n, &ViewerOutput::LengthChanged, this, &ViewerWidget::LengthChangedSlot);
  disconnect(n, &ViewerOutput::VideoParamsChanged, this, &ViewerWidget::UpdateRendererParameters);
  disconnect(n, &ViewerOutput::VisibleInvalidated, this, &ViewerWidget::InvalidateVisible);
  disconnect(n, &ViewerOutput::VideoGraphChanged, this, &ViewerWidget::UpdateStack);
  disconnect(n, &ViewerOutput::AudioGraphChanged, this, &ViewerWidget::UpdateStack);

  // Effectively disables the viewer and clears the state
  SizeChangedSlot(0, 0);

  foreach (ViewerGLWidget* glw, gl_widgets_) { glw->DisconnectColorManager(); }

  waveform_view_->ConnectTimelinePoints(nullptr);
}

void ViewerWidget::ConnectedNodeChanged(ViewerOutput* n) {
  video_renderer_->SetViewerNode(n);
  audio_renderer_->SetViewerNode(n);
}

void ViewerWidget::ScaleChangedEvent(const double& s) {
  TimeBasedWidget::ScaleChangedEvent(s);

  waveform_view_->SetScale(s);
}

void ViewerWidget::resizeEvent(QResizeEvent* event) {
  TimeBasedWidget::resizeEvent(event);

  int new_div = CalculateDivider();
  if (new_div != divider_) {
    divider_ = new_div;

    UpdateRendererParameters();
  }

  UpdateMinimumScale();
}

const QList<ViewerGLWidget*>& ViewerWidget::gl_widgets() const { return gl_widgets_; }

ViewerGLWidget* ViewerWidget::main_gl_widget() const { return gl_widgets_.first(); }

void ViewerWidget::TogglePlayPause() {
  if (IsPlaying()) {
    Pause();
  } else {
    Play();
  }
}

bool ViewerWidget::IsPlaying() const { return playback_speed_ != 0; }

void ViewerWidget::ConnectViewerNode(ViewerOutput* node, ColorManager* color_manager) {
  override_color_manager_ = color_manager;

  TimeBasedWidget::ConnectViewerNode(node);

  // Set texture to new texture (or null if no viewer node is available)
  UpdateTextureFromNode(GetTime());
}

void ViewerWidget::SetColorMenuEnabled(bool enabled) { color_menu_enabled_ = enabled; }

void ViewerWidget::SetOverrideSize(int width, int height) { SizeChangedSlot(width, height); }

void ViewerWidget::SetMatrix(const QMatrix4x4& mat) {
  foreach (ViewerGLWidget* glw, gl_widgets_) { glw->SetMatrix(mat); }
}

void ViewerWidget::SetFullScreen(QScreen* screen) {
  if (!screen) {
    // Try to find the screen that contains the mouse cursor currently
    foreach (QScreen* test, QGuiApplication::screens()) {
      if (test->geometry().contains(QCursor::pos())) {
        screen = test;
        break;
      }
    }

    // Fallback, just use the first screen
    if (!screen) {
      screen = QGuiApplication::screens().first();
    }
  }

  ViewerWindow* vw = new ViewerWindow(this);

  vw->showFullScreen();
  vw->setGeometry(screen->geometry());
  vw->gl_widget()->ConnectColorManager(main_gl_widget()->color_manager());
  main_gl_widget()->ConnectSibling(vw->gl_widget());
  connect(vw, &ViewerWindow::destroyed, this, &ViewerWidget::WindowAboutToClose);
  connect(vw->gl_widget(), &ViewerGLWidget::customContextMenuRequested, this, &ViewerWidget::ShowContextMenu);

  if (GetConnectedNode()) {
    vw->SetResolution(GetConnectedNode()->video_params().width(), GetConnectedNode()->video_params().height());
  }

  windows_.append(vw);
  gl_widgets_.append(vw->gl_widget());
}

VideoRenderBackend* ViewerWidget::video_renderer() const { return video_renderer_; }

void ViewerWidget::UpdateTextureFromNode(const rational& time) {
  if (!GetConnectedNode() || time >= GetConnectedNode()->Length()) {
    main_gl_widget()->SetImage(QString());
  } else {
    QString frame_fn = video_renderer_->GetCachedFrame(time);

    if (!frame_fn.isEmpty()) {
      main_gl_widget()->SetImage(frame_fn);
    }
  }
}

void ViewerWidget::PlayInternal(int speed) {
  Q_ASSERT(speed != 0);

  if (timebase().isNull()) {
    qWarning() << "ViewerWidget can't play with an invalid timebase";
    return;
  }

  playback_speed_ = speed;

  QIODevice* audio_src = audio_renderer_->GetAudioPullDevice();
  if (audio_src != nullptr && audio_src->open(QIODevice::ReadOnly)) {
    audio_src->seek(audio_renderer_->params().time_to_bytes(GetTime()));
    AudioManager::instance()->SetOutputParams(audio_renderer_->params());
    AudioManager::instance()->StartOutput(audio_src, playback_speed_);
  }

  start_msec_ = QDateTime::currentMSecsSinceEpoch();
  start_timestamp_ = ruler()->GetTime();

  controls_->ShowPauseButton();

  if (stack_->currentWidget() == sizer_) {
    connect(main_gl_widget(), &ViewerGLWidget::frameSwapped, this, &ViewerWidget::PlaybackTimerUpdate);
  } else {
    connect(AudioManager::instance(), &AudioManager::OutputNotified, this, &ViewerWidget::PlaybackTimerUpdate);
  }
}

void ViewerWidget::PushScrubbedAudio() {
  if (!IsPlaying() && Config::Current()["AudioScrubbing"].toBool()) {
    // Get audio src device from renderer
    QIODevice* audio_src = audio_renderer_->GetAudioPullDevice();

    if (audio_src && audio_src->open(QFile::ReadOnly)) {
      // FIXME: Hardcoded scrubbing interval (20ms)
      int size_of_sample = audio_renderer_->params().time_to_bytes(rational(20, 1000));

      // Push audio
      audio_src->seek(audio_renderer_->params().time_to_bytes(GetTime()));
      QByteArray frame_audio = audio_src->read(size_of_sample);
      AudioManager::instance()->SetOutputParams(audio_renderer_->params());
      AudioManager::instance()->PushToOutput(frame_audio);

      audio_src->close();
    }
  }
}

int ViewerWidget::CalculateDivider() {
  if (GetConnectedNode() && Config::Current()["AutoSelectDivider"].toBool()) {
    int long_side_of_video =
        qMax(GetConnectedNode()->video_params().width(), GetConnectedNode()->video_params().height());
    int long_side_of_widget = qMax(main_gl_widget()->width(), main_gl_widget()->height());

    return qMax(1, long_side_of_video / long_side_of_widget);
  }

  return divider_;
}

void ViewerWidget::UpdateMinimumScale() {
  if (!GetConnectedNode()) {
    return;
  }

  if (GetConnectedNode()->Length().isNull()) {
    // Avoids divide by zero
    SetMinimumScale(0);
  } else {
    SetMinimumScale(static_cast<double>(ruler()->width()) / GetConnectedNode()->Length().toDouble());
  }
}

void ViewerWidget::UpdateStack() {
  if (!GetConnectedNode() || GetConnectedNode()->texture_input()->IsConnected()) {
    stack_->setCurrentWidget(sizer_);
  } else {
    stack_->setCurrentWidget(waveform_view_);
  }
}

void ViewerWidget::ContextMenuSetFullScreen(QAction* action) {
  SetFullScreen(QGuiApplication::screens().at(action->data().toInt()));
}

void ViewerWidget::WindowAboutToClose() {
  ViewerWindow* vw = static_cast<ViewerWindow*>(sender());

  windows_.removeAll(vw);
  gl_widgets_.removeAll(vw->gl_widget());
}

void ViewerWidget::UpdateRendererParameters() {
  if (!GetConnectedNode()) {
    return;
  }

  RenderMode::Mode render_mode = RenderMode::kOffline;

  VideoRenderingParams vparam(GetConnectedNode()->video_params(),
                              PixelFormat::instance()->GetConfiguredFormatForMode(render_mode), render_mode, divider_);

  if (video_renderer_->params() != vparam) {
    video_renderer_->SetParameters(vparam);
    video_renderer_->InvalidateCache(TimeRange(0, GetConnectedNode()->Length()));
  }

  AudioRenderingParams aparam(GetConnectedNode()->audio_params(),
                              SampleFormat::GetConfiguredFormatForMode(render_mode));

  if (audio_renderer_->params() != aparam) {
    audio_renderer_->SetParameters(aparam);
    audio_renderer_->InvalidateCache(TimeRange(0, GetConnectedNode()->Length()));
  }
}

void ViewerWidget::ShowContextMenu(const QPoint& pos) {
  QMenu menu(static_cast<QWidget*>(sender()));

  context_menu_widget_ = static_cast<ViewerGLWidget*>(sender());

  // Color options
  if (context_menu_widget_->color_manager() && color_menu_enabled_) {
    QStringList displays = context_menu_widget_->color_manager()->ListAvailableDisplays();
    QMenu* ocio_display_menu = menu.addMenu(tr("Display"));
    connect(ocio_display_menu, &QMenu::triggered, this, &ViewerWidget::ContextMenuOCIODisplay);
    foreach (const QString& d, displays) {
      QAction* action = ocio_display_menu->addAction(d);
      action->setCheckable(true);
      action->setChecked(context_menu_widget_->ocio_display() == d);
      action->setData(d);
    }

    QStringList views = context_menu_widget_->color_manager()->ListAvailableViews(context_menu_widget_->ocio_display());
    QMenu* ocio_view_menu = menu.addMenu(tr("View"));
    connect(ocio_view_menu, &QMenu::triggered, this, &ViewerWidget::ContextMenuOCIOView);
    foreach (const QString& v, views) {
      QAction* action = ocio_view_menu->addAction(v);
      action->setCheckable(true);
      action->setChecked(context_menu_widget_->ocio_view() == v);
      action->setData(v);
    }

    QStringList looks = context_menu_widget_->color_manager()->ListAvailableLooks();
    QMenu* ocio_look_menu = menu.addMenu(tr("Look"));
    connect(ocio_look_menu, &QMenu::triggered, this, &ViewerWidget::ContextMenuOCIOLook);
    QAction* no_look_action = ocio_look_menu->addAction(tr("(None)"));
    no_look_action->setCheckable(true);
    no_look_action->setChecked(context_menu_widget_->ocio_look().isEmpty());
    foreach (const QString& l, looks) {
      QAction* action = ocio_look_menu->addAction(l);
      action->setCheckable(true);
      action->setChecked(context_menu_widget_->ocio_look() == l);
      action->setData(l);
    }

    menu.addSeparator();
  }

  // Playback resolution
  QMenu* playback_resolution_menu = menu.addMenu(tr("Resolution"));
  playback_resolution_menu->addAction(tr("Full"))->setData(1);
  int dividers[] = {2, 4, 8, 16};
  for (int i = 0; i < 4; i++) {
    playback_resolution_menu->addAction(tr("1/%1").arg(dividers[i]))->setData(dividers[i]);
  }
  connect(playback_resolution_menu, &QMenu::triggered, this, &ViewerWidget::SetDividerFromMenu);

  foreach (QAction* a, playback_resolution_menu->actions()) {
    a->setCheckable(true);
    if (a->data() == divider_) {
      a->setChecked(true);
    }
  }

  // Viewer Zoom Level
  QMenu* zoom_menu = menu.addMenu(tr("Zoom"));
  int zoom_levels[] = {10, 25, 50, 75, 100, 150, 200, 400};
  zoom_menu->addAction(tr("Fit"))->setData(0);
  for (int i = 0; i < 8; i++) {
    zoom_menu->addAction(tr("%1%").arg(zoom_levels[i]))->setData(zoom_levels[i]);
  }
  connect(zoom_menu, &QMenu::triggered, this, &ViewerWidget::SetZoomFromMenu);

  // Full Screen Menu
  QMenu* full_screen_menu = menu.addMenu(tr("Full Screen"));
  for (int i = 0; i < QGuiApplication::screens().size(); i++) {
    QScreen* s = QGuiApplication::screens().at(i);

    QAction* a = full_screen_menu->addAction(
        tr("Screen %1: %2x%3")
            .arg(QString::number(i), QString::number(s->size().width()), QString::number(s->size().height())));

    a->setData(i);
  }
  connect(full_screen_menu, &QMenu::triggered, this, &ViewerWidget::ContextMenuSetFullScreen);

  menu.exec(static_cast<QWidget*>(sender())->mapToGlobal(pos));
}

void ViewerWidget::Play() { PlayInternal(1); }

void ViewerWidget::Pause() {
  if (IsPlaying()) {
    AudioManager::instance()->StopOutput();
    playback_speed_ = 0;
    controls_->ShowPlayButton();

    if (stack_->currentWidget() == sizer_) {
      disconnect(main_gl_widget(), &ViewerGLWidget::frameSwapped, this, &ViewerWidget::PlaybackTimerUpdate);
    } else {
      disconnect(AudioManager::instance(), &AudioManager::OutputNotified, this, &ViewerWidget::PlaybackTimerUpdate);
    }
  }
}

void ViewerWidget::ShuttleLeft() {
  int current_speed = playback_speed_;

  if (current_speed != 0) {
    Pause();
  }

  current_speed--;

  if (current_speed == 0) {
    current_speed--;
  }

  PlayInternal(current_speed);
}

void ViewerWidget::ShuttleStop() { Pause(); }

void ViewerWidget::ShuttleRight() {
  int current_speed = playback_speed_;

  if (current_speed != 0) {
    Pause();
  }

  current_speed++;

  if (current_speed == 0) {
    current_speed++;
  }

  PlayInternal(current_speed);
}

void ViewerWidget::SetOCIOParameters(const QString& display, const QString& view, const QString& look) {
  SetOCIOParameters(display, view, look, main_gl_widget());
}

void ViewerWidget::SetOCIOParameters(const QString& display, const QString& view, const QString& look,
                                     ViewerGLWidget* sender) {
  sender->SetOCIOParameters(display, view, look);
}

void ViewerWidget::SetOCIODisplay(const QString& display) { SetOCIODisplay(display, main_gl_widget()); }

void ViewerWidget::SetOCIODisplay(const QString& display, ViewerGLWidget* sender) { sender->SetOCIODisplay(display); }

void ViewerWidget::SetOCIOView(const QString& view) { SetOCIOView(view, main_gl_widget()); }

void ViewerWidget::SetOCIOLook(const QString& look) { SetOCIOLook(look, main_gl_widget()); }

void ViewerWidget::SetOCIOView(const QString& view, ViewerGLWidget* sender) { sender->SetOCIOView(view); }

void ViewerWidget::SetOCIOLook(const QString& look, ViewerGLWidget* sender) { sender->SetOCIOLook(look); }

void ViewerWidget::SetSignalCursorColorEnabled(bool e) {
  foreach (ViewerGLWidget* glw, gl_widgets_) { glw->SetSignalCursorColorEnabled(e); }
}

void ViewerWidget::TimebaseChangedEvent(const rational& timebase) {
  TimeBasedWidget::TimebaseChangedEvent(timebase);

  controls_->SetTimebase(timebase);

  controls_->SetTime(ruler()->GetTime());
  LengthChangedSlot(GetConnectedNode() ? GetConnectedNode()->Length() : 0);
}

void ViewerWidget::PlaybackTimerUpdate() {
  int64_t real_time = QDateTime::currentMSecsSinceEpoch() - start_msec_;

  int64_t frames_since_start = qRound(static_cast<double>(real_time) / (timebase_dbl() * 1000));

  int64_t current_time = start_timestamp_ + frames_since_start * playback_speed_;

  if (current_time < 0) {
    SetTimeAndSignal(0);
  } else {
    time_changed_from_timer_ = true;
    SetTimeAndSignal(current_time);
    time_changed_from_timer_ = false;
  }
}

void ViewerWidget::RendererCachedTime(const rational& time, qint64 job_time) {
  if (GetTime() == time && job_time > frame_cache_job_time_) {
    frame_cache_job_time_ = job_time;

    UpdateTextureFromNode(GetTime());
  }
}

void ViewerWidget::SizeChangedSlot(int width, int height) {
  sizer_->SetChildSize(width, height);

  foreach (ViewerWindow* vw, windows_) { vw->SetResolution(width, height); }
}

void ViewerWidget::LengthChangedSlot(const rational& length) {
  controls_->SetEndTime(Timecode::time_to_timestamp(length, timebase()));
  ruler()->SetCacheStatusLength(length);
  UpdateMinimumScale();
}

void ViewerWidget::ContextMenuOCIODisplay(QAction* action) {
  SetOCIODisplay(action->data().toString(), context_menu_widget_);
}

void ViewerWidget::ContextMenuOCIOView(QAction* action) {
  SetOCIOView(action->data().toString(), context_menu_widget_);
}

void ViewerWidget::ContextMenuOCIOLook(QAction* action) {
  SetOCIOLook(action->data().toString(), context_menu_widget_);
}

void ViewerWidget::SetDividerFromMenu(QAction* action) {
  int divider = action->data().toInt();

  if (divider <= 0) {
    qWarning() << "Tried to set invalid divider:" << divider;
    return;
  }

  divider_ = divider;

  UpdateRendererParameters();
}

void ViewerWidget::SetZoomFromMenu(QAction* action) { sizer_->SetZoom(action->data().toInt()); }

void ViewerWidget::InvalidateVisible() { video_renderer_->InvalidateCache(TimeRange(GetTime(), GetTime())); }
