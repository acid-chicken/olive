#include "keyframeviewbase.h"

#include <QMouseEvent>
#include <QVBoxLayout>

#include "dialog/keyframeproperties/keyframeproperties.h"
#include "keyframeviewundo.h"
#include "node/node.h"
#include "widget/menu/menu.h"
#include "widget/menu/menushared.h"
#include "widget/nodeparamview/nodeparamviewundo.h"

KeyframeViewBase::KeyframeViewBase(QWidget *parent) :
    TimelineViewBase(parent),
    y_scale_(1.0),
    dragging_bezier_point_(nullptr),
    y_axis_enabled_(false)
{
    SetDefaultDragMode(RubberBandDrag);
    setContextMenuPolicy(Qt::CustomContextMenu);

    connect(this, &KeyframeViewBase::customContextMenuRequested, this, &KeyframeViewBase::ShowContextMenu);
}

void KeyframeViewBase::Clear()
{
    QMap<NodeKeyframe*, KeyframeViewItem*>::iterator iterator;

    for (iterator=item_map_.begin(); iterator!=item_map_.end(); iterator++) {
        delete iterator.value();
    }

    item_map_.clear();
}

const double &KeyframeViewBase::GetYScale() const
{
    return y_scale_;
}

void KeyframeViewBase::SetYScale(const double &y_scale)
{
    y_scale_ = y_scale;

    if (y_axis_enabled_) {
        VerticalScaleChangedEvent(y_scale_);

        viewport()->update();
    }
}

void KeyframeViewBase::RemoveKeyframe(NodeKeyframePtr key)
{
    KeyframeAboutToBeRemoved(key.get());

    delete item_map_.take(key.get());
}

KeyframeViewItem *KeyframeViewBase::AddKeyframeInternal(NodeKeyframePtr key)
{
    KeyframeViewItem* item = new KeyframeViewItem(key);
    item->SetScale(GetScale());
    item_map_.insert(key.get(), item);
    scene()->addItem(item);
    return item;
}

void KeyframeViewBase::mousePressEvent(QMouseEvent *event)
{
    if (HandPress(event) || PlayheadPress(event)) {
        return;
    }

    active_tool_ = Core::instance()->tool();

    if (event->button() == Qt::LeftButton) {
        QGraphicsView::mousePressEvent(event);

        if (active_tool_ == Tool::kPointer) {
            QGraphicsItem* item_under_cursor = itemAt(event->pos());

            if (item_under_cursor) {

                drag_start_ = event->pos();

                // Determine what type of item is under the cursor
                dragging_bezier_point_ = dynamic_cast<BezierControlPointItem*>(item_under_cursor);

                if (dragging_bezier_point_) {
                    dragging_bezier_point_start_ = dragging_bezier_point_->GetCorrespondingKeyframeHandle();
                    dragging_bezier_point_opposing_start_ = dragging_bezier_point_->key()->bezier_control(NodeKeyframe::get_opposing_bezier_type(dragging_bezier_point_->mode()));
                } else {
                    QList<QGraphicsItem*> selected_items = scene()->selectedItems();

                    selected_keys_.resize(selected_items.size());

                    for (int i=0; i<selected_items.size(); i++) {
                        KeyframeViewItem* key = static_cast<KeyframeViewItem*>(selected_items.at(i));

                        selected_keys_.replace(i, {key, key->x(), key->key()->time(), key->key()->value().toDouble()});
                    }
                }
            }
        }
    }
}

void KeyframeViewBase::mouseMoveEvent(QMouseEvent *event)
{
    if (HandMove(event) || PlayheadMove(event)) {
        return;
    }

    if (event->buttons() & Qt::LeftButton) {
        QGraphicsView::mouseMoveEvent(event);

        if (active_tool_ == Tool::kPointer) {
            // Calculate cursor difference and scale it
            QPointF mouse_diff_scaled = GetScaledCursorPos(event->pos() - drag_start_);

            if (dragging_bezier_point_) {
                ProcessBezierDrag(mouse_diff_scaled,
                                  !(event->modifiers() & Qt::ControlModifier),
                                  false);
            } else if (!selected_keys_.isEmpty()) {
                foreach (const KeyframeItemAndTime& keypair, selected_keys_) {
                    NodeInput* input_parent = keypair.key->key()->parent();

                    input_parent->blockSignals(true);

                    keypair.key->key()->set_time(CalculateNewTimeFromScreen(keypair.time, mouse_diff_scaled.x()));

                    if (y_axis_enabled_) {
                        keypair.key->key()->set_value(keypair.value - mouse_diff_scaled.y());
                    }

                    // We emit a custom value changed signal while the keyframe is being dragged so only the currently viewed
                    // frame gets rendered in this time
                    input_parent->blockSignals(false);

                    input_parent->parentNode()->InvalidateVisible(input_parent);
                }
            }
        }
    }
}

void KeyframeViewBase::mouseReleaseEvent(QMouseEvent *event)
{
    if (HandRelease(event) || PlayheadRelease(event)) {
        return;
    }

    if (event->button() == Qt::LeftButton) {
        QGraphicsView::mouseReleaseEvent(event);

        if (active_tool_ == Tool::kPointer) {
            QPoint mouse_diff = event->pos() - drag_start_;
            QPointF mouse_diff_scaled = GetScaledCursorPos(mouse_diff);

            if (!mouse_diff.isNull()) {
                if (dragging_bezier_point_) {
                    ProcessBezierDrag(mouse_diff_scaled,
                                      !(event->modifiers() & Qt::ControlModifier),
                                      true);

                    dragging_bezier_point_ = nullptr;
                } else if (!selected_keys_.isEmpty()) {
                    QUndoCommand* command = new QUndoCommand();

                    foreach (const KeyframeItemAndTime& keypair, selected_keys_) {
                        KeyframeViewItem* item = keypair.key;

                        keypair.key->key()->parent()->blockSignals(true);

                        // Calculate the new time for this keyframe
                        rational new_time = CalculateNewTimeFromScreen(keypair.time, mouse_diff_scaled.x());



                        // Commit movement

                        // Since we overrode the cache signalling while dragging, we simulate here precisely the change that
                        // occurred by first setting the keyframe to its original position, and then letting the input handle
                        // the signalling once the undo command is pushed.
                        item->key()->set_time(keypair.time);
                        new NodeParamSetKeyframeTimeCommand(item->key(),
                                                            new_time,
                                                            keypair.time,
                                                            command);

                        // Commit value if we're setting a value
                        if (y_axis_enabled_) {
                            item->key()->set_value(keypair.value);
                            new NodeParamSetKeyframeValueCommand(item->key(),
                                                                 keypair.value - mouse_diff_scaled.y(),
                                                                 keypair.value,
                                                                 command);
                        }

                        keypair.key->key()->parent()->blockSignals(false);
                    }

                    Core::instance()->undo_stack()->push(command);
                }
            }

            selected_keys_.clear();
        }
    }
}

void KeyframeViewBase::ScaleChangedEvent(const double &scale)
{
    TimelineViewBase::ScaleChangedEvent(scale);

    QMap<NodeKeyframe*, KeyframeViewItem*>::const_iterator iterator;

    for (iterator=item_map_.begin(); iterator!=item_map_.end(); iterator++) {
        iterator.value()->SetScale(scale);
    }
}

void KeyframeViewBase::VerticalScaleChangedEvent(double)
{
}

const QMap<NodeKeyframe *, KeyframeViewItem *> &KeyframeViewBase::item_map() const
{
    return item_map_;
}

void KeyframeViewBase::KeyframeAboutToBeRemoved(NodeKeyframe *)
{
}

void KeyframeViewBase::SetYAxisEnabled(bool e)
{
    y_axis_enabled_ = e;
}

rational KeyframeViewBase::CalculateNewTimeFromScreen(const rational &old_time, double cursor_diff)
{
    return rational::fromDouble(old_time.toDouble() + cursor_diff);
}

QPointF KeyframeViewBase::GenerateBezierControlPosition(const NodeKeyframe::BezierType mode, const QPointF &start_point, const QPointF &scaled_cursor_diff)
{
    QPointF new_bezier_pos = start_point;

    new_bezier_pos += scaled_cursor_diff;

    // LIMIT bezier handles from overlapping each other
    if (mode == NodeKeyframe::kInHandle) {
        if (new_bezier_pos.x() > 0) {
            new_bezier_pos.setX(0);
        }
    } else {
        if (new_bezier_pos.x() < 0) {
            new_bezier_pos.setX(0);
        }
    }

    return new_bezier_pos;
}

void KeyframeViewBase::ProcessBezierDrag(QPointF mouse_diff_scaled, bool include_opposing, bool undoable)
{
    // Flip the mouse Y because bezier control points are drawn bottom to top, not top to bottom
    mouse_diff_scaled.setY(-mouse_diff_scaled.y());

    QPointF new_bezier_pos = GenerateBezierControlPosition(dragging_bezier_point_->mode(),
                             dragging_bezier_point_start_,
                             mouse_diff_scaled);

    // If the user is NOT holding control, we set the other handle to the exact negative of this handle
    QPointF new_opposing_pos;
    NodeKeyframe::BezierType opposing_type = NodeKeyframe::get_opposing_bezier_type(dragging_bezier_point_->mode());

    if (include_opposing) {
        new_opposing_pos = GenerateBezierControlPosition(opposing_type,
                           dragging_bezier_point_opposing_start_,
                           -mouse_diff_scaled);
    } else {
        new_opposing_pos = dragging_bezier_point_opposing_start_;
    }

    NodeInput* input_parent = dragging_bezier_point_->key()->parent();

    if (undoable) {
        QUndoCommand* command = new QUndoCommand();

        // Similar to the code in MouseRelease, we manipulated the signalling earlier and need to set the keys back to their
        // original position to allow the input to signal correctly when the undo command is pushed.

        input_parent->blockSignals(true);

        dragging_bezier_point_->key()->set_bezier_control(dragging_bezier_point_->mode(),
                dragging_bezier_point_start_);

        new KeyframeSetBezierControlPoint(dragging_bezier_point_->key(),
                                          dragging_bezier_point_->mode(),
                                          new_bezier_pos,
                                          dragging_bezier_point_start_,
                                          command);

        if (include_opposing) {
            dragging_bezier_point_->key()->set_bezier_control(opposing_type,
                    dragging_bezier_point_opposing_start_);

            new KeyframeSetBezierControlPoint(dragging_bezier_point_->key(),
                                              opposing_type,
                                              new_opposing_pos,
                                              dragging_bezier_point_opposing_start_,
                                              command);
        }

        input_parent->blockSignals(false);

        Core::instance()->undo_stack()->push(command);
    } else {
        input_parent->blockSignals(true);

        dragging_bezier_point_->key()->set_bezier_control(dragging_bezier_point_->mode(),
                new_bezier_pos);

        dragging_bezier_point_->key()->set_bezier_control(opposing_type,
                new_opposing_pos);

        input_parent->blockSignals(false);

        input_parent->parentNode()->InvalidateVisible(input_parent);
    }
}

QPointF KeyframeViewBase::GetScaledCursorPos(const QPoint &cursor_pos)
{
    return QPointF(static_cast<double>(cursor_pos.x()) / GetScale(),
                   static_cast<double>(cursor_pos.y()) / y_scale_);
}

void KeyframeViewBase::ShowContextMenu()
{
    Menu m;

    MenuShared::instance()->AddItemsForEditMenu(&m);

    QAction* linear_key_action = nullptr;
    QAction* bezier_key_action = nullptr;
    QAction* hold_key_action = nullptr;

    QList<QGraphicsItem*> items = scene()->selectedItems();
    if (!items.isEmpty()) {
        bool all_keys_are_same_type = true;
        NodeKeyframe::Type type = static_cast<KeyframeViewItem*>(items.first())->key()->type();

        for (int i=1; i<items.size(); i++) {
            KeyframeViewItem* key_item = static_cast<KeyframeViewItem*>(items.at(i));
            KeyframeViewItem* prev_item = static_cast<KeyframeViewItem*>(items.at(i-1));

            if (key_item->key()->type() != prev_item->key()->type()) {
                all_keys_are_same_type = false;
                break;
            }
        }

        m.addSeparator();

        linear_key_action = m.addAction(tr("Linear"));
        bezier_key_action = m.addAction(tr("Bezier"));
        hold_key_action = m.addAction(tr("Hold"));

        if (all_keys_are_same_type) {
            switch (type) {
            case NodeKeyframe::kLinear:
                linear_key_action->setChecked(true);
                break;
            case NodeKeyframe::kBezier:
                bezier_key_action->setChecked(true);
                break;
            case NodeKeyframe::kHold:
                hold_key_action->setChecked(true);
                break;
            }
        }

        m.addSeparator();

        QAction* properties_action = m.addAction(tr("P&roperties"));
        connect(properties_action, &QAction::triggered, this, &KeyframeViewBase::ShowKeyframePropertiesDialog);
    }

    QAction* selected = m.exec(QCursor::pos());

    // Process keyframe type changes
    if (!items.isEmpty()) {
        if (selected == linear_key_action
                || selected == bezier_key_action
                || selected == hold_key_action) {
            NodeKeyframe::Type new_type;

            if (selected == hold_key_action) {
                new_type = NodeKeyframe::kHold;
            } else if (selected == bezier_key_action) {
                new_type = NodeKeyframe::kBezier;
            } else {
                new_type = NodeKeyframe::kLinear;
            }

            QUndoCommand* command = new QUndoCommand();
            foreach (QGraphicsItem* item, items) {
                new KeyframeSetTypeCommand(static_cast<KeyframeViewItem*>(item)->key(),
                                           new_type,
                                           command);
            }
            Core::instance()->undo_stack()->pushIfHasChildren(command);
        }
    }
}

void KeyframeViewBase::ShowKeyframePropertiesDialog()
{
    QList<QGraphicsItem*> items = scene()->selectedItems();
    QList<NodeKeyframePtr> keys;

    foreach (QGraphicsItem* item, items) {
        keys.append(static_cast<KeyframeViewItem*>(item)->key());
    }

    if (!keys.isEmpty()) {
        KeyframePropertiesDialog kd(keys, timebase(), this);
        kd.exec();
    }
}

