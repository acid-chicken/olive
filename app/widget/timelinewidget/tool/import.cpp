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

#include "widget/timelinewidget/timelinewidget.h"

#include <QMimeData>
#include <QToolTip>

#include "common/qtutils.h"
#include "config/config.h"
#include "core.h"
#include "node/audio/volume/volume.h"
#include "node/distort/transform/transform.h"
#include "node/input/media/audio/audio.h"
#include "node/input/media/video/video.h"
#include "widget/nodeview/nodeviewundo.h"

Timeline::TrackType TrackTypeFromStreamType(Stream::Type stream_type) {
  switch (stream_type) {
    case Stream::kVideo:
    case Stream::kImage:
      return Timeline::kTrackTypeVideo;
    case Stream::kAudio:
      return Timeline::kTrackTypeAudio;
    case Stream::kSubtitle:
      return Timeline::kTrackTypeSubtitle;
    case Stream::kUnknown:
    case Stream::kData:
    case Stream::kAttachment:
      break;
  }

  return Timeline::kTrackTypeNone;
}

TimelineWidget::ImportTool::ImportTool(TimelineWidget* parent) : Tool(parent) {
  // Calculate width used for importing to give ghosts a slight lead-in so the ghosts aren't right on the cursor
  import_pre_buffer_ = QFontMetricsWidth(parent->fontMetrics(), "HHHHHHHH");
}

void TimelineWidget::ImportTool::DragEnter(TimelineViewMouseEvent* event) {
  QStringList mime_formats = event->GetMimeData()->formats();

  // Listen for MIME data from a ProjectViewModel
  if (mime_formats.contains("application/x-oliveprojectitemdata")) {
    // Data is drag/drop data from a ProjectViewModel
    QByteArray model_data = event->GetMimeData()->data("application/x-oliveprojectitemdata");

    // Use QDataStream to deserialize the data
    QDataStream stream(&model_data, QIODevice::ReadOnly);

    // Variables to deserialize into
    quintptr item_ptr;
    int r;

    // Set drag start position
    drag_start_ = event->GetCoordinates();

    // Set ghosts to start where the cursor entered

    rational ghost_start = drag_start_.GetFrame() - parent()->SceneToTime(import_pre_buffer_);

    snap_points_.clear();

    while (!stream.atEnd()) {
      stream >> r >> item_ptr;

      // Get Item object
      Item* item = reinterpret_cast<Item*>(item_ptr);

      // Check if Item is Footage
      if (item->type() == Item::kFootage) {
        // If the Item is Footage, we can create a Ghost from it
        Footage* footage = static_cast<Footage*>(item);

        // Each stream is offset by one track per track "type", we keep track of them in this vector
        QVector<int> track_offsets(Timeline::kTrackTypeCount);
        track_offsets.fill(drag_start_.GetTrack().index());

        rational footage_duration;

        // Loop through all streams in footage
        foreach (StreamPtr stream, footage->streams()) {
          Timeline::TrackType track_type = TrackTypeFromStreamType(stream->type());

          // Check if this stream has a compatible TrackList
          if (track_type == Timeline::kTrackTypeNone || !stream->enabled()) {
            continue;
          }

          TimelineViewGhostItem* ghost = new TimelineViewGhostItem();

          if (stream->type() == Stream::kImage) {
            // Stream is essentially length-less - use config's default image length
            footage_duration = Config::Current()["DefaultStillLength"].value<rational>();
          } else {
            // Use duration from file
            int64_t stream_duration = stream->duration();

            // Rescale to timeline timebase
            stream_duration =
                qCeil(static_cast<double>(stream_duration) * stream->timebase().toDouble() / parent()->timebase_dbl());

            // Convert to rational time
            footage_duration =
                rational(parent()->timebase().numerator() * stream_duration, parent()->timebase().denominator());
          }

          ghost->SetIn(ghost_start);
          ghost->SetOut(ghost_start + footage_duration);
          ghost->SetTrack(TrackReference(track_type, track_offsets.at(track_type)));

          // Increment track count for this track type
          track_offsets[track_type]++;

          snap_points_.append(ghost->In());
          snap_points_.append(ghost->Out());

          ghost->setData(TimelineViewGhostItem::kAttachedFootage, QVariant::fromValue(stream));
          ghost->SetMode(Timeline::kMove);

          parent()->AddGhost(ghost);
        }

        // Stack each ghost one after the other
        ghost_start += footage_duration;
      }
    }

    event->accept();
  } else {
    // FIXME: Implement dropping from file
    event->ignore();
  }
}

void TimelineWidget::ImportTool::DragMove(TimelineViewMouseEvent* event) {
  if (parent()->HasGhosts()) {
    rational time_movement = event->GetFrame() - drag_start_.GetFrame();
    int track_movement = event->GetTrack().index() - drag_start_.GetTrack().index();

    // If snapping is enabled, check for snap points
    if (Core::instance()->snapping()) {
      SnapPoint(snap_points_, &time_movement);
    }

    time_movement = ValidateFrameMovement(time_movement, parent()->ghost_items_);
    track_movement = ValidateTrackMovement(track_movement, parent()->ghost_items_);

    rational earliest_ghost = RATIONAL_MAX;

    // Move ghosts to the mouse cursor
    foreach (TimelineViewGhostItem* ghost, parent()->ghost_items_) {
      ghost->SetInAdjustment(time_movement);
      ghost->SetOutAdjustment(time_movement);
      ghost->SetTrackAdjustment(track_movement);

      TrackReference adjusted_track = ghost->GetAdjustedTrack();
      ghost->SetYCoords(parent()->GetTrackY(adjusted_track), parent()->GetTrackHeight(adjusted_track));

      earliest_ghost = qMin(earliest_ghost, ghost->GetAdjustedIn());
    }

    // Generate tooltip (showing earliest in point of imported clip)
    int64_t earliest_timestamp = Timecode::time_to_timestamp(earliest_ghost, parent()->timebase());
    QString tooltip_text =
        Timecode::timestamp_to_timecode(earliest_timestamp, parent()->timebase(), Timecode::CurrentDisplay());

    // Force tooltip to update (otherwise the tooltip won't move as written in the documentation, and could get in the
    // way of the cursor)
    QToolTip::hideText();
    QToolTip::showText(QCursor::pos(), tooltip_text, parent());

    event->accept();
  } else {
    event->ignore();
  }
}

void TimelineWidget::ImportTool::DragLeave(QDragLeaveEvent* event) {
  if (parent()->HasGhosts()) {
    parent()->ClearGhosts();

    event->accept();
  } else {
    event->ignore();
  }
}

void TimelineWidget::ImportTool::DragDrop(TimelineViewMouseEvent* event) {
  if (parent()->HasGhosts()) {
    QUndoCommand* command = new QUndoCommand();
    NodeGraph* dst_graph = static_cast<NodeGraph*>(parent()->GetConnectedNode()->parent());

    QVector<Block*> block_items(parent()->ghost_items_.size());

    for (int i = 0; i < parent()->ghost_items_.size(); i++) {
      TimelineViewGhostItem* ghost = parent()->ghost_items_.at(i);

      StreamPtr footage_stream = ghost->data(TimelineViewGhostItem::kAttachedFootage).value<StreamPtr>();

      ClipBlock* clip = new ClipBlock();
      clip->set_length_and_media_out(ghost->Length());
      clip->set_block_name(footage_stream->footage()->name());
      new NodeAddCommand(dst_graph, clip, command);

      switch (footage_stream->type()) {
        case Stream::kVideo:
        case Stream::kImage: {
          VideoInput* video_input = new VideoInput();
          video_input->SetFootage(footage_stream);
          new NodeAddCommand(dst_graph, video_input, command);
          new NodeEdgeAddCommand(video_input->output(), clip->texture_input(), command);

          TransformDistort* transform = new TransformDistort();
          new NodeAddCommand(dst_graph, transform, command);
          new NodeEdgeAddCommand(transform->output(), video_input->matrix_input(), command);

          // OpacityNode* opacity = new OpacityNode();
          // NodeParam::ConnectEdge(opacity->texture_output(), clip->texture_input());
          // NodeParam::ConnectEdge(media->texture_output(), opacity->texture_input());
          break;
        }
        case Stream::kAudio: {
          AudioInput* audio_input = new AudioInput();
          audio_input->SetFootage(footage_stream);
          new NodeAddCommand(dst_graph, audio_input, command);

          VolumeNode* volume_node = new VolumeNode();
          new NodeAddCommand(dst_graph, volume_node, command);

          new NodeEdgeAddCommand(audio_input->output(), volume_node->samples_input(), command);
          new NodeEdgeAddCommand(volume_node->output(), clip->texture_input(), command);
          break;
        }
        default:
          break;
      }

      if (event->GetModifiers() & Qt::ControlModifier) {
        // emit parent()->RequestInsertBlockAtTime(clip, ghost->GetAdjustedIn());
      } else {
        new TrackPlaceBlockCommand(parent()->GetConnectedNode()->track_list(ghost->GetAdjustedTrack().type()),
                                   ghost->GetAdjustedTrack().index(), clip, ghost->GetAdjustedIn(), command);
      }

      block_items.replace(i, clip);

      // Link any clips so far that share the same Footage with this one
      for (int j = 0; j < i; j++) {
        StreamPtr footage_compare =
            parent()->ghost_items_.at(j)->data(TimelineViewGhostItem::kAttachedFootage).value<StreamPtr>();

        if (footage_compare->footage() == footage_stream->footage()) {
          Block::Link(block_items.at(j), clip);
        }
      }
    }

    Core::instance()->undo_stack()->pushIfHasChildren(command);

    parent()->ClearGhosts();

    event->accept();
  } else {
    event->ignore();
  }
}
