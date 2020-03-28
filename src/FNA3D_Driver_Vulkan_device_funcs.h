/* FNA3D - 3D Graphics Library for FNA
 *
 * Copyright (c) 2020 Ethan Lee
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software in a
 * product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Ethan "flibitijibibo" Lee <flibitijibibo@flibitijibibo.com>
 *
 */

VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkAcquireNextImageKHR, (VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkAllocateCommandBuffers, (VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo, VkCommandBuffer *pCommandBuffers))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkBeginCommandBuffer, (VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdClearColorImage, (VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue *pColor, uint32_t rangeCount, const VkImageSubresourceRange *pRanges))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdPipelineBarrier, (VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateCommandPool, (VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateFence, (VkDevice device, const VkFenceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFence *pFence))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateImageView, (VkDevice device, const VkImageViewCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImageView *pView))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateSemaphore, (VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateSwapchainKHR, (VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyCommandPool, (VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyDevice, (VkDevice device, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyFence, (VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyImageView, (VkDevice device, VkImageView imageView, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroySemaphore, (VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroySwapchainKHR, (VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkDeviceWaitIdle, (VkDevice device))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkEndCommandBuffer, (VkCommandBuffer commandBuffer))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkFreeCommandBuffers, (VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkGetDeviceQueue, (VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkGetFenceStatus, (VkDevice device, VkFence fence))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkGetSwapchainImagesKHR, (VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkQueuePresentKHR, (VkQueue queue, const VkPresentInfoKHR *pPresentInfo))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkQueueSubmit, (VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkResetCommandBuffer, (VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkResetFences, (VkDevice device, uint32_t fenceCount, const VkFence *pFences))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkWaitForFences, (VkDevice device, uint32_t fenceCount, const VkFence *pFences, VkBool32 waitAll, uint64_t timeout))
