//
// Created by adamyuan on 2/12/24.
//

#include "VkCommand.hpp"

#include <cassert>

namespace default_executor {

struct State {
	VkPipelineStageFlags2 stage_mask;
	VkAccessFlags2 access_mask;
	VkImageLayout layout;

	inline State &operator|=(const State &r) {
		stage_mask |= r.stage_mask;
		access_mask |= r.access_mask;
		assert(!layout || layout == r.layout);
		layout = r.layout;
		return *this;
	}
};
struct Barrier {
	VkPipelineStageFlags2 src_stage_mask;
	VkPipelineStageFlags2 dst_stage_mask;
	VkAccessFlags2 src_access_mask;
	VkAccessFlags2 dst_access_mask;
	VkImageLayout old_layout;
	VkImageLayout new_layout;
};

inline static State GetSrcState(const InputBase *p_src) {
	return {.stage_mask = UsageGetWriteAccessFlags(p_src->GetUsage()),
	        .access_mask = p_src->GetPipelineStages(),
	        .layout = UsageGetImageLayout(p_src->GetUsage())};
}
inline static State GetSrcState(std::ranges::input_range auto src_s) {
	State state = {};
	for (const InputBase *p_src : src_s)
		state |= GetSrcState(p_src);
	return state;
}
inline static State GetValidateSrcState(std::ranges::input_range auto src_s) {
	State state = {};
	for (const InputBase *p_src : src_s)
		state |= {.access_mask = p_src->GetPipelineStages()};
	return state;
}
inline static State GetDstState(const InputBase *p_dst) {
	return {.stage_mask = UsageGetAccessFlags(p_dst->GetUsage()),
	        .access_mask = p_dst->GetPipelineStages(),
	        .layout = UsageGetImageLayout(p_dst->GetUsage())};
}
inline static State GetDstState(std::ranges::input_range auto dst_s) {
	State state = {};
	for (const InputBase *p_dst : dst_s)
		state |= GetDstState(p_dst);
	return state;
}

template <typename T> inline static void AddSrcBarrier(T *p_barrier, const State &state) {
	p_barrier->src_access_mask |= state.access_mask;
	p_barrier->src_stage_mask |= state.stage_mask;
	if constexpr (requires(T t) { t.old_layout; }) {
		assert(p_barrier->old_layout == VK_IMAGE_LAYOUT_UNDEFINED || p_barrier->old_layout == state.layout);
		p_barrier->old_layout = state.layout;
	}
}

template <typename T> inline static void AddDstBarrier(T *p_barrier, const State &state) {
	p_barrier->dst_access_mask |= state.access_mask;
	p_barrier->dst_stage_mask |= state.stage_mask;
	if constexpr (requires(T t) { t.new_layout; }) {
		assert(p_barrier->new_layout == VK_IMAGE_LAYOUT_UNDEFINED || p_barrier->new_layout == state.layout);
		p_barrier->new_layout = state.layout;
	}
}
template <typename T> inline static void AddBarrier(T *p_barrier, const State &src_state, const State &dst_state) {
	AddSrcBarrier(p_barrier, src_state);
	AddDstBarrier(p_barrier, dst_state);
}

template <typename T> inline static bool IsValidBarrier(const T &barrier) {
	bool layout_transition = false;
	if constexpr (requires(T t) {
		              t.old_layout;
		              t.new_layout;
	              })
		layout_transition = barrier.old_layout != barrier.new_layout && barrier.new_layout != VK_IMAGE_LAYOUT_UNDEFINED;

	return (barrier.src_stage_mask | barrier.src_access_mask) && (barrier.dst_stage_mask | barrier.dst_access_mask) ||
	       layout_transition;
}
template <typename Src_T, typename Dst_T> inline static void CopyBarrier(const Src_T &src, Dst_T *p_dst) {
	p_dst->src_stage_mask = src.src_stage_mask;
	p_dst->dst_stage_mask = src.dst_stage_mask;
	p_dst->src_access_mask = src.src_access_mask;
	p_dst->dst_access_mask = src.dst_access_mask;
	if constexpr (requires(Src_T src_t, Dst_T dst_t) {
		              src_t.old_layout;
		              src_t.new_layout;
		              dst_t.old_layout;
		              dst_t.new_layout;
	              }) {
		p_dst->old_layout = src.old_layout;
		p_dst->new_layout = src.new_layout;
	}
}

class VkCommand::Builder {
private:
	struct SubpassPair {
		uint32_t src_subpass, dst_subpass;
		inline bool operator<=>(const SubpassPair &r) const = default;
	};
	struct SubpassPairHash {
		inline std::size_t operator()(SubpassPair x) const {
			uint64_t u = (uint64_t(x.src_subpass) << uint64_t(32)) | uint64_t(x.dst_subpass);
			return std::hash<uint64_t>{}(u);
		}
	};
	struct SubpassDependency {
		VkPipelineStageFlags2 src_stage_mask{};
		VkAccessFlags2 src_access_mask{};
		VkPipelineStageFlags2 dst_stage_mask{};
		VkAccessFlags2 dst_access_mask{};
	};
	struct AttachmentData {
		VkImageLayout initial_layout{}, final_layout{};
		bool load{false}, store{false}, may_alias{false};
		uint32_t id{};
	};
	struct PassData {
		std::span<const PassBase *const> subpasses; // pointed to subpasses in Schedule::PassGroup
		std::unordered_map<const ResourceBase *, Barrier> prior_barriers;
		std::unordered_map<SubpassPair, SubpassDependency, SubpassPairHash> by_region_subpass_deps, subpass_deps;
		std::unordered_map<const ImageBase *, AttachmentData> attachment_data_s;
	};

	std::vector<PassData> m_pass_data_s;
	std::unordered_map<const ResourceBase *, Barrier> m_post_barriers;

	void make_pass_data(const Args &args) {
		m_pass_data_s.reserve(args.schedule.GetPassGroups().size());
		for (const auto &pass_group : args.schedule.GetPassGroups()) {
			m_pass_data_s.emplace_back();
			auto &pass_data = m_pass_data_s.back();
			pass_data.subpasses = pass_group.subpasses;
			// Push Internal Subpass Dependencies and Attachments
			for (const auto &subpass_dep : pass_group.subpass_deps) {
				const PassBase *p_src_pass = Dependency::GetInputPass(subpass_dep.p_src),
				               *p_dst_pass = Dependency::GetInputPass(subpass_dep.p_dst);
				uint32_t src_subpass = Schedule::GetU32SubpassID(p_src_pass),
				         dst_subpass = Schedule::GetU32SubpassID(p_dst_pass);
				AddBarrier(&pass_data.by_region_subpass_deps[{src_subpass, dst_subpass}],
				           GetSrcState(subpass_dep.p_src), GetDstState(subpass_dep.p_dst));
				pass_data.attachment_data_s[subpass_dep.p_attachment];
			}
		}
	}

	inline PassData *get_p_pass_data(const InputBase *p_input) {
		std::size_t group_id = Schedule::GetGroupID(Dependency::GetInputPass(p_input));
		return &m_pass_data_s[group_id];
	}
	inline std::tuple<PassData *, AttachmentData *, uint32_t>
	get_p_pass_att_data(const ResourceBase *p_resource, const std::span<const InputBase *const> &inputs) {
		if (UsageIsAttachment(inputs[0]->GetUsage())) {
			assert(inputs.size() == 1);
			assert(p_resource->GetType() == ResourceType::kImage);
			auto p_image = static_cast<const ImageBase *>(p_resource);
			auto p_pass_data = get_p_pass_data(inputs[0]);
			return {p_pass_data, &p_pass_data->attachment_data_s[p_image],
			        Schedule::GetU32SubpassID(Dependency::GetInputPass(inputs[0]))};
		}
		return {nullptr, nullptr, VK_SUBPASS_EXTERNAL};
	}
	inline auto get_src_p_pass_att_data(const Schedule::PassBarrier &pass_barrier) {
		return get_p_pass_att_data(pass_barrier.p_resource, pass_barrier.src_s);
	}
	inline auto get_dst_p_pass_att_data(const Schedule::PassBarrier &pass_barrier) {
		return get_p_pass_att_data(pass_barrier.p_resource, pass_barrier.dst_s);
	}
	Barrier *get_p_barrier_data(const Schedule::PassBarrier &pass_barrier) {
		if (pass_barrier.dst_s.empty())
			return &m_post_barriers[pass_barrier.p_resource];
		// It is guaranteed dst_s[0] has the smallest GroupID
		return &get_p_pass_data(pass_barrier.dst_s[0])->prior_barriers[pass_barrier.p_resource];
	}

	void add_local_barrier(const Schedule::PassBarrier &pass_barrier) {
		if (auto [_, p_src_att_data, _1] = get_src_p_pass_att_data(pass_barrier); p_src_att_data) {
			p_src_att_data->store = true;
			p_src_att_data->final_layout = UsageGetImageLayout(pass_barrier.src_s[0]->GetUsage());
		}
		if (auto [_, p_dst_att_data, _1] = get_dst_p_pass_att_data(pass_barrier); p_dst_att_data) {
			p_dst_att_data->load = true;
			p_dst_att_data->initial_layout = UsageGetImageLayout(pass_barrier.dst_s[0]->GetUsage());
		}
		AddBarrier(get_p_barrier_data(pass_barrier), GetSrcState(pass_barrier.src_s), GetDstState(pass_barrier.dst_s));
	}

	void add_validate_barrier(const Args &args, const Schedule::PassBarrier &pass_barrier) {
		std::vector<const ResourceBase *> alias_resources;
		for (const ResourceBase *p_resource : args.dependency.GetPhysIDResources()) {
			if (args.dependency.IsResourceLess(p_resource, pass_barrier.p_resource) &&
			    args.vk_allocation.IsResourceAliased(p_resource, pass_barrier.p_resource))
				alias_resources.push_back(p_resource);
		}

		if (auto [p_dst_pass_data, p_dst_att_data, dst_subpass] = get_dst_p_pass_att_data(pass_barrier);
		    p_dst_att_data) {
			// Dst is a RenderPass, so no need for explicit layout transition
			// p_dst_att_data->initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
			for (const ResourceBase *p_resource : alias_resources) {
				const auto &last_inputs = Schedule::GetLastInputs(p_resource);

				SubpassDependency *p_subpass_dep;
				if (auto [p_src_pass_data, p_src_att_data, src_subpass] = get_p_pass_att_data(p_resource, last_inputs);
				    p_src_att_data && p_src_pass_data == p_dst_pass_data) {
					// From the same RenderPass
					p_subpass_dep = &p_dst_pass_data->subpass_deps[{src_subpass, dst_subpass}];
				} else
					p_subpass_dep = &p_dst_pass_data->subpass_deps[{VK_SUBPASS_EXTERNAL, dst_subpass}];

				AddBarrier(p_subpass_dep, GetValidateSrcState(last_inputs), GetDstState(pass_barrier.dst_s));
			}
		} else {
			auto *p_barrier = get_p_barrier_data(pass_barrier);
			// Might need explicit initial image layout transition
			AddDstBarrier(p_barrier, GetDstState(pass_barrier.dst_s));
			for (const ResourceBase *p_resource : alias_resources)
				AddSrcBarrier(p_barrier, GetValidateSrcState(Schedule::GetLastInputs(p_resource)));
		}
	}

	void add_input_barrier(const Schedule::PassBarrier &pass_barrier, const State &src_state) {
		if (auto [p_dst_pass_data, p_dst_att_data, dst_subpass] = get_dst_p_pass_att_data(pass_barrier);
		    p_dst_att_data) {
			p_dst_att_data->load = true; // Input ==> Load
			p_dst_att_data->initial_layout = src_state.layout;

			SubpassDependency *p_subpass_dep = &p_dst_pass_data->subpass_deps[{VK_SUBPASS_EXTERNAL, dst_subpass}];
			AddBarrier(p_subpass_dep, src_state, GetDstState(pass_barrier.dst_s));
		} else {
			auto *p_barrier = get_p_barrier_data(pass_barrier);
			AddBarrier(p_barrier, src_state, GetDstState(pass_barrier.dst_s));
		}
	}

	void add_output_barrier(const Schedule::PassBarrier &pass_barrier, const State &dst_state) {
		if (auto [p_src_pass_data, p_src_att_data, src_subpass] = get_src_p_pass_att_data(pass_barrier);
		    p_src_att_data) {
			p_src_att_data->store = true; // Store ==> Output
			p_src_att_data->final_layout = dst_state.layout;

			SubpassDependency *p_subpass_dep = &p_src_pass_data->subpass_deps[{src_subpass, VK_SUBPASS_EXTERNAL}];
			AddBarrier(p_subpass_dep, GetSrcState(pass_barrier.src_s), dst_state);
		} else {
			auto *p_barrier = get_p_barrier_data(pass_barrier);
			AddBarrier(p_barrier, GetSrcState(pass_barrier.src_s), dst_state);
		}
	}

	void make_barriers(const Args &args) {
		for (const auto &pass_barrier : args.schedule.GetPassBarriers()) {
			switch (pass_barrier.type) {
			case Schedule::BarrierType::kLocal:
				add_local_barrier(pass_barrier);
				break;
			case Schedule::BarrierType::kValidate:
				add_validate_barrier(args, pass_barrier);
				break;
			case Schedule::BarrierType::kLFInput:
				add_input_barrier(pass_barrier, GetSrcState(Schedule::GetLastInputs(
				                                    Dependency::GetLFResource(pass_barrier.p_resource))));
				break;
			case Schedule::BarrierType::kExtInput:
				add_input_barrier(pass_barrier, pass_barrier.p_resource->Visit(overloaded(
				                                    [](const ExternalImageBase *p_ext_image) -> State {
					                                    return {.stage_mask = p_ext_image->GetSrcPipelineStages(),
					                                            .access_mask = p_ext_image->GetSrcAccessFlags(),
					                                            .layout = p_ext_image->GetSrcLayout()};
				                                    },
				                                    [](const ExternalBufferBase *p_ext_buffer) -> State {
					                                    return {.stage_mask = p_ext_buffer->GetSrcPipelineStages(),
					                                            .access_mask = p_ext_buffer->GetSrcAccessFlags()};
				                                    },
				                                    [](auto &&) -> State { return {}; })));
				break;
			case Schedule::BarrierType::kLFOutput:
				// Mark as Store, also not changing its layout
				add_output_barrier(pass_barrier, {.layout = UsageGetImageLayout(pass_barrier.src_s[0]->GetUsage())});
				break;
			case Schedule::BarrierType::kExtOutput:
				add_output_barrier(pass_barrier, pass_barrier.p_resource->Visit(overloaded(
				                                     [](const ExternalImageBase *p_ext_image) -> State {
					                                     return {.stage_mask = p_ext_image->GetDstPipelineStages(),
					                                             .access_mask = p_ext_image->GetDstAccessFlags(),
					                                             .layout = p_ext_image->GetDstLayout()};
				                                     },
				                                     [](const ExternalBufferBase *p_ext_buffer) -> State {
					                                     return {.stage_mask = p_ext_buffer->GetDstPipelineStages(),
					                                             .access_mask = p_ext_buffer->GetDstAccessFlags()};
				                                     },
				                                     [](auto &&) -> State { return {}; })));
				break;
			}
		}
	}

	inline static void pop_barriers(const std::unordered_map<const ResourceBase *, Barrier> &in,
	                                std::vector<BarrierCmd> *p_out) {
		for (const auto &[p_resource, barrier] : in) {
			if (IsValidBarrier(barrier)) {
				p_out->push_back({.p_resource = p_resource});
				CopyBarrier(barrier, &p_out->back());
			}
		}
	};
	void pop_pass_commands(VkCommand *p_target) const {}

public:
	inline explicit Builder(const Args &args) {
		make_pass_data(args);
		make_barriers(args);
	}

	void PopResult(VkCommand *p_target) const {
		pop_pass_commands(p_target);
		pop_barriers(m_post_barriers, &p_target->m_post_barriers);
	}
};

VkCommand VkCommand::Create(const myvk::Ptr<myvk::Device> &device_ptr, const Args &args) {
	VkCommand c = {};
	Builder builder{args};
	return c;
}

} // namespace default_executor
