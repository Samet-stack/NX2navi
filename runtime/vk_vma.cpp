// Vulkan Memory Allocator implementation (AMD, MIT; from the MSYS2 package vulkan-memory-allocator, not copied).
// VMA is C++ only, so this is the one C++ file of the runtime; the rest uses the C interface of vk_mem_alloc.h.
//
// Every new VkDeviceMemory VMA takes comes through vkAllocateMemory. A suballocation into a block it already holds
// does not. The wrapper refuses a device-local block that would enter the last slice of the OS budget
// (vk_mem_device_alloc_ok): crossing that budget once left the GPU slow for the rest of a session, and freeing a
// texture does not lower the figure until its whole block is empty. The check only reads a sample taken outside
// this call. VMA invokes vkAllocateMemory with its own lock held, so the check must not call back into VMA.
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <vulkan/vulkan.h>

extern "C" int vk_mem_device_alloc_ok(uint32_t memory_type, uint64_t bytes);

static VkResult (*nx2_real_vkAllocateMemory)(VkDevice, const VkMemoryAllocateInfo *, const VkAllocationCallbacks *,
                                            VkDeviceMemory *) = vkAllocateMemory;

static VkResult nx2_vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo *info, const VkAllocationCallbacks *cb,
                                     VkDeviceMemory *mem) {
    if (info && !vk_mem_device_alloc_ok(info->memoryTypeIndex, (uint64_t)info->allocationSize))
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    return nx2_real_vkAllocateMemory(device, info, cb, mem);
}

#define vkAllocateMemory nx2_vkAllocateMemory
#include "vk_mem_alloc.h"
