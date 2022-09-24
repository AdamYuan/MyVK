#include "myvk/Instance.hpp"
#include "myvk/Queue.hpp"

int main() {
	myvk::Ptr<myvk::Device> device;
	myvk::Ptr<myvk::Queue> generic_queue;
	{
		auto instance = myvk::Instance::Create({});
		auto physical_device = myvk::PhysicalDevice::Fetch(instance)[0];
		device = myvk::Device::Create(physical_device, myvk::GenericQueueSelector{&generic_queue},
		                              physical_device->GetDefaultFeatures(), {});
	}

	return 0;
}