#ifndef MYVK_RG_RENDER_GRAPH_DESCRIPTOR_HPP
#define MYVK_RG_RENDER_GRAPH_DESCRIPTOR_HPP

#include "RenderGraphResolver.hpp"

#include <myvk/DescriptorPool.hpp>

namespace myvk_rg::_details_ {

class RenderGraphDescriptor {
private:
	const RenderGraphResolver *m_p_resolved{};

	struct PassDescriptor {
		myvk::Ptr<myvk::DescriptorSet> set;
	};
	std::vector<PassDescriptor> m_pass_descriptors;

public:
	void Create(const myvk::Ptr<myvk::Device> &device, const RenderGraphResolver &resolved);
};

} // namespace myvk_rg::_details_

#endif
