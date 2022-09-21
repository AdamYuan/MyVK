#ifdef MYVK_ENABLE_GLFW

#ifndef MYVK_SURFACE_HPP
#define MYVK_SURFACE_HPP

#include "volk.h"

#include "GLFW/glfw3.h"
#include "Instance.hpp"
#include <memory>

namespace myvk {
class Surface {
private:
	Ptr<Instance> m_instance_ptr;
	GLFWwindow *m_window{nullptr};

	VkSurfaceKHR m_surface{VK_NULL_HANDLE};

public:
	static Ptr<Surface> Create(const Ptr<Instance> &instance, GLFWwindow *window);

	VkSurfaceKHR GetHandle() const { return m_surface; }

	GLFWwindow *GetGlfwWindow() const { return m_window; }

	~Surface();
};
} // namespace myvk

#endif

#endif
