/* FNA3D - 3D Graphics Library for FNA
 *
 * Copyright (c) 2020-2021 Ethan Lee
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

/*
 * Global functions from the Vulkan Loader
 */

#ifndef VULKAN_GLOBAL_FUNCTION
#define VULKAN_GLOBAL_FUNCTION(name)
#endif
VULKAN_GLOBAL_FUNCTION(vkCreateInstance)
VULKAN_GLOBAL_FUNCTION(vkEnumerateInstanceExtensionProperties)
VULKAN_GLOBAL_FUNCTION(vkEnumerateInstanceLayerProperties)

/*
 * vkInstance, created by global vkCreateInstance function
 */

#ifndef VULKAN_INSTANCE_FUNCTION
#define VULKAN_INSTANCE_FUNCTION(ext, ret, func, params)
#endif
VULKAN_INSTANCE_FUNCTION(BaseVK, PFN_vkVoidFunction, vkGetDeviceProcAddr, (VkDevice device, const char *pName))
VULKAN_INSTANCE_FUNCTION(BaseVK, VkResult, vkCreateDevice, (VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDevice *pDevice))
VULKAN_INSTANCE_FUNCTION(BaseVK, void, vkDestroyInstance, (VkInstance instance, const VkAllocationCallbacks *pAllocator))
VULKAN_INSTANCE_FUNCTION(BaseVK, void, vkDestroySurfaceKHR, (VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator))
VULKAN_INSTANCE_FUNCTION(BaseVK, VkResult, vkEnumerateDeviceExtensionProperties, (VkPhysicalDevice physicalDevice, const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties))
VULKAN_INSTANCE_FUNCTION(BaseVK, VkResult, vkEnumeratePhysicalDevices, (VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices))
VULKAN_INSTANCE_FUNCTION(BaseVK, void, vkGetPhysicalDeviceFeatures, (VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures *pFeatures))
VULKAN_INSTANCE_FUNCTION(BaseVK, void, vkGetPhysicalDeviceFormatProperties, (VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties *pFormatProperties))
VULKAN_INSTANCE_FUNCTION(BaseVK, VkResult, vkGetPhysicalDeviceImageFormatProperties, (VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties *pImageFormatProperties))
VULKAN_INSTANCE_FUNCTION(BaseVK, void, vkGetPhysicalDeviceMemoryProperties, (VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties *pMemoryProperties))
VULKAN_INSTANCE_FUNCTION(BaseVK, void, vkGetPhysicalDeviceProperties, (VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties *pProperties))
VULKAN_INSTANCE_FUNCTION(BaseVK, void, vkGetPhysicalDeviceProperties2KHR, (VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2 *pProperties))
VULKAN_INSTANCE_FUNCTION(BaseVK, void, vkGetPhysicalDeviceQueueFamilyProperties, (VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties *pQueueFamilyProperties))
VULKAN_INSTANCE_FUNCTION(BaseVK, VkResult, vkGetPhysicalDeviceSurfaceCapabilitiesKHR, (VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *pSurfaceCapabilities))
VULKAN_INSTANCE_FUNCTION(BaseVK, VkResult, vkGetPhysicalDeviceSurfaceFormatsKHR, (VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pSurfaceFormatCount, VkSurfaceFormatKHR *pSurfaceFormats))
VULKAN_INSTANCE_FUNCTION(BaseVK, VkResult, vkGetPhysicalDeviceSurfacePresentModesKHR, (VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes))
VULKAN_INSTANCE_FUNCTION(BaseVK, VkResult, vkGetPhysicalDeviceSurfaceSupportKHR, (VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32 *pSupported))

/* Optional debug feature, used by SetStringMarker */
VULKAN_INSTANCE_FUNCTION(VK_EXT_debug_utils, void, vkCmdInsertDebugUtilsLabelEXT, (VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT *pMarkerInfo))

/*
 * vkDevice, created by a vkInstance
 */

#ifndef VULKAN_DEVICE_FUNCTION
#define VULKAN_DEVICE_FUNCTION(ext, ret, func, params)
#endif
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkAcquireNextImageKHR, (VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkAllocateCommandBuffers, (VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo, VkCommandBuffer *pCommandBuffers))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkAllocateDescriptorSets, (VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo, VkDescriptorSet *pDescriptorSets))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkAllocateMemory, (VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo, const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkBeginCommandBuffer, (VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkBindBufferMemory, (VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkBindImageMemory, (VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdBeginRenderPass, (VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin, VkSubpassContents contents))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdBindDescriptorSets, (VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t *pDynamicOffsets))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdBindIndexBuffer, (VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdBindPipeline, (VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdBindVertexBuffers, (VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdBlitImage, (VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit *pRegions, VkFilter filter))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdClearAttachments, (VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment *pAttachments, uint32_t rectCount, const VkClearRect *pRects))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdClearColorImage, (VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue *pColor, uint32_t rangeCount, const VkImageSubresourceRange *pRanges))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdClearDepthStencilImage, (VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue *pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange *pRanges))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdCopyBufferToImage, (VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy *pRegions))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdCopyImageToBuffer, (VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy *pRegions))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdDraw, (VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdDrawIndexed, (VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdEndRenderPass, (VkCommandBuffer commandBuffer))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdPipelineBarrier, (VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdResolveImage, (VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve *pRegions))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdSetBlendConstants, (VkCommandBuffer commandBuffer, const float blendConstants[4]))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdSetDepthBias, (VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdSetScissor, (VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D *pScissors))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdSetStencilReference, (VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdSetViewport, (VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport *pViewports))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateBuffer, (VkDevice device, const VkBufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateCommandPool, (VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateDescriptorPool, (VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateDescriptorSetLayout, (VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateFence, (VkDevice device, const VkFenceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFence *pFence))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateFramebuffer, (VkDevice device, const VkFramebufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateGraphicsPipelines, (VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo *pCreateInfos, const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateImage, (VkDevice device, const VkImageCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImage *pImage))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateImageView, (VkDevice device, const VkImageViewCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImageView *pView))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreatePipelineCache, (VkDevice device, const VkPipelineCacheCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkPipelineCache *pPipelineCache))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreatePipelineLayout, (VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkPipelineLayout *pPipelineLayout))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateRenderPass, (VkDevice device, const VkRenderPassCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateSampler, (VkDevice device, const VkSamplerCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSampler *pSampler))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateSemaphore, (VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateShaderModule, (VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateSwapchainKHR, (VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkCreateQueryPool, (VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkQueryPool *pQueryPool))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyBuffer, (VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyCommandPool, (VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyDescriptorPool, (VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyDescriptorSetLayout, (VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyDevice, (VkDevice device, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyFence, (VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyFramebuffer, (VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyImage, (VkDevice device, VkImage image, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyImageView, (VkDevice device, VkImageView imageView, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyPipeline, (VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyPipelineCache, (VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyPipelineLayout, (VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyRenderPass, (VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroySampler, (VkDevice device, VkSampler sampler, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroySemaphore, (VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroySwapchainKHR, (VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkDestroyQueryPool, (VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkDeviceWaitIdle, (VkDevice device))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkEndCommandBuffer, (VkCommandBuffer commandBuffer))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkFreeCommandBuffers, (VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkFreeMemory, (VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *pAllocator))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkGetBufferMemoryRequirements2KHR, (VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkGetDeviceQueue, (VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkGetPipelineCacheData, (VkDevice device, VkPipelineCache pipelineCache, size_t* pDataSize, void* pData))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkGetImageMemoryRequirements2KHR, (VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkGetFenceStatus, (VkDevice device, VkFence fence))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkGetSwapchainImagesKHR, (VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkMapMemory, (VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void **ppData))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkQueuePresentKHR, (VkQueue queue, const VkPresentInfoKHR *pPresentInfo))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkQueueSubmit, (VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkQueueWaitIdle, (VkQueue queue))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkResetCommandBuffer, (VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkResetCommandPool, (VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkResetDescriptorPool, (VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkResetFences, (VkDevice device, uint32_t fenceCount, const VkFence *pFences))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkUnmapMemory, (VkDevice device, VkDeviceMemory memory))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkUpdateDescriptorSets, (VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet *pDescriptorCopies))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkWaitForFences, (VkDevice device, uint32_t fenceCount, const VkFence *pFences, VkBool32 waitAll, uint64_t timeout))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdResetQueryPool, (VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdBeginQuery, (VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags))
VULKAN_DEVICE_FUNCTION(BaseVK, void, vkCmdEndQuery, (VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query))
VULKAN_DEVICE_FUNCTION(BaseVK, VkResult, vkGetQueryPoolResults, (VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void *pData, VkDeviceSize stride, VkQueryResultFlags flags))

/*
 * Redefine these every time you include this header!
 */
#undef VULKAN_GLOBAL_FUNCTION
#undef VULKAN_INSTANCE_FUNCTION
#undef VULKAN_DEVICE_FUNCTION
