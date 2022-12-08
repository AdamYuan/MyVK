#ifndef MYVK_RENDER_GRAPH_HPP
#define MYVK_RENDER_GRAPH_HPP

#include "CommandBuffer.hpp"
#include "FrameManager.hpp"
#include "RenderGraphScheduler.hpp"
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>

namespace myvk::render_graph {

class RenderGraphBase {};
class RGPassBase;

enum class RGInputUsage {
	kPreserve,
	kColorAttachment,
	kDepthAttachmentR,
	kDepthAttachmentRW,
	kInputAttachment,
	kPresent,
	kSampledImage,
	kStorageImageR,
	kStorageImageW,
	kStorageImageRW,
	kUniformBuffer,
	kStorageBufferR,
	kStorageBufferW,
	kStorageBufferRW,
	kIndexBuffer,
	kVertexBuffer,
	kTransferSrc,
	kTransferDst
};

#define CASE(NAME) case RGInputUsage::NAME:
inline constexpr bool RGInputUsageIsDescriptor(RGInputUsage usage) {
	switch (usage) {
		CASE(kInputAttachment)
		CASE(kSampledImage)
		CASE(kStorageImageR)
		CASE(kStorageImageW)
		CASE(kStorageImageRW)
		CASE(kUniformBuffer)
		CASE(kStorageBufferR)
		CASE(kStorageBufferW)
		CASE(kStorageBufferRW)
		return true;
	default:
		return false;
	}
}
inline constexpr VkDescriptorType RGInputUsageGetDescriptorType(RGInputUsage usage) {
	switch (usage) {
		CASE(kInputAttachment)
		return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		CASE(kSampledImage)
		return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		CASE(kStorageImageR)
		CASE(kStorageImageW)
		CASE(kStorageImageRW)
		return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		CASE(kUniformBuffer)
		return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		CASE(kStorageBufferR)
		CASE(kStorageBufferW)
		CASE(kStorageBufferRW)
		return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	default:
		return VK_DESCRIPTOR_TYPE_MAX_ENUM;
	}
}
inline constexpr bool RGInputUsageIsReadOnly(RGInputUsage usage) {
	switch (usage) {
		CASE(kPresent)
		CASE(kPreserve)
		CASE(kDepthAttachmentR)
		CASE(kInputAttachment)
		CASE(kSampledImage)
		CASE(kStorageImageR)
		CASE(kUniformBuffer)
		CASE(kStorageBufferR)
		CASE(kIndexBuffer)
		CASE(kVertexBuffer)
		CASE(kTransferSrc)
		return true;
	default:
		return false;
	}
}
inline constexpr bool RGInputUsageForBuffer(RGInputUsage usage) {
	switch (usage) {
		CASE(kPreserve)
		CASE(kUniformBuffer)
		CASE(kStorageBufferR)
		CASE(kStorageBufferW)
		CASE(kStorageBufferRW)
		CASE(kIndexBuffer)
		CASE(kVertexBuffer)
		CASE(kTransferSrc)
		CASE(kTransferDst)
		return true;
	default:
		return false;
	}
}
inline constexpr bool RGInputUsageForImage(RGInputUsage usage) {
	switch (usage) {
		CASE(kPreserve)
		CASE(kPresent)
		CASE(kColorAttachment)
		CASE(kDepthAttachmentR)
		CASE(kDepthAttachmentRW)
		CASE(kInputAttachment)
		CASE(kSampledImage)
		CASE(kStorageImageR)
		CASE(kStorageImageW)
		CASE(kStorageImageRW)
		CASE(kTransferSrc)
		CASE(kTransferDst)
		return true;
	default:
		return false;
	}
}
inline constexpr bool RGInputUsageGetImageLayout(RGInputUsage usage) {
	switch (usage) {
		CASE(kPresent)
		return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		CASE(kColorAttachment)
		return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		CASE(kDepthAttachmentR)
		CASE(kDepthAttachmentRW)
		return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		CASE(kInputAttachment)
		CASE(kSampledImage)
		return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		CASE(kStorageImageR)
		CASE(kStorageImageW)
		CASE(kStorageImageRW)
		return VK_IMAGE_LAYOUT_GENERAL;
		CASE(kTransferSrc)
		return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		CASE(kTransferDst)
		return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	default:
		return VK_IMAGE_LAYOUT_UNDEFINED;
	}
}
#undef CASE
inline constexpr bool RGInputUsageAll(RGInputUsage) { return true; }

// Input Usage Operation
using RGInputUsageClass = bool(RGInputUsage);
namespace _details_rg_input_usage_op_ {
template <RGInputUsageClass... Args> struct Union;
template <RGInputUsageClass X, RGInputUsageClass... Args> struct Union<X, Args...> {
	constexpr bool operator()(RGInputUsage x) const { return X(x) || Union<Args...>(x); }
};
template <> struct Union<> {
	constexpr bool operator()(RGInputUsage) const { return false; }
};
template <bool... Args(RGInputUsage)> struct Intersect;
template <RGInputUsageClass X, RGInputUsageClass... Args> struct Intersect<X, Args...> {
	constexpr bool operator()(RGInputUsage x) const { return X(x) && Intersect<Args...>(x); }
};
template <> struct Intersect<> {
	constexpr bool operator()(RGInputUsage) const { return true; }
};
} // namespace _details_rg_input_usage_op_
// Union: A | B
template <RGInputUsageClass... Args> inline constexpr bool RGInputUsageUnion(RGInputUsage x) {
	return _details_rg_input_usage_op_::Union<Args...>(x);
}
// Intersect: A & B
template <RGInputUsageClass... Args> inline constexpr bool RGInputUsageIntersect(RGInputUsage x) {
	return _details_rg_input_usage_op_::Intersect<Args...>(x);
}
// Complement: ~X
template <RGInputUsageClass X> inline constexpr bool RGInputUsageComplement(RGInputUsage x) { return !X(x); }
// Minus: A & (~B)
template <RGInputUsageClass A, RGInputUsageClass B> inline constexpr bool RGInputUsageMinus(RGInputUsage x) {
	return RGInputUsageIntersect<A, RGInputUsageComplement<B>>(x);
}

class RGKey {
public:
	using LengthType = uint8_t;
	using IDType = uint16_t;
	constexpr static const std::size_t kMaxStrLen = 32 - sizeof(LengthType) - sizeof(IDType);

private:
	union {
		struct {
			IDType m_id;
			LengthType m_len;
			char m_str[kMaxStrLen];
		};
		std::tuple<uint64_t, uint64_t, uint64_t, uint64_t> _32_;
	};
	static_assert(sizeof(_32_) == 32);

public:
	inline RGKey() : _32_{} {}
	template <typename IntType = IDType, typename = std::enable_if_t<std::is_integral_v<IntType>>>
	inline RGKey(std::string_view str, IntType id = 0) : m_str{}, m_len(std::min(str.length(), kMaxStrLen)), m_id(id) {
		std::copy(str.begin(), str.begin() + m_len, m_str);
	}
	inline RGKey(const RGKey &r) : _32_{r._32_} {}
	inline RGKey &operator=(const RGKey &r) {
		_32_ = r._32_;
		return *this;
	}
	inline std::string_view GetName() const { return std::string_view{m_str, m_len}; }
	inline IDType GetID() const { return m_id; }
	inline void SetName(std::string_view str) {
		m_len = std::min(str.length(), kMaxStrLen);
		std::copy(str.begin(), str.begin() + m_len, m_str);
		std::fill(m_str + m_len, m_str + kMaxStrLen, '\0');
	}
	inline void SetID(IDType id) { m_id = id; }

	inline bool operator<(const RGKey &r) const { return _32_ < r._32_; }
	inline bool operator>(const RGKey &r) const { return _32_ > r._32_; }
	inline bool operator==(const RGKey &r) const { return _32_ == r._32_; }
	inline bool operator!=(const RGKey &r) const { return _32_ != r._32_; }
	struct Hash {
		inline std::size_t operator()(RGKey const &r) const noexcept {
			return std::get<0>(r._32_) ^ std::get<1>(r._32_) ^ std::get<2>(r._32_) ^ std::get<3>(r._32_);
			// return ((std::get<0>(r._32_) * 37 + std::get<1>(r._32_)) * 37 + std::get<2>(r._32_)) * 37 +
			//        std::get<3>(r._32_);
		}
	};
};
static_assert(sizeof(RGKey) == 32 && std::is_move_constructible_v<RGKey>);
template <typename Value> using RGKeyMap = std::unordered_map<RGKey, Value, RGKey::Hash>;

// Object Base
class RGObjectBase {
private:
	RenderGraphBase *m_render_graph_ptr{};
	const RGKey *m_key_ptr{};

	inline void set_render_graph_ptr(RenderGraphBase *render_graph_ptr) { m_render_graph_ptr = render_graph_ptr; }
	inline void set_key_ptr(const RGKey *key_ptr) { m_key_ptr = key_ptr; }

	template <typename, typename...> friend class RGObjectPool;

public:
	inline RGObjectBase() = default;
	inline virtual ~RGObjectBase() = default;

	inline RenderGraphBase *GetRenderGraphPtr() const { return m_render_graph_ptr; }
	inline const RGKey &GetKey() const { return *m_key_ptr; }

	// Disable Copy
	inline RGObjectBase(RGObjectBase &&r) noexcept = default;
	// inline RGObjectBase &operator=(RGObjectBase &&r) noexcept = default;
};

// Resource & Resource Input
enum class RGResourceType { kImage, kBuffer };
enum class RGResourceState { kManaged, kExternal };
template <RGResourceType Type> inline constexpr bool RGInputUsageForType(RGInputUsage usage) {
	if constexpr (Type == RGResourceType::kImage) {
		return RGInputUsageForImage(usage);
	} else {
		return RGInputUsageForBuffer(usage);
	}
};

class RGResourceBase : virtual public RGObjectBase {
private:
	RGPassBase *m_producer_pass_ptr{};

	inline void set_producer_pass_ptr(RGPassBase *producer_pass_ptr) { m_producer_pass_ptr = producer_pass_ptr; }

	template <typename, typename...> friend class RGObjectPool;

public:
	inline ~RGResourceBase() override = default;
	inline RGResourceBase() = default;
	inline RGResourceBase(RGResourceBase &&) noexcept = default;

	virtual RGResourceType GetType() const = 0;
	virtual RGResourceState GetState() const = 0;
	virtual bool IsAlias() const = 0;
	// virtual bool IsPerFrame() const = 0;
	// virtual void Resize(uint32_t width, uint32_t height) {}

	inline RGPassBase *GetProducerPassPtr() const { return m_producer_pass_ptr; }
};

#include "RenderGraphObjectPool.inl"

// Buffer and Image
class RGBufferBase : virtual public RGResourceBase {
public:
	inline ~RGBufferBase() override = default;
	inline RGBufferBase() = default;
	inline RGBufferBase(RGBufferBase &&) noexcept = default;

	inline RGResourceType GetType() const final { return RGResourceType::kBuffer; }
	virtual const Ptr<BufferBase> &GetBuffer(uint32_t frame = 0) const = 0;
};

enum class RGAttachmentLoadOp { kClear, kLoad, kDontCare };
class RGImageBase : virtual public RGResourceBase {
public:
	inline ~RGImageBase() override = default;
	inline RGImageBase() = default;
	inline RGImageBase(RGImageBase &&) noexcept = default;

	inline RGResourceType GetType() const final { return RGResourceType::kImage; }
	virtual const Ptr<ImageView> &GetImageView(uint32_t frame = 0) const = 0;

	virtual RGAttachmentLoadOp GetLoadOp() const = 0;
	virtual const VkClearValue &GetClearValue() const = 0;
};

// Alias
class RGResourceAliasBase : virtual public RGResourceBase {
private:
	RGResourceBase *m_resource;

public:
	inline explicit RGResourceAliasBase(RGResourceBase *resource)
	    : m_resource{resource->IsAlias() ? dynamic_cast<RGResourceAliasBase *>(resource)->GetResource() : resource} {}
	inline RGResourceAliasBase(RGResourceAliasBase &&) noexcept = default;

	bool IsAlias() const final { return true; }
	RGResourceState GetState() const final { return m_resource->GetState(); }

	template <typename Type = RGResourceBase, typename = std::enable_if_t<std::is_base_of_v<RGResourceBase, Type>>>
	Type *GetResource() const {
		return dynamic_cast<Type *>(m_resource);
	}
	~RGResourceAliasBase() override = default;
};
class RGImageAlias final : public RGResourceAliasBase, public RGImageBase {
public:
	inline explicit RGImageAlias(RGImageBase *image) : RGResourceAliasBase(image) {}
	inline RGImageAlias(RGImageAlias &&) noexcept = default;
	inline ~RGImageAlias() final = default;

	inline const Ptr<ImageView> &GetImageView(uint32_t frame = 0) const final {
		return GetResource<RGImageBase>()->GetImageView(frame);
	}
	inline RGAttachmentLoadOp GetLoadOp() const final { return GetResource<RGImageBase>()->GetLoadOp(); }
	virtual const VkClearValue &GetClearValue() const final { return GetResource<RGImageBase>()->GetClearValue(); }
};
class RGBufferAlias final : public RGResourceAliasBase, public RGBufferBase {
public:
	inline explicit RGBufferAlias(RGBufferBase *image) : RGResourceAliasBase(image) {}
	inline RGBufferAlias(RGBufferAlias &&) = default;
	inline ~RGBufferAlias() final = default;

	inline const Ptr<BufferBase> &GetBuffer(uint32_t frame = 0) const final {
		return GetResource<RGBufferBase>()->GetBuffer(frame);
	}
};

// Resource Input
// TODO: where to use RGInputDescriptorInfo?
struct RGInputDescriptorInfo {
	VkShaderStageFlags stage_flags;
	Ptr<Sampler> sampler;
};
class RGInput {
private:
	RGResourceBase *m_resource_ptr{};
	RGInputUsage m_usage{};

public:
	inline RGInput() = default;
	RGInput(RGResourceBase *resource_ptr, RGInputUsage usage) : m_resource_ptr{resource_ptr}, m_usage{usage} {}

	inline RGResourceBase *GetResource() const { return m_resource_ptr; }
	inline RGInputUsage GetUsage() const { return m_usage; }
};

// Resource Pool
template <typename RGDerived>
class RGResourcePool : public RGObjectPool<RGDerived, RGObjectVariant<RGBufferBase, RGImageBase>> {
private:
	using ResourcePool = RGObjectPool<RGDerived, RGObjectVariant<RGBufferBase, RGImageBase>>;

public:
	inline RGResourcePool() = default;
	inline RGResourcePool(RGResourcePool &&) noexcept = default;
	inline ~RGResourcePool() override = default;

protected:
	template <
	    typename Type, typename... Args,
	    typename = std::enable_if_t<std::is_base_of_v<RGBufferBase, Type> || std::is_base_of_v<RGImageBase, Type>>>
	inline Type *CreateResource(const RGKey &resource_key, Args &&...args) {
		return ResourcePool::template CreateAndInitialize<0, Type, Args...>(resource_key, std::forward<Args>(args)...);
	}
	inline void DeleteResource(const RGKey &resource_key) { return ResourcePool::Delete(resource_key); }

	inline RGBufferBase *GetBufferResource(const RGKey &resource_buffer_key) const {
		return ResourcePool::template Get<0, RGBufferBase>(resource_buffer_key);
	}
	inline RGImageBase *GetImageResource(const RGKey &resource_image_key) const {
		return ResourcePool::template Get<0, RGImageBase>(resource_image_key);
	}
};

// Input Pool
template <typename RGDerived>
class RGInputPool : public RGObjectPool<RGDerived, RGInput, RGObjectVariant<RGBufferAlias, RGImageAlias>> {
private:
	using InputPool = RGObjectPool<RGDerived, RGInput, RGObjectVariant<RGBufferAlias, RGImageAlias>>;

	template <typename RGType> inline RGType *create_output(const RGKey &input_key) {
		// RGType can only be RGBufferBase or RGImageBase
		constexpr RGResourceType kResType =
		    std::is_base_of_v<RGImageBase, RGType> ? RGResourceType::kImage : RGResourceType::kBuffer;

		static_assert(std::is_base_of_v<RGPassBase, RGDerived>);
		const RGInput *input = InputPool::template Get<0, RGInput>(input_key);
		if (!input)
			return nullptr;

		if (input->GetResource()->GetProducerPassPtr() == (RGPassBase *)static_cast<RGDerived *>(this))
			return kResType == input->GetResource()->GetType() ? dynamic_cast<RGType *>(input->GetResource()) : nullptr;
		else {
			if constexpr (kResType == RGResourceType::kBuffer) {
				return (input->GetResource()->GetType() == kResType)
				           ? InputPool::template InitializeOrGet<1, RGBufferAlias>(
				                 input_key, dynamic_cast<RGBufferBase *>(input->GetResource()))
				           : nullptr;
			} else {
				return (input->GetResource()->GetType() == kResType)
				           ? InputPool::template InitializeOrGet<1, RGImageAlias>(
				                 input_key, dynamic_cast<RGImageBase *>(input->GetResource()))
				           : nullptr;
			}
		}
	}

public:
	inline RGInputPool() = default;
	inline RGInputPool(RGInputPool &&) noexcept = default;
	inline ~RGInputPool() override = default;

protected:
	template <RGInputUsage Usage, typename = std::enable_if_t<RGInputUsageForBuffer(Usage)>>
	inline void AddInput(const RGKey &input_key, RGBufferBase *buffer) {
		InputPool::template CreateAndInitialize<0, RGInput>(input_key, buffer, Usage);
	}
	template <RGInputUsage Usage, typename = std::enable_if_t<RGInputUsageForImage(Usage)>>
	inline void AddInput(const RGKey &input_key, RGImageBase *image) {
		InputPool::template CreateAndInitialize<0, RGInput>(input_key, image, Usage);
	}
	/* inline const RGInput *GetInput(const RGKey &input_key) const {
	    return InputPool::template Get<0, RGInput>(input_key);
	} */
	inline void RemoveInput(const RGKey &input_key) { InputPool::Delete(input_key); }

	inline RGBufferBase *CreateBufferOutput(const RGKey &input_buffer_key) {
		return create_output<RGBufferBase>(input_buffer_key);
	}
	inline RGImageBase *CreateImageOutput(const RGKey &input_image_key) {
		return create_output<RGImageBase>(input_image_key);
	}
};

/* template <typename RGDerived>
class RGInputSlot {
    // Add...
}; */

// Descriptor Pool

// Render Pass
class RGPassBase : virtual public RGObjectBase {};

// Buffers
class RGManagedBuffer final : virtual public RGBufferBase {
public:
	inline RGManagedBuffer() = default;
	inline RGManagedBuffer(RGManagedBuffer &&) noexcept = default;
	~RGManagedBuffer() override = default;

	const Ptr<BufferBase> &GetBuffer(uint32_t frame = 0) const final {
		static Ptr<BufferBase> x;
		return x;
	}
	bool IsAlias() const final { return false; }
	RGResourceState GetState() const final { return RGResourceState::kManaged; }
};

// TODO: Debug Type Traits
static_assert(std::is_same_v<RGObjectVariant<RGBufferAlias, RGObjectBase, RGResourceBase, RGImageAlias>,
                             std::variant<std::monostate, RGImageAlias, std::unique_ptr<RGResourceBase>,
                                          std::unique_ptr<RGObjectBase>, RGBufferAlias>>);
} // namespace myvk::render_graph

#endif
