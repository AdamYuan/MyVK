#ifndef MYVK_RG_RENDER_GRAPH_DESCRIPTOR_HPP
#define MYVK_RG_RENDER_GRAPH_DESCRIPTOR_HPP

#include "RenderGraphResolver.hpp"

#include "RenderGraphAllocator.hpp"

#include <myvk/DescriptorPool.hpp>

namespace myvk_rg::_details_ {

class RenderGraphDescriptor {
private:
	template <typename Resource> struct DescriptorBinding {
		const Resource *resource{};
		VkDescriptorType type{};
	};
	template <typename Resource> using DescriptorBindingMap = std::unordered_map<uint32_t, DescriptorBinding<Resource>>;
	struct PassDescriptor {
		DescriptorBindingMap<InternalImageBase> int_image_bindings;
		DescriptorBindingMap<LastFrameImage> lf_image_bindings;
		DescriptorBindingMap<ExternalImageBase> ext_image_bindings;

		DescriptorBindingMap<ManagedBuffer> int_buffer_bindings;
		DescriptorBindingMap<LastFrameBuffer> lf_buffer_bindings;
		DescriptorBindingMap<ExternalBufferBase> ext_buffer_bindings;

		myvk::Ptr<myvk::DescriptorSet> sets[2]{};
	};
	std::vector<PassDescriptor> m_pass_descriptors;

public:
	void Create(const myvk::Ptr<myvk::Device> &device, const RenderGraphResolver &resolved);
	void PreBind(const RenderGraphAllocator &allocated);
};

} // namespace myvk_rg::_details_

#endif
