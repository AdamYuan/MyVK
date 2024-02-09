//
// Created by adamyuan on 2/4/24.
//

#pragma once
#ifndef MYVK_SCHEDULE_HPP
#define MYVK_SCHEDULE_HPP

#include "Collection.hpp"
#include "Dependency.hpp"

namespace default_executor {

class Schedule {
private:
	struct Args {
		const RenderGraphBase &render_graph;
		const Collection &collection;
		const Dependency &dependency;
	};

public:
	struct PassCluster {
		std::vector<const PassBase *> subpasses;
	};
};

} // namespace default_executor

#endif // MYVK_SCHEDULE_HPP
