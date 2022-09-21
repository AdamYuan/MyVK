#ifdef MYVK_ENABLE_GLFW

#ifndef MYVK_GLFW_HELPER_HPP
#define MYVK_GLFW_HELPER_HPP

#include <GLFW/glfw3.h>

namespace myvk {
inline GLFWwindow *GLFWCreateWindow(const char *title, uint32_t width, uint32_t height) {
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	return glfwCreateWindow(width, height, title, nullptr, nullptr);
}
} // namespace myvk

#endif

#endif
