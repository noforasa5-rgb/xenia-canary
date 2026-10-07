/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/x360_toast.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <locale>
#include <sstream>

#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/string.h"
#include "xenia/ui/imgui_drawer.h"

#if XE_PLATFORM_WIN32
#include <playsoundapi.h>
#endif

DEFINE_bool(logros360, true,
            "Use the Xbox 360 console style achievement toast when its "
            "resources folder is available.",
            "Logros360");
DEFINE_path(logros360_folder, "",
            "Folder with the Xbox 360 toast resources. Empty means "
            "\"logros_360\" next to the executable.",
            "Logros360");
DEFINE_double(logros360_scale, 1.0,
              "Size of the Xbox 360 toast (1.0 = same size as the console at "
              "any resolution).",
              "Logros360");
DEFINE_double(logros360_fps, 60.0,
              "Animation speed of the Xbox 360 toast in frames per second.",
              "Logros360");
DEFINE_int32(logros360_loops, 0,
             "Extra repetitions of the logo/trophy part of the toast (makes "
             "it stay longer on screen).",
             "Logros360");
DEFINE_bool(logros360_sound, true, "Play the Xbox 360 toast sound.",
            "Logros360");

namespace xe {
namespace ui {

namespace {

std::string Trim(std::string s) {
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n' ||
                        s.back() == ' ' || s.back() == '\t')) {
    s.pop_back();
  }
  size_t start = 0;
  while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) {
    ++start;
  }
  return s.substr(start);
}

void ReplaceAll(std::string& s, std::string_view from, std::string_view to) {
  if (from.empty()) {
    return;
  }
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
}

// Removes UTF-8 characters from the end until the text fits.
std::string FitText(ImFont* font, float size, std::string text,
                    float max_width) {
  if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x <= max_width) {
    return text;
  }
  const std::string ellipsis = "...";
  while (!text.empty()) {
    // Drop one UTF-8 code point.
    do {
      text.pop_back();
    } while (!text.empty() &&
             (static_cast<uint8_t>(text.back()) & 0xC0) == 0x80);
    if (!text.empty()) {
      text.pop_back();
    }
    const std::string candidate = Trim(text) + ellipsis;
    if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, candidate.c_str()).x <=
        max_width) {
      return candidate;
    }
  }
  return ellipsis;
}

ImVec2 Transform(const float* m, float x, float y, float scale, ImVec2 origin) {
  return ImVec2(origin.x + scale * (m[0] * x + m[1] * y + m[4]),
                origin.y + scale * (m[2] * x + m[3] * y + m[5]));
}

}  // namespace

std::filesystem::path GetX360ToastFolder() {
  if (!cvars::logros360_folder.empty()) {
    return cvars::logros360_folder;
  }
  return xe::filesystem::GetExecutableFolder() / "logros_360";
}

const X360ToastScene* X360ToastScene::Get() {
  static const X360ToastScene* scene = []() -> const X360ToastScene* {
    if (!cvars::logros360) {
      return nullptr;
    }
    auto* loaded = new X360ToastScene();
    if (!loaded->Load(GetX360ToastFolder())) {
      delete loaded;
      return nullptr;
    }
    return loaded;
  }();
  return scene;
}

bool X360ToastScene::Load(const std::filesystem::path& base_folder) {
  folder = base_folder;
  const auto scene_path = folder / "escena" / "escena.txt";
  std::ifstream file(scene_path);
  if (!file.is_open()) {
    XELOGI("Logros360: {} not found, using the default achievement toast.",
           xe::path_to_utf8(scene_path));
    return false;
  }

  std::string line;
  std::getline(file, line);
  if (Trim(line) != "LOGROS360 1") {
    XELOGE("Logros360: unknown scene format in {}",
           xe::path_to_utf8(scene_path));
    return false;
  }

  while (std::getline(file, line)) {
    std::istringstream ss(line);
    ss.imbue(std::locale::classic());
    std::string key;
    ss >> key;
    if (key == "text_color") {
      std::string hex;
      ss >> hex;
      text_color = static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
    } else if (key == "text_point_size") {
      ss >> text_point_size;
    } else if (key == "text_line_adjust") {
      ss >> text_line_adjust;
    } else if (key == "text_em_to_line") {
      ss >> text_em_to_line;
    } else if (key == "bbox") {
      ss >> bbox[0] >> bbox[1] >> bbox[2] >> bbox[3];
    } else if (key == "named") {
      std::string name;
      uint32_t value;
      while (ss >> name >> value) {
        if (name == "loop") {
          frame_loop = value;
        } else if (name == "end_in") {
          frame_end_in = value;
        } else if (name == "out") {
          frame_out = value;
        } else if (name == "end") {
          frame_end = value;
        }
      }
    } else if (key == "item") {
      uint32_t index;
      Item item;
      ss >> index >> item.kind >> item.width >> item.height >> item.texture;
      if (index != items.size()) {
        return false;
      }
      items.push_back(item);
    } else if (key == "f") {
      uint32_t frame_index, count;
      ss >> frame_index >> count;
      if (frame_index != frames.size()) {
        return false;
      }
      std::vector<Instance> instances(count);
      for (auto& instance : instances) {
        if (!std::getline(file, line)) {
          return false;
        }
        std::istringstream fs(line);
        fs.imbue(std::locale::classic());
        fs >> instance.item >> instance.opacity;
        for (float& v : instance.m) {
          fs >> v;
        }
        if (fs.fail() || instance.item >= items.size()) {
          return false;
        }
      }
      frames.push_back(std::move(instances));
    }
  }

  if (frames.empty() || frame_end >= frames.size() ||
      frame_end_in >= frames.size() || frame_loop >= frame_end_in ||
      frame_out > frame_end) {
    XELOGE("Logros360: incomplete scene in {}", xe::path_to_utf8(scene_path));
    return false;
  }

  // Localized strings (optional).
  std::ifstream texts(folder / "textos.txt");
  if (texts.is_open()) {
    bool first_line = true;
    while (std::getline(texts, line)) {
      if (first_line && line.size() >= 3 &&
          line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        line = line.substr(3);
      }
      first_line = false;
      line = Trim(line);
      const size_t eq = line.find('=');
      if (line.empty() || line[0] == '#' || eq == std::string::npos) {
        continue;
      }
      const std::string name = Trim(line.substr(0, eq));
      const std::string value = Trim(line.substr(eq + 1));
      if (name == "titulo" || name == "title") {
        title_text = value;
      } else if (name == "descripcion" || name == "description") {
        description_format = value;
      }
    }
  }

  XELOGI("Logros360: Xbox 360 achievement toast loaded from {}",
         xe::path_to_utf8(folder));
  return true;
}

std::string X360ToastScene::FormatDescription(uint32_t gamerscore,
                                              std::string_view name) const {
  std::string result = description_format;
  ReplaceAll(result, "{gamerscore}", std::to_string(gamerscore));
  ReplaceAll(result, "{name}", name);
  return result;
}

double X360AchievementToast::GetScenePosition(const X360ToastScene& scene,
                                              double t) const {
  const double intro = scene.frame_end_in;
  const double loop_length = double(scene.frame_end_in - scene.frame_loop);
  const double loops =
      double(std::max(0, int(cvars::logros360_loops))) * loop_length;
  if (t < intro) {
    return t;
  }
  if (t < intro + loops) {
    return scene.frame_loop + std::fmod(t - intro, loop_length);
  }
  const double out = t - intro - loops;
  if (out < 1.0) {
    return scene.frame_end_in;
  }
  const double position = scene.frame_out + (out - 1.0);
  if (position > scene.frame_end) {
    return -1.0;
  }
  return position;
}

void X360AchievementToast::OnDraw(ImGuiIO& io) {
  const X360ToastScene* scene = X360ToastScene::Get();
  if (!scene) {
    delete this;
    return;
  }

  const uint64_t now = Clock::QueryHostUptimeMillis();
  if (!start_time_) {
    start_time_ = now;
#if XE_PLATFORM_WIN32
    if (cvars::logros360_sound) {
      const auto sound_path = scene->folder / "NotifyPopup.wav";
      if (std::filesystem::exists(sound_path)) {
        PlaySoundW(sound_path.c_str(), NULL,
                   SND_FILENAME | SND_NODEFAULT | SND_ASYNC);
      }
    }
#endif
  }

  const double fps = std::max(1.0, double(cvars::logros360_fps));
  const double t = double(now - start_time_) * fps / 1000.0;
  const double position = GetScenePosition(*scene, t);
  if (position < 0.0) {
    delete this;
    return;
  }

  const uint32_t frame = std::min(uint32_t(position), scene->frame_end);
  const float frac = float(position - double(frame));
  // Interpolate only inside the same segment of the timeline.
  const bool can_blend =
      frame + 1 <= scene->frame_end && frame != scene->frame_end_in;
  const auto& current = scene->frames[frame];
  const std::vector<X360ToastScene::Instance>* next =
      can_blend ? &scene->frames[frame + 1] : nullptr;

  const ImVec2 screen_size = io.DisplaySize;
  const float scale = std::max(0.1f, float(cvars::logros360_scale)) *
                      (screen_size.y / default_drawing_resolution.y);
  const ImVec2 toast_size((scene->bbox[2] - scene->bbox[0]) * scale,
                          (scene->bbox[3] - scene->bbox[1]) * scale);
  ImVec2 top_left = CalculateNotificationScreenPosition(screen_size, toast_size,
                                                        GetPositionId());
  if (std::isnan(top_left.x) || std::isnan(top_left.y)) {
    top_left = CalculateNotificationScreenPosition(screen_size, toast_size, 2);
    if (std::isnan(top_left.x) || std::isnan(top_left.y)) {
      return;
    }
  }
  const ImVec2 origin(top_left.x - scene->bbox[0] * scale,
                      top_left.y - scene->bbox[1] * scale);

  const std::string player_indicator =
      "ind" + std::to_string(std::min<uint32_t>(GetUserIndex(), 3) + 1);

  ImDrawList* draw_list = ImGui::GetForegroundDrawList();
  ImGuiDrawer* drawer = GetDrawer();

  for (const auto& instance : current) {
    const auto& item = scene->items[instance.item];
    if (item.kind.compare(0, 3, "ind") == 0 && item.kind != player_indicator) {
      continue;
    }

    float m[6];
    float opacity = instance.opacity;
    std::copy(std::begin(instance.m), std::end(instance.m), m);
    if (next && frac > 0.0f) {
      for (const auto& other : *next) {
        if (other.item == instance.item) {
          for (int i = 0; i < 6; ++i) {
            m[i] += (other.m[i] - m[i]) * frac;
          }
          opacity += (other.opacity - opacity) * frac;
          break;
        }
      }
    }
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    if (opacity <= 0.002f) {
      continue;
    }

    if (item.kind == "text") {
      DrawToastText(*scene, draw_list, item, m, opacity, scale, origin);
      continue;
    }

    std::filesystem::path texture_path;
    if (item.kind == "logo") {
      texture_path = scene->folder / "xenonLogo.png";
    } else if (item.kind == "icon") {
      texture_path = scene->folder / "Achievement.png";
    } else if (item.texture != "-") {
      texture_path = scene->folder / "escena" / item.texture;
    } else {
      continue;
    }
    ImmediateTexture* texture = drawer->GetFileTexture(texture_path);
    if (!texture) {
      continue;
    }

    const ImU32 color =
        IM_COL32(255, 255, 255, int(std::lround(opacity * 255.0f)));
    draw_list->AddImageQuad(
        reinterpret_cast<ImTextureID>(texture),
        Transform(m, 0.0f, 0.0f, scale, origin),
        Transform(m, item.width, 0.0f, scale, origin),
        Transform(m, item.width, item.height, scale, origin),
        Transform(m, 0.0f, item.height, scale, origin), ImVec2(0, 0),
        ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), color);
  }
}

void X360AchievementToast::DrawToastText(const X360ToastScene& scene,
                                         ImDrawList* draw_list,
                                         const X360ToastScene::Item& item,
                                         const float* m, float opacity,
                                         float scale, ImVec2 origin) {
  ImFont* font = GetDrawer()->GetX360Font();
  if (!font) {
    font = ImGui::GetIO().Fonts->Fonts[0];
  }
  const float element_scale =
      scale * std::sqrt(std::abs(m[0] * m[3] - m[1] * m[2]));
  // XUI point size -> pixels (96 dpi) -> Dear ImGui size (line height).
  const float em = scene.text_point_size * (96.0f / 72.0f);
  const float font_size = em * scene.text_em_to_line * element_scale;
  const float line_height =
      (em * scene.text_em_to_line + scene.text_line_adjust) * element_scale;
  const float max_width = item.width * element_scale;

  const std::string lines[2] = {
      FitText(font, font_size, std::string(GetTitle()), max_width),
      FitText(font, font_size, std::string(GetDescription()), max_width)};
  const int line_count = GetDescription().empty() ? 1 : 2;

  const ImVec2 box = Transform(m, 0.0f, 0.0f, scale, origin);
  float y =
      box.y + (item.height * element_scale - line_height * line_count) * 0.5f;
  const uint32_t c = scene.text_color;
  const ImU32 color = IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF,
                               int(std::lround(((c >> 24) & 0xFF) * opacity)));
  for (int i = 0; i < line_count; ++i) {
    draw_list->AddText(font, font_size,
                       ImVec2(std::floor(box.x), std::floor(y)), color,
                       lines[i].c_str());
    y += line_height;
  }
}

}  // namespace ui
}  // namespace xe
