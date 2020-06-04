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

VULKAN_INSTANCE_FUNCTION(BaseVK, PFN_vkVoidFunction, vkGetDeviceProcAddr, (VkDevice device, const char *pName))
VULKAN_INSTANCE_FUNCTION(BaseVK, VkResult, vkCreateDevice, (VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDevice *pDevice))
VULKAN_INSTANCE_FUNCTION(BaseVK, void, vkDestroyInstance, (VkInstance instance, const VkAllocationCallbacks *pAllocator))
VULKAN_INSTANCE_FUNCTION(BaseVK, void, vkDestroySurfaceKHR, (VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator))
VULKAN_INSTANCE_FUNCTION(BaseVK, VkResult, vkEnumerateDeviceExtensionProperties, (VkPhysicalDevice physicalDevice, const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties))
VULKAN_INSTANCE_FUNCTION(BaseVK, VkResult, vkEnumeratePhysicalDevices, (VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices))
VULKAN_INSTANCE_FUNCTION(BaseVK, void, vkGetPhysicalDeviceFeatures, (VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures *pFeatures))
VULKAN_INSTANCE_FUNCTION(BaseVK, void, vkGetPhysicalDeviceFormatProperties, (VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties))
VULKAN_INSTANCE_FUNCTION(BaseVK, void, vkGetPhysicalDeviceMemoryProperties, (VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties *pMemoryProperties))
VULKAN_INSTANCE_FUNCTION(BaseVK, void, vkGetPhysicalDeviceProperties, (VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties *pProperties))
VULKAN_INSTANCE_FUNCTION(BaseVK, void, vkGetPhysicalDeviceProperties2, (VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2 *pProperties))
VULKAN_INSTANCE_FUNCTION(BaseVK, void, vkGetPhysicalDeviceQueueFamilyProperties, (VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties *pQueueFamilyProperties))
VULKAN_INSTANCE_FUNCTION(BaseVK, VkResult, vkGetPhysicalDeviceSurfaceCapabilitiesKHR, (VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *pSurfaceCapabilities))
VULKAN_INSTANCE_FUNCTION(BaseVK, VkResult, vkGetPhysicalDeviceSurfaceFormatsKHR, (VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pSurfaceFormatCount, VkSurfaceFormatKHR *pSurfaceFormats))
VULKAN_INSTANCE_FUNCTION(BaseVK, VkResult, vkGetPhysicalDeviceSurfacePresentModesKHR, (VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes))
VULKAN_INSTANCE_FUNCTION(BaseVK, VkResult, vkGetPhysicalDeviceSurfaceSupportKHR, (VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32 *pSupported))
