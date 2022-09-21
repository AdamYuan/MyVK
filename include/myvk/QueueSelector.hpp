#ifndef MYVK_QUEUE_SELECTOR_HPP
#define MYVK_QUEUE_SELECTOR_HPP

#include "PhysicalDevice.hpp"
#include <functional>
#include <memory>
#include <vector>

namespace myvk {

class Queue;
class PresentQueue;
class Surface;
struct QueueSelection {
	Ptr<Queue> *target;
	uint32_t family, index_specifier;
};
struct PresentQueueSelection {
	Ptr<PresentQueue> *target;
	Ptr<Surface> surface;
	uint32_t family, index_specifier;
};
using QueueSelectorFunc = std::function<bool(const Ptr<PhysicalDevice> &, std::vector<QueueSelection> *const,
                                             std::vector<PresentQueueSelection> *const)>;

// default queue selectors
class GenericPresentQueueSelector {
private:
	Ptr<Surface> m_surface;
	Ptr<Queue> *m_generic_queue;
	Ptr<PresentQueue> *m_present_queue;

public:
	GenericPresentQueueSelector(Ptr<Queue> *generic_queue, const Ptr<Surface> &surface,
	                            Ptr<PresentQueue> *present_queue);
	bool operator()(const Ptr<PhysicalDevice> &, std::vector<QueueSelection> *const,
	                std::vector<PresentQueueSelection> *const) const;
};

// default queue selectors
class GenericPresentTransferQueueSelector {
private:
	Ptr<Surface> m_surface;
	Ptr<Queue> *m_generic_queue, *m_transfer_queue;
	Ptr<PresentQueue> *m_present_queue;

public:
	GenericPresentTransferQueueSelector(Ptr<Queue> *generic_queue, Ptr<Queue> *transfer_queue,
	                                    const Ptr<Surface> &surface, Ptr<PresentQueue> *present_queue);
	bool operator()(const Ptr<PhysicalDevice> &, std::vector<QueueSelection> *const,
	                std::vector<PresentQueueSelection> *const) const;
};

} // namespace myvk

#endif
