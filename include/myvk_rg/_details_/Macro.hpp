#ifndef MYVK_RG_MACRO_HPP
#define MYVK_RG_MACRO_HPP

#define MYVK_RG_INITIALIZER_FUNC __myvk_rg_initialize__

#define MYVK_RG_OBJECT_INLINE_INITIALIZER(...) \
	template <typename, typename...> friend class myvk_rg::_details_::Pool; \
	inline void MYVK_RG_INITIALIZER_FUNC(__VA_ARGS__)

#define MYVK_RG_RENDER_GRAPH_INLINE_INITIALIZER(...) \
	template <typename> friend class myvk_rg::_details_::RenderGraph; \
	inline void MYVK_RG_INITIALIZER_FUNC(__VA_ARGS__)

#define MYVK_RG_INLINE_INITIALIZER(...) \
	template <typename, typename...> friend class myvk_rg::_details_::Pool; \
	template <typename> friend class myvk_rg::_details_::RenderGraph; \
	inline void MYVK_RG_INITIALIZER_FUNC(__VA_ARGS__)

#endif
