#ifndef MYVK_RG_RENDER_GRAPH_SCHEDULER_HPP
#define MYVK_RG_RENDER_GRAPH_SCHEDULER_HPP

#include "RenderGraphResolver.hpp"

namespace myvk_rg::_details_ {

class RenderGraphScheduler {
public:
	struct DependencyLink {
		const Input *p_input{};
		const PassBase *pass{};
		inline DependencyLink() = default;
		inline explicit DependencyLink(const RenderGraphResolver::EdgeLink &edge_link)
		    : p_input{edge_link.p_input}, pass{edge_link.pass} {}
		inline DependencyLink(const Input *p_input, const PassBase *pass) : p_input{p_input}, pass{pass} {}
	};
	struct SubpassDependency {
		const ResourceBase *resource{};
		DependencyLink from{}, to{};
	};
	struct SubpassInfo {
		struct ResourceValidation {
			const ResourceBase *resource;
			const Input *p_input;
		};
		const PassBase *pass{};
		std::vector<ResourceValidation> validate_resources;
	};
	struct PassDependency {
		const ResourceBase *resource{};
		std::vector<DependencyLink> from, to;
	};
	struct RenderPassArea {
		VkExtent2D extent{};
		uint32_t layers{};
		inline bool operator==(const RenderPassArea &r) const {
			return std::tie(extent.width, extent.height, layers) == std::tie(r.extent.width, r.extent.height, r.layers);
		}
	};
	struct RenderPassInfo {
		std::vector<SubpassDependency> subpass_dependencies;
		std::unordered_map<const ImageBase *, uint32_t> attachment_id_map;
		RenderPassArea area;
	};
	struct PassInfo {
		std::vector<SubpassInfo> subpasses;
		RenderPassInfo *p_render_pass_info{};
	};

private:
	std::vector<PassInfo> m_passes;
	std::vector<RenderPassInfo> m_render_passes;
	std::vector<PassDependency> m_pass_dependencies;

	struct RenderPassMergeInfo;
	static std::vector<RenderPassMergeInfo> _compute_pass_merge_info(const RenderGraphResolver &resolved);

	void extract_grouped_passes(const RenderGraphResolver &resolved);
	void extract_dependencies_and_resource_validations(const RenderGraphResolver &resolved);
	void sort_and_insert_image_dependencies();
	void extract_pass_attachments();
	void extract_resource_transient_info();

public:
	void Schedule(const RenderGraphResolver &resolved);

	inline uint32_t GetPassCount() const { return m_passes.size(); }
	inline const PassInfo &GetPassInfo(uint32_t pass_id) const { return m_passes[pass_id]; }
	inline static uint32_t GetPassID(const PassBase *pass) { return pass->m_scheduled_info.pass_id; }
	inline static uint32_t GetSubpassID(const PassBase *pass) { return pass->m_scheduled_info.subpass_id; }

	inline const std::vector<PassInfo> &GetPassInfos() const { return m_passes; }
	inline const std::vector<PassDependency> &GetPassDependencies() const { return m_pass_dependencies; }
};

} // namespace myvk_rg::_details_

#endif
