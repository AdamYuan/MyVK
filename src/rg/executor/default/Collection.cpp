#include "Collection.hpp"

CompileResult<Collection> Collection::Create(const RenderGraphBase &rg) {
	Collection c;
	UNWRAP(c.collect_resources(rg));
	UNWRAP(c.collect_passes(rg));
	return c;
}

template <typename Container> CompileResult<void> Collection::collect_resources(const Container &pool) {
	for (const auto &it : pool.GetResourcePoolData()) {
		const auto *p_resource = it.second.template Get<ResourceBase>();
		if (p_resource == nullptr)
			return error::NullResource{.parent = pool.GetGlobalKey()};
		m_resources[p_resource->GetGlobalKey()] = p_resource;
	}
	return {};
}

template <typename Container> CompileResult<void> Collection::collect_inputs(const Container &pool) {
	for (const auto &it : pool.GetInputPoolData()) {
		const auto *p_input = it.second.template Get<InputBase>();
		if (p_input == nullptr)
			return error::NullInput{.parent = pool.GetGlobalKey()};
		m_inputs[p_input->GetGlobalKey()] = p_input;
	}
	return {};
}

template <typename Container> CompileResult<void> Collection::collect_passes(const Container &pool) {
	for (const auto &it : pool.GetPassPoolData()) {
		const PassBase *p_pass = it.second.template Get<PassBase>();
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
