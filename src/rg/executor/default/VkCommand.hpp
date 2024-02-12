//
// Created by adamyuan on 2/12/24.
//

#pragma once
#ifndef MYVK_RG_DEF_EXE_VKCOMMAND_HPP
#define MYVK_RG_DEF_EXE_VKCOMMAND_HPP

#include "VkAllocation.hpp"

namespace default_executor {

class VkCommand {
private:
	struct Args {
		const RenderGraphBase &render_graph;
		const Collection &collection;
		const Dependency &dependency;
		const Metadata &metadata;
		const VkAllocation &vk_allocation;
	};

public:
	static VkCommand Create(const myvk::Ptr<myvk::Device> &device_ptr, const Args &args);
};

} // namespace default_executor

#endif
