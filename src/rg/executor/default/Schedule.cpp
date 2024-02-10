//
// Created by adamyuan on 2/4/24.
//

#include "Schedule.hpp"

namespace default_executor {

Schedule Schedule::Create(const Args &args) {
	Schedule s = {};
	s.fetch_render_areas(args);
	s.group_passes(args);

	return s;
}

void Schedule::fetch_render_areas(const Args &args) {
	auto graphics_pass_visitor = [&](const GraphicsPassBase *p_graphics_pass) {
		const auto &opt_area = p_graphics_pass->GetOptRenderArea();
		if (opt_area) {
			get_sched_info(p_graphics_pass).render_area =
			    std::visit(overloaded(
			                   [&](const std::invocable<VkExtent2D> auto &area_func) {
				                   return area_func(args.render_graph.GetCanvasSize());
			                   },
			                   [](const RenderPassArea &area) { return area; }),
			               *opt_area);
		} else {
			// Loop through Pass Inputs and find the largest attachment

		}
	};
	for (const PassBase *p_pass : args.dependency.GetTopoIDPasses())
		p_pass->Visit(overloaded(graphics_pass_visitor, [](auto &&) {}));
}
void Schedule::group_passes(const Args &args) {}

} // namespace default_executor