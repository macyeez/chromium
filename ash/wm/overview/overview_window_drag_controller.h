// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_OVERVIEW_OVERVIEW_WINDOW_DRAG_CONTROLLER_H_
#define ASH_WM_OVERVIEW_OVERVIEW_WINDOW_DRAG_CONTROLLER_H_

#include <memory>

#include "ash/ash_export.h"
#include "ash/wm/splitview/split_view_controller.h"
#include "base/macros.h"
#include "ui/gfx/geometry/point_f.h"

namespace ash {

class OverviewItem;
class OverviewSession;

// The drag controller for an overview window item in overview mode. It updates
// the position of the corresponding window item using transform while dragging.
// It also updates the split view drag indicators, which handles showing
// indicators where to drag, and preview areas showing the bounds of the
// window about to be snapped.
class ASH_EXPORT OverviewWindowDragController {
 public:
  enum class DragBehavior {
    kNoDrag,       // No drag has started.
    kUndefined,    // Drag has started, but it is undecided whether we want to
                   // drag to snap or drag to close yet.
    kDragToSnap,   // On drag complete, the window will be snapped, if it meets
                   // requirements.
    kDragToClose,  // On drag complete, the window will be closed, if it meets
                   // requirements.
  };

  explicit OverviewWindowDragController(OverviewSession* overview_session);
  ~OverviewWindowDragController();

  void InitiateDrag(OverviewItem* item, const gfx::PointF& location_in_screen);
  void Drag(const gfx::PointF& location_in_screen);
  void CompleteDrag(const gfx::PointF& location_in_screen);
  void StartSplitViewDragMode(const gfx::PointF& location_in_screen);
  void Fling(const gfx::PointF& location_in_screen,
             float velocity_x,
             float velocity_y);
  void ActivateDraggedWindow();
  void ResetGesture();

  // Resets |overview_session_| to nullptr. It's needed since we defer the
  // deletion of OverviewWindowDragController in Overview destructor and
  // we need to reset |overview_session_| to nullptr to avoid null pointer
  // dereference.
  void ResetOverviewSession();

  OverviewItem* item() { return item_; }

  DragBehavior current_drag_behavior() { return current_drag_behavior_; }

 private:
  // Updates visuals for the user while dragging items around.
  void UpdateDragIndicatorsAndOverviewGrid(
      const gfx::PointF& location_in_screen);

  // Dragged items should not attempt to update the indicators or snap if
  // the drag started in a snap region and has not been dragged pass the
  // threshold.
  bool ShouldUpdateDragIndicatorsOrSnap(const gfx::PointF& event_location);

  SplitViewController::SnapPosition GetSnapPosition(
      const gfx::PointF& location_in_screen) const;

  // Returns the expected window grid bounds based on |snap_position|.
  gfx::Rect GetGridBounds(SplitViewController::SnapPosition snap_position);

  void SnapWindow(SplitViewController::SnapPosition snap_position);

  OverviewSession* overview_session_;

  SplitViewController* split_view_controller_;

  // The drag target window in the overview mode.
  OverviewItem* item_ = nullptr;

  DragBehavior current_drag_behavior_ = DragBehavior::kNoDrag;

  // The location of the initial mouse/touch/gesture event in screen.
  gfx::PointF initial_event_location_;

  // Stores the bounds of |item_| when a drag is started. Used to calculate the
  // new bounds on a drag event.
  gfx::PointF initial_centerpoint_;

  // False if the initial drag location was not a snap region, or if it was in
  // a snap region but the drag has since moved out.
  bool started_in_snap_region_ = false;

  // The opacity of |item_| changes if we are in drag to close mode. Store the
  // orginal opacity of |item_| and restore it to the item when we leave drag
  // to close mode.
  float original_opacity_ = 1.f;

  // Set to true once the bounds of |item_| change.
  bool did_move_ = false;

  SplitViewController::SnapPosition snap_position_ = SplitViewController::NONE;

  DISALLOW_COPY_AND_ASSIGN(OverviewWindowDragController);
};

}  // namespace ash

#endif  // ASH_WM_OVERVIEW_OVERVIEW_WINDOW_DRAG_CONTROLLER_H_
