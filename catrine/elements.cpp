#include "elements.h"

struct tab_element {
  float element_opacity;
};

bool elements::tab(const char *name, bool boolean, float width) {
  ImGuiWindow *window = ImGui::GetCurrentWindow();
  if (window->SkipItems)
    return false;

  ImGuiContext &g = *GImGui;
  const ImGuiStyle &style = g.Style;
  const ImGuiID id = window->GetID(name);
  const ImVec2 label_size = ImGui::CalcTextSize(name, NULL, true);
  ImVec2 pos = ImGui::GetCursorScreenPos();

  const float horizontal_padding = 12.0f;
  const float vpad = style.FramePadding.y;
  float tab_width =
      (width > 0.0f) ? width : (label_size.x + horizontal_padding * 2);

  const ImRect rect(
      ImVec2(pos.x, pos.y),
      ImVec2(pos.x + tab_width, pos.y + label_size.y + vpad * 2));
  ImGui::ItemSize(rect.GetSize());
  if (!ImGui::ItemAdd(rect, id))
    return false;

  bool hovered, held;
  bool pressed = ImGui::ButtonBehavior(rect, id, &hovered, &held, NULL);

  static std::map<ImGuiID, tab_element> anim;
  auto it_anim = anim.find(id);
  if (it_anim == anim.end()) {
    anim.insert({id, {0.0f}});
    it_anim = anim.find(id);
  }

  it_anim->second.element_opacity =
      ImLerp(it_anim->second.element_opacity,
             (boolean   ? 0.8f
              : hovered ? 0.6f
                        : 0.4f),
             0.07f * (1.0f - ImGui::GetIO().DeltaTime));

  if (boolean || hovered) {
    float bg_alpha = boolean ? 0.12f : 0.06f;
    window->DrawList->AddRectFilled(
        rect.Min, rect.Max,
        ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, bg_alpha)),
        style.FrameRounding);
  }

  ImGui::PushStyleColor(
      ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, it_anim->second.element_opacity));
  float text_offset_x = (tab_width - label_size.x) * 0.5f;
  ImVec2 text_pos(rect.Min.x + text_offset_x, rect.Min.y + vpad);
  window->DrawList->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), name);
  ImGui::PopStyleColor();

  return pressed;
}
