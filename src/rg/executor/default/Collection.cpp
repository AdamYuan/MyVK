#include "Collection.hpp"

namespace default_executor {

CompileResult<Collection> Collection::Create(const RenderGraphBase &rg) {
	Collection c;
	UNWRAP(c.collect_resources(rg));
	UNWRAP(c.collect_passes(rg));
	c.make_infos();
	return c;
}

void Collection::make_infos() {
	m_resource_infos.reserve(m_resources.size());
	for (const auto &[_, p_resource] : m_resources) {
		m_resource_infos.emplace_back();
		p_resource->__SetPExecutorInfo(&m_resource_infos.back());
	}

	m_pass_infos.reserve(m_passes.size());
	for (const auto &[_, p_pass] : m_passes) {
		m_pass_infos.emplace_back();
		p_pass->__SetPExecutorInfo(&m_pass_infos.back());
	}
}

template <typename Container> CompileResult<void> Collection::collect_resources(const Container &pool) {
	for (const auto &[_, pool_data] : pool.GetResourcePoolData()) {
		const auto *p_resource = pool_data.template Get<ResourceBase>();
		if (p_resource == nullptr)
			return error::NullResource{.parent = pool.GetGlobalKey()};
		m_resources[p_resource->GetGlobalKey()] = p_resource;
	}
	return {};
}

template <typename Container> CompileResult<void> Collection::collect_inputs(const Container &pool) {
	for (const auto &[_, pool_data] : pool.GetInputPoolData()) {
		const auto *p_input = pool_data.template Get<InputBase>();
		if (p_input == nullptr)
			return error::NullInput{.parent = pool.GetGlobalKey()};
		m_inputs[p_input->GetGlobalKey()] = p_input;
	}
	return {};
}

template <typename Container> CompileResult<void> Collection::collect_passes(const Container &pool) {
	for (const auto &[_, pool_data] : pool.GetPassPoolData()) {
		const PassBase *p_pass = pool_data.template Get<PassBase>();
		if (p_pass == nullptr)
			return error::NullPass{.parent = pool.GetGlobalKey()};

		UNWRAP(p_pass->Visit(overloaded(
		    [this](const PassGroupBase *p_pass) -> CompileResult<void> {
			    UNWRAP(collect_resources(*p_pass));
			    return collect_passes(*p_pass);
		    },
		    [this](const auto *p_pass) -> CompileResult<void> {
			    UNWRAP(collect_resources(*p_pass));
			    UNWRAP(collect_inputs(*p_pass));
			    m_passes[p_pass->GetGlobalKey()] = p_pass;
			    return {};
		    })));
	}
	return {};
}

} // namespace default_executor
