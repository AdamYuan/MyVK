//
// Created by adamyuan on 2/16/24.
//

#pragma once
#ifndef MYVK_VKRUNNER_HPP
#define MYVK_VKRUNNER_HPP

#include "VkCommand.hpp"
#include "VkDescriptor.hpp"

namespace myvk_rg_executor {

struct VkRunner {
	static void Run(const myvk::Ptr<myvk::CommandBuffer> &command_buffer, const VkCommand &vk_command,
	                const VkDescriptor &vk_descriptor);
};

} // namespace myvk_rg_executor

#endif // MYVK_VKRUNNER_HPP
