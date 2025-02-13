// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_AURA_NATIVE_WINDOW_OCCLUSION_TRACKER_WIN_H_
#define UI_AURA_NATIVE_WINDOW_OCCLUSION_TRACKER_WIN_H_

#include <windows.h>
#include <winuser.h>

#include <memory>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "ui/aura/aura_export.h"
#include "ui/aura/window.h"
#include "ui/aura/window_observer.h"
#include "ui/base/win/session_change_observer.h"

namespace aura {

// This class keeps track of whether any HWNDs are occluding any app windows.
// It notifies the host of any app window whose occlusion state changes. Most
// code should not need to use this; it's an implementation detail.
class AURA_EXPORT NativeWindowOcclusionTrackerWin : public WindowObserver {
 public:
  static NativeWindowOcclusionTrackerWin* GetOrCreateInstance();

  // Enables notifying the host of |window| via SetNativeWindowOcclusionState()
  // when the occlusion state has been computed.
  void Enable(Window* window);

  // Disables notifying the host of |window| via
  // OnNativeWindowOcclusionStateChanged() when the occlusion state has been
  // computed. It's not neccesary to call this when |window| is deleted because
  // OnWindowDestroying calls Disable.
  void Disable(Window* window);

  // aura::WindowObserver:
  void OnWindowVisibilityChanged(Window* window, bool visible) override;
  void OnWindowDestroying(Window* window) override;

 private:
  friend class NativeWindowOcclusionTrackerTest;

  // This class computes the occlusion state of the tracked windows.
  // It runs on a separate thread, and notifies the main thread of
  // the occlusion state of the tracked windows.
  class WindowOcclusionCalculator {
   public:
    WindowOcclusionCalculator(
        scoped_refptr<base::SequencedTaskRunner> task_runner,
        scoped_refptr<base::SequencedTaskRunner> ui_thread_task_runner);
    ~WindowOcclusionCalculator();

    void EnableOcclusionTrackingForWindow(HWND hwnd);
    void DisableOcclusionTrackingForWindow(HWND hwnd);

    // If a window becomes visible, makes sure event hooks are registered.
    void HandleVisibilityChanged(bool visible);

   private:
    friend class NativeWindowOcclusionTrackerTest;
    struct NativeWindowOcclusionState {
      // The region of the native window that is not occluded by other windows.
      SkRegion unoccluded_region;

      // The current occlusion state of the native window. Default to UNKNOWN
      // because we do not know the state starting out. More information on
      // these states can be found in aura::Window.
      aura::Window::OcclusionState occlusion_state =
          aura::Window::OcclusionState::UNKNOWN;
    };

    // Registers event hooks, if not registered.
    void MaybeRegisterEventHooks();

    // This is the callback registered to get notified of various Windows
    // events, like window moving/resizing.
    static void CALLBACK EventHookCallback(HWINEVENTHOOK hWinEventHook,
                                           DWORD event,
                                           HWND hwnd,
                                           LONG idObject,
                                           LONG idChild,
                                           DWORD dwEventThread,
                                           DWORD dwmsEventTime);

    // EnumWindows callback used to iterate over all hwnds to determine
    // occlusion status of all tracked root windows.  Also builds up
    // |current_pids_with_visible_windows_| and registers event hooks for newly
    // discovered processes with visible hwnds.
    static BOOL CALLBACK
    ComputeNativeWindowOcclusionStatusCallback(HWND hwnd, LPARAM lParam);

    // EnumWindows callback used to update the list of process ids with
    // visible hwnds, |pids_for_location_change_hook_|.
    static BOOL CALLBACK UpdateVisibleWindowProcessIdsCallback(HWND hwnd,
                                                               LPARAM lParam);

    // Determines which processes owning visible application windows to set the
    // EVENT_OBJECT_LOCATIONCHANGE event hook for and stores the pids in
    // |pids_for_location_change_hook_|.
    void UpdateVisibleWindowProcessIds();

    // Computes the native window occlusion status for all tracked root aura
    // windows in |root_window_hwnds_occlusion_state_| and notifies them if
    // their occlusion status has changed.
    void ComputeNativeWindowOcclusionStatus();

    // Schedules an occlusion calculation |update_occlusion_delay_| time in the
    // future, if one isn't already scheduled.
    void ScheduleOcclusionCalculationIfNeeded();

    // Registers a global event hook (not per process) for the events in the
    // range from |event_min| to |event_max|, inclusive.
    void RegisterGlobalEventHook(UINT event_min, UINT event_max);

    // Registers the EVENT_OBJECT_LOCATIONCHANGE event hook for the process with
    // passed id. The process has one or more visible, opaque windows.
    void RegisterEventHookForProcess(DWORD pid);

    // Registers/Unregisters the event hooks necessary for occlusion tracking
    // via calls to RegisterEventHook. These event hooks are disabled when all
    // tracked windows are minimized.
    void RegisterEventHooks();
    void UnregisterEventHooks();

    // EnumWindows callback for occlusion calculation. Returns true to
    // continue enumeration, false otherwise. Currently, always returns
    // true because this function also updates
    // |current_pids_with_visible_windows|, and needs to see all HWNDs.
    bool ProcessComputeNativeWindowOcclusionStatusCallback(
        HWND hwnd,
        base::flat_set<DWORD>* current_pids_with_visible_windows);

    // Processes events sent to OcclusionEventHookCallback.
    // It generally triggers scheduling of the occlusion calculation, but
    // ignores certain events in order to not calculate occlusion more than
    // necessary.
    void ProcessEventHookCallback(DWORD event,
                                  HWND hwnd,
                                  LONG idObject,
                                  LONG idChild);

    // EnumWindows callback for determining which processes to set the
    // EVENT_OBJECT_LOCATIONCHANGE event hook for. We set that event hook for
    // processes hosting fully visible, opaque windows.
    void ProcessUpdateVisibleWindowProcessIdsCallback(HWND hwnd);

    // Task runner for our thread.
    scoped_refptr<base::SequencedTaskRunner> task_runner_;

    // Task runner for the thread that created |this|.  UpdateOcclusionState
    // task is posted to this task runner.
    const scoped_refptr<base::SequencedTaskRunner> ui_thread_task_runner_;

    // Map of root app window hwnds and their occlusion state. This contains
    // both visible and hidden windows.
    base::flat_map<HWND, NativeWindowOcclusionState>
        root_window_hwnds_occlusion_state_;

    // Values returned by SetWinEventHook are stored so that hooks can be
    // unregistered when necessary.
    std::vector<HWINEVENTHOOK> global_event_hooks_;

    // Map from process id to EVENT_OBJECT_LOCATIONCHANGE event hook.
    base::flat_map<DWORD, HWINEVENTHOOK> process_event_hooks_;

    // Pids of processes for which the EVENT_OBJECT_LOCATIONCHANGE event hook is
    // set. These are the processes hosting windows in
    // |visible_and_fully_opaque_windows_|.
    base::flat_set<DWORD> pids_for_location_change_hook_;

    // Timer to delay occlusion update.
    base::OneShotTimer occlusion_update_timer_;

    // Used to keep track of whether we're in the middle of getting window move
    // events, in order to wait until the window move is complete before
    // calculating window occlusion.
    bool window_is_moving_ = false;

    SEQUENCE_CHECKER(sequence_checker_);

    DISALLOW_COPY_AND_ASSIGN(WindowOcclusionCalculator);
  };

  NativeWindowOcclusionTrackerWin();
  ~NativeWindowOcclusionTrackerWin() override;

  // Returns true if we are interested in |hwnd| for purposes of occlusion
  // calculation. We are interested in |hwnd| if it is a window that is visible,
  // opaque, and bounded. If we are interested in |hwnd|, stores the window
  // rectangle in |window_rect|.
  static bool IsWindowVisibleAndFullyOpaque(HWND hwnd, gfx::Rect* window_rect);

  // Updates root windows occclusion state.
  void UpdateOcclusionState(const base::flat_map<HWND, Window::OcclusionState>&
                                root_window_hwnds_occlusion_state);

  // This is called with session changed notifications. If the screen is locked,
  // it marks app windows as occluded.
  void OnSessionChange(WPARAM status_code);

  // Task runner to call ComputeNativeWindowOcclusionStatus, and to handle
  // Windows event notifications, off of the UI thread.
  const scoped_refptr<base::SequencedTaskRunner> update_occlusion_task_runner_;

  // Map of HWND to root app windows. Maintained on the UI thread, and used
  // to send occlusion state notifications to Windows from
  // |root_window_hwnds_occlusion_state_|.
  base::flat_map<HWND, Window*> hwnd_root_window_map_;

  // This is set by UpdateOcclusionState. It is currently only used by tests.
  int num_visible_root_windows_ = 0;

  std::unique_ptr<WindowOcclusionCalculator> occlusion_calculator_;

  // Manages observation of Windows Session Change messages.
  ui::SessionChangeObserver session_change_observer_;

  DISALLOW_COPY_AND_ASSIGN(NativeWindowOcclusionTrackerWin);
};

}  // namespace aura

#endif  // UI_AURA_NATIVE_WINDOW_OCCLUSION_TRACKER_WIN_H_
