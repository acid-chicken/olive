#include "widget/timelinewidget/timelinewidget.h"

#include "core.h"
#include "widget/nodeview/nodeviewundo.h"

TimelineWidget::AddTool::AddTool(TimelineWidget *parent) :
    Tool(parent),
    ghost_(nullptr)
{
}

void TimelineWidget::AddTool::MousePress(TimelineViewMouseEvent *event)
{
    const TrackReference& track = event->GetTrack();
    TrackOutput* t = parent()->GetTrackFromReference(track);

    if (t && t->IsLocked()) {
        return;
    }

    drag_start_point_ = event->GetFrame();

    ghost_ = new TimelineViewGhostItem();
    ghost_->SetIn(drag_start_point_);
    ghost_->SetOut(drag_start_point_);
    ghost_->SetTrack(track);
    ghost_->SetYCoords(parent()->GetTrackY(track), parent()->GetTrackHeight(track));
    parent()->AddGhost(ghost_);

    snap_points_.append(drag_start_point_);
}

void TimelineWidget::AddTool::MouseMove(TimelineViewMouseEvent *event)
{
    if (!ghost_) {
        return;
    }

    MouseMoveInternal(event->GetFrame(), event->GetModifiers() & Qt::AltModifier);
}

void TimelineWidget::AddTool::MouseRelease(TimelineViewMouseEvent *event)
{
    MouseMove(event);

    const TrackReference& track = ghost_->Track();

    if (ghost_) {
        if (!ghost_->AdjustedLength().isNull()) {
            QUndoCommand* command = new QUndoCommand();

            ClipBlock* clip = new ClipBlock();
            clip->set_length_and_media_out(ghost_->AdjustedLength());
            new NodeAddCommand(static_cast<NodeGraph*>(parent()->GetConnectedNode()->parent()),
                               clip,
                               command);

            new TrackPlaceBlockCommand(parent()->GetConnectedNode()->track_list(track.type()),
                                       track.index(),
                                       clip,
                                       ghost_->GetAdjustedIn(),
                                       command);

            Core::instance()->undo_stack()->push(command);
        }

        parent()->ClearGhosts();
        snap_points_.clear();
        ghost_ = nullptr;
    }
}

void TimelineWidget::AddTool::MouseMoveInternal(const rational &cursor_frame, bool outwards)
{
    // Calculate movement
    rational movement = cursor_frame - drag_start_point_;

    // Snap movement
    bool snapped = SnapPoint(snap_points_, &movement);

    // If alt is held, our movement goes both ways (outwards)
    if (!snapped && outwards) {
        // Snap backwards too
        movement = -movement;
        SnapPoint(snap_points_, &movement);
        // We don't need to un-neg here because outwards means all future processing will be done both pos and neg
    }

    // Validation: Ensure in point never goes below 0
    if (movement < -ghost_->In() || (outwards && -movement < -ghost_->In())) {
        movement = -ghost_->In();
    }

    // Make adjustment
    if (!movement) {
        ghost_->SetInAdjustment(0);
        ghost_->SetOutAdjustment(0);
    } else if (movement > 0) {
        ghost_->SetInAdjustment(outwards ? -movement : 0);
        ghost_->SetOutAdjustment(movement);
    } else if (movement < 0) {
        ghost_->SetInAdjustment(movement);
        ghost_->SetOutAdjustment(outwards ? -movement : 0);
    }
}
