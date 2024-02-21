#include <myvk_rg/interface/Resource.hpp>

#include <myvk_rg/interface/RenderGraph.hpp>

namespace myvk_rg::interface {

const myvk::Ptr<myvk::ImageView> &CombinedImage::GetVkImageView() const {
	return executor::Executor::GetVkImageView(this);
}
const myvk::Ptr<myvk::ImageView> &ManagedImage::GetVkImageView() const {
	return executor::Executor::GetVkImageView(this);
}

const myvk::Ptr<myvk::BufferBase> &ManagedBuffer::GetVkBuffer() const { return executor::Executor::GetVkBuffer(this); }

void *ManagedBuffer::GetMappedData() const { return executor::Executor::GetMappedData(this); }

} // namespace myvk_rg::interface
