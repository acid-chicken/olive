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

#ifndef VIEWER_WIDGET_H
#define VIEWER_WIDGET_H

#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QWidget>

#include "common/rational.h"
#include "node/output/viewer/viewer.h"
#include "render/backend/opengl/openglbackend.h"
#include "render/backend/opengl/opengltexture.h"
#include "render/backend/audio/audiobackend.h"
#include "viewerglwidget.h"
#include "viewersizer.h"
#include "widget/playbackcontrols/playbackcontrols.h"
#include "widget/timeruler/timeruler.h"

/**
 * @brief An OpenGL-based viewer widget with playback controls (a PlaybackControls widget).
 */
class ViewerWidget : public QWidget
{
    Q_OBJECT
public:
    ViewerWidget(QWidget* parent = nullptr);

    void SetPlaybackControlsEnabled(bool enabled);

    void SetTimeRulerEnabled(bool enabled);

    const double& scale() const;

    rational GetTime() const;

    void SetScale(const double& scale_);

    void SetTime(const int64_t& time);

    void TogglePlayPause();

    bool IsPlaying() const;

    void ConnectViewerNode(ViewerOutput* node, ColorManager *color_manager = nullptr);

    ViewerOutput* GetConnectedViewer() const;

    void SetColorMenuEnabled(bool enabled);

    void SetOverrideSize(int width, int height);

    void SetMatrix(const QMatrix4x4& mat);

    VideoRenderBackend* video_renderer() const;

public slots:
    /**
     * @brief Set the texture to draw and draw it
     *
     * Wrapper function for ViewerGLWidget::SetTexture().
     *
     * @param tex
     */
    void SetTexture(OpenGLTexturePtr tex);

    void SetTimebase(const rational& r);

    void GoToStart();

    void PrevFrame();

    void Play();

    void Pause();

    void NextFrame();

    void GoToEnd();

    void ShuttleLeft();

    void ShuttleStop();

    void ShuttleRight();

    void SetOCIOParameters(const QString& display, const QString& view, const QString& look);

    /**
     * @brief Externally set the OCIO display to use
     *
     * This value must be a valid display in the current OCIO configuration.
     */
    void SetOCIODisplay(const QString& display);

    /**
     * @brief Externally set the OCIO view to use
     *
     * This value must be a valid display in the current OCIO configuration.
     */
    void SetOCIOView(const QString& view);

    /**
     * @brief Externally set the OCIO look to use (use empty string if none)
     *
     * This value must be a valid display in the current OCIO configuration.
     */
    void SetOCIOLook(const QString& look);

signals:
    void TimeChanged(const int64_t&);

protected:
    virtual void resizeEvent(QResizeEvent *event) override;

    OpenGLBackend* video_renderer_;
    AudioBackend* audio_renderer_;

private:
    void UpdateTimeInternal(int64_t i);

    void UpdateTextureFromNode(const rational &time);

    void PlayInternal(int speed);

    void PushScrubbedAudio();

    ViewerSizer* sizer_;

    ViewerGLWidget* gl_widget_;

    PlaybackControls* controls_;

    TimeRuler* ruler_;

    QScrollBar* scrollbar_;

    rational time_base_;

    double time_base_dbl_;

    QTimer playback_timer_;

    qint64 start_msec_;
    int64_t start_timestamp_;

    ViewerOutput* viewer_node_;

    int playback_speed_;

    qint64 frame_cache_job_time_;

    int64_t last_time_;

private slots:
    void RulerTimeChange(int64_t);

    void PlaybackTimerUpdate();

    void RendererCachedFrame(const rational& time, QVariant value, qint64 job_time);
    void RendererCachedTime(const rational& time, qint64 job_time);

    void SizeChangedSlot(int width, int height);

    void LengthChangedSlot(const rational& length);

    void UpdateRendererParameters();

};

#endif // VIEWER_WIDGET_H
