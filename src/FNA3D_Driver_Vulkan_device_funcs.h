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
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkAllocateMemory, (VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo, const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkBeginCommandBuffer, (VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkBindBufferMemory, (VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkBindImageMemory, (VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdBeginRenderPass, (VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin, VkSubpassContents contents))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdBindPipeline, (VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdBindVertexBuffers, (VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdBlitImage, (VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit *pRegions, VkFilter filter))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdClearAttachments, (VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment *pAttachments, uint32_t rectCount, const VkClearRect *pRects))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdClearColorImage, (VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue *pColor, uint32_t rangeCount, const VkImageSubresourceRange *pRanges))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdDraw, (VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdEndRenderPass, (VkCommandBuffer commandBuffer))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdPipelineBarrier, (VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdSetBlendConstants, (VkCommandBuffer commandBuffer, const float blendConstants[4]))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdSetDepthBias, (VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdSetScissor, (VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D *pScissors))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdSetStencilReference, (VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdSetViewport, (VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport *pViewports))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateBuffer, (VkDevice device, const VkBufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateCommandPool, (VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateFence, (VkDevice device, const VkFenceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFence *pFence))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateFramebuffer, (VkDevice device, const VkFramebufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateGraphicsPipelines, (VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo *pCreateInfos, const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateImage, (VkDevice device, const VkImageCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImage *pImage))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateImageView, (VkDevice device, const VkImageViewCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImageView *pView))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreatePipelineCache, (VkDevice device, const VkPipelineCacheCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkPipelineCache *pPipelineCache))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreatePipelineLayout, (VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkPipelineLayout *pPipelineLayout))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateRenderPass, (VkDevice device, const VkRenderPassCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateSemaphore, (VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateShaderModule, (VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateSwapchainKHR, (VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyCommandPool, (VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyDevice, (VkDevice device, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyFence, (VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyFramebuffer, (VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyImage, (VkDevice device, VkImage image, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyImageView, (VkDevice device, VkImageView imageView, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyPipeline, (VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyPipelineCache, (VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyPipelineLayout, (VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyRenderPass, (VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroySemaphore, (VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroySwapchainKHR, (VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkDeviceWaitIdle, (VkDevice device))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkEndCommandBuffer, (VkCommandBuffer commandBuffer))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkFreeCommandBuffers, (VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkFreeMemory, (VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkGetBufferMemoryRequirements, (VkDevice device, VkBuffer buffer, VkMemoryRequirements *pMemoryRequirements))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkGetDeviceQueue, (VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkGetImageMemoryRequirements, (VkDevice device, VkImage image, VkMemoryRequirements *pMemoryRequirements))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkGetFenceStatus, (VkDevice device, VkFence fence))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkGetSwapchainImagesKHR, (VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkMapMemory, (VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void **ppData))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkQueuePresentKHR, (VkQueue queue, const VkPresentInfoKHR *pPresentInfo))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkQueueSubmit, (VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkResetCommandBuffer, (VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkResetCommandPool, (VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkResetFences, (VkDevice device, uint32_t fenceCount, const VkFence *pFences))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkUnmapMemory, (VkDevice device, VkDeviceMemory memory))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkWaitForFences, (VkDevice device, uint32_t fenceCount, const VkFence *pFences, VkBool32 waitAll, uint64_t timeout))
