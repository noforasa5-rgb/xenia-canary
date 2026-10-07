/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_X360_TOAST_H_
#define XENIA_UI_X360_TOAST_H_

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "xenia/ui/imgui_guest_notification.h"

// Console-style ("Xbox 360 dashboard") achievement toast.
//
// Nothing from the console is bundled with Xenia. The toast is only enabled
// when the user provides a folder (by default "logros_360" next to the
// executable) with resources extracted from their own console:
//   escena/escena.txt   - flattened animation of the notification scene
//   escena/fig_XX.png   - pre-rendered gradient figures of the scene
//   xenonLogo.png       - Xbox logo
//   Achievement.png     - achievement (trophy) icon
//   NotifyPopup.wav     - popup sound (optional)
//   XboxTC.ttf          - dashboard font (optional)
//   textos.txt          - localized strings (optional), UTF-8:
//                           titulo=Achievement unlocked
//                           descripcion={gamerscore}G - {name}

namespace xe {
namespace ui {

std::filesystem::path GetX360ToastFolder();

struct X360ToastScene {
  struct Item {
    std::string kind;
    float width = 0.0f;
    float height = 0.0f;
    std::string texture;
  };

  struct Instance {
    uint32_t item = 0;
    float opacity = 1.0f;
    // Affine transform: x' = m[0] * x + m[1] * y + m[4],
    //                   y' = m[2] * x + m[3] * y + m[5].
    float m[6] = {};
  };

  std::vector<Item> items;
  std::vector<std::vector<Instance>> frames;

  uint32_t text_color = 0xFFEBEBEB;
  float text_point_size = 11.0f;
  float text_line_adjust = -2.0f;
  float text_em_to_line = 1.2134f;
  float bbox[4] = {0.0f, 0.0f, 1.0f, 1.0f};

  uint32_t frame_loop = 65;
  uint32_t frame_end_in = 185;
  uint32_t frame_out = 186;
  uint32_t frame_end = 240;

  std::string title_text = "Achievement unlocked";
  std::string description_format = "{gamerscore}G - {name}";

  std::filesystem::path folder;

  // Returns nullptr when the toast is disabled or resources are missing.
  static const X360ToastScene* Get();

  std::string FormatDescription(uint32_t gamerscore,
                                std::string_view name) const;

 private:
  bool Load(const std::filesystem::path& base_folder);
};

class X360AchievementToast final : public ImGuiGuestNotification {
 public:
  X360AchievementToast(ui::ImGuiDrawer* imgui_drawer, std::string title,
                       std::string description, uint8_t user_index,
                       uint8_t position_id = 2)
      : ImGuiGuestNotification(imgui_drawer, title, description, user_index,
                               position_id) {}

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  // Returns the scene frame position for the given time, or a negative value
  // once the animation has finished.
  double GetScenePosition(const X360ToastScene& scene, double frames) const;
  void DrawToastText(const X360ToastScene& scene, ImDrawList* draw_list,
                     const X360ToastScene::Item& item, const float* m,
                     float opacity, float scale, ImVec2 origin);

  uint64_t start_time_ = 0;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_X360_TOAST_H_
