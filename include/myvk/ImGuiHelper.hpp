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
} // namespace myvk

#endif // MYVK_IMGUIHELPER_HPP

#endif
