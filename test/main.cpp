#include <myvk/GLFWHelper.hpp>
#include <myvk/Instance.hpp>

int main() {
	auto instance = myvk::Instance::CreateWithGlfwExtensions(false);
	auto window = myvk::GLFWCreateWindow("Test", 640, 480);

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
	}

	glfwTerminate();
	return 0;
}