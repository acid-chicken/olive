#ifndef TIMELINEWIDGET_H
#define TIMELINEWIDGET_H

#include <QRubberBand>
#include <QScrollBar>
#include <QWidget>

#include "core.h"
#include "node/output/viewer/viewer.h"
#include "timelineandtrackview.h"
#include "widget/slider/timeslider.h"
#include "widget/timebased/timebased.h"

/**
 * @brief Full widget for working with TimelineOutput nodes
 *
 * Encapsulates TimelineViews, TimeRulers, and scrollbars for a complete widget to manipulate Timelines
 */
class TimelineWidget : public TimeBasedWidget {
  Q_OBJECT
 public:
  enum DropWithoutSequenceBehavior { kDWSAsk, kDWSAuto, kDWSManual, kDWSDisable };

  TimelineWidget(QWidget* parent = nullptr);

  virtual ~TimelineWidget() override;

  void Clear();

  void SelectAll();

  void DeselectAll();

  void RippleToIn();

  void RippleToOut();

  void EditToIn();

  void EditToOut();

  void SplitAtPlayhead();

  void DeleteSelected(bool ripple = false);

  void IncreaseTrackHeight();

  void DecreaseTrackHeight();

  void InsertFootageAtPlayhead(const QList<Footage*>& footage);

  void OverwriteFootageAtPlayhead(const QList<Footage*>& footage);

  QList<TimelineViewBlockItem*> GetSelectedBlocks();

 signals:
  void SelectionChanged(const QList<Node*>& selected_blocks);

 protected:
  virtual void resizeEvent(QResizeEvent* event) override;

  virtual void TimebaseChangedEvent(const rational&) override;
  virtual void TimeChangedEvent(const int64_t&) override;
  virtual void ScaleChangedEvent(const double&) override;

  virtual void ConnectNodeInternal(ViewerOutput* n) override;
  virtual void DisconnectNodeInternal(ViewerOutput* n) override;

 private:
  class Tool {
   public:
    Tool(TimelineWidget* parent);
    virtual ~Tool();

    virtual void MousePress(TimelineViewMouseEvent*) {}
    virtual void MouseMove(TimelineViewMouseEvent*) {}
    virtual void MouseRelease(TimelineViewMouseEvent*) {}
    virtual void MouseDoubleClick(TimelineViewMouseEvent*) {}

    virtual void HoverMove(TimelineViewMouseEvent*) {}

    virtual void DragEnter(TimelineViewMouseEvent*) {}
    virtual void DragMove(TimelineViewMouseEvent*) {}
    virtual void DragLeave(QDragLeaveEvent*) {}
    virtual void DragDrop(TimelineViewMouseEvent*) {}

    TimelineWidget* parent();

    static Timeline::MovementMode FlipTrimMode(const Timeline::MovementMode& trim_mode);

   protected:
    /**
     * @brief Retrieve the QGraphicsItem at a particular scene position
     *
     * Requires a float-based scene position. If you have a screen position, use GetScenePos() first to convert it to a
     * scene position
     */
    TimelineViewBlockItem* GetItemAtScenePos(const TimelineCoordinate& coord);

    /**
     * @brief Validates Ghosts that are moving horizontally (time-based)
     *
     * Validation is the process of ensuring that whatever movements the user is making are "valid" and "legal". This
     * function's validation ensures that no Ghost's in point ends up in a negative timecode.
     */
    rational ValidateFrameMovement(rational movement, const QVector<TimelineViewGhostItem*> ghosts);

    /**
     * @brief Validates Ghosts that are moving vertically (track-based)
     *
     * This function's validation ensures that no Ghost's track ends up in a negative (non-existent) track.
     */
    int ValidateTrackMovement(int movement, const QVector<TimelineViewGhostItem*> ghosts);

    enum SnapPoints { kSnapToClips = 0x1, kSnapToPlayhead = 0x2, kSnapAll = 0xFF };

    /**
     * @brief Snaps point `start_point` that is moving by `movement` to currently existing clips
     */
    bool SnapPoint(QList<rational> start_times, rational* movement, int snap_points = kSnapAll);

    void InsertGapsAt(const rational& time, const rational& length, QUndoCommand* command);

    void GetGhostData(const QVector<TimelineViewGhostItem*>& ghosts, rational* earliest_point, rational* latest_point);

    void InsertGapsAtGhostDestination(const QVector<TimelineViewGhostItem*>& ghosts, QUndoCommand* command);

    QList<rational> snap_points_;

    bool dragging_;

    TimelineCoordinate drag_start_;

   private:
    TimelineWidget* parent_;
  };

  class PointerTool : public Tool {
   public:
    PointerTool(TimelineWidget* parent);

    virtual void MousePress(TimelineViewMouseEvent* event) override;
    virtual void MouseMove(TimelineViewMouseEvent* event) override;
    virtual void MouseRelease(TimelineViewMouseEvent* event) override;

    virtual void HoverMove(TimelineViewMouseEvent* event) override;

   protected:
    void SetMovementAllowed(bool allowed);
    void SetTrackMovementAllowed(bool allowed);
    void SetTrimmingAllowed(bool allowed);
    virtual void MouseReleaseInternal(TimelineViewMouseEvent* event);
    virtual rational FrameValidateInternal(rational time_movement, const QVector<TimelineViewGhostItem*>& ghosts);

    virtual void InitiateGhosts(TimelineViewBlockItem* clicked_item, Timeline::MovementMode trim_mode,
                                bool allow_gap_trimming);

    TimelineViewGhostItem* AddGhostFromBlock(Block* block, const TrackReference& track, Timeline::MovementMode mode);

    TimelineViewGhostItem* AddGhostFromNull(const rational& in, const rational& out, const TrackReference& track,
                                            Timeline::MovementMode mode);

    /**
     * @brief Validates Ghosts that are getting their in points trimmed
     *
     * Assumes ghost->data() is a Block. Ensures no Ghost's in point becomes a negative timecode. Also ensures no
     * Ghost's length becomes 0 or negative.
     */
    rational ValidateInTrimming(rational movement, const QVector<TimelineViewGhostItem*> ghosts,
                                bool prevent_overwriting);

    /**
     * @brief Validates Ghosts that are getting their out points trimmed
     *
     * Assumes ghost->data() is a Block. Ensures no Ghost's in point becomes a negative timecode. Also ensures no
     * Ghost's length becomes 0 or negative.
     */
    rational ValidateOutTrimming(rational movement, const QVector<TimelineViewGhostItem*> ghosts,
                                 bool prevent_overwriting);

    virtual void ProcessDrag(const TimelineCoordinate& mouse_pos);

   private:
    Timeline::MovementMode IsCursorInTrimHandle(TimelineViewBlockItem* block, qreal cursor_x);

    void InitiateDrag(TimelineViewMouseEvent* mouse_pos);

    void AddGhostInternal(TimelineViewGhostItem* ghost, Timeline::MovementMode mode);

    bool IsClipTrimmable(TimelineViewBlockItem* clip, const QList<TimelineViewBlockItem*>& items,
                         const Timeline::MovementMode& mode);

    TrackReference track_start_;
    bool movement_allowed_;
    bool trimming_allowed_;
    bool track_movement_allowed_;
    bool rubberband_selecting_;

    Timeline::TrackType drag_track_type_;
  };

  class ImportTool : public Tool {
   public:
    ImportTool(TimelineWidget* parent);

    virtual void DragEnter(TimelineViewMouseEvent* event) override;
    virtual void DragMove(TimelineViewMouseEvent* event) override;
    virtual void DragLeave(QDragLeaveEvent* event) override;
    virtual void DragDrop(TimelineViewMouseEvent* event) override;

    void PlaceAt(const QList<Footage*>& footage, const rational& start, bool insert);

   private:
    void FootageToGhosts(rational ghost_start, const QList<Footage*>& footage, const rational& dest_tb,
                         const int& track_start);

    void PrepGhosts(const rational& frame, const int& track_index);

    void DropGhosts(bool insert);

    QList<Footage*> dragged_footage_;

    int import_pre_buffer_;
  };

  class EditTool : public Tool {
   public:
    EditTool(TimelineWidget* parent);

    virtual void MousePress(TimelineViewMouseEvent* event) override;
    virtual void MouseMove(TimelineViewMouseEvent* event) override;
    virtual void MouseRelease(TimelineViewMouseEvent* event) override;
  };

  class RazorTool : public Tool {
   public:
    RazorTool(TimelineWidget* parent);

    virtual void MousePress(TimelineViewMouseEvent* event) override;
    virtual void MouseMove(TimelineViewMouseEvent* event) override;
    virtual void MouseRelease(TimelineViewMouseEvent* event) override;

   private:
    QVector<TrackReference> split_tracks_;
  };

  class RippleTool : public PointerTool {
   public:
    RippleTool(TimelineWidget* parent);

   protected:
    virtual void MouseReleaseInternal(TimelineViewMouseEvent* event) override;
    virtual rational FrameValidateInternal(rational time_movement,
                                           const QVector<TimelineViewGhostItem*>& ghosts) override;

    virtual void InitiateGhosts(TimelineViewBlockItem* clicked_item, Timeline::MovementMode trim_mode,
                                bool allow_gap_trimming) override;
  };

  class RollingTool : public PointerTool {
   public:
    RollingTool(TimelineWidget* parent);

   protected:
    virtual void MouseReleaseInternal(TimelineViewMouseEvent* event) override;
    virtual rational FrameValidateInternal(rational time_movement,
                                           const QVector<TimelineViewGhostItem*>& ghosts) override;

    virtual void InitiateGhosts(TimelineViewBlockItem* clicked_item, Timeline::MovementMode trim_mode,
                                bool allow_gap_trimming) override;
  };

  class SlideTool : public PointerTool {
   public:
    SlideTool(TimelineWidget* parent);

   protected:
    virtual void MouseReleaseInternal(TimelineViewMouseEvent* event) override;
    virtual rational FrameValidateInternal(rational time_movement,
                                           const QVector<TimelineViewGhostItem*>& ghosts) override;
    virtual void InitiateGhosts(TimelineViewBlockItem* clicked_item, Timeline::MovementMode trim_mode,
                                bool allow_gap_trimming) override;
  };

  class SlipTool : public PointerTool {
   public:
    SlipTool(TimelineWidget* parent);

   protected:
    virtual void ProcessDrag(const TimelineCoordinate& mouse_pos) override;
    virtual void MouseReleaseInternal(TimelineViewMouseEvent* event) override;
  };

  class ZoomTool : public Tool {
   public:
    ZoomTool(TimelineWidget* parent);

    virtual void MousePress(TimelineViewMouseEvent* event) override;
    virtual void MouseMove(TimelineViewMouseEvent* event) override;
    virtual void MouseRelease(TimelineViewMouseEvent* event) override;
  };

  class AddTool : public Tool {
   public:
    AddTool(TimelineWidget* parent);

    virtual void MousePress(TimelineViewMouseEvent* event) override;
    virtual void MouseMove(TimelineViewMouseEvent* event) override;
    virtual void MouseRelease(TimelineViewMouseEvent* event) override;

   protected:
    void MouseMoveInternal(const rational& cursor_frame, bool outwards);

    TimelineViewGhostItem* ghost_;

    rational drag_start_point_;
  };

  class TransitionTool : public AddTool {
   public:
    TransitionTool(TimelineWidget* parent);

    virtual void MousePress(TimelineViewMouseEvent* event) override;
    virtual void MouseMove(TimelineViewMouseEvent* event) override;
    virtual void MouseRelease(TimelineViewMouseEvent* event) override;

   private:
    bool dual_transition_;
  };

  void DeleteSelectedInternal(QList<Block*> blocks, bool transition_aware, bool remove_from_graph,
                              QUndoCommand* command);

  void SetBlockLinksSelected(Block* block, bool selected);

  QPoint drag_origin_;

  void StartRubberBandSelect(bool enable_selecting, bool select_links);
  void MoveRubberBandSelect(bool enable_selecting, bool select_links);
  void EndRubberBandSelect(bool enable_selecting, bool select_links);
  QRubberBand rubberband_;
  QList<QGraphicsItem*> rubberband_now_selected_;

  Tool* GetActiveTool();

  QVector<Tool*> tools_;

  ImportTool* import_tool_;

  Tool* active_tool_;

  void ClearGhosts();

  bool HasGhosts();

  QVector<TimelineViewGhostItem*> ghost_items_;

  QMap<Block*, TimelineViewBlockItem*> block_items_;

  void RippleEditTo(Timeline::MovementMode mode, bool insert_gaps);

  TrackOutput* GetTrackFromReference(const TrackReference& ref);

  QList<TimelineAndTrackView*> views_;

  TimeSlider* timecode_label_;

  int deferred_scroll_value_;

  int GetTrackY(const TrackReference& ref);
  int GetTrackHeight(const TrackReference& ref);

  void CenterOn(qreal scene_pos);

  void AddGhost(TimelineViewGhostItem* ghost);

 private slots:
  void UpdateTimelineLength(const rational& length);

  void ViewMousePressed(TimelineViewMouseEvent* event);
  void ViewMouseMoved(TimelineViewMouseEvent* event);
  void ViewMouseReleased(TimelineViewMouseEvent* event);
  void ViewMouseDoubleClicked(TimelineViewMouseEvent* event);

  void ViewDragEntered(TimelineViewMouseEvent* event);
  void ViewDragMoved(TimelineViewMouseEvent* event);
  void ViewDragLeft(QDragLeaveEvent* event);
  void ViewDragDropped(TimelineViewMouseEvent* event);

  void AddBlock(Block* block, TrackReference track);
  void RemoveBlock(Block* block);

  void AddTrack(TrackOutput* track, Timeline::TrackType type);
  void RemoveTrack(TrackOutput* track);

  void ViewSelectionChanged();

  /**
   * @brief Slot for when a Block node changes its parameters and the graphics need to update
   *
   * This slot does a static_cast on sender() to Block*, meaning all objects triggering this slot must be Blocks or
   * derivatives.
   */
  void BlockChanged();

  void PreviewUpdated();

  void UpdateHorizontalSplitters();

  void UpdateTimecodeWidthFromSplitters(QSplitter* s);

  void TrackHeightChanged(Timeline::TrackType type, int index, int height);

  void ShowContextMenu();

  void ShowSpeedDurationDialog();

  void DeferredScrollAction();
};

#endif  // TIMELINEWIDGET_H
