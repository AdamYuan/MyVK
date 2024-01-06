cmake_minimum_required(VERSION 3.15)
project(MyVK)

option(MYVK_TESTING "Testing" ON)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(dep)

add_library(MyVK_Vulkan STATIC
        src/ImageBase.cpp
        src/Image.cpp
        src/BufferBase.cpp
        src/Buffer.cpp
        src/CommandBuffer.cpp
        src/CommandPool.cpp
        src/Device.cpp
        src/Instance.cpp
        src/PhysicalDevice.cpp
        src/Queue.cpp
        src/QueueSelector.cpp
        src/Fence.cpp
        src/Semaphore.cpp
        src/ImageView.cpp
        src/RenderPass.cpp
        src/PipelineBase.cpp
        src/PipelineLayout.cpp
        src/DescriptorSetLayout.cpp
        src/ShaderModule.cpp
        src/GraphicsPipeline.cpp
        src/ComputePipeline.cpp
        src/Framebuffer.cpp
        src/DescriptorPool.cpp
        src/DescriptorSet.cpp
        src/Sampler.cpp
        src/ObjectTracker.cpp
        src/QueryPool.cpp
        src/FramebufferBase.cpp
        src/ImagelessFramebuffer.cpp
)
add_library(myvk::vulkan ALIAS MyVK_Vulkan)
target_include_directories(MyVK_Vulkan PUBLIC include)
target_link_libraries(MyVK_Vulkan PUBLIC MyVK_Dep_Vulkan)

add_library(MyVK_RenderGraph STATIC
        src/rg/interface/Alias.cpp
        src/rg/interface/Resource.cpp
        src/rg/interface/Input.cpp
        src/rg/interface/Object.cpp

        src/rg/executor/default/Executor.cpp
        src/rg/executor/default/Collection.cpp
        src/rg/executor/default/Graph.cpp
        # src/rg/executor/default/RenderGraphAllocator.cpp
        # src/rg/executor/default/RenderGraphExecutor.cpp
        # src/rg/executor/default/RenderGraphScheduler.cpp
        # src/rg/executor/default/RenderGraphDescriptor.cpp
        # src/rg/executor/default/RenderGraphLFInit.cpp
        # src/rg/executor/default/RenderGraph.cpp
)
add_library(myvk::rg ALIAS MyVK_RenderGraph)
target_compile_definitions(MyVK_RenderGraph PUBLIC MYVK_ENABLE_RG)
target_link_libraries(MyVK_RenderGraph PUBLIC MyVK_Vulkan)
if (MYVK_TESTING)
    target_compile_definitions(MyVK_RenderGraph PUBLIC MYVK_RG_DEBUG)
endif ()

add_library(MyVK_GLFW STATIC
        src/glfw/Surface.cpp
        src/glfw/Swapchain.cpp
        src/glfw/SwapchainImage.cpp
        src/glfw/FrameManager.cpp
        src/glfw/Instance.cpp
        src/glfw/PhysicalDevice.cpp
        src/glfw/ImageView.cpp
        src/glfw/QueueSelector.cpp
)
add_library(myvk::glfw ALIAS MyVK_GLFW)
target_compile_definitions(MyVK_GLFW PUBLIC MYVK_ENABLE_GLFW)
target_link_libraries(MyVK_GLFW PUBLIC MyVK_Vulkan MyVK_Dep_GLFW)

add_library(MyVK_ImGui STATIC
        src/imgui/ImGuiHelper.cpp
        src/imgui/ImGuiRenderer.cpp
)
add_library(myvk::imgui ALIAS MyVK_ImGui)
target_compile_definitions(MyVK_ImGui PUBLIC MYVK_ENABLE_IMGUI)
target_link_libraries(MyVK_ImGui PUBLIC MyVK_Vulkan MyVK_GLFW MyVK_Dep_ImGui)

# Testings
if (MYVK_TESTING)
    add_executable(MyVK_TestWindow test/window/main.cpp)
    target_link_libraries(MyVK_TestWindow PRIVATE myvk::vulkan myvk::glfw myvk::imgui)

    add_executable(MyVK_TestRenderGraph test/render_graph/main.cpp)
    target_link_libraries(MyVK_TestRenderGraph PRIVATE myvk::vulkan myvk::glfw myvk::imgui myvk::rg)

    add_executable(MyVK_TestRGUnit test/rg_unit/main.cpp)
    target_link_libraries(MyVK_TestRGUnit PRIVATE myvk::vulkan myvk::rg)

    add_executable(MyVK_TestOffscreen test/offscreen/main.cpp)
    target_link_libraries(MyVK_TestOffscreen PRIVATE myvk::vulkan)
endif ()

