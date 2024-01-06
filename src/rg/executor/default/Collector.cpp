#include "Collector.hpp"

CompileResult<void> Collector::Collect(const RenderGraphBase &rg) {
	UNWRAP(collect_resources(rg));
	UNWRAP(collect_passes(rg));
	return {};
}

template <typename Container> CompileResult<void> Collector::collect_resources(const Container &pool) {
	for (const auto &it : pool.GetResourcePoolData()) {
		const auto *p_resource = it.second.template Get<0, ResourceBase>();
		if (p_resource == nullptr)
			return error::NullResource{.key = pool.GetGlobalKey()};
		m_resources[p_resource->GetGlobalKey()] = p_resource;
	}
	return {};
}

template <typename Container> CompileResult<void> Collector::collect_inputs(const Container &pool) {
	for (const auto &it : pool.GetInputPoolData()) {
		const auto *p_input = it.second.template Get<0, InputBase>();
		if (p_input == nullptr)
			return error::NullInput{.key = pool.GetGlobalKey()};
		m_inputs[p_input->GetGlobalKey()] = p_input;
	}
	return {};
}

template <typename Container> CompileResult<void> Collector::collect_passes(const Container &pool) {
	for (const auto &it : pool.GetPassPoolData()) {
		const PassBase *p_pass = it.second.template Get<0, PassBase>();
		if (p_pass == nullptr)
			return error::NullPass{.key = pool.GetGlobalKey()};

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
