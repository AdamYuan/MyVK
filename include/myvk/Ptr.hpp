#ifndef MYVK_PTR_HPP
#define MYVK_PTR_HPP

#include <memory>

namespace myvk {
template <typename T> using Ptr = std::shared_ptr<const T>;
}

#endif // MYVK_PTR_HPP
