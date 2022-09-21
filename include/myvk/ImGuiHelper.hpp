#ifdef MYVK_ENABLE_IMGUI

#ifndef MYVK_IMGUI_HELPER_HPP
#define MYVK_IMGUI_HELPER_HPP

#include <imgui.h>
#include <imgui_impl_glfw.h>

namespace myvk {
inline void ImGuiInit(GLFWwindow *window) {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui_ImplGlfw_InitForVulkan(window, true);
}
inline void ImGuiNewFrame() {
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}
} // namespace myvk

#endif // MYVK_IMGUIHELPER_HPP

#endif
