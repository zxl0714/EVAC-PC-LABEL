/**
 * @file        rex/ui/overlay/achievement_notification.h
 * @brief       Replaceable achievement notification dialog interface.
 *
 * @copyright   Copyright (c) 2026 Rien Gupta <rgupta9@scu.edu>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/system/achievement_manager.h>
#include <rex/ui/imgui_dialog.h>

namespace rex::ui {

class AchievementNotificationDialog : public ImGuiDialog,
    public std::enable_shared_from_this<AchievementNotificationDialog> {
 public:
  ~AchievementNotificationDialog() override = default;

  // Thread-safe implementations may receive events from guest threads.
  virtual void Push(const rex::system::AchievementEvent& event) = 0;

 protected:
  explicit AchievementNotificationDialog(ImGuiDrawer* drawer, bool initially_visible = true)
      : ImGuiDialog(drawer, false, initially_visible) {}

  // ReXApp shares ownership before registering guest-thread event callbacks.
  // A queued wake-up must not retain or dereference a destroyed dialog.
  void ShowFromAnyThread() {
    auto weak = weak_from_this();
    imgui_drawer()->app_context().CallInUIThreadDeferred([weak]() {
      if (auto dialog = weak.lock()) dialog->SetVisible(true);
    });
  }
};

}  // namespace rex::ui
