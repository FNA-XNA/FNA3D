/* FNA3D - 3D Graphics Library for FNA
 *
 * Copyright (c) 2020-2024 Ethan Lee
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

#if FNA3D_DRIVER_VULKAN

/* Needed for VK_KHR_portability_subset */
#define VK_ENABLE_BETA_EXTENSIONS

#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"

#include "FNA3D_Driver.h"
#include "FNA3D_Memory.h"
#include "FNA3D_CommandBuffer.h"
#include "FNA3D_PipelineCache.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#define VULKAN_INTERNAL_clamp(val, min, max) SDL_max(min, SDL_min(val, max))

/* Global Vulkan Loader Entry Points */

static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = NULL;

#define VULKAN_GLOBAL_FUNCTION(name) \
	static PFN_##name name = NULL;
#include "FNA3D_Driver_Vulkan_vkfuncs.h"

/* Vulkan Extensions */

typedef struct VulkanExtensions
{
	/* These extensions are required! */

	/* Globally supported */
	uint8_t KHR_swapchain;
	/* Core since 1.1, needed for negative VkViewport::height */
	uint8_t KHR_maintenance1;

	/* These extensions are optional! */

	/* Core since 1.2, but requires annoying paperwork to implement */
	uint8_t KHR_driver_properties;
	/* EXT, probably not going to be Core */
	uint8_t EXT_vertex_attribute_divisor;
	/* Only required for special implementations (i.e. MoltenVK) */
	uint8_t KHR_portability_subset;
	/* Vendor-specific extensions */
	uint8_t GGP_frame_token;
} VulkanExtensions;

static inline uint8_t CheckDeviceExtensions(
	VkExtensionProperties *extensions,
	uint32_t numExtensions,
	VulkanExtensions *supports
) {
	uint32_t i;

	SDL_memset(supports, '\0', sizeof(VulkanExtensions));
	for (i = 0; i < numExtensions; i += 1)
	{
		const char *name = extensions[i].extensionName;
		#define CHECK(ext) \
			if (SDL_strcmp(name, "VK_" #ext) == 0) \
			{ \
				supports->ext = 1; \
			}
		CHECK(KHR_swapchain)
		else CHECK(KHR_maintenance1)
		else CHECK(KHR_driver_properties)
		else CHECK(EXT_vertex_attribute_divisor)
		else CHECK(KHR_portability_subset)
		else CHECK(GGP_frame_token)
		#undef CHECK
	}

	return (	supports->KHR_swapchain &&
			supports->KHR_maintenance1	);
}

static inline uint32_t GetDeviceExtensionCount(VulkanExtensions *supports)
{
	return (
		supports->KHR_swapchain +
		supports->KHR_maintenance1 +
		supports->KHR_driver_properties +
		supports->EXT_vertex_attribute_divisor +
		supports->KHR_portability_subset +
		supports->GGP_frame_token
	);
}

static inline void CreateDeviceExtensionArray(
	VulkanExtensions *supports,
	const char **extensions
) {
	uint8_t cur = 0;
	#define CHECK(ext) \
		if (supports->ext) \
		{ \
			extensions[cur++] = "VK_" #ext; \
		}
	CHECK(KHR_swapchain)
	CHECK(KHR_maintenance1)
	CHECK(KHR_driver_properties)
	CHECK(EXT_vertex_attribute_divisor)
	CHECK(KHR_portability_subset)
	CHECK(GGP_frame_token)
	#undef CHECK
}

/* Constants/Limits */

#define TEXTURE_COUNT MAX_TOTAL_SAMPLERS
#define MAX_MULTISAMPLE_MASK_SIZE 2
#define MAX_QUERIES 16
#define MAX_UNIFORM_DESCRIPTOR_SETS 1024

/* Should be equivalent to the number of values in FNA3D_PrimitiveType */
#define PRIMITIVE_TYPES_COUNT 5

#define STARTING_SAMPLER_DESCRIPTOR_POOL_SIZE 16

#define DEFAULT_PIPELINE_CACHE_FILE_NAME "FNA3D_Vulkan_PipelineCache.blob"

#define WINDOW_SWAPCHAIN_DATA "FNA3D_VulkanSwapchain"

#define IDENTITY_SWIZZLE \
{ \
	VK_COMPONENT_SWIZZLE_IDENTITY, \
	VK_COMPONENT_SWIZZLE_IDENTITY, \
	VK_COMPONENT_SWIZZLE_IDENTITY, \
	VK_COMPONENT_SWIZZLE_IDENTITY \
}

static const VkComponentMapping RGBA_SWIZZLE =
{
	VK_COMPONENT_SWIZZLE_R,
	VK_COMPONENT_SWIZZLE_G,
	VK_COMPONENT_SWIZZLE_B,
	VK_COMPONENT_SWIZZLE_A
};

static const uint8_t DEVICE_PRIORITY[] =
{
	0,	/* VK_PHYSICAL_DEVICE_TYPE_OTHER */
	3,	/* VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU */
	4,	/* VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU */
	2,	/* VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU */
	1	/* VK_PHYSICAL_DEVICE_TYPE_CPU */
};

/* Enumerations */

typedef enum VulkanResourceAccessType
{
	/* Reads */
	RESOURCE_ACCESS_NONE, /* For initialization */
	RESOURCE_ACCESS_INDEX_BUFFER,
	RESOURCE_ACCESS_VERTEX_BUFFER,
	RESOURCE_ACCESS_VERTEX_SHADER_READ_UNIFORM_BUFFER,
	RESOURCE_ACCESS_VERTEX_SHADER_READ_SAMPLED_IMAGE,
	RESOURCE_ACCESS_FRAGMENT_SHADER_READ_UNIFORM_BUFFER,
	RESOURCE_ACCESS_FRAGMENT_SHADER_READ_SAMPLED_IMAGE,
	RESOURCE_ACCESS_FRAGMENT_SHADER_READ_COLOR_ATTACHMENT,
	RESOURCE_ACCESS_FRAGMENT_SHADER_READ_DEPTH_STENCIL_ATTACHMENT,
	RESOURCE_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE,
	RESOURCE_ACCESS_COLOR_ATTACHMENT_READ,
	RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ,
	RESOURCE_ACCESS_TRANSFER_READ,
	RESOURCE_ACCESS_HOST_READ,
	RESOURCE_ACCESS_PRESENT,
	RESOURCE_ACCESS_END_OF_READ,

	/* Writes */
	RESOURCE_ACCESS_VERTEX_SHADER_WRITE,
	RESOURCE_ACCESS_FRAGMENT_SHADER_WRITE,
	RESOURCE_ACCESS_COLOR_ATTACHMENT_WRITE,
	RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE,
	RESOURCE_ACCESS_TRANSFER_WRITE,
	RESOURCE_ACCESS_HOST_WRITE,

	/* Read-Writes */
	RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE,
	RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_WRITE,
	RESOURCE_ACCESS_MEMORY_TRANSFER_READ_WRITE,
	RESOURCE_ACCESS_GENERAL,

	/* Count */
	RESOURCE_ACCESS_TYPES_COUNT
} VulkanResourceAccessType;

typedef enum CreateSwapchainResult
{
	CREATE_SWAPCHAIN_FAIL,
	CREATE_SWAPCHAIN_SUCCESS,
	CREATE_SWAPCHAIN_SURFACE_ZERO,
} CreateSwapchainResult;

/* Image Barriers */

typedef struct VulkanResourceAccessInfo
{
	VkPipelineStageFlags stageMask;
	VkAccessFlags accessMask;
	VkImageLayout imageLayout;
} VulkanResourceAccessInfo;

static const VulkanResourceAccessInfo AccessMap[RESOURCE_ACCESS_TYPES_COUNT] =
{
	/* RESOURCE_ACCESS_NONE */
	{
		0,
		0,
		VK_IMAGE_LAYOUT_UNDEFINED
	},

	/* RESOURCE_ACCESS_INDEX_BUFFER */
	{
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		VK_ACCESS_INDEX_READ_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED
	},

	/* RESOURCE_ACCESS_VERTEX_BUFFER */
	{
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		VK_ACCESS_INDEX_READ_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED
	},

	/* RESOURCE_ACCESS_VERTEX_SHADER_READ_UNIFORM_BUFFER */
	{
		VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
		VK_ACCESS_SHADER_READ_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED
	},

	/* RESOURCE_ACCESS_VERTEX_SHADER_READ_SAMPLED_IMAGE */
	{
		VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
		VK_ACCESS_SHADER_READ_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	},

	/* RESOURCE_ACCESS_FRAGMENT_SHADER_READ_UNIFORM_BUFFER */
	{
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_UNIFORM_READ_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED
	},

	/* RESOURCE_ACCESS_FRAGMENT_SHADER_READ_SAMPLED_IMAGE */
	{
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_SHADER_READ_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	},

	/* RESOURCE_ACCESS_FRAGMENT_SHADER_READ_COLOR_ATTACHMENT */
	{
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	},

	/* RESOURCE_ACCESS_FRAGMENT_SHADER_READ_DEPTH_STENCIL_ATTACHMENT */
	{
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
	},

	/* RESOURCE_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE */
	{
		VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	},

	/* RESOURCE_ACCESS_COLOR_ATTACHMENT_READ */
	{
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	},

	/* RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ */
	{
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
	},

	/* RESOURCE_ACCESS_TRANSFER_READ */
	{
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_TRANSFER_READ_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
	},

	/* RESOURCE_ACCESS_HOST_READ */
	{
		VK_PIPELINE_STAGE_HOST_BIT,
		VK_ACCESS_HOST_READ_BIT,
		VK_IMAGE_LAYOUT_GENERAL
	},

	/* RESOURCE_ACCESS_PRESENT */
	{
		0,
		0,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
	},

	/* RESOURCE_ACCESS_END_OF_READ */
	{
		0,
		0,
		VK_IMAGE_LAYOUT_UNDEFINED
	},

	/* RESOURCE_ACCESS_VERTEX_SHADER_WRITE */
	{
		VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
		VK_ACCESS_SHADER_WRITE_BIT,
		VK_IMAGE_LAYOUT_GENERAL
	},

	/* RESOURCE_ACCESS_FRAGMENT_SHADER_WRITE */
	{
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_SHADER_WRITE_BIT,
		VK_IMAGE_LAYOUT_GENERAL
	},

	/* RESOURCE_ACCESS_COLOR_ATTACHMENT_WRITE */
	{
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	},

	/* RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE */
	{
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	},

	/* RESOURCE_ACCESS_TRANSFER_WRITE */
	{
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
	},

	/* RESOURCE_ACCESS_HOST_WRITE */
	{
		VK_PIPELINE_STAGE_HOST_BIT,
		VK_ACCESS_HOST_WRITE_BIT,
		VK_IMAGE_LAYOUT_GENERAL
	},

	/* RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE */
	{
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	},

	/* RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_WRITE */
	{
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	},

	/* RESOURCE_ACCESS_MEMORY_TRANSFER_READ_WRITE */
	{
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED
	},

	/* RESOURCE_ACCESS_GENERAL */
	{
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
		VK_IMAGE_LAYOUT_GENERAL
	}
};

/* Shader Resources */

typedef struct ShaderResources
{
	VkDescriptorPool *samplerDescriptorPools;
	uint32_t samplerDescriptorPoolCount;
	uint32_t nextPoolSize;

	VkDescriptorSetLayout samplerLayout;
	uint8_t *samplerBindingIndices;
	uint32_t samplerCount;

	VkDescriptorSet *inactiveDescriptorSets;
	uint32_t inactiveDescriptorSetCount;
	uint32_t inactiveDescriptorSetCapacity;

	VkDescriptorSet uniformDescriptorSet;
	VkDescriptorBufferInfo uniformBufferInfo;

	/* VK_NULL_HANDLE unless samplerCount is 0 */
	VkDescriptorSet dummySamplerDescriptorSet;
} ShaderResources;

typedef struct ShaderResourcesHashMap
{
	MOJOSHADER_vkShader *key;
	ShaderResources *value;
} ShaderResourcesHashMap;

typedef struct ShaderResourcesHashArray
{
	ShaderResourcesHashMap *elements;
	int32_t count;
	int32_t capacity;
} ShaderResourcesHashArray;

#define NUM_SHADER_RESOURCES_BUCKETS 1031

typedef struct ShaderResourcesHashTable
{
	ShaderResourcesHashArray buckets[NUM_SHADER_RESOURCES_BUCKETS];
} ShaderResourcesHashTable;

static inline ShaderResources *ShaderResourcesHashTable_Fetch(
	ShaderResourcesHashTable *table,
	MOJOSHADER_vkShader *key
) {
	int32_t i;
	uint64_t hashcode = (uint64_t) (size_t) key;
	ShaderResourcesHashArray *arr = &table->buckets[hashcode % NUM_SHADER_RESOURCES_BUCKETS];

	for (i = 0; i < arr->count; i += 1)
	{
		const MOJOSHADER_vkShader *e = arr->elements[i].key;
		if (key == e)
		{
			return arr->elements[i].value;
		}
	}

	return NULL;
}

static inline void ShaderResourcesHashTable_Insert(
	ShaderResourcesHashTable *table,
	MOJOSHADER_vkShader *key,
	ShaderResources *value
) {
	uint64_t hashcode = (uint64_t) (size_t) key;
	ShaderResourcesHashArray *arr = &table->buckets[hashcode % NUM_SHADER_RESOURCES_BUCKETS];

	ShaderResourcesHashMap map;
	map.key = key;
	map.value = value;

	EXPAND_ARRAY_IF_NEEDED(arr, 2, ShaderResourcesHashMap)

	arr->elements[arr->count] = map;
	arr->count += 1;
}

/* Internal Structures */

typedef struct SwapChainSupportDetails
{
	VkSurfaceCapabilitiesKHR capabilities;
	VkSurfaceFormatKHR *formats;
	uint32_t formatsLength;
	VkPresentModeKHR *presentModes;
	uint32_t presentModesLength;
} SwapChainSupportDetails;

/* FIXME: This could be packed better */
typedef struct PipelineHash
{
	PackedState blendState;
	PackedState rasterizerState;
	PackedState depthStencilState;
	uint32_t vertexBufferBindingsIndex;
	FNA3D_PrimitiveType primitiveType;
	VkSampleMask sampleMask;
	MOJOSHADER_vkShader *vertShader;
	MOJOSHADER_vkShader *fragShader;
	/* Pipelines have to be compatible with a render pass */
	VkRenderPass renderPass;
} PipelineHash;

typedef struct PipelineHashMap
{
	PipelineHash key;
	VkPipeline value;
} PipelineHashMap;

typedef struct PipelineHashArray
{
	PipelineHashMap *elements;
	int32_t count;
	int32_t capacity;
} PipelineHashArray;

#define NUM_PIPELINE_HASH_BUCKETS 1031

typedef struct PipelineHashTable
{
	PipelineHashArray buckets[NUM_PIPELINE_HASH_BUCKETS];
} PipelineHashTable;

static inline uint64_t PipelineHashTable_GetHashCode(PipelineHash hash)
{
	/* The algorithm for this hashing function
	 * is taken from Josh Bloch's "Effective Java".
	 * (https://stackoverflow.com/a/113600/12492383)
	 */
	const uint64_t HASH_FACTOR = 97;
	uint64_t result = 1;
	result = result * HASH_FACTOR + hash.blendState.a;
	result = result * HASH_FACTOR + hash.blendState.b;
	result = result * HASH_FACTOR + hash.rasterizerState.a;
	result = result * HASH_FACTOR + hash.rasterizerState.b;
	result = result * HASH_FACTOR + hash.depthStencilState.a;
	result = result * HASH_FACTOR + hash.depthStencilState.b;
	result = result * HASH_FACTOR + hash.vertexBufferBindingsIndex;
	result = result * HASH_FACTOR + hash.primitiveType;
	result = result * HASH_FACTOR + hash.sampleMask;
	result = result * HASH_FACTOR + (uint64_t) (size_t) hash.vertShader;
	result = result * HASH_FACTOR + (uint64_t) (size_t) hash.fragShader;
	result = result * HASH_FACTOR + (uint64_t) (size_t) hash.renderPass;
	return result;
}

static inline VkPipeline PipelineHashTable_Fetch(
	PipelineHashTable *table,
	PipelineHash key
) {
	int32_t i;
	uint64_t hashcode = PipelineHashTable_GetHashCode(key);
	PipelineHashArray *arr = &table->buckets[hashcode % NUM_PIPELINE_HASH_BUCKETS];

	for (i = 0; i < arr->count; i += 1)
	{
		const PipelineHash *e = &arr->elements[i].key;
		if (	key.blendState.a == e->blendState.a &&
			key.blendState.b == e->blendState.b &&
			key.rasterizerState.a == e->rasterizerState.a &&
			key.rasterizerState.b == e->rasterizerState.b &&
			key.depthStencilState.a == e->depthStencilState.a &&
			key.depthStencilState.b == e->depthStencilState.b &&
			key.vertexBufferBindingsIndex == e->vertexBufferBindingsIndex &&
			key.primitiveType == e->primitiveType &&
			key.sampleMask == e->sampleMask &&
			key.vertShader == e->vertShader &&
			key.fragShader == e->fragShader &&
			key.renderPass == e->renderPass	)
		{
			return arr->elements[i].value;
		}
	}

	return VK_NULL_HANDLE;
}

static inline void PipelineHashTable_Insert(
	PipelineHashTable *table,
	PipelineHash key,
	VkPipeline value
) {
	uint64_t hashcode = PipelineHashTable_GetHashCode(key);
	PipelineHashArray *arr = &table->buckets[hashcode % NUM_PIPELINE_HASH_BUCKETS];
	PipelineHashMap map;
	map.key = key;
	map.value = value;

	EXPAND_ARRAY_IF_NEEDED(arr, 2, PipelineHashMap)

	arr->elements[arr->count] = map;
	arr->count += 1;
}

typedef struct RenderPassHash
{
	VkFormat colorAttachmentFormatOne;
	VkFormat colorAttachmentFormatTwo;
	VkFormat colorAttachmentFormatThree;
	VkFormat colorAttachmentFormatFour;
	VkFormat depthStencilAttachmentFormat;
	uint32_t width;
	uint32_t height;
	uint32_t multiSampleCount;
	uint8_t  clearColor;
	uint8_t  clearDepth;
	uint8_t  clearStencil;
	uint8_t  preserveTargetContents;
} RenderPassHash;

typedef struct RenderPassHashMap
{
	RenderPassHash key;
	VkRenderPass value;
} RenderPassHashMap;

typedef struct RenderPassHashArray
{
	RenderPassHashMap *elements;
	int32_t count;
	int32_t capacity;
} RenderPassHashArray;

static inline VkRenderPass RenderPassHashArray_Fetch(
	RenderPassHashArray *arr,
	RenderPassHash key
) {
	int32_t i;

	for (i = 0; i < arr->count; i += 1)
	{
		const RenderPassHash *e = &arr->elements[i].key;
		if (	key.colorAttachmentFormatOne == e->colorAttachmentFormatOne &&
			key.colorAttachmentFormatTwo == e->colorAttachmentFormatTwo &&
			key.colorAttachmentFormatThree == e->colorAttachmentFormatThree &&
			key.colorAttachmentFormatFour == e->colorAttachmentFormatFour &&
			key.depthStencilAttachmentFormat == e->depthStencilAttachmentFormat &&
			key.width == e->width &&
			key.height == e->height &&
			key.multiSampleCount == e->multiSampleCount &&
			key.clearColor == e->clearColor &&
			key.clearDepth == e->clearDepth &&
			key.clearStencil == e->clearStencil &&
			key.preserveTargetContents == e->preserveTargetContents	)
		{
			return arr->elements[i].value;
		}
	}

	return VK_NULL_HANDLE;
}

static inline void RenderPassHashArray_Insert(
	RenderPassHashArray *arr,
	RenderPassHash key,
	VkRenderPass value
) {
	RenderPassHashMap map;

	map.key = key;
	map.value = value;

	EXPAND_ARRAY_IF_NEEDED(arr, 4, RenderPassHashMap)

	arr->elements[arr->count] = map;
	arr->count += 1;
}

typedef struct FramebufferHash
{
	VkImageView colorAttachmentViews[MAX_RENDERTARGET_BINDINGS];
	VkImageView colorMultiSampleAttachmentViews[MAX_RENDERTARGET_BINDINGS];
	VkImageView depthStencilAttachmentView;
	uint32_t width;
	uint32_t height;
} FramebufferHash;

typedef struct FramebufferHashMap
{
	FramebufferHash key;
	VkFramebuffer value;
} FramebufferHashMap;

typedef struct FramebufferHashArray
{
	FramebufferHashMap *elements;
	int32_t count;
	int32_t capacity;
} FramebufferHashArray;

static inline VkFramebuffer FramebufferHashArray_Fetch(
	FramebufferHashArray *arr,
	FramebufferHash key
) {
	int32_t i;

	for (i = 0; i < arr->count; i += 1)
	{
		const FramebufferHash *e = &arr->elements[i].key;
		if (	SDL_memcmp(key.colorAttachmentViews, e->colorAttachmentViews, sizeof(key.colorAttachmentViews)) == 0 &&
			SDL_memcmp(key.colorAttachmentViews, e->colorAttachmentViews, sizeof(key.colorAttachmentViews)) == 0 &&
			key.depthStencilAttachmentView == e->depthStencilAttachmentView &&
			key.width == e->width &&
			key.height == e->height	)
		{
			return arr->elements[i].value;
		}
	}

	return VK_NULL_HANDLE;
}

static inline void FramebufferHashArray_Insert(
	FramebufferHashArray *arr,
	FramebufferHash key,
	VkFramebuffer value
) {
	FramebufferHashMap map;
	map.key = key;
	map.value = value;

	EXPAND_ARRAY_IF_NEEDED(arr, 4, FramebufferHashMap)

	arr->elements[arr->count] = map;
	arr->count += 1;
}

static inline void FramebufferHashArray_Remove(
	FramebufferHashArray *arr,
	uint32_t index
) {
	if (index != arr->count - 1)
	{
		arr->elements[index] = arr->elements[arr->count - 1];
	}

	arr->count -= 1;
}

typedef struct SamplerStateHashMap
{
	PackedState key;
	VkSampler value;
} SamplerStateHashMap;

typedef struct SamplerStateHashArray
{
	SamplerStateHashMap *elements;
	int32_t count;
	int32_t capacity;
} SamplerStateHashArray;

static inline VkSampler SamplerStateHashArray_Fetch(
	SamplerStateHashArray *arr,
	PackedState key
) {
	int32_t i;

	for (i = 0; i < arr->count; i += 1)
	{
		if (	key.a == arr->elements[i].key.a &&
			key.b == arr->elements[i].key.b		)
		{
			return arr->elements[i].value;
		}
	}

	return VK_NULL_HANDLE;
}

static inline void SamplerStateHashArray_Insert(
	SamplerStateHashArray *arr,
	PackedState key,
	VkSampler value
) {
	SamplerStateHashMap map;
	map.key.a = key.a;
	map.key.b = key.b;
	map.value = value;

	EXPAND_ARRAY_IF_NEEDED(arr, 4, SamplerStateHashMap)

	arr->elements[arr->count] = map;
	arr->count += 1;
}

typedef struct DescriptorSetLayoutHash
{
	VkDescriptorType descriptorType;
	uint16_t bitmask;
	VkShaderStageFlagBits stageFlag;
} DescriptorSetLayoutHash;

typedef struct DescriptorSetLayoutHashMap
{
	DescriptorSetLayoutHash key;
	VkDescriptorSetLayout value;
} DescriptorSetLayoutHashMap;

typedef struct DescriptorSetLayoutHashArray
{
	DescriptorSetLayoutHashMap *elements;
	int32_t count;
	int32_t capacity;
} DescriptorSetLayoutHashArray;

#define NUM_DESCRIPTOR_SET_LAYOUT_BUCKETS 1031

typedef struct DescriptorSetLayoutHashTable
{
	DescriptorSetLayoutHashArray buckets[NUM_DESCRIPTOR_SET_LAYOUT_BUCKETS];
} DescriptorSetLayoutHashTable;

static inline uint64_t DescriptorSetLayoutHashTable_GetHashCode(DescriptorSetLayoutHash key)
{
	const uint64_t HASH_FACTOR = 97;
	uint64_t result = 1;
	result = result * HASH_FACTOR + key.descriptorType;
	result = result * HASH_FACTOR + key.bitmask;
	result = result * HASH_FACTOR + key.stageFlag;
	return result;
}

static inline VkDescriptorSetLayout DescriptorSetLayoutHashTable_Fetch(
	DescriptorSetLayoutHashTable *table,
	DescriptorSetLayoutHash key
) {
	int32_t i;
	uint64_t hashcode = DescriptorSetLayoutHashTable_GetHashCode(key);
	DescriptorSetLayoutHashArray *arr = &table->buckets[hashcode % NUM_DESCRIPTOR_SET_LAYOUT_BUCKETS];

	for (i = 0; i < arr->count; i += 1)
	{
		const DescriptorSetLayoutHash *e = &arr->elements[i].key;
		if (    key.descriptorType == e->descriptorType &&
			key.bitmask == e->bitmask &&
			key.stageFlag == e->stageFlag   )
		{
			return arr->elements[i].value;
		}
	}

	return VK_NULL_HANDLE;
}

static inline void DescriptorSetLayoutHashTable_Insert(
	DescriptorSetLayoutHashTable *table,
	DescriptorSetLayoutHash key,
	VkDescriptorSetLayout value
) {
	uint64_t hashcode = DescriptorSetLayoutHashTable_GetHashCode(key);
	DescriptorSetLayoutHashArray *arr = &table->buckets[hashcode % NUM_DESCRIPTOR_SET_LAYOUT_BUCKETS];

	DescriptorSetLayoutHashMap map;
	map.key = key;
	map.value = value;

	EXPAND_ARRAY_IF_NEEDED(arr, 4, DescriptorSetLayoutHashMap);

	arr->elements[arr->count] = map;
	arr->count += 1;
}

typedef struct PipelineLayoutHash
{
	VkDescriptorSetLayout vertexSamplerLayout;
	VkDescriptorSetLayout fragSamplerLayout;
	VkDescriptorSetLayout vertexUniformLayout;
	VkDescriptorSetLayout fragUniformLayout;
} PipelineLayoutHash;

typedef struct PipelineLayoutHashMap
{
	PipelineLayoutHash key;
	VkPipelineLayout value;
} PipelineLayoutHashMap;

typedef struct PipelineLayoutHashArray
{
	PipelineLayoutHashMap *elements;
	int32_t count;
	int32_t capacity;
} PipelineLayoutHashArray;

#define NUM_PIPELINE_LAYOUT_BUCKETS 1031

typedef struct PipelineLayoutHashTable
{
	PipelineLayoutHashArray buckets[NUM_PIPELINE_LAYOUT_BUCKETS];
} PipelineLayoutHashTable;

static inline uint64_t PipelineLayoutHashTable_GetHashCode(PipelineLayoutHash key)
{
	const uint64_t HASH_FACTOR = 97;
	uint64_t result = 1;
	result = result * HASH_FACTOR + (uint64_t) key.vertexSamplerLayout;
	result = result * HASH_FACTOR + (uint64_t) key.fragSamplerLayout;
	result = result * HASH_FACTOR + (uint64_t) key.vertexUniformLayout;
	result = result * HASH_FACTOR + (uint64_t) key.fragUniformLayout;
	return result;
}

static inline VkPipelineLayout PipelineLayoutHashArray_Fetch(
	PipelineLayoutHashTable *table,
	PipelineLayoutHash key
) {
	int32_t i;
	uint64_t hashcode = PipelineLayoutHashTable_GetHashCode(key);
	PipelineLayoutHashArray *arr = &table->buckets[hashcode % NUM_PIPELINE_LAYOUT_BUCKETS];

	for (i = 0; i < arr->count; i += 1)
	{
		const PipelineLayoutHash *e = &arr->elements[i].key;
		if (	key.vertexSamplerLayout == e->vertexSamplerLayout &&
			key.fragSamplerLayout == e->fragSamplerLayout &&
			key.vertexUniformLayout == e->vertexUniformLayout &&
			key.fragUniformLayout == e->fragUniformLayout	)
		{
			return arr->elements[i].value;
		}
	}

	return VK_NULL_HANDLE;
}

static inline void PipelineLayoutHashArray_Insert(
	PipelineLayoutHashTable *table,
	PipelineLayoutHash key,
	VkPipelineLayout value
) {
	uint64_t hashcode = PipelineLayoutHashTable_GetHashCode(key);
	PipelineLayoutHashArray *arr = &table->buckets[hashcode % NUM_PIPELINE_HASH_BUCKETS];

	PipelineLayoutHashMap map;
	map.key = key;
	map.value = value;

	EXPAND_ARRAY_IF_NEEDED(arr, 4, PipelineLayoutHashMap)

	arr->elements[arr->count] = map;
	arr->count += 1;
}

typedef struct VulkanSwapchainData
{
	/* Window surface */
	VkSurfaceKHR surface;
	VkSurfaceFormatKHR surfaceFormat;
	void *windowHandle;

	/* Swapchain for window surface */
	VkSwapchainKHR swapchain;
	VkFormat swapchainFormat;
	VkComponentMapping swapchainSwizzle;
	VkPresentModeKHR presentMode;

	/* Swapchain images */
	VkExtent2D extent;
	VkImage *images;
	VkImageView *views;
	VulkanResourceAccessType *resourceAccessTypes;
	uint32_t imageCount;

	/* Synchronization */
	VkSemaphore imageAvailableSemaphore;
	VkSemaphore renderFinishedSemaphore;
	VkFence fence; /* owned by command buffer container */
} VulkanSwapchainData;

typedef struct VulkanBuffer VulkanBuffer;
typedef struct VulkanTexture VulkanTexture;

struct VulkanTexture /* Cast from FNA3D_Texture* */
{
	FNA3D_MemoryUsedRegion *usedRegion;
	VkImage image;
	VkImageView view;
	VkImageView rtViews[6];
	VkExtent2D dimensions;
	uint32_t depth;
	uint8_t external;
	VkFormat surfaceFormat;
	uint32_t layerCount;
	uint32_t levelCount;
	uint8_t isRenderTarget;
	VulkanResourceAccessType resourceAccessType;
	VkImageCreateInfo imageCreateInfo; /* used for resource copy */
	VkImageViewCreateInfo viewCreateInfo; /* used for resource copy */
	FNA3DNAMELESS union
	{
		FNA3D_SurfaceFormat colorFormat;
		FNA3D_DepthFormat depthStencilFormat;
	};
};

static VulkanTexture NullTexture =
{
	(FNA3D_MemoryUsedRegion*) 0,
	(VkImage) 0,
	(VkImageView) 0,
	{ 0, 0, 0, 0, 0, 0 },
	{0, 0},
	0,
	0,
	VK_FORMAT_UNDEFINED,
	0,
	RESOURCE_ACCESS_NONE
};

struct VulkanBuffer
{
	VkDeviceSize size;
	FNA3D_MemoryUsedRegion *usedRegion;
	VkBuffer buffer;
	VulkanResourceAccessType resourceAccessType;
	VkBufferCreateInfo bufferCreateInfo; /* used for resource copy */
	VkBufferUsageFlags usage;
	uint8_t preferDeviceLocal;
	uint8_t isTransferBuffer;
	SDL_atomic_t refcount;
};

typedef struct VulkanColorBuffer
{
	VulkanTexture *handle;
	VulkanTexture *multiSampleTexture;
	uint32_t multiSampleCount;
} VulkanColorBuffer;

typedef struct VulkanDepthStencilBuffer
{
	VulkanTexture *handle;
} VulkanDepthStencilBuffer;

typedef struct VulkanRenderbuffer /* Cast from FNA3D_Renderbuffer* */
{
	VulkanColorBuffer *colorBuffer;
	VulkanDepthStencilBuffer *depthBuffer;
} VulkanRenderbuffer;

typedef struct VulkanEffect /* Cast from FNA3D_Effect* */
{
	MOJOSHADER_effect *effect;
} VulkanEffect;

typedef struct VulkanQuery /* Cast from FNA3D_Query* */
{
	uint32_t index;
} VulkanQuery;

typedef struct DescriptorSetData
{
	VkDescriptorSet descriptorSet;
	ShaderResources *parent;
} DescriptorSetData;

typedef struct VulkanCommandBuffer
{
	VkCommandBuffer commandBuffer;
	VkFence inFlightFence;

	DescriptorSetData *usedDescriptorSetDatas;
	uint32_t usedDescriptorSetDataCount;
	uint32_t usedDescriptorSetDataCapacity;
} VulkanCommandBuffer;

typedef struct VulkanRenderer
{
	FNA3D_Device *parentDevice;
	FNA3D_MemoryAllocator *allocator;

	VkInstance instance;
	VkPhysicalDevice physicalDevice;
	VkPhysicalDeviceProperties2 physicalDeviceProperties;
	VkPhysicalDeviceDriverPropertiesKHR physicalDeviceDriverProperties;
	VkDevice logicalDevice;
	uint8_t unifiedMemoryWarning;

	uint32_t queueFamilyIndex;
	VkQueue unifiedQueue;

	VulkanSwapchainData** swapchainDatas;
	uint32_t swapchainDataCount;
	uint32_t swapchainDataCapacity;

	PackedVertexBufferBindingsArray vertexBufferBindingsCache;
	VkPipelineCache pipelineCache;

	VkRenderPass renderPass;
	VkPipeline currentPipeline;
	VkPipelineLayout currentPipelineLayout;
	int32_t currentVertexBufferBindingsIndex;

	/* Command Buffers */
	VkCommandPool commandPool;
	FNA3D_CommandBufferManager *commandBuffers;

	/* Queries */
	VkQueryPool queryPool;
	int8_t freeQueryIndexStack[MAX_QUERIES];
	int8_t freeQueryIndexStackHead;

	FNA3D_SurfaceFormat backbufferFormat;
	FNA3D_PresentInterval presentInterval;

	VulkanColorBuffer fauxBackbufferColor;
	VulkanTexture *fauxBackbufferMultiSampleColor;
	VulkanDepthStencilBuffer fauxBackbufferDepthStencil;
	VkFramebuffer fauxBackbufferFramebuffer;
	uint32_t fauxBackbufferWidth;
	uint32_t fauxBackbufferHeight;
	uint32_t fauxBackbufferMultiSampleCount;

	VulkanTexture *colorAttachments[MAX_RENDERTARGET_BINDINGS];
	VulkanTexture *colorMultiSampleAttachments[MAX_RENDERTARGET_BINDINGS];
	FNA3D_CubeMapFace attachmentCubeFaces[MAX_RENDERTARGET_BINDINGS];
	uint32_t multiSampleCount;
	uint32_t colorAttachmentCount;

	VulkanTexture *depthStencilAttachment;
	FNA3D_DepthFormat currentDepthFormat;

	/* Separating the future render pass setting from the current one*/
	VulkanTexture *nextRenderPassColorAttachments[MAX_RENDERTARGET_BINDINGS];
	VulkanTexture *nextRenderPassColorMultiSampleAttachments[MAX_RENDERTARGET_BINDINGS];
	FNA3D_CubeMapFace nextRenderPassAttachmentCubeFaces[MAX_RENDERTARGET_BINDINGS];
	uint32_t nextRenderPassMultiSampleCount;
	uint32_t nextRenderPassColorAttachmentCount;

	VulkanTexture *nextRenderPassDepthStencilAttachment;
	FNA3D_DepthFormat nextRenderPassDepthFormat;
	uint8_t nextRenderPassPreserveTargetContents;

	FNA3D_Viewport viewport;
	FNA3D_Rect scissorRect;

	VkSampleMask multiSampleMask[MAX_MULTISAMPLE_MASK_SIZE];
	FNA3D_BlendState blendState;

	FNA3D_DepthStencilState depthStencilState;
	FNA3D_RasterizerState rasterizerState;
	FNA3D_PrimitiveType currentPrimitiveType;

	VkPhysicalDeviceMemoryProperties memoryProperties;
	VkDeviceSize maxDeviceLocalHeapUsage;
	VkDeviceSize deviceLocalHeapUsage;

	uint32_t numVertexBindings;
	FNA3D_VertexBufferBinding vertexBindings[MAX_BOUND_VERTEX_BUFFERS];
	FNA3D_VertexElement vertexElements[MAX_BOUND_VERTEX_BUFFERS][MAX_VERTEX_ATTRIBUTES];
	VkBuffer boundVertexBuffers[MAX_BOUND_VERTEX_BUFFERS];
	VkDeviceSize boundVertexBufferOffsets[MAX_BOUND_VERTEX_BUFFERS];

	int32_t stencilRef;

	int32_t numTextureSlots;
	int32_t numVertexTextureSlots;

	VulkanTexture *textures[TEXTURE_COUNT];
	VkSampler samplers[TEXTURE_COUNT];
	uint8_t textureNeedsUpdate[TEXTURE_COUNT];
	uint8_t samplerNeedsUpdate[TEXTURE_COUNT];

	VulkanBuffer *dummyVertUniformBuffer;
	VulkanBuffer *dummyFragUniformBuffer;

	VkSampler dummyVertSamplerState;
	VkSampler dummyVertSampler3DState;
	VkSampler dummyVertSamplerCubeState;
	VkSampler dummyFragSamplerState;
	VkSampler dummyFragSampler3DState;
	VkSampler dummyFragSamplerCubeState;

	VulkanTexture *dummyVertTexture;
	VulkanTexture *dummyVertTexture3D;
	VulkanTexture *dummyVertTextureCube;
	VulkanTexture *dummyFragTexture;
	VulkanTexture *dummyFragTexture3D;
	VulkanTexture *dummyFragTextureCube;

	VkDescriptorPool uniformBufferDescriptorPool;
	VkDescriptorSetLayout vertexUniformBufferDescriptorSetLayout;
	VkDescriptorSetLayout fragUniformBufferDescriptorSetLayout;
	VkDescriptorSet dummyVertexUniformBufferDescriptorSet;
	VkDescriptorSet dummyFragUniformBufferDescriptorSet;

	uint8_t vertexSamplerDescriptorSetDataNeedsUpdate;
	uint8_t fragSamplerDescriptorSetDataNeedsUpdate;

	VkDescriptorSet currentVertexSamplerDescriptorSet;
	VkDescriptorSet currentFragSamplerDescriptorSet;

	ShaderResourcesHashTable shaderResourcesHashTable;
	DescriptorSetLayoutHashTable descriptorSetLayoutTable;
	PipelineLayoutHashTable pipelineLayoutTable;
	PipelineHashTable pipelineHashTable;
	RenderPassHashArray renderPassArray;
	FramebufferHashArray framebufferArray;
	SamplerStateHashArray samplerStateArray;

	VkSemaphore defragSemaphore;

	uint8_t bufferDefragInProgress;
	uint8_t needDefrag;
	uint32_t defragTimer;
	uint8_t resourceFreed;

	VkBuffer *defragmentedBuffersToDestroy;
	uint32_t defragmentedBuffersToDestroyCount;
	uint32_t defragmentedBuffersToDestroyCapacity;

	VkImage *defragmentedImagesToDestroy;
	uint32_t defragmentedImagesToDestroyCount;
	uint32_t defragmentedImagesToDestroyCapacity;

	VkImageView *defragmentedImageViewsToDestroy;
	uint32_t defragmentedImageViewsToDestroyCount;
	uint32_t defragmentedImageViewsToDestroyCapacity;

	/* MojoShader Interop */
	MOJOSHADER_vkContext *mojoshaderContext;
	MOJOSHADER_effect *currentEffect;
	const MOJOSHADER_effectTechnique *currentTechnique;
	uint32_t currentPass;

	VkShaderModule currentVertShader;
	VkShaderModule currentFragShader;

	uint8_t renderPassInProgress;
	uint8_t needNewRenderPass;
	uint8_t renderTargetBound;
	uint8_t needNewPipeline;

	uint8_t shouldClearColorOnBeginPass;
	uint8_t shouldClearDepthOnBeginPass;
	uint8_t shouldClearStencilOnBeginPass;
	uint8_t drawCallMadeThisPass;

	VkClearColorValue clearColorValue;
	VkClearDepthStencilValue clearDepthStencilValue;

	/* Depth Formats (may not match the format implied by the name!) */
	VkFormat D16Format;
	VkFormat D24Format;
	VkFormat D24S8Format;

	/* Capabilities */
	uint8_t supportsDxt1;
	uint8_t supportsS3tc;
	uint8_t supportsBc7;
	uint8_t supportsDebugUtils;
	uint8_t supportsDeviceProperties2;
	uint8_t supportsSRGBRenderTarget;
	uint8_t supportsPreciseOcclusionQueries;
	uint8_t supportsBaseVertex;
	uint8_t debugMode;
	VulkanExtensions supports;

	uint8_t submitCounter; /* used so we don't clobber data being used by GPU */

	/* Threading */
	SDL_mutex *passLock;
	SDL_mutex *disposeLock;

	#define VULKAN_INSTANCE_FUNCTION(name) \
		PFN_##name name;
	#define VULKAN_DEVICE_FUNCTION(name) \
		PFN_##name name;
	#include "FNA3D_Driver_Vulkan_vkfuncs.h"
} VulkanRenderer;

/* Command Buffer Recording Macro */

#define RECORD_CMD(cmdCall)							\
	FNA3D_CommandBuffer_LockForRendering(renderer->commandBuffers);		\
	cmdCall;								\
	FNA3D_CommandBuffer_UnlockFromRendering(renderer->commandBuffers);

/* XNA->Vulkan Translation Arrays */

static VkIndexType XNAToVK_IndexType[] =
{
	VK_INDEX_TYPE_UINT16,	/* FNA3D_INDEXELEMENTSIZE_16BIT */
	VK_INDEX_TYPE_UINT32	/* FNA3D_INDEXELEMENTSIZE_32BIT */
};

static inline VkSampleCountFlagBits XNAToVK_SampleCount(int32_t sampleCount)
{
	if (sampleCount <= 1)
	{
		return VK_SAMPLE_COUNT_1_BIT;
	}
	else if (sampleCount == 2)
	{
		return VK_SAMPLE_COUNT_2_BIT;
	}
	else if (sampleCount <= 4)
	{
		return VK_SAMPLE_COUNT_4_BIT;
	}
	else if (sampleCount <= 8)
	{
		return VK_SAMPLE_COUNT_8_BIT;
	}
	else if (sampleCount <= 16)
	{
		return VK_SAMPLE_COUNT_16_BIT;
	}
	else if (sampleCount <= 32)
	{
		return VK_SAMPLE_COUNT_32_BIT;
	}
	else if (sampleCount <= 64)
	{
		return VK_SAMPLE_COUNT_64_BIT;
	}
	else
	{
		FNA3D_LogWarn("Unexpected sample count: %d", sampleCount);
		return VK_SAMPLE_COUNT_1_BIT;
	}
}

static VkComponentMapping XNAToVK_SurfaceSwizzle[] =
{
	IDENTITY_SWIZZLE,	/* SurfaceFormat.Color */
	{			/* SurfaceFormat.Bgr565 */
		VK_COMPONENT_SWIZZLE_B,
		VK_COMPONENT_SWIZZLE_G,
		VK_COMPONENT_SWIZZLE_R,
		VK_COMPONENT_SWIZZLE_ONE
	},
	IDENTITY_SWIZZLE,	/* SurfaceFormat.Bgra5551 */
	IDENTITY_SWIZZLE,	/* SurfaceFormat.Bgra4444 */
	IDENTITY_SWIZZLE,	/* SurfaceFormat.Dxt1 */
	IDENTITY_SWIZZLE,	/* SurfaceFormat.Dxt3 */
	IDENTITY_SWIZZLE,	/* SurfaceFormat.Dxt5 */
	{			/* SurfaceFormat.NormalizedByte2 */
		VK_COMPONENT_SWIZZLE_R,
		VK_COMPONENT_SWIZZLE_G,
		VK_COMPONENT_SWIZZLE_ONE,
		VK_COMPONENT_SWIZZLE_ONE
	},
	IDENTITY_SWIZZLE,	/* SurfaceFormat.NormalizedByte4 */
	IDENTITY_SWIZZLE,	/* SurfaceFormat.Rgba1010102 */
	{			/* SurfaceFormat.Rg32 */
		VK_COMPONENT_SWIZZLE_R,
		VK_COMPONENT_SWIZZLE_G,
		VK_COMPONENT_SWIZZLE_ONE,
		VK_COMPONENT_SWIZZLE_ONE
	},
	IDENTITY_SWIZZLE,	/* SurfaceFormat.Rgba64 */
	{			/* SurfaceFormat.Alpha8 */
		VK_COMPONENT_SWIZZLE_ZERO,
		VK_COMPONENT_SWIZZLE_ZERO,
		VK_COMPONENT_SWIZZLE_ZERO,
		VK_COMPONENT_SWIZZLE_R
	},
	{			/* SurfaceFormat.Single */
		VK_COMPONENT_SWIZZLE_R,
		VK_COMPONENT_SWIZZLE_ONE,
		VK_COMPONENT_SWIZZLE_ONE,
		VK_COMPONENT_SWIZZLE_ONE
	},
	{			/* SurfaceFormat.Vector2 */
		VK_COMPONENT_SWIZZLE_R,
		VK_COMPONENT_SWIZZLE_G,
		VK_COMPONENT_SWIZZLE_ONE,
		VK_COMPONENT_SWIZZLE_ONE
	},
	IDENTITY_SWIZZLE,	/* SurfaceFormat.Vector4 */
	{			/* SurfaceFormat.HalfSingle */
		VK_COMPONENT_SWIZZLE_R,
		VK_COMPONENT_SWIZZLE_ONE,
		VK_COMPONENT_SWIZZLE_ONE,
		VK_COMPONENT_SWIZZLE_ONE
	},
	{			/* SurfaceFormat.HalfVector2 */
		VK_COMPONENT_SWIZZLE_R,
		VK_COMPONENT_SWIZZLE_G,
		VK_COMPONENT_SWIZZLE_ONE,
		VK_COMPONENT_SWIZZLE_ONE
	},
	IDENTITY_SWIZZLE,	/* SurfaceFormat.HalfVector4 */
	IDENTITY_SWIZZLE,	/* SurfaceFormat.HdrBlendable */
	IDENTITY_SWIZZLE,	/* SurfaceFormat.ColorBgraEXT */
	IDENTITY_SWIZZLE,	/* SurfaceFormat.ColorSrgbEXT */
	IDENTITY_SWIZZLE,	/* SurfaceFormat.Dxt5SrgbEXT */
	IDENTITY_SWIZZLE,	/* SurfaceFormat.Bc7EXT */
	IDENTITY_SWIZZLE,	/* SurfaceFormat.Bc7SrgbEXT */
};

static VkFormat XNAToVK_SurfaceFormat[] =
{
	VK_FORMAT_R8G8B8A8_UNORM,		/* SurfaceFormat.Color */
	VK_FORMAT_R5G6B5_UNORM_PACK16,		/* SurfaceFormat.Bgr565 */
	VK_FORMAT_A1R5G5B5_UNORM_PACK16,	/* SurfaceFormat.Bgra5551 */
	VK_FORMAT_B4G4R4A4_UNORM_PACK16,	/* SurfaceFormat.Bgra4444 */
	VK_FORMAT_BC1_RGBA_UNORM_BLOCK,		/* SurfaceFormat.Dxt1 */
	VK_FORMAT_BC2_UNORM_BLOCK,		/* SurfaceFormat.Dxt3 */
	VK_FORMAT_BC3_UNORM_BLOCK,		/* SurfaceFormat.Dxt5 */
	VK_FORMAT_R8G8_SNORM,			/* SurfaceFormat.NormalizedByte2 */
	VK_FORMAT_R8G8B8A8_SNORM,		/* SurfaceFormat.NormalizedByte4 */
	VK_FORMAT_A2R10G10B10_UNORM_PACK32,	/* SurfaceFormat.Rgba1010102 */
	VK_FORMAT_R16G16_UNORM,			/* SurfaceFormat.Rg32 */
	VK_FORMAT_R16G16B16A16_UNORM,		/* SurfaceFormat.Rgba64 */
	VK_FORMAT_R8_UNORM,			/* SurfaceFormat.Alpha8 */
	VK_FORMAT_R32_SFLOAT,			/* SurfaceFormat.Single */
	VK_FORMAT_R32G32_SFLOAT,		/* SurfaceFormat.Vector2 */
	VK_FORMAT_R32G32B32A32_SFLOAT,		/* SurfaceFormat.Vector4 */
	VK_FORMAT_R16_SFLOAT,			/* SurfaceFormat.HalfSingle */
	VK_FORMAT_R16G16_SFLOAT,		/* SurfaceFormat.HalfVector2 */
	VK_FORMAT_R16G16B16A16_SFLOAT,		/* SurfaceFormat.HalfVector4 */
	VK_FORMAT_R16G16B16A16_SFLOAT,		/* SurfaceFormat.HdrBlendable */
	VK_FORMAT_B8G8R8A8_UNORM,		/* SurfaceFormat.ColorBgraEXT */
	VK_FORMAT_R8G8B8A8_SRGB,		/* SurfaceFormat.ColorSrgbEXT */
	VK_FORMAT_BC3_SRGB_BLOCK,		/* SurfaceFormat.Dxt5SrgbEXT */
	VK_FORMAT_BC7_UNORM_BLOCK,		/* SurfaceFormat.Bc7EXT */
	VK_FORMAT_BC7_SRGB_BLOCK,		/* SurfaceFormat.Bc7SrgbEXT */
};

static inline VkFormat XNAToVK_DepthFormat(
	VulkanRenderer *renderer,
	FNA3D_DepthFormat format
) {
	switch (format)
	{
		case FNA3D_DEPTHFORMAT_D16:
			return renderer->D16Format;
		case FNA3D_DEPTHFORMAT_D24:
			return renderer->D24Format;
		case FNA3D_DEPTHFORMAT_D24S8:
			return renderer->D24S8Format;
		default:
			return VK_FORMAT_UNDEFINED;
	}
}

static inline float XNAToVK_DepthBiasScale(VkFormat format)
{
	switch (format)
	{
		case VK_FORMAT_D16_UNORM:
			return (float) ((1 << 16) - 1);

		case VK_FORMAT_D24_UNORM_S8_UINT:
			return (float) ((1 << 24) - 1);

		case VK_FORMAT_D32_SFLOAT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return (float) ((1 << 23) - 1);

		default:
			return 0.0f;
	}
}

static inline uint8_t DepthFormatContainsStencil(VkFormat format)
{
	switch (format)
	{
		case VK_FORMAT_D16_UNORM:
		case VK_FORMAT_D32_SFLOAT:
			return 0;
		case VK_FORMAT_D24_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return 1;
		default:
			SDL_assert(0 && "Invalid depth pixel format");
			return 0;
	}
}

static inline uint8_t IsDepthFormat(VkFormat format)
{
	return
		format == VK_FORMAT_D16_UNORM ||
		format == VK_FORMAT_D16_UNORM_S8_UINT ||
		format == VK_FORMAT_D24_UNORM_S8_UINT ||
		format == VK_FORMAT_D32_SFLOAT ||
		format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

static VkBlendFactor XNAToVK_BlendFactor[] =
{
	VK_BLEND_FACTOR_ONE, 				/* FNA3D_BLEND_ONE */
	VK_BLEND_FACTOR_ZERO, 				/* FNA3D_BLEND_ZERO */
	VK_BLEND_FACTOR_SRC_COLOR, 			/* FNA3D_BLEND_SOURCECOLOR */
	VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,		/* FNA3D_BLEND_INVERSESOURCECOLOR */
	VK_BLEND_FACTOR_SRC_ALPHA,			/* FNA3D_BLEND_SOURCEALPHA */
	VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,		/* FNA3D_BLEND_INVERSESOURCEALPHA */
	VK_BLEND_FACTOR_DST_COLOR,			/* FNA3D_BLEND_DESTINATIONCOLOR */
	VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,		/* FNA3D_BLEND_INVERSEDESTINATIONCOLOR */
	VK_BLEND_FACTOR_DST_ALPHA,			/* FNA3D_BLEND_DESTINATIONALPHA */
	VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,		/* FNA3D_BLEND_INVERSEDESTINATIONALPHA */
	VK_BLEND_FACTOR_CONSTANT_COLOR,			/* FNA3D_BLEND_BLENDFACTOR */
	VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,	/* FNA3D_BLEND_INVERSEBLENDFACTOR */
	VK_BLEND_FACTOR_SRC_ALPHA_SATURATE 		/* FNA3D_BLEND_SOURCEALPHASATURATION */
};

static VkBlendOp XNAToVK_BlendOp[] =
{
	VK_BLEND_OP_ADD, 		/* FNA3D_BLENDFUNCTION_ADD */
	VK_BLEND_OP_SUBTRACT, 		/* FNA3D_BLENDFUNCTION_SUBTRACT */
	VK_BLEND_OP_REVERSE_SUBTRACT, 	/* FNA3D_BLENDFUNCTION_REVERSESUBTRACT */
	VK_BLEND_OP_MAX, 		/* FNA3D_BLENDFUNCTION_MAX */
	VK_BLEND_OP_MIN			/* FNA3D_BLENDFUNCTION_MIN */
};

static VkPolygonMode XNAToVK_PolygonMode[] =
{
	VK_POLYGON_MODE_FILL, 	/* FNA3D_FILLMODE_SOLID */
	VK_POLYGON_MODE_LINE	/* FNA3D_FILLMODE_WIREFRAME */
};

static VkCullModeFlags XNAToVK_CullMode[] =
{
	VK_CULL_MODE_NONE, 	/* FNA3D_CULLMODE_NONE */
	VK_CULL_MODE_FRONT_BIT, /* FNA3D_CULLMODE_CULLCLOCKWISEFACE */
	VK_CULL_MODE_BACK_BIT	/* FNA3D_CULLMODE_CULLCOUNTERCLOCKWISEFACE */
};

static VkPrimitiveTopology XNAToVK_Topology[] =
{
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,	/* FNA3D_PRIMITIVETYPE_TRIANGLELIST */
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,	/* FNA3D_PRIMITIVETYPE_TRIANGLESTRIP */
	VK_PRIMITIVE_TOPOLOGY_LINE_LIST,	/* FNA3D_PRIMITIVETYPE_LINELIST */
	VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,	/* FNA3D_PRIMITIVETYPE_LINESTRIP */
	VK_PRIMITIVE_TOPOLOGY_POINT_LIST	/* FNA3D_PRIMITIVETYPE_POINTLIST_EXT */
};

static VkSamplerAddressMode XNAToVK_SamplerAddressMode[] =
{
	VK_SAMPLER_ADDRESS_MODE_REPEAT, 	/* FNA3D_TEXTUREADDRESSMODE_WRAP */
	VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 	/* FNA3D_TEXTUREADDRESSMODE_CLAMP */
	VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT /* FNA3D_TEXTUREADDRESSMODE_MIRROR */
};

static VkFilter XNAToVK_MagFilter[] =
{
	VK_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_LINEAR */
	VK_FILTER_NEAREST, 	/* FNA3D_TEXTUREFILTER_POINT */
	VK_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_ANISOTROPIC */
	VK_FILTER_LINEAR,	/* FNA3D_TEXTUREFILTER_LINEAR_MIPPOINT */
	VK_FILTER_NEAREST, 	/* FNA3D_TEXTUREFILTER_POINT_MIPLINEAR */
	VK_FILTER_NEAREST,	/* FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPLINEAR */
	VK_FILTER_NEAREST, 	/* FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPPOINT */
	VK_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPLINEAR */
	VK_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPPOINT */
};

static VkSamplerMipmapMode XNAToVK_MipFilter[] =
{
	VK_SAMPLER_MIPMAP_MODE_LINEAR, 	/* FNA3D_TEXTUREFILTER_LINEAR */
	VK_SAMPLER_MIPMAP_MODE_NEAREST, /* FNA3D_TEXTUREFILTER_POINT */
	VK_SAMPLER_MIPMAP_MODE_LINEAR, 	/* FNA3D_TEXTUREFILTER_ANISOTROPIC */
	VK_SAMPLER_MIPMAP_MODE_NEAREST,	/* FNA3D_TEXTUREFILTER_LINEAR_MIPPOINT */
	VK_SAMPLER_MIPMAP_MODE_LINEAR, 	/* FNA3D_TEXTUREFILTER_POINT_MIPLINEAR */
	VK_SAMPLER_MIPMAP_MODE_LINEAR,	/* FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPLINEAR */
	VK_SAMPLER_MIPMAP_MODE_NEAREST, /* FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPPOINT */
	VK_SAMPLER_MIPMAP_MODE_LINEAR, 	/* FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPLINEAR */
	VK_SAMPLER_MIPMAP_MODE_NEAREST, /* FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPPOINT */
};

static VkFilter XNAToVK_MinFilter[] =
{
	VK_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_LINEAR */
	VK_FILTER_NEAREST, 	/* FNA3D_TEXTUREFILTER_POINT */
	VK_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_ANISOTROPIC */
	VK_FILTER_LINEAR,	/* FNA3D_TEXTUREFILTER_LINEAR_MIPPOINT */
	VK_FILTER_NEAREST, 	/* FNA3D_TEXTUREFILTER_POINT_MIPLINEAR */
	VK_FILTER_LINEAR,	/* FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPLINEAR */
	VK_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPPOINT */
	VK_FILTER_NEAREST, 	/* FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPLINEAR */
	VK_FILTER_NEAREST, 	/* FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPPOINT */
};

static VkCompareOp XNAToVK_CompareOp[] =
{
	VK_COMPARE_OP_ALWAYS, 		/* FNA3D_COMPAREFUNCTION_ALWAYS */
	VK_COMPARE_OP_NEVER, 		/* FNA3D_COMPAREFUNCTION_NEVER */
	VK_COMPARE_OP_LESS,		/* FNA3D_COMPAREFUNCTION_LESS */
	VK_COMPARE_OP_LESS_OR_EQUAL, 	/* FNA3D_COMPAREFUNCTION_LESSEQUAL */
	VK_COMPARE_OP_EQUAL,		/* FNA3D_COMPAREFUNCTION_EQUAL */
	VK_COMPARE_OP_GREATER_OR_EQUAL,	/* FNA3D_COMPAREFUNCTION_GREATEREQUAL */
	VK_COMPARE_OP_GREATER,		/* FNA3D_COMPAREFUNCTION_GREATER */
	VK_COMPARE_OP_NOT_EQUAL		/* FNA3D_COMPAREFUNCTION_NOTEQUAL */
};

static VkStencilOp XNAToVK_StencilOp[] =
{
	VK_STENCIL_OP_KEEP,			/* FNA3D_STENCILOPERATION_KEEP */
	VK_STENCIL_OP_ZERO,			/* FNA3D_STENCILOPERATION_ZERO */
	VK_STENCIL_OP_REPLACE,			/* FNA3D_STENCILOPERATION_REPLACE */
	VK_STENCIL_OP_INCREMENT_AND_WRAP, 	/* FNA3D_STENCILOPERATION_INCREMENT */
	VK_STENCIL_OP_DECREMENT_AND_WRAP,	/* FNA3D_STENCILOPERATION_DECREMENT */
	VK_STENCIL_OP_INCREMENT_AND_CLAMP, 	/* FNA3D_STENCILOPERATION_INCREMENTSATURATION */
	VK_STENCIL_OP_DECREMENT_AND_CLAMP,	/* FNA3D_STENCILOPERATION_DECREMENTSATURATION */
	VK_STENCIL_OP_INVERT			/* FNA3D_STENCILOPERATION_INVERT */
};

static VkFormat XNAToVK_VertexAttribType[] =
{
	VK_FORMAT_R32_SFLOAT,		/* FNA3D_VERTEXELEMENTFORMAT_SINGLE */
	VK_FORMAT_R32G32_SFLOAT,	/* FNA3D_VERTEXELEMENTFORMAT_VECTOR2 */
	VK_FORMAT_R32G32B32_SFLOAT,	/* FNA3D_VERTEXELEMENTFORMAT_VECTOR3 */
	VK_FORMAT_R32G32B32A32_SFLOAT, 	/* FNA3D_VERTEXELEMENTFORMAT_VECTOR4 */
	VK_FORMAT_R8G8B8A8_UNORM,	/* FNA3D_VERTEXELEMENTFORMAT_COLOR */
	VK_FORMAT_R8G8B8A8_USCALED,	/* FNA3D_VERTEXELEMENTFORMAT_BYTE4 */
	VK_FORMAT_R16G16_SSCALED, 	/* FNA3D_VERTEXELEMENTFORMAT_SHORT2 */
	VK_FORMAT_R16G16B16A16_SSCALED,	/* FNA3D_VERTEXELEMENTFORMAT_SHORT4 */
	VK_FORMAT_R16G16_SNORM,		/* FNA3D_VERTEXELEMENTFORMAT_NORMALIZEDSHORT2 */
	VK_FORMAT_R16G16B16A16_SNORM,	/* FNA3D_VERTEXELEMENTFORMAT_NORMALIZEDSHORT4 */
	VK_FORMAT_R16G16_SFLOAT,	/* FNA3D_VERTEXELEMENTFORMAT_HALFVECTOR2 */
	VK_FORMAT_R16G16B16A16_SFLOAT	/* FNA3D_VERTEXELEMENTFORMAT_HALFVECTOR4 */
};

/* Error Handling */

static inline const char* VkErrorMessages(VkResult code)
{
	#define ERR_TO_STR(e) \
		case e: return #e;
	switch (code)
	{
		ERR_TO_STR(VK_ERROR_OUT_OF_HOST_MEMORY)
		ERR_TO_STR(VK_ERROR_OUT_OF_DEVICE_MEMORY)
		ERR_TO_STR(VK_ERROR_FRAGMENTED_POOL)
		ERR_TO_STR(VK_ERROR_OUT_OF_POOL_MEMORY)
		ERR_TO_STR(VK_ERROR_INITIALIZATION_FAILED)
		ERR_TO_STR(VK_ERROR_LAYER_NOT_PRESENT)
		ERR_TO_STR(VK_ERROR_EXTENSION_NOT_PRESENT)
		ERR_TO_STR(VK_ERROR_FEATURE_NOT_PRESENT)
		ERR_TO_STR(VK_ERROR_TOO_MANY_OBJECTS)
		ERR_TO_STR(VK_ERROR_DEVICE_LOST)
		ERR_TO_STR(VK_ERROR_INCOMPATIBLE_DRIVER)
		ERR_TO_STR(VK_ERROR_OUT_OF_DATE_KHR)
		ERR_TO_STR(VK_ERROR_SURFACE_LOST_KHR)
		ERR_TO_STR(VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)
		ERR_TO_STR(VK_SUBOPTIMAL_KHR)
		default: return "Unhandled VkResult!";
	}
	#undef ERR_TO_STR
}

#define VULKAN_ERROR_CHECK(res, fn, ret) \
	if (res != VK_SUCCESS) \
	{ \
		FNA3D_LogError("%s %s", #fn, VkErrorMessages(res)); \
		return ret; \
	}

/* Forward Declarations */

static CreateSwapchainResult VULKAN_INTERNAL_CreateSwapchain(VulkanRenderer* renderer, void* windowHandle);
static void VULKAN_INTERNAL_RecreateSwapchain(VulkanRenderer *renderer, void *windowHandle);

static void VULKAN_INTERNAL_MaybeEndRenderPass(VulkanRenderer *renderer);

/* Vulkan: Internal Implementation */

/* Vulkan: Extensions */

static inline uint8_t SupportsInstanceExtension(
	const char *ext,
	VkExtensionProperties *availableExtensions,
	uint32_t numAvailableExtensions
) {
	uint32_t i;
	for (i = 0; i < numAvailableExtensions; i += 1)
	{
		if (SDL_strcmp(ext, availableExtensions[i].extensionName) == 0)
		{
			return 1;
		}
	}
	return 0;
}

static uint8_t VULKAN_INTERNAL_CheckInstanceExtensions(
	uint8_t debugMode,
	const char **requiredExtensions,
	uint32_t requiredExtensionsLength,
	uint8_t *supportsDeviceProperties2,
	uint8_t *supportsDebugUtils
) {
	uint32_t extensionCount, i;
	VkExtensionProperties *availableExtensions;
	uint8_t allExtensionsSupported = 1;

	vkEnumerateInstanceExtensionProperties(
		NULL,
		&extensionCount,
		NULL
	);
	availableExtensions = SDL_malloc(
		extensionCount * sizeof(VkExtensionProperties)
	);
	vkEnumerateInstanceExtensionProperties(
		NULL,
		&extensionCount,
		availableExtensions
	);

	for (i = 0; i < requiredExtensionsLength; i += 1)
	{
		if (!SupportsInstanceExtension(
			requiredExtensions[i],
			availableExtensions,
			extensionCount
		)) {
			allExtensionsSupported = 0;
			break;
		}
	}

	/* These are optional, but nice to have! */
	*supportsDeviceProperties2 = SupportsInstanceExtension(
		VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
		availableExtensions,
		extensionCount
	);
	*supportsDebugUtils = debugMode && SupportsInstanceExtension(
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
		availableExtensions,
		extensionCount
	);

	SDL_free(availableExtensions);
	return allExtensionsSupported;
}

static uint8_t VULKAN_INTERNAL_CheckDeviceExtensions(
	VulkanRenderer *renderer,
	VkPhysicalDevice physicalDevice,
	VulkanExtensions *physicalDeviceExtensions
) {
	uint32_t extensionCount;
	VkExtensionProperties *availableExtensions;
	uint8_t allExtensionsSupported;

	renderer->vkEnumerateDeviceExtensionProperties(
		physicalDevice,
		NULL,
		&extensionCount,
		NULL
	);
	availableExtensions = (VkExtensionProperties*) SDL_malloc(
		extensionCount * sizeof(VkExtensionProperties)
	);
	renderer->vkEnumerateDeviceExtensionProperties(
		physicalDevice,
		NULL,
		&extensionCount,
		availableExtensions
	);

	allExtensionsSupported = CheckDeviceExtensions(
		availableExtensions,
		extensionCount,
		physicalDeviceExtensions
	);

	SDL_free(availableExtensions);
	return allExtensionsSupported;
}

/* Vulkan: Validation Layers */

static uint8_t VULKAN_INTERNAL_CheckValidationLayers(
	const char** validationLayers,
	uint32_t validationLayersLength
) {
	uint32_t layerCount;
	VkLayerProperties *availableLayers;
	uint32_t i, j;
	uint8_t layerFound;

	vkEnumerateInstanceLayerProperties(&layerCount, NULL);
	availableLayers = (VkLayerProperties*) SDL_malloc(
		layerCount * sizeof(VkLayerProperties)
	);
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);

	for (i = 0; i < validationLayersLength; i += 1)
	{
		layerFound = 0;

		for (j = 0; j < layerCount; j += 1)
		{
			if (SDL_strcmp(validationLayers[i], availableLayers[j].layerName) == 0)
			{
				layerFound = 1;
				break;
			}
		}

		if (!layerFound)
		{
			break;
		}
	}

	SDL_free(availableLayers);
	return layerFound;
}

/* Vulkan: Device Feature Queries */

static uint8_t VULKAN_INTERNAL_QuerySwapChainSupport(
	VulkanRenderer *renderer,
	VkPhysicalDevice physicalDevice,
	VkSurfaceKHR surface,
	SwapChainSupportDetails *outputDetails
) {
	VkResult result;
	VkBool32 supportsPresent;

	renderer->vkGetPhysicalDeviceSurfaceSupportKHR(
		physicalDevice,
		renderer->queueFamilyIndex,
		surface,
		&supportsPresent
	);

	/* Initialize these in case anything fails */
	outputDetails->formatsLength = 0;
	outputDetails->presentModesLength = 0;

	if (!supportsPresent)
	{
		FNA3D_LogWarn("This surface does not support presenting!");
		return 0;
	}

	/* Run the device surface queries */
	result = renderer->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
		physicalDevice,
		surface,
		&outputDetails->capabilities
	);
	VULKAN_ERROR_CHECK(result, vkGetPhysicalDeviceSurfaceCapabilitiesKHR, 0)

	if (!(outputDetails->capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
	{
		FNA3D_LogWarn("Opaque presentation unsupported! Expect weird transparency bugs!");
	}

	result = renderer->vkGetPhysicalDeviceSurfaceFormatsKHR(
		physicalDevice,
		surface,
		&outputDetails->formatsLength,
		NULL
	);
	VULKAN_ERROR_CHECK(result, vkGetPhysicalDeviceSurfaceFormatsKHR, 0)
	result = renderer->vkGetPhysicalDeviceSurfacePresentModesKHR(
		physicalDevice,
		surface,
		&outputDetails->presentModesLength,
		NULL
	);
	VULKAN_ERROR_CHECK(result, vkGetPhysicalDeviceSurfacePresentModesKHR, 0)

	/* Generate the arrays, if applicable */
	if (outputDetails->formatsLength != 0)
	{
		outputDetails->formats = (VkSurfaceFormatKHR*) SDL_malloc(
			sizeof(VkSurfaceFormatKHR) * outputDetails->formatsLength
		);

		if (!outputDetails->formats)
		{
			SDL_OutOfMemory();
			return 0;
		}

		result = renderer->vkGetPhysicalDeviceSurfaceFormatsKHR(
			physicalDevice,
			surface,
			&outputDetails->formatsLength,
			outputDetails->formats
		);
		if (result != VK_SUCCESS)
		{
			FNA3D_LogError(
				"vkGetPhysicalDeviceSurfaceFormatsKHR: %s",
				VkErrorMessages(result)
			);

			SDL_free(outputDetails->formats);
			return 0;
		}
	}
	if (outputDetails->presentModesLength != 0)
	{
		outputDetails->presentModes = (VkPresentModeKHR*) SDL_malloc(
			sizeof(VkPresentModeKHR) * outputDetails->presentModesLength
		);

		if (!outputDetails->presentModes)
		{
			SDL_OutOfMemory();
			return 0;
		}

		result = renderer->vkGetPhysicalDeviceSurfacePresentModesKHR(
			physicalDevice,
			surface,
			&outputDetails->presentModesLength,
			outputDetails->presentModes
		);
		if (result != VK_SUCCESS)
		{
			FNA3D_LogError(
				"vkGetPhysicalDeviceSurfacePresentModesKHR: %s",
				VkErrorMessages(result)
			);

			SDL_free(outputDetails->formats);
			SDL_free(outputDetails->presentModes);
			return 0;
		}
	}

	/* If we made it here, all the queries were successfull. This does NOT
	 * necessarily mean there are any supported formats or present modes!
	 */
	return 1;
}

static uint8_t VULKAN_INTERNAL_ChooseSwapSurfaceFormat(
	VkFormat desiredFormat,
	VkSurfaceFormatKHR *availableFormats,
	uint32_t availableFormatsLength,
	VkSurfaceFormatKHR *outputFormat
) {
	uint32_t i;
	VkColorSpaceKHR colorSpace;

	if (SDL_GetHintBoolean("FNA3D_ENABLE_HDR_COLORSPACE", SDL_FALSE))
	{
		if (	desiredFormat == VK_FORMAT_A2R10G10B10_UNORM_PACK32 ||
			desiredFormat == VK_FORMAT_A2B10G10R10_UNORM_PACK32	)
		{
			colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT;
		}
		else if (desiredFormat == VK_FORMAT_R16G16B16A16_SFLOAT)
		{
			colorSpace = VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
		}
		else
		{
			colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		}
	}
	else
	{
		colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	}

	for (i = 0; i < availableFormatsLength; i += 1)
	{
		if (	availableFormats[i].format == desiredFormat &&
			availableFormats[i].colorSpace == colorSpace	)
		{
			*outputFormat = availableFormats[i];
			return 1;
		}
	}
	return 0;
}

static uint8_t VULKAN_INTERNAL_ChooseSwapPresentMode(
	FNA3D_PresentInterval desiredPresentInterval,
	VkPresentModeKHR *availablePresentModes,
	uint32_t availablePresentModesLength,
	VkPresentModeKHR *outputPresentMode
) {
	#define CHECK_MODE(m) \
		for (i = 0; i < availablePresentModesLength; i += 1) \
		{ \
			if (availablePresentModes[i] == m) \
			{ \
				*outputPresentMode = m; \
				FNA3D_LogInfo("Using " #m "!"); \
				return 1; \
			} \
		} \
		FNA3D_LogInfo(#m " unsupported.");

	uint32_t i;
	if (	desiredPresentInterval == FNA3D_PRESENTINTERVAL_DEFAULT ||
		desiredPresentInterval == FNA3D_PRESENTINTERVAL_ONE	)
	{
		if (SDL_GetHintBoolean("FNA3D_ENABLE_LATESWAPTEAR", 0))
		{
			CHECK_MODE(VK_PRESENT_MODE_FIFO_RELAXED_KHR)
		}
		else if (SDL_GetHintBoolean("FNA3D_VULKAN_FORCE_MAILBOX_VSYNC", 0))
		{
			CHECK_MODE(VK_PRESENT_MODE_MAILBOX_KHR)
		}
		else
		{
			*outputPresentMode = VK_PRESENT_MODE_FIFO_KHR;
			return 1;
		}
	}
	else if (desiredPresentInterval ==  FNA3D_PRESENTINTERVAL_IMMEDIATE)
	{
		CHECK_MODE(VK_PRESENT_MODE_IMMEDIATE_KHR)

		/* Some implementations have mailbox in place of immediate. */
		FNA3D_LogInfo("Fall back to VK_PRESENT_MODE_MAILBOX_KHR.");
		CHECK_MODE(VK_PRESENT_MODE_MAILBOX_KHR)
	}
	else if (desiredPresentInterval == FNA3D_PRESENTINTERVAL_TWO)
	{
		FNA3D_LogError("FNA3D_PRESENTINTERVAL_TWO not supported in Vulkan");
		return 0;
	}
	else
	{
		FNA3D_LogError(
			"Unrecognized PresentInterval: %d",
			desiredPresentInterval
		);
		return 0;
	}

	#undef CHECK_MODE

	FNA3D_LogInfo("Fall back to VK_PRESENT_MODE_FIFO_KHR.");
	*outputPresentMode = VK_PRESENT_MODE_FIFO_KHR;
	return 1;
}

static uint8_t VULKAN_INTERNAL_FindMemoryType(
	VulkanRenderer *renderer,
	uint32_t typeFilter,
	VkMemoryPropertyFlags requiredProperties,
	VkMemoryPropertyFlags ignoredProperties,
	uint32_t *memoryTypeIndex
) {
	uint32_t i;

	for (i = *memoryTypeIndex; i < renderer->memoryProperties.memoryTypeCount; i += 1)
	{
		if (	(typeFilter & (1 << i)) &&
			(renderer->memoryProperties.memoryTypes[i].propertyFlags & requiredProperties) == requiredProperties &&
			(renderer->memoryProperties.memoryTypes[i].propertyFlags & ignoredProperties) == 0	)
		{
			*memoryTypeIndex = i;
			return 1;
		}
	}

	FNA3D_LogWarn(
		"Failed to find memory type %X, required %X, ignored %X",
		typeFilter,
		requiredProperties,
		ignoredProperties
	);
	return 0;
}

static uint8_t VULKAN_INTERNAL_IsDeviceSuitable(
	VulkanRenderer *renderer,
	VkPhysicalDevice physicalDevice,
	VulkanExtensions *physicalDeviceExtensions,
	VkSurfaceKHR surface,
	uint32_t *queueFamilyIndex,
	uint8_t *deviceRank
) {
	uint32_t queueFamilyCount, queueFamilyRank, queueFamilyBest;
	SwapChainSupportDetails swapchainSupportDetails;
	VkQueueFamilyProperties *queueProps;
	VkBool32 supportsPresent;
	uint8_t querySuccess;
	VkPhysicalDeviceProperties deviceProperties;
	uint32_t i;

	/* Get the device rank before doing any checks, in case one fails.
	 * Note: If no dedicated device exists, one that supports our features
	 * would be fine
	 */
	renderer->vkGetPhysicalDeviceProperties(
		physicalDevice,
		&deviceProperties
	);
	if (*deviceRank < DEVICE_PRIORITY[deviceProperties.deviceType])
	{
		/* This device outranks the best device we've found so far!
		 * This includes a dedicated GPU that has less features than an
		 * integrated GPU, because this is a freak case that is almost
		 * never intentionally desired by the end user
		 */
		*deviceRank = DEVICE_PRIORITY[deviceProperties.deviceType];
	}
	else if (*deviceRank > DEVICE_PRIORITY[deviceProperties.deviceType])
	{
		/* Device is outranked by a previous device, don't even try to
		 * run a query and reset the rank to avoid overwrites
		 */
		*deviceRank = 0;
		return 0;
	}

	if (!VULKAN_INTERNAL_CheckDeviceExtensions(
		renderer,
		physicalDevice,
		physicalDeviceExtensions
	)) {
		return 0;
	}

	renderer->vkGetPhysicalDeviceQueueFamilyProperties(
		physicalDevice,
		&queueFamilyCount,
		NULL
	);

	queueProps = (VkQueueFamilyProperties*) SDL_stack_alloc(
		VkQueueFamilyProperties,
		queueFamilyCount
	);
	renderer->vkGetPhysicalDeviceQueueFamilyProperties(
		physicalDevice,
		&queueFamilyCount,
		queueProps
	);

	queueFamilyBest = 0;
	*queueFamilyIndex = UINT32_MAX;
	for (i = 0; i < queueFamilyCount; i += 1)
	{
		renderer->vkGetPhysicalDeviceSurfaceSupportKHR(
			physicalDevice,
			i,
			surface,
			&supportsPresent
		);
		if (	!supportsPresent ||
			!(queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)	)
		{
			/* Not a graphics family, ignore. */
			continue;
		}

		/* The queue family bitflags are kind of annoying.
		 *
		 * We of course need a graphics family, but we ideally want the
		 * _primary_ graphics family. The spec states that at least one
		 * graphics family must also be a compute family, so generally
		 * drivers make that the first one. But hey, maybe something
		 * genuinely can't do compute or something, and FNA doesn't
		 * need it, so we'll be open to a non-compute queue family.
		 *
		 * Additionally, it's common to see the primary queue family
		 * have the transfer bit set, which is great! But this is
		 * actually optional; it's impossible to NOT have transfers in
		 * graphics/compute but it _is_ possible for a graphics/compute
		 * family, even the primary one, to just decide not to set the
		 * bitflag. Admittedly, a driver may want to isolate transfer
		 * queues to a dedicated family so that queues made solely for
		 * transfers can have an optimized DMA queue.
		 *
		 * That, or the driver author got lazy and decided not to set
		 * the bit. Looking at you, Android.
		 *
		 * -flibit
		 */
		if (queueProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
		{
			if (queueProps[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
			{
				/* Has all attribs! */
				queueFamilyRank = 3;
			}
			else
			{
				/* Probably has a DMA transfer queue family */
				queueFamilyRank = 2;
			}
		}
		else
		{
			/* Just a graphics family, probably has something better */
			queueFamilyRank = 1;
		}
		if (queueFamilyRank > queueFamilyBest)
		{
			*queueFamilyIndex = i;
			queueFamilyBest = queueFamilyRank;
		}
	}

	SDL_stack_free(queueProps);

	if (*queueFamilyIndex == UINT32_MAX)
	{
		/* Somehow no graphics queues existed. Compute-only device? */
		return 0;
	}

	/* FIXME: Need better structure for checking vs storing support details */
	querySuccess = VULKAN_INTERNAL_QuerySwapChainSupport(
		renderer,
		physicalDevice,
		surface,
		&swapchainSupportDetails
	);
	if (swapchainSupportDetails.formatsLength > 0)
	{
		SDL_free(swapchainSupportDetails.formats);
	}
	if (swapchainSupportDetails.presentModesLength > 0)
	{
		SDL_free(swapchainSupportDetails.presentModes);
	}

	return (	querySuccess &&
			swapchainSupportDetails.formatsLength > 0 &&
			swapchainSupportDetails.presentModesLength > 0	);
}

/* Vulkan: vkInstance/vkDevice Creation */

static VkBool32 VKAPI_PTR VULKAN_INTERNAL_DebugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageTypes,
	const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
	void* pUserData
) {
	void (*logFunc)(const char *fmt, ...);
	if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
	{
		logFunc = FNA3D_LogError;
	}
	else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		logFunc = FNA3D_LogWarn;
	}
	else
	{
		logFunc = FNA3D_LogInfo;
	}
	logFunc("VULKAN DEBUG: %s", pCallbackData->pMessage);
	return VK_FALSE;
}

static uint8_t VULKAN_INTERNAL_CreateInstance(
	VulkanRenderer *renderer,
	FNA3D_PresentationParameters *presentationParameters
) {
	VkResult vulkanResult;
	VkApplicationInfo appInfo;
	const char **instanceExtensionNames;
	uint32_t instanceExtensionCount;
	VkInstanceCreateInfo createInfo;
	VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo;
	static const char *layerNames[] = { "VK_LAYER_KHRONOS_validation" };

	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pNext = NULL;
	appInfo.pApplicationName = NULL;
	appInfo.applicationVersion = 0;
	appInfo.pEngineName = "FNA3D";
	appInfo.engineVersion = FNA3D_COMPILED_VERSION;
	appInfo.apiVersion = VK_MAKE_VERSION(1, 0, 0);

	if (!SDL_Vulkan_GetInstanceExtensions(
		(SDL_Window*) presentationParameters->deviceWindowHandle,
		&instanceExtensionCount,
		NULL
	)) {
		FNA3D_LogWarn(
			"SDL_Vulkan_GetInstanceExtensions(): getExtensionCount: %s",
			SDL_GetError()
		);

		return 0;
	}

	/* Extra space for the following extensions:
	 * VK_KHR_get_physical_device_properties2
	 * VK_EXT_debug_utils
	 */
	instanceExtensionNames = SDL_stack_alloc(
		const char*,
		instanceExtensionCount + 2
	);

	if (!SDL_Vulkan_GetInstanceExtensions(
		(SDL_Window*) presentationParameters->deviceWindowHandle,
		&instanceExtensionCount,
		instanceExtensionNames
	)) {
		FNA3D_LogWarn(
			"SDL_Vulkan_GetInstanceExtensions(): %s",
			SDL_GetError()
		);
		goto create_instance_fail;
	}

	if (!VULKAN_INTERNAL_CheckInstanceExtensions(
		renderer->debugMode,
		instanceExtensionNames,
		instanceExtensionCount,
		&renderer->supportsDeviceProperties2,
		&renderer->supportsDebugUtils
	)) {
		FNA3D_LogWarn(
			"Required Vulkan instance extensions not supported"
		);
		goto create_instance_fail;
	}

	if (renderer->supportsDeviceProperties2)
	{
		instanceExtensionNames[instanceExtensionCount++] =
			VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
	}
	else
	{
		FNA3D_LogWarn(
			"%s is not supported!",
			VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
		);
	}

	if (renderer->supportsDebugUtils)
	{
		instanceExtensionNames[instanceExtensionCount++] =
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	}
	else if (renderer->debugMode)
	{
		FNA3D_LogWarn(
			"%s is not supported!",
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME
		);
	}

	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pNext = NULL;
	createInfo.flags = 0;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.ppEnabledLayerNames = layerNames;
	createInfo.enabledExtensionCount = instanceExtensionCount;
	createInfo.ppEnabledExtensionNames = instanceExtensionNames;
	if (renderer->debugMode)
	{
		createInfo.enabledLayerCount = SDL_arraysize(layerNames);
		if (VULKAN_INTERNAL_CheckValidationLayers(
			layerNames,
			createInfo.enabledLayerCount
		)) {
			FNA3D_LogInfo("Vulkan validation enabled! Expect debug-level performance!");
		}
		else
		{
			FNA3D_LogWarn("Validation layers not found, continuing without validation");
			createInfo.enabledLayerCount = 0;
		}
	}
	else
	{
		createInfo.enabledLayerCount = 0;
	}
	if (renderer->supportsDebugUtils)
	{
		debugMessengerCreateInfo.sType =
			VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debugMessengerCreateInfo.pNext = NULL;
		debugMessengerCreateInfo.flags = 0;
		debugMessengerCreateInfo.messageSeverity = (
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
		);
		debugMessengerCreateInfo.messageType = (
			VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT
		);
		debugMessengerCreateInfo.pfnUserCallback = VULKAN_INTERNAL_DebugCallback;
		debugMessengerCreateInfo.pUserData = NULL;

		createInfo.pNext = &debugMessengerCreateInfo;
	}

	vulkanResult = vkCreateInstance(&createInfo, NULL, &renderer->instance);
	if (vulkanResult != VK_SUCCESS)
	{
		FNA3D_LogWarn(
			"vkCreateInstance failed: %s",
			VkErrorMessages(vulkanResult)
		);
		goto create_instance_fail;
	}

	SDL_stack_free((char*) instanceExtensionNames);
	return 1;

create_instance_fail:
	SDL_stack_free((char*) instanceExtensionNames);
	return 0;
}

static uint8_t VULKAN_INTERNAL_DeterminePhysicalDevice(VulkanRenderer *renderer, VkSurfaceKHR surface)
{
	VkResult vulkanResult;
	VkPhysicalDevice *physicalDevices;
	VulkanExtensions *physicalDeviceExtensions;
	uint32_t physicalDeviceCount, i, suitableIndex;
	uint32_t queueFamilyIndex, suitableQueueFamilyIndex;
	VkDeviceSize deviceLocalHeapSize;
	const char *deviceLocalHeapUsageFactorStr;
	float deviceLocalHeapUsageFactor = 1.0f;
	uint8_t deviceRank, highestRank;

	vulkanResult = renderer->vkEnumeratePhysicalDevices(
		renderer->instance,
		&physicalDeviceCount,
		NULL
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkEnumeratePhysicalDevices, 0)

	if (physicalDeviceCount == 0)
	{
		FNA3D_LogWarn("Failed to find any GPUs with Vulkan support");
		return 0;
	}

	physicalDevices = SDL_stack_alloc(VkPhysicalDevice, physicalDeviceCount);
	physicalDeviceExtensions = SDL_stack_alloc(VulkanExtensions, physicalDeviceCount);

	vulkanResult = renderer->vkEnumeratePhysicalDevices(
		renderer->instance,
		&physicalDeviceCount,
		physicalDevices
	);

	/* This should be impossible to hit, but from what I can tell this can
	 * be triggerd not because the array is too small, but because there
	 * were drivers that turned out to be bogus, so this is the loader's way
	 * of telling us that the list is now smaller than expected :shrug:
	 */
	if (vulkanResult == VK_INCOMPLETE)
	{
		FNA3D_LogWarn("vkEnumeratePhysicalDevices returned VK_INCOMPLETE, will keep trying anyway...");
		vulkanResult = VK_SUCCESS;
	}

	if (vulkanResult != VK_SUCCESS)
	{
		FNA3D_LogWarn(
			"vkEnumeratePhysicalDevices failed: %s",
			VkErrorMessages(vulkanResult)
		);
		SDL_stack_free(physicalDevices);
		SDL_stack_free(physicalDeviceExtensions);
		return 0;
	}

	/* Any suitable device will do, but we'd like the best */
	suitableIndex = -1;
	highestRank = 0;
	for (i = 0; i < physicalDeviceCount; i += 1)
	{
		deviceRank = highestRank;
		if (VULKAN_INTERNAL_IsDeviceSuitable(
			renderer,
			physicalDevices[i],
			&physicalDeviceExtensions[i],
			surface,
			&queueFamilyIndex,
			&deviceRank
		)) {
			/* Use this for rendering.
			 * Note that this may override a previous device that
			 * supports rendering, but shares the same device rank.
			 */
			suitableIndex = i;
			suitableQueueFamilyIndex = queueFamilyIndex;
			highestRank = deviceRank;
		}
		else if (deviceRank > highestRank)
		{
			/* In this case, we found a... "realer?" GPU,
			 * but it doesn't actually support our Vulkan.
			 * We should disqualify all devices below as a
			 * result, because if we don't we end up
			 * ignoring real hardware and risk using
			 * something like LLVMpipe instead!
			 * -flibit
			 */
			suitableIndex = -1;
			highestRank = deviceRank;
		}
	}

	if (suitableIndex != -1)
	{
		renderer->supports = physicalDeviceExtensions[suitableIndex];
		renderer->physicalDevice = physicalDevices[suitableIndex];
		renderer->queueFamilyIndex = suitableQueueFamilyIndex;
	}
	else
	{
		SDL_stack_free(physicalDevices);
		SDL_stack_free(physicalDeviceExtensions);
		return 0;
	}

	if (	renderer->supportsDeviceProperties2 &&
		renderer->supports.KHR_driver_properties	)
	{
		renderer->physicalDeviceProperties.sType =
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		renderer->physicalDeviceProperties.pNext =
			&renderer->physicalDeviceDriverProperties;

		renderer->physicalDeviceDriverProperties.sType =
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR;
		renderer->physicalDeviceDriverProperties.pNext = NULL;

		renderer->vkGetPhysicalDeviceProperties2KHR(
			renderer->physicalDevice,
			&renderer->physicalDeviceProperties
		);
	}
	else
	{
		/* These won't be used, just initialize them to something */
		renderer->physicalDeviceProperties.sType = ~0;
		renderer->physicalDeviceProperties.pNext = NULL;

		renderer->vkGetPhysicalDeviceProperties(
			renderer->physicalDevice,
			&renderer->physicalDeviceProperties.properties
		);
	}

	renderer->vkGetPhysicalDeviceMemoryProperties(
		renderer->physicalDevice,
		&renderer->memoryProperties
	);

	deviceLocalHeapUsageFactorStr = SDL_GetHint("FNA3D_VULKAN_DEVICE_LOCAL_HEAP_USAGE_FACTOR");
	if (deviceLocalHeapUsageFactorStr != NULL)
	{
		double factor = SDL_atof(deviceLocalHeapUsageFactorStr);
		if (factor > 0.0 && factor < 1.0)
		{
			deviceLocalHeapUsageFactor = factor;
		}

		deviceLocalHeapSize = 0;
		for (i = 0; i < renderer->memoryProperties.memoryHeapCount; i += 1)
		{
			if (	renderer->memoryProperties.memoryHeaps[i].flags &
				VK_MEMORY_HEAP_DEVICE_LOCAL_BIT	)
			{
				if (renderer->memoryProperties.memoryHeaps[i].size > deviceLocalHeapSize)
				{
					deviceLocalHeapSize = renderer->memoryProperties.memoryHeaps[i].size;
				}
			}
		}

		renderer->maxDeviceLocalHeapUsage = deviceLocalHeapSize * deviceLocalHeapUsageFactor;
	}
	else
	{
		/* Don't even attempt to track this, let the driver do the work */
		renderer->maxDeviceLocalHeapUsage = UINT64_MAX;
	}

	renderer->deviceLocalHeapUsage = 0;

	SDL_stack_free(physicalDevices);
	SDL_stack_free(physicalDeviceExtensions);
	return 1;
}

static uint8_t VULKAN_INTERNAL_CreateLogicalDevice(VulkanRenderer *renderer)
{
	VkResult vulkanResult;
	VkDeviceCreateInfo deviceCreateInfo;
	VkPhysicalDeviceFeatures deviceFeatures;
	VkPhysicalDevicePortabilitySubsetFeaturesKHR portabilityFeatures;
	const char **deviceExtensions;

	VkDeviceQueueCreateInfo queueCreateInfo;
	float queuePriority = 1.0f;

	queueCreateInfo.sType =
		VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfo.pNext = NULL;
	queueCreateInfo.flags = 0;
	queueCreateInfo.queueFamilyIndex = renderer->queueFamilyIndex;
	queueCreateInfo.queueCount = 1;
	queueCreateInfo.pQueuePriorities = &queuePriority;

	/* specifying used device features */

	SDL_zero(deviceFeatures);
	deviceFeatures.occlusionQueryPrecise = renderer->supportsPreciseOcclusionQueries;
	deviceFeatures.fillModeNonSolid = VK_TRUE;
	deviceFeatures.samplerAnisotropy = VK_TRUE;

	/* Creating the logical device */

	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	if (renderer->supports.KHR_portability_subset)
	{
		portabilityFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR;
		portabilityFeatures.pNext = NULL;
		portabilityFeatures.constantAlphaColorBlendFactors = VK_FALSE;
		portabilityFeatures.events = VK_FALSE;
		portabilityFeatures.imageViewFormatReinterpretation = VK_FALSE;
		portabilityFeatures.imageViewFormatSwizzle = VK_TRUE;
		portabilityFeatures.imageView2DOn3DImage = VK_FALSE;
		portabilityFeatures.multisampleArrayImage = VK_FALSE;
		portabilityFeatures.mutableComparisonSamplers = VK_FALSE;
		portabilityFeatures.pointPolygons = VK_FALSE;
		portabilityFeatures.samplerMipLodBias = VK_FALSE; /* Technically should be true, but eh */
		portabilityFeatures.separateStencilMaskRef = VK_FALSE;
		portabilityFeatures.shaderSampleRateInterpolationFunctions = VK_FALSE;
		portabilityFeatures.tessellationIsolines = VK_FALSE;
		portabilityFeatures.tessellationPointMode = VK_FALSE;
		portabilityFeatures.triangleFans = VK_FALSE;
		portabilityFeatures.vertexAttributeAccessBeyondStride = VK_FALSE;
		deviceCreateInfo.pNext = &portabilityFeatures;
	}
	else
	{
		deviceCreateInfo.pNext = NULL;
	}
	deviceCreateInfo.flags = 0;
	deviceCreateInfo.queueCreateInfoCount = 1;
	deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
	deviceCreateInfo.enabledLayerCount = 0;
	deviceCreateInfo.ppEnabledLayerNames = NULL;
	deviceCreateInfo.enabledExtensionCount = GetDeviceExtensionCount(
		&renderer->supports
	);
	deviceExtensions = SDL_stack_alloc(
		const char*,
		deviceCreateInfo.enabledExtensionCount
	);
	CreateDeviceExtensionArray(&renderer->supports, deviceExtensions);
	deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions;
	deviceCreateInfo.pEnabledFeatures = &deviceFeatures;

	vulkanResult = renderer->vkCreateDevice(
		renderer->physicalDevice,
		&deviceCreateInfo,
		NULL,
		&renderer->logicalDevice
	);
	SDL_stack_free(deviceExtensions);
	VULKAN_ERROR_CHECK(vulkanResult, vkCreateDevice, 0)

	/* Load vkDevice entry points */

	#define VULKAN_DEVICE_FUNCTION(name) \
		renderer->name = (PFN_##name) \
			renderer->vkGetDeviceProcAddr( \
				renderer->logicalDevice, \
				#name \
			);
	#include "FNA3D_Driver_Vulkan_vkfuncs.h"

	renderer->vkGetDeviceQueue(
		renderer->logicalDevice,
		renderer->queueFamilyIndex,
		0,
		&renderer->unifiedQueue
	);

	return 1;
}

/* Vulkan: Memory Allocation */

static uint8_t VULKAN_INTERNAL_FindBufferMemoryRequirements(
	VulkanRenderer *renderer,
	VkBuffer buffer,
	VkMemoryPropertyFlags requiredMemoryProperties,
	VkMemoryPropertyFlags ignoredMemoryProperties,
	VkMemoryRequirements *pMemoryRequirements,
	uint32_t *pMemoryTypeIndex
) {
	renderer->vkGetBufferMemoryRequirements(
		renderer->logicalDevice,
		buffer,
		pMemoryRequirements
	);

	return VULKAN_INTERNAL_FindMemoryType(
		renderer,
		pMemoryRequirements->memoryTypeBits,
		requiredMemoryProperties,
		ignoredMemoryProperties,
		pMemoryTypeIndex
	);
}

static uint8_t VULKAN_INTERNAL_FindImageMemoryRequirements(
	VulkanRenderer *renderer,
	VkImage image,
	VkMemoryPropertyFlags requiredMemoryPropertyFlags,
	VkMemoryPropertyFlags ignoredMemoryPropertyFlags,
	VkMemoryRequirements *pMemoryRequirements,
	uint32_t *pMemoryTypeIndex
) {
	renderer->vkGetImageMemoryRequirements(
		renderer->logicalDevice,
		image,
		pMemoryRequirements
	);

	return VULKAN_INTERNAL_FindMemoryType(
		renderer,
		pMemoryRequirements->memoryTypeBits,
		requiredMemoryPropertyFlags,
		ignoredMemoryPropertyFlags,
		pMemoryTypeIndex
	);
}

static uint8_t VULKAN_INTERNAL_BindMemoryForImage(
	VulkanRenderer* renderer,
	VkImage image,
	VulkanTexture *imageHandle,
	uint8_t isRenderTarget,
	FNA3D_MemoryUsedRegion** usedRegion
) {
	uint8_t bindResult = 0;
	uint32_t memoryTypeIndex = 0;
	VkMemoryPropertyFlags requiredMemoryPropertyFlags;
	VkMemoryPropertyFlags ignoredMemoryPropertyFlags;
	VkMemoryRequirements memoryRequirements;

	/* Prefer GPU allocation */
	requiredMemoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	ignoredMemoryPropertyFlags = 0;

	while (VULKAN_INTERNAL_FindImageMemoryRequirements(
		renderer,
		image,
		requiredMemoryPropertyFlags,
		ignoredMemoryPropertyFlags,
		&memoryRequirements,
		&memoryTypeIndex
	)) {
		bindResult = FNA3D_Memory_BindResource(
			renderer->allocator,
			memoryTypeIndex,
			memoryRequirements.size,
			memoryRequirements.alignment,
			(renderer->memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags &
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0,
			(renderer->memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags &
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0,
			isRenderTarget,
			memoryRequirements.size,
			1,
			(FNA3D_MemoryPlatformHandle) (size_t) image,
			imageHandle,
			usedRegion
		);

		if (bindResult == 1)
		{
			break;
		}
		else /* Bind failed, try the next device-local heap */
		{
			memoryTypeIndex += 1;
		}
	}

	/* Bind _still_ failed, try again without device local */
	if (bindResult != 1)
	{
		memoryTypeIndex = 0;
		requiredMemoryPropertyFlags = 0;
		ignoredMemoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

		if (isRenderTarget)
		{
			FNA3D_LogWarn("RenderTarget is allocated in host memory, pre-allocate your targets!");
		}

		FNA3D_LogWarn("Out of device local memory, falling back to host memory");

		while (VULKAN_INTERNAL_FindImageMemoryRequirements(
			renderer,
			image,
			requiredMemoryPropertyFlags,
			ignoredMemoryPropertyFlags,
			&memoryRequirements,
			&memoryTypeIndex
		)) {
			bindResult = FNA3D_Memory_BindResource(
				renderer->allocator,
				memoryTypeIndex,
				memoryRequirements.size,
				memoryRequirements.alignment,
				(renderer->memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags &
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0,
				(renderer->memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags &
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0,
				isRenderTarget,
				memoryRequirements.size,
				1,
				(FNA3D_MemoryPlatformHandle) (size_t) image,
				imageHandle,
				usedRegion
			);

			if (bindResult == 1)
			{
				break;
			}
			else /* Bind failed, try the next heap */
			{
				memoryTypeIndex += 1;
			}
		}
	}

	return bindResult;
}

static uint8_t VULKAN_INTERNAL_BindMemoryForBuffer(
	VulkanRenderer* renderer,
	VkBuffer buffer,
	VulkanBuffer *bufferHandle,
	VkDeviceSize size,
	uint8_t preferDeviceLocal,
	uint8_t isTransferBuffer,
	FNA3D_MemoryUsedRegion** usedRegion
) {
	uint8_t bindResult = 0;
	uint32_t memoryTypeIndex = 0;
	VkMemoryPropertyFlags requiredMemoryPropertyFlags;
	VkMemoryPropertyFlags ignoredMemoryPropertyFlags;
	VkMemoryRequirements memoryRequirements;

	requiredMemoryPropertyFlags =
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	if (preferDeviceLocal)
	{
		requiredMemoryPropertyFlags |=
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	}
	ignoredMemoryPropertyFlags = 0;

	while (VULKAN_INTERNAL_FindBufferMemoryRequirements(
		renderer,
		buffer,
		requiredMemoryPropertyFlags,
		ignoredMemoryPropertyFlags,
		&memoryRequirements,
		&memoryTypeIndex
	)) {
		bindResult = FNA3D_Memory_BindResource(
			renderer->allocator,
			memoryTypeIndex,
			memoryRequirements.size,
			memoryRequirements.alignment,
			(renderer->memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags &
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0,
			(renderer->memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags &
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0,
			isTransferBuffer,
			size,
			0,
			(FNA3D_MemoryPlatformHandle) (size_t) buffer,
			bufferHandle,
			usedRegion
		);

		if (bindResult == 1)
		{
			break;
		}
		else /* Bind failed, try the next device-local heap */
		{
			memoryTypeIndex += 1;
		}
	}

	/* Bind failed, try again if originally preferred device local */
	if (bindResult != 1 && preferDeviceLocal)
	{
		memoryTypeIndex = 0;
		requiredMemoryPropertyFlags =
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

		/* Follow-up for the warning logged by FindMemoryType */
		if (!renderer->unifiedMemoryWarning)
		{
			FNA3D_LogWarn("No unified memory found, falling back to host memory");
			renderer->unifiedMemoryWarning = 1;
		}

		while (VULKAN_INTERNAL_FindBufferMemoryRequirements(
			renderer,
			buffer,
			requiredMemoryPropertyFlags,
			ignoredMemoryPropertyFlags,
			&memoryRequirements,
			&memoryTypeIndex
		)) {
			bindResult = FNA3D_Memory_BindResource(
				renderer->allocator,
				memoryTypeIndex,
				memoryRequirements.size,
				memoryRequirements.alignment,
				(renderer->memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags &
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0,
				(renderer->memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags &
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0,
				isTransferBuffer,
				size,
				0,
				(FNA3D_MemoryPlatformHandle) (size_t) buffer,
				bufferHandle,
				usedRegion
			);

			if (bindResult == 1)
			{
				break;
			}
			else /* Bind failed, try the next heap */
			{
				memoryTypeIndex += 1;
			}
		}
	}

	return bindResult;
}

/* Vulkan: Resource Disposal */

static void VULKAN_INTERNAL_DestroyBuffer(
	VulkanRenderer *renderer,
	VulkanBuffer *buffer
) {
	renderer->vkDestroyBuffer(
		renderer->logicalDevice,
		buffer->buffer,
		NULL
	);

	renderer->needDefrag |= FNA3D_INTERNAL_RemoveMemoryUsedRegion(
		renderer->allocator,
		buffer->usedRegion
	);
	renderer->resourceFreed = 1;

	FNA3D_CommandBuffer_ClearDestroyedBuffer(
		renderer->commandBuffers,
		(FNA3D_BufferHandle*) buffer
	);

	SDL_free(buffer);
}

static void VULKAN_INTERNAL_DestroyImageView(
	VulkanRenderer* renderer,
	VkImageView imageView
) {
	renderer->vkDestroyImageView(
		renderer->logicalDevice,
		imageView,
		NULL
	);
}

/* When a render target image view is destroyed we need to invalidate
 * the framebuffers that reference it */
static void VULKAN_INTERNAL_RemoveViewFramebuffer(
	VulkanRenderer *renderer,
	VkImageView imageView
) {
	int32_t i, j;

	for (i = renderer->framebufferArray.count - 1; i >= 0; i -= 1)
	{
		if (renderer->framebufferArray.elements[i].key.depthStencilAttachmentView == imageView)
		{
			renderer->vkDestroyFramebuffer(
				renderer->logicalDevice,
				renderer->framebufferArray.elements[i].value,
				NULL
			);

			FramebufferHashArray_Remove(
				&renderer->framebufferArray,
				i
			);
		}
		else
		{
			for (j = 0; j < MAX_RENDERTARGET_BINDINGS; j += 1)
			{
				if (
					renderer->framebufferArray.elements[i].key.colorAttachmentViews[j] == imageView ||
					renderer->framebufferArray.elements[i].key.colorMultiSampleAttachmentViews[j] == imageView
				)
				{
					renderer->vkDestroyFramebuffer(
						renderer->logicalDevice,
						renderer->framebufferArray.elements[i].value,
						NULL
					);

					FramebufferHashArray_Remove(
						&renderer->framebufferArray,
						i
					);

					break;
				}
			}
		}
	}

	VULKAN_INTERNAL_DestroyImageView(
		renderer,
		imageView
	);
}

static void VULKAN_INTERNAL_DestroyTexture(
	VulkanRenderer *renderer,
	VulkanTexture *texture
) {
	int32_t i;

	if (texture->external)
	{
		SDL_free(texture);
		return;
	}

	VULKAN_INTERNAL_DestroyImageView(
		renderer,
		texture->view
	);

	if (texture->isRenderTarget)
	{
		if (texture->rtViews[0] != texture->view)
		{
			VULKAN_INTERNAL_RemoveViewFramebuffer(
				renderer,
				texture->rtViews[0]
			);
		}

		if (texture->rtViews[1] != VK_NULL_HANDLE)
		{
			/* Free all the other cube RT views */
			for (i = 1; i < 6; i += 1)
			{
				VULKAN_INTERNAL_RemoveViewFramebuffer(
					renderer,
					texture->rtViews[i]
				);
			}
		}
	}

	renderer->vkDestroyImage(
		renderer->logicalDevice,
		texture->image,
		NULL
	);

	renderer->needDefrag |= FNA3D_INTERNAL_RemoveMemoryUsedRegion(
		renderer->allocator,
		texture->usedRegion
	);
	renderer->resourceFreed = 1;

	SDL_free(texture);
}

/* Vulkan: Memory Barriers */

static void VULKAN_INTERNAL_BufferMemoryBarrier(
	VulkanRenderer *renderer,
	VulkanResourceAccessType nextResourceAccessType,
	VkBuffer buffer,
	VulkanResourceAccessType *resourceAccessType
) {
	VulkanCommandBuffer *commandBuffer;
	VkPipelineStageFlags srcStages = 0;
	VkPipelineStageFlags dstStages = 0;
	VkBufferMemoryBarrier memoryBarrier;
	VulkanResourceAccessType prevAccess, nextAccess;
	const VulkanResourceAccessInfo *prevAccessInfo, *nextAccessInfo;

	SDL_LockMutex(renderer->passLock);

	memoryBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	memoryBarrier.pNext = NULL;
	memoryBarrier.srcAccessMask = 0;
	memoryBarrier.dstAccessMask = 0;
	memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrier.buffer = buffer;
	memoryBarrier.offset = 0;
	memoryBarrier.size = VK_WHOLE_SIZE;

	prevAccess = *resourceAccessType;
	prevAccessInfo = &AccessMap[prevAccess];

	srcStages |= prevAccessInfo->stageMask;

	if (prevAccess > RESOURCE_ACCESS_END_OF_READ)
	{
		memoryBarrier.srcAccessMask |= prevAccessInfo->accessMask;
	}

	nextAccess = nextResourceAccessType;
	nextAccessInfo = &AccessMap[nextAccess];

	dstStages |= nextAccessInfo->stageMask;

	if (memoryBarrier.srcAccessMask != 0)
	{
		memoryBarrier.dstAccessMask |= nextAccessInfo->accessMask;
	}

	if (srcStages == 0)
	{
		srcStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	}
	if (dstStages == 0)
	{
		dstStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	}

	VULKAN_INTERNAL_MaybeEndRenderPass(renderer);
	renderer->needNewRenderPass = 1;

	commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
		renderer->commandBuffers
	);
	RECORD_CMD(renderer->vkCmdPipelineBarrier(
		commandBuffer->commandBuffer,
		srcStages,
		dstStages,
		0,
		0,
		NULL,
		1,
		&memoryBarrier,
		0,
		NULL
	));

	*resourceAccessType = nextResourceAccessType;

	SDL_UnlockMutex(renderer->passLock);
}

static void VULKAN_INTERNAL_ImageMemoryBarrier(
	VulkanRenderer *renderer,
	VulkanResourceAccessType nextAccess,
	VkImageAspectFlags aspectMask,
	uint32_t baseLayer,
	uint32_t layerCount,
	uint32_t baseLevel,
	uint32_t levelCount,
	uint8_t discardContents,
	VkImage image,
	VulkanResourceAccessType *resourceAccessType
) {
	VulkanCommandBuffer *commandBuffer;
	VkPipelineStageFlags srcStages = 0;
	VkPipelineStageFlags dstStages = 0;
	VkImageMemoryBarrier memoryBarrier;
	VulkanResourceAccessType prevAccess;
	const VulkanResourceAccessInfo *pPrevAccessInfo, *pNextAccessInfo;

	SDL_LockMutex(renderer->passLock);

	memoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	memoryBarrier.pNext = NULL;
	memoryBarrier.srcAccessMask = 0;
	memoryBarrier.dstAccessMask = 0;
	memoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	memoryBarrier.newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrier.image = image;
	memoryBarrier.subresourceRange.aspectMask = aspectMask;
	memoryBarrier.subresourceRange.baseArrayLayer = baseLayer;
	memoryBarrier.subresourceRange.layerCount = layerCount;
	memoryBarrier.subresourceRange.baseMipLevel = baseLevel;
	memoryBarrier.subresourceRange.levelCount = levelCount;

	prevAccess = *resourceAccessType;
	pPrevAccessInfo = &AccessMap[prevAccess];

	srcStages |= pPrevAccessInfo->stageMask;

	if (prevAccess > RESOURCE_ACCESS_END_OF_READ)
	{
		memoryBarrier.srcAccessMask |= pPrevAccessInfo->accessMask;
	}

	if (discardContents)
	{
		memoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}
	else
	{
		memoryBarrier.oldLayout = pPrevAccessInfo->imageLayout;
	}

	pNextAccessInfo = &AccessMap[nextAccess];

	dstStages |= pNextAccessInfo->stageMask;

	memoryBarrier.dstAccessMask |= pNextAccessInfo->accessMask;
	memoryBarrier.newLayout = pNextAccessInfo->imageLayout;

	if (srcStages == 0)
	{
		srcStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	}
	if (dstStages == 0)
	{
		dstStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	}

	VULKAN_INTERNAL_MaybeEndRenderPass(renderer);
	renderer->needNewRenderPass = 1;

	commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
		renderer->commandBuffers
	);
	RECORD_CMD(renderer->vkCmdPipelineBarrier(
		commandBuffer->commandBuffer,
		srcStages,
		dstStages,
		0,
		0,
		NULL,
		0,
		NULL,
		1,
		&memoryBarrier
	));

	*resourceAccessType = nextAccess;

	SDL_UnlockMutex(renderer->passLock);
}

/* Allocator functions */

static VulkanBuffer* VULKAN_INTERNAL_CreateBuffer(
	VulkanRenderer *renderer,
	VkDeviceSize size,
	VulkanResourceAccessType resourceAccessType,
	VkBufferUsageFlags usage,
	uint8_t preferDeviceLocal,
	uint8_t isTransferBuffer
) {
	VkBufferCreateInfo bufferCreateInfo;
	VkResult vulkanResult;
	uint8_t bindResult = 0;
	VulkanBuffer *buffer = SDL_malloc(sizeof(VulkanBuffer));

	buffer->size = size;
	buffer->resourceAccessType = resourceAccessType;
	buffer->usage = usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	buffer->preferDeviceLocal = preferDeviceLocal;
	buffer->isTransferBuffer = isTransferBuffer;
	SDL_AtomicSet(&buffer->refcount, 0);

	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.pNext = NULL;
	bufferCreateInfo.flags = 0;
	bufferCreateInfo.size = buffer->size;
	bufferCreateInfo.usage = buffer->usage;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	bufferCreateInfo.queueFamilyIndexCount = 1;
	bufferCreateInfo.pQueueFamilyIndices = &renderer->queueFamilyIndex;

	vulkanResult = renderer->vkCreateBuffer(
		renderer->logicalDevice,
		&bufferCreateInfo,
		NULL,
		&buffer->buffer
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkCreateBuffer, NULL)

	buffer->bufferCreateInfo = bufferCreateInfo;

	bindResult = VULKAN_INTERNAL_BindMemoryForBuffer(
		renderer,
		buffer->buffer,
		buffer,
		buffer->size,
		buffer->preferDeviceLocal,
		buffer->isTransferBuffer,
		&buffer->usedRegion
	);

	/* Binding failed, bail out! */
	if (bindResult != 1)
	{
		renderer->vkDestroyBuffer(
			renderer->logicalDevice,
			buffer->buffer,
			NULL);

		return NULL;
	}

	return buffer;
}

/* Transfer buffer functions */

static inline VkDeviceSize VULKAN_INTERNAL_NextHighestAlignment(
	VkDeviceSize n,
	VkDeviceSize align
) {
	return align * ((n + align - 1) / align);
}

static void VULKAN_INTERNAL_CopyToTransferBuffer(
	VulkanRenderer *renderer,
	void* data,
	uint32_t uploadLength,
	uint32_t copyLength,
	VulkanBuffer **pTransferBuffer,
	VkDeviceSize *pOffset,
	VkDeviceSize alignment
) {
	FNA3D_TransferBuffer *transferBuffer;
	VulkanBuffer *parentBuffer;
	uint8_t *transferBufferPointer;
	VkDeviceSize fmtAlignment = VULKAN_INTERNAL_NextHighestAlignment(
		alignment,
		renderer->physicalDeviceProperties.properties.limits.optimalBufferCopyOffsetAlignment
	);

	transferBuffer = FNA3D_CommandBuffer_AcquireTransferBuffer(
		renderer->commandBuffers,
		uploadLength,
		fmtAlignment
	);
	parentBuffer = (VulkanBuffer*) transferBuffer->buffer;

	transferBufferPointer = FNA3D_Memory_GetHostPointer(
		parentBuffer->usedRegion,
		transferBuffer->offset
	);

	SDL_memcpy(
		transferBufferPointer,
		data,
		copyLength
	);

	*pTransferBuffer = (VulkanBuffer*) transferBuffer->buffer;
	*pOffset = transferBuffer->offset;

	transferBuffer->offset += copyLength;
}

static void VULKAN_INTERNAL_PrepareCopyFromTransferBuffer(
	VulkanRenderer *renderer,
	VkDeviceSize dataLength,
	VkDeviceSize alignment,
	FNA3D_TransferBuffer **pTransferBuffer,
	void **pTransferBufferPointer
) {
	VkDeviceSize fmtAlignment = VULKAN_INTERNAL_NextHighestAlignment(
		alignment,
		renderer->physicalDeviceProperties.properties.limits.optimalBufferCopyOffsetAlignment
	);

	FNA3D_TransferBuffer *transferBuffer = FNA3D_CommandBuffer_AcquireTransferBuffer(
		renderer->commandBuffers,
		dataLength,
		fmtAlignment
	);
	VulkanBuffer *parentBuffer = (VulkanBuffer*) transferBuffer->buffer;

	*pTransferBuffer = transferBuffer;
	*pTransferBufferPointer = FNA3D_Memory_GetHostPointer(
		parentBuffer->usedRegion,
		transferBuffer->offset
	);
}

/* Vulkan: Descriptor Set Logic */

static uint8_t VULKAN_INTERNAL_CreateDescriptorPool(
	VulkanRenderer *renderer,
	VkDescriptorType descriptorType,
	uint32_t descriptorSetCount,
	uint32_t descriptorCount,
	VkDescriptorPool *pDescriptorPool
) {
	VkResult vulkanResult;

	VkDescriptorPoolSize descriptorPoolSize;
	VkDescriptorPoolCreateInfo descriptorPoolInfo;

	descriptorPoolSize.type = descriptorType;
	descriptorPoolSize.descriptorCount = descriptorCount;

	descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolInfo.pNext = NULL;
	descriptorPoolInfo.flags = 0;
	descriptorPoolInfo.maxSets = descriptorSetCount;
	descriptorPoolInfo.poolSizeCount = 1;
	descriptorPoolInfo.pPoolSizes = &descriptorPoolSize;

	vulkanResult = renderer->vkCreateDescriptorPool(
		renderer->logicalDevice,
		&descriptorPoolInfo,
		NULL,
		pDescriptorPool
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkCreateDescriptorPool, 0)

	return 1;
}

static uint8_t VULKAN_INTERNAL_AllocateDescriptorSets(
	VulkanRenderer *renderer,
	VkDescriptorPool descriptorPool,
	VkDescriptorSetLayout descriptorSetLayout,
	uint32_t descriptorSetCount,
	VkDescriptorSet *descriptorSetArray
) {
	VkResult vulkanResult;
	uint32_t i;
	VkDescriptorSetAllocateInfo descriptorSetAllocateInfo;
	VkDescriptorSetLayout *descriptorSetLayouts = SDL_stack_alloc(VkDescriptorSetLayout, descriptorSetCount);

	for (i = 0; i < descriptorSetCount; i += 1)
	{
		descriptorSetLayouts[i] = descriptorSetLayout;
	}

	descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAllocateInfo.pNext = NULL;
	descriptorSetAllocateInfo.descriptorPool = descriptorPool;
	descriptorSetAllocateInfo.descriptorSetCount = descriptorSetCount;
	descriptorSetAllocateInfo.pSetLayouts = descriptorSetLayouts;

	vulkanResult = renderer->vkAllocateDescriptorSets(
		renderer->logicalDevice,
		&descriptorSetAllocateInfo,
		descriptorSetArray
	);

	SDL_stack_free(descriptorSetLayouts);

	VULKAN_ERROR_CHECK(vulkanResult, vkAllocateDescriptorSets, 0)

	return 1;
}

static uint16_t VULKAN_INTERNAL_FetchSamplerBitmask(
	MOJOSHADER_vkShader *shader
) {
	const MOJOSHADER_parseData *parseData = MOJOSHADER_vkGetShaderParseData(shader);
	uint16_t bitmask = 0;
	uint8_t i;

	for (i = 0; i < parseData->sampler_count; i += 1)
	{
		bitmask |= (1 << parseData->samplers[i].index);
	}

	return bitmask;
}

static VkDescriptorSetLayout VULKAN_INTERNAL_FetchSamplerDescriptorSetLayout(
	VulkanRenderer *renderer,
	MOJOSHADER_vkShader *shader,
	VkShaderStageFlagBits stageFlag
) {
	DescriptorSetLayoutHash descriptorSetLayoutHash;
	VkDescriptorSetLayout descriptorSetLayout;

	VkDescriptorSetLayoutBinding setLayoutBindings[MAX_TEXTURE_SAMPLERS];
	VkDescriptorSetLayoutCreateInfo setLayoutCreateInfo;

	uint32_t samplerCount = MOJOSHADER_vkGetShaderParseData(shader)->sampler_count;
	MOJOSHADER_sampler *samplerInfos = MOJOSHADER_vkGetShaderParseData(shader)->samplers;

	VkResult vulkanResult;
	uint32_t i;

	descriptorSetLayoutHash.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorSetLayoutHash.stageFlag = stageFlag;
	descriptorSetLayoutHash.bitmask = VULKAN_INTERNAL_FetchSamplerBitmask(shader);

	descriptorSetLayout = DescriptorSetLayoutHashTable_Fetch(&renderer->descriptorSetLayoutTable, descriptorSetLayoutHash);

	if (descriptorSetLayout != VK_NULL_HANDLE)
	{
		return descriptorSetLayout;
	}

	if (samplerCount == 0) /* dummy sampler case */
	{
		setLayoutBindings[0].binding = 0;
		setLayoutBindings[0].descriptorCount = 1;
		setLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		setLayoutBindings[0].stageFlags = stageFlag;
		setLayoutBindings[0].pImmutableSamplers = NULL;
	}
	else
	{
		for (i = 0; i < samplerCount; i += 1)
		{
			setLayoutBindings[i].binding = samplerInfos[i].index;
			setLayoutBindings[i].descriptorCount = 1;
			setLayoutBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			setLayoutBindings[i].stageFlags = stageFlag;
			setLayoutBindings[i].pImmutableSamplers = NULL;
		}
	}

	setLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setLayoutCreateInfo.pNext = NULL;
	setLayoutCreateInfo.flags = 0;
	setLayoutCreateInfo.bindingCount = SDL_max(samplerCount, 1);
	setLayoutCreateInfo.pBindings = setLayoutBindings;

	vulkanResult = renderer->vkCreateDescriptorSetLayout(
		renderer->logicalDevice,
		&setLayoutCreateInfo,
		NULL,
		&descriptorSetLayout
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkCreateDescriptorSetLayout, VK_NULL_HANDLE)

	DescriptorSetLayoutHashTable_Insert(
		&renderer->descriptorSetLayoutTable,
		descriptorSetLayoutHash,
		descriptorSetLayout
	);

	return descriptorSetLayout;
}

static void VULKAN_INTERNAL_ClearDescriptorSets(
	FNA3D_Renderer *driverData,
	FNA3D_CommandBuffer *handle,
	void* callbackData
) {
	VulkanCommandBuffer *commandBuffer = (VulkanCommandBuffer*) handle;
	ShaderResources *shaderResources = (ShaderResources*) callbackData;
	uint32_t i;

	for (i = 0; i < commandBuffer->usedDescriptorSetDataCount; i += 1)
	{
		if (commandBuffer->usedDescriptorSetDatas[i].parent == shaderResources)
		{
			commandBuffer->usedDescriptorSetDatas[i].descriptorSet = VK_NULL_HANDLE;
		}
	}
}

static void ShaderResources_Destroy(
	VulkanRenderer *renderer,
	ShaderResources *shaderResources
) {
	uint32_t i;
	for (i = 0; i < shaderResources->samplerDescriptorPoolCount; i += 1)
	{
		renderer->vkDestroyDescriptorPool(
			renderer->logicalDevice,
			shaderResources->samplerDescriptorPools[i],
			NULL
		);
	}

	/* Don't return descriptor sets allocated from this shader resource to the inactive pool! */
	FNA3D_CommandBuffer_ForEachSubmittedBuffer(
		renderer->commandBuffers,
		VULKAN_INTERNAL_ClearDescriptorSets,
		shaderResources
	);

	SDL_free(shaderResources->samplerDescriptorPools);
	SDL_free(shaderResources->samplerBindingIndices);
	SDL_free(shaderResources->inactiveDescriptorSets);

	SDL_free(shaderResources);
}

static ShaderResources *ShaderResources_Init(
	VulkanRenderer *renderer,
	MOJOSHADER_vkShader *shader,
	VkShaderStageFlagBits shaderStageFlag
) {
	uint32_t i;
	VkBuffer vUniform, fUniform;
	VkWriteDescriptorSet writeDescriptorSet;
	VkDescriptorImageInfo imageInfo;
	unsigned long long vOff, fOff, vSize, fSize;
	uint32_t descriptorPoolSize = STARTING_SAMPLER_DESCRIPTOR_POOL_SIZE;
	ShaderResources *shaderResources = SDL_malloc(sizeof(ShaderResources));

	/*
	 * Lock to prevent mojoshader resources
	 * from being overwritten during initialization
	 */
	SDL_LockMutex(renderer->passLock);

	shaderResources->samplerLayout = VULKAN_INTERNAL_FetchSamplerDescriptorSetLayout(renderer, shader, shaderStageFlag);
	shaderResources->samplerCount = MOJOSHADER_vkGetShaderParseData(shader)->sampler_count;

	shaderResources->samplerDescriptorPools = SDL_malloc(sizeof(VkDescriptorPool));
	shaderResources->samplerDescriptorPoolCount = 1;

	if (shaderResources->samplerCount == 0)
	{
		descriptorPoolSize = 1;
	}

	VULKAN_INTERNAL_CreateDescriptorPool(
		renderer,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		descriptorPoolSize,
		descriptorPoolSize * SDL_max(shaderResources->samplerCount, 1), /* in case of dummy data */
		&shaderResources->samplerDescriptorPools[0]
	);

	shaderResources->nextPoolSize = STARTING_SAMPLER_DESCRIPTOR_POOL_SIZE * 2;

	shaderResources->samplerBindingIndices = SDL_malloc(sizeof(uint8_t) * shaderResources->samplerCount);

	for (i = 0; i < shaderResources->samplerCount; i += 1)
	{
		shaderResources->samplerBindingIndices[i] = MOJOSHADER_vkGetShaderParseData(shader)->samplers[i].index;
	}

	if (shaderResources->samplerCount > 0)
	{
		shaderResources->inactiveDescriptorSetCapacity = STARTING_SAMPLER_DESCRIPTOR_POOL_SIZE;
		shaderResources->inactiveDescriptorSetCount = STARTING_SAMPLER_DESCRIPTOR_POOL_SIZE - 1;
		shaderResources->inactiveDescriptorSets = SDL_malloc(sizeof(VkDescriptorSet) * STARTING_SAMPLER_DESCRIPTOR_POOL_SIZE);

		VULKAN_INTERNAL_AllocateDescriptorSets(
			renderer,
			shaderResources->samplerDescriptorPools[0],
			shaderResources->samplerLayout,
			STARTING_SAMPLER_DESCRIPTOR_POOL_SIZE,
			shaderResources->inactiveDescriptorSets
		);
	}
	else
	{
		shaderResources->inactiveDescriptorSetCapacity = 0;
		shaderResources->inactiveDescriptorSetCount = 0;
		shaderResources->inactiveDescriptorSets = NULL;

		/* Set up the dummy descriptor set */
		VULKAN_INTERNAL_AllocateDescriptorSets(
			renderer,
			shaderResources->samplerDescriptorPools[0],
			shaderResources->samplerLayout,
			1,
			&shaderResources->dummySamplerDescriptorSet
		);

		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		if (shaderStageFlag == VK_SHADER_STAGE_VERTEX_BIT)
		{
			imageInfo.imageView = renderer->dummyVertTexture->view;
			imageInfo.sampler = renderer->dummyVertSamplerState;
		}
		else
		{
			imageInfo.imageView = renderer->dummyFragTexture->view;
			imageInfo.sampler = renderer->dummyFragSamplerState;
		}

		writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSet.pNext = NULL;
		writeDescriptorSet.descriptorCount = 1;
		writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writeDescriptorSet.dstArrayElement = 0;
		writeDescriptorSet.dstBinding = 0;
		writeDescriptorSet.dstSet = shaderResources->dummySamplerDescriptorSet;
		writeDescriptorSet.pBufferInfo = NULL;
		writeDescriptorSet.pImageInfo = &imageInfo;
		writeDescriptorSet.pTexelBufferView = NULL;

		renderer->vkUpdateDescriptorSets(
			renderer->logicalDevice,
			1,
			&writeDescriptorSet,
			0,
			NULL
		);
	}

	MOJOSHADER_vkGetUniformBuffers(
		renderer->mojoshaderContext,
		&vUniform, &vOff, &vSize,
		&fUniform, &fOff, &fSize
	);

	if (shaderStageFlag == VK_SHADER_STAGE_VERTEX_BIT)
	{
		VULKAN_INTERNAL_AllocateDescriptorSets(
			renderer,
			renderer->uniformBufferDescriptorPool,
			renderer->vertexUniformBufferDescriptorSetLayout,
			1,
			&shaderResources->uniformDescriptorSet
		);

		shaderResources->uniformBufferInfo.buffer = vUniform;
		shaderResources->uniformBufferInfo.offset = 0;
		shaderResources->uniformBufferInfo.range = vSize;
	}
	else
	{
		VULKAN_INTERNAL_AllocateDescriptorSets(
			renderer,
			renderer->uniformBufferDescriptorPool,
			renderer->fragUniformBufferDescriptorSetLayout,
			1,
			&shaderResources->uniformDescriptorSet
		);

		shaderResources->uniformBufferInfo.buffer = fUniform;
		shaderResources->uniformBufferInfo.offset = 0;
		shaderResources->uniformBufferInfo.range = fSize;
	}

	if (shaderResources->uniformBufferInfo.buffer != VK_NULL_HANDLE)
	{
		writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSet.pNext = NULL;
		writeDescriptorSet.descriptorCount = 1;
		writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		writeDescriptorSet.dstArrayElement = 0;
		writeDescriptorSet.dstBinding = 0;
		writeDescriptorSet.dstSet = shaderResources->uniformDescriptorSet;
		writeDescriptorSet.pBufferInfo = &shaderResources->uniformBufferInfo;
		writeDescriptorSet.pImageInfo = NULL;

		renderer->vkUpdateDescriptorSets(
			renderer->logicalDevice,
			1,
			&writeDescriptorSet,
			0,
			NULL
		);
	}

	SDL_UnlockMutex(renderer->passLock);

	return shaderResources;
}

static ShaderResources* VULKAN_INTERNAL_FetchShaderResources(
	VulkanRenderer *renderer,
	MOJOSHADER_vkShader *shader,
	VkShaderStageFlagBits shaderStageFlag
) {
	ShaderResources *shaderResources = ShaderResourcesHashTable_Fetch(&renderer->shaderResourcesHashTable, shader);

	if (shaderResources == VK_NULL_HANDLE)
	{
		shaderResources = ShaderResources_Init(renderer, shader, shaderStageFlag);
		ShaderResourcesHashTable_Insert(&renderer->shaderResourcesHashTable, shader, shaderResources);
	}

	return shaderResources;
}

/* Fetches or creates a new descriptor set based on data */
static VkDescriptorSet ShaderResources_FetchDescriptorSet(
	VulkanRenderer *renderer,
	ShaderResources *shaderResources
) {
	VkDescriptorSet newDescriptorSet;

	/* If no inactive descriptor sets remain, create a new pool and allocate new inactive sets */

	if (shaderResources->inactiveDescriptorSetCount == 0)
	{
		shaderResources->samplerDescriptorPoolCount += 1;
		shaderResources->samplerDescriptorPools = SDL_realloc(
			shaderResources->samplerDescriptorPools,
			sizeof(VkDescriptorPool) * shaderResources->samplerDescriptorPoolCount
		);

		VULKAN_INTERNAL_CreateDescriptorPool(
			renderer,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			shaderResources->nextPoolSize,
			shaderResources->nextPoolSize * SDL_max(shaderResources->samplerCount, 1), /* dont want 0 in case of dummy data */
			&shaderResources->samplerDescriptorPools[shaderResources->samplerDescriptorPoolCount - 1]
		);

		shaderResources->inactiveDescriptorSetCapacity += shaderResources->nextPoolSize;

		shaderResources->inactiveDescriptorSets = SDL_realloc(
			shaderResources->inactiveDescriptorSets,
			sizeof(VkDescriptorSet) * shaderResources->inactiveDescriptorSetCapacity
		);

		VULKAN_INTERNAL_AllocateDescriptorSets(
			renderer,
			shaderResources->samplerDescriptorPools[shaderResources->samplerDescriptorPoolCount - 1],
			shaderResources->samplerLayout,
			shaderResources->nextPoolSize,
			shaderResources->inactiveDescriptorSets
		);

		shaderResources->inactiveDescriptorSetCount = shaderResources->nextPoolSize;

		shaderResources->nextPoolSize *= 2;
	}

	newDescriptorSet = shaderResources->inactiveDescriptorSets[shaderResources->inactiveDescriptorSetCount - 1];
	shaderResources->inactiveDescriptorSetCount -= 1;

	return newDescriptorSet;
}

static void VULKAN_INTERNAL_RegisterUsedDescriptorSet(
	VulkanRenderer *renderer,
	ShaderResources *parent,
	VkDescriptorSet descriptorSet
) {
	VulkanCommandBuffer *commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
		renderer->commandBuffers
	);

	if (commandBuffer->usedDescriptorSetDataCount >= commandBuffer->usedDescriptorSetDataCapacity)
	{
		commandBuffer->usedDescriptorSetDataCapacity *= 2;
		commandBuffer->usedDescriptorSetDatas = SDL_realloc(
			commandBuffer->usedDescriptorSetDatas,
			commandBuffer->usedDescriptorSetDataCapacity * sizeof(DescriptorSetData)
		);
	}

	commandBuffer->usedDescriptorSetDatas[commandBuffer->usedDescriptorSetDataCount].descriptorSet = descriptorSet;
	commandBuffer->usedDescriptorSetDatas[commandBuffer->usedDescriptorSetDataCount].parent = parent;
	commandBuffer->usedDescriptorSetDataCount += 1;
}

/* Must take an array of descriptor sets of size 4 */
static void VULKAN_INTERNAL_FetchDescriptorSetDataAndOffsets(
	VulkanRenderer *renderer,
	ShaderResources *vertShaderResources,
	ShaderResources *fragShaderResources,
	VkDescriptorSet *descriptorSets,
	uint32_t *dynamicOffsets
) {
	VkBuffer vUniform, fUniform;
	unsigned long long vOff, fOff, vSize, fSize; /* MojoShader type */

	MOJOSHADER_vkShader *vertShader, *fragShader;
	MOJOSHADER_samplerType samplerType;
	VkWriteDescriptorSet writeDescriptorSets[MAX_TEXTURE_SAMPLERS];
	VkDescriptorImageInfo imageInfos[MAX_TEXTURE_SAMPLERS];

	uint32_t i;

	MOJOSHADER_vkGetBoundShaders(renderer->mojoshaderContext, &vertShader, &fragShader);

	if (renderer->vertexSamplerDescriptorSetDataNeedsUpdate)
	{
		if (vertShaderResources->samplerCount == 0)
		{
			renderer->currentVertexSamplerDescriptorSet = vertShaderResources->dummySamplerDescriptorSet;
		}
		else
		{
			renderer->currentVertexSamplerDescriptorSet = ShaderResources_FetchDescriptorSet(
				renderer,
				vertShaderResources
			);

			for (i = 0; i < vertShaderResources->samplerCount; i += 1)
			{
				if (renderer->textures[MAX_TEXTURE_SAMPLERS + vertShaderResources->samplerBindingIndices[i]] != &NullTexture)
				{
					imageInfos[i].imageView = renderer->textures[MAX_TEXTURE_SAMPLERS + vertShaderResources->samplerBindingIndices[i]]->view;
					imageInfos[i].sampler = renderer->samplers[MAX_TEXTURE_SAMPLERS + vertShaderResources->samplerBindingIndices[i]];
					imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				}
				else
				{
					samplerType = MOJOSHADER_vkGetShaderParseData(vertShader)->samplers[i].type;
					if (samplerType == MOJOSHADER_SAMPLER_2D)
					{
						imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
						imageInfos[i].imageView = renderer->dummyVertTexture->view;
						imageInfos[i].sampler = renderer->dummyVertSamplerState;
					}
					else if (samplerType == MOJOSHADER_SAMPLER_VOLUME)
					{
						imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
						imageInfos[i].imageView = renderer->dummyVertTexture3D->view;
						imageInfos[i].sampler = renderer->dummyVertSampler3DState;
					}
					else if (samplerType == MOJOSHADER_SAMPLER_CUBE)
					{
						imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
						imageInfos[i].imageView = renderer->dummyVertTextureCube->view;
						imageInfos[i].sampler = renderer->dummyVertSamplerCubeState;
					}
				}

				writeDescriptorSets[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[i].pNext = NULL;
				writeDescriptorSets[i].descriptorCount = 1;
				writeDescriptorSets[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writeDescriptorSets[i].dstArrayElement = 0;
				writeDescriptorSets[i].dstBinding = vertShaderResources->samplerBindingIndices[i];
				writeDescriptorSets[i].dstSet = renderer->currentVertexSamplerDescriptorSet;
				writeDescriptorSets[i].pBufferInfo = NULL;
				writeDescriptorSets[i].pImageInfo = &imageInfos[i];
				writeDescriptorSets[i].pTexelBufferView = NULL;
			}

			renderer->vkUpdateDescriptorSets(
				renderer->logicalDevice,
				vertShaderResources->samplerCount,
				writeDescriptorSets,
				0,
				NULL
			);

			VULKAN_INTERNAL_RegisterUsedDescriptorSet(
				renderer,
				vertShaderResources,
				renderer->currentVertexSamplerDescriptorSet
			);
		}
	}

	if (renderer->fragSamplerDescriptorSetDataNeedsUpdate)
	{
		if (fragShaderResources->samplerCount == 0)
		{
			renderer->currentFragSamplerDescriptorSet = fragShaderResources->dummySamplerDescriptorSet;
		}
		else
		{
			renderer->currentFragSamplerDescriptorSet = ShaderResources_FetchDescriptorSet(
				renderer,
				fragShaderResources
			);

			for (i = 0; i < fragShaderResources->samplerCount; i += 1)
			{
				if (renderer->textures[fragShaderResources->samplerBindingIndices[i]] != &NullTexture)
				{
					imageInfos[i].imageView = renderer->textures[fragShaderResources->samplerBindingIndices[i]]->view;
					imageInfos[i].sampler = renderer->samplers[fragShaderResources->samplerBindingIndices[i]];
					imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				}
				else
				{

					samplerType = MOJOSHADER_vkGetShaderParseData(fragShader)->samplers[i].type;
					if (samplerType == MOJOSHADER_SAMPLER_2D)
					{
						imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
						imageInfos[i].imageView = renderer->dummyFragTexture->view;
						imageInfos[i].sampler = renderer->dummyFragSamplerState;
					}
					else if (samplerType == MOJOSHADER_SAMPLER_VOLUME)
					{
						imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
						imageInfos[i].imageView = renderer->dummyFragTexture3D->view;
						imageInfos[i].sampler = renderer->dummyFragSampler3DState;
					}
					else if (samplerType == MOJOSHADER_SAMPLER_CUBE)
					{
						imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
						imageInfos[i].imageView = renderer->dummyFragTextureCube->view;
						imageInfos[i].sampler = renderer->dummyFragSamplerCubeState;
					}
				}

				writeDescriptorSets[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[i].pNext = NULL;
				writeDescriptorSets[i].descriptorCount = 1;
				writeDescriptorSets[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writeDescriptorSets[i].dstArrayElement = 0;
				writeDescriptorSets[i].dstBinding = fragShaderResources->samplerBindingIndices[i];
				writeDescriptorSets[i].dstSet = renderer->currentFragSamplerDescriptorSet;
				writeDescriptorSets[i].pBufferInfo = NULL;
				writeDescriptorSets[i].pImageInfo = &imageInfos[i];
				writeDescriptorSets[i].pTexelBufferView = NULL;
			}

			renderer->vkUpdateDescriptorSets(
				renderer->logicalDevice,
				fragShaderResources->samplerCount,
				writeDescriptorSets,
				0,
				NULL
			);

			VULKAN_INTERNAL_RegisterUsedDescriptorSet(
				renderer,
				fragShaderResources,
				renderer->currentFragSamplerDescriptorSet
			);
		}
	}

	renderer->vertexSamplerDescriptorSetDataNeedsUpdate = 0;
	renderer->fragSamplerDescriptorSetDataNeedsUpdate = 0;

	descriptorSets[0] = renderer->currentVertexSamplerDescriptorSet;
	descriptorSets[1] = renderer->currentFragSamplerDescriptorSet;
	descriptorSets[2] = vertShaderResources->uniformDescriptorSet;
	descriptorSets[3] = fragShaderResources->uniformDescriptorSet;

	MOJOSHADER_vkGetUniformBuffers(
		renderer->mojoshaderContext,
		&vUniform, &vOff, &vSize,
		&fUniform, &fOff, &fSize
	);

	dynamicOffsets[0] = vOff;
	dynamicOffsets[1] = fOff;
}

/* Vulkan: Command Submission */

static void VULKAN_INTERNAL_SwapChainBlit(
	VulkanRenderer *renderer,
	VulkanSwapchainData *swapchainData,
	FNA3D_Rect * sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	uint32_t swapchainImageIndex
) {
	VulkanCommandBuffer *commandBuffer;
	FNA3D_Rect srcRect;
	FNA3D_Rect dstRect;
	VkImageBlit blit;

	if (sourceRectangle != NULL)
	{
		srcRect = *sourceRectangle;
	}
	else
	{
		srcRect.x = 0;
		srcRect.y = 0;
		srcRect.w = renderer->fauxBackbufferWidth;
		srcRect.h = renderer->fauxBackbufferHeight;
	}

	if (destinationRectangle != NULL)
	{
		dstRect = *destinationRectangle;
	}
	else
	{
		dstRect.x = 0;
		dstRect.y = 0;
		dstRect.w = swapchainData->extent.width;
		dstRect.h = swapchainData->extent.height;
	}

	/* Blit the framebuffer! */

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_READ,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		1,
		0,
		1,
		0,
		renderer->fauxBackbufferColor.handle->image,
		&renderer->fauxBackbufferColor.handle->resourceAccessType
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		1,
		0,
		1,
		0,
		swapchainData->images[swapchainImageIndex],
		&swapchainData->resourceAccessTypes[swapchainImageIndex]
	);

	blit.srcOffsets[0].x = srcRect.x;
	blit.srcOffsets[0].y = srcRect.y;
	blit.srcOffsets[0].z = 0;
	blit.srcOffsets[1].x = srcRect.x + srcRect.w;
	blit.srcOffsets[1].y = srcRect.y + srcRect.h;
	blit.srcOffsets[1].z = 1;

	blit.srcSubresource.mipLevel = 0;
	blit.srcSubresource.baseArrayLayer = 0;
	blit.srcSubresource.layerCount = 1;
	blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	blit.dstOffsets[0].x = dstRect.x;
	blit.dstOffsets[0].y = dstRect.y;
	blit.dstOffsets[0].z = 0;
	blit.dstOffsets[1].x = dstRect.x + dstRect.w;
	blit.dstOffsets[1].y = dstRect.y + dstRect.h;
	blit.dstOffsets[1].z = 1;

	blit.dstSubresource.mipLevel = 0;
	blit.dstSubresource.baseArrayLayer = 0;
	blit.dstSubresource.layerCount = 1;
	blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
		renderer->commandBuffers
	);
	RECORD_CMD(renderer->vkCmdBlitImage(
		commandBuffer->commandBuffer,
		renderer->fauxBackbufferColor.handle->image,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		swapchainData->images[swapchainImageIndex],
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&blit,
		VK_FILTER_LINEAR
	));

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_PRESENT,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		1,
		0,
		1,
		0,
		swapchainData->images[swapchainImageIndex],
		&swapchainData->resourceAccessTypes[swapchainImageIndex]
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		1,
		0,
		1,
		0,
		renderer->fauxBackbufferColor.handle->image,
		&renderer->fauxBackbufferColor.handle->resourceAccessType
	);
}

static void VULKAN_INTERNAL_CleanDefrag(VulkanRenderer *renderer)
{
	uint32_t i;

	FNA3D_Memory_LockAllocator(renderer->allocator);

	/* Destroy old resources that were copied on defragment */

	for (i = 0; i < renderer->defragmentedBuffersToDestroyCount; i += 1)
	{
		renderer->vkDestroyBuffer(
			renderer->logicalDevice,
			renderer->defragmentedBuffersToDestroy[i],
			NULL
		);
	}

	renderer->defragmentedBuffersToDestroyCount = 0;

	for (i = 0; i < renderer->defragmentedImagesToDestroyCount; i += 1)
	{
		renderer->vkDestroyImage(
			renderer->logicalDevice,
			renderer->defragmentedImagesToDestroy[i],
			NULL
		);
	}

	renderer->defragmentedImagesToDestroyCount = 0;

	for (i = 0; i < renderer->defragmentedImageViewsToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyImageView(
			renderer,
			renderer->defragmentedImageViewsToDestroy[i]
		);
	}

	renderer->defragmentedImageViewsToDestroyCount = 0;

	renderer->needDefrag |= FNA3D_Memory_DestroyDefragmentedRegions(
		renderer->allocator
	);
	renderer->resourceFreed = 1;

	FNA3D_Memory_UnlockAllocator(renderer->allocator);
}

static void VULKAN_INTERNAL_SubmitCommands(
	VulkanRenderer *renderer,
	uint8_t present,
	FNA3D_Rect *sourceRectangle,		/* ignored if present is false */
	FNA3D_Rect *destinationRectangle,	/* ignored if present is false */
	void *windowHandle 					/* ignored if present is false */
) {
	VkSemaphore semaphores[2];
	VkFence fences[2];
	uint32_t fenceCount = 0;
	VkSubmitInfo submitInfo;
	VkResult result;
	VkResult acquireResult = VK_SUCCESS;
	VkResult presentResult = VK_SUCCESS;
	uint8_t acquireSuccess = 0;
	uint8_t performDefrag = 0;
	uint8_t createSwapchainResult = 0;
	uint8_t validSwapchainExists = 0;
	VulkanSwapchainData *swapchainData = NULL;
	uint32_t swapchainImageIndex;
	VulkanCommandBuffer *commandBufferToSubmit;

	SDL_DisplayMode mode;
	VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkPresentInfoKHR presentInfo;
	struct
	{
		VkStructureType sType;
		const void *pNext;
		uint64_t frameToken;
	} presentInfoGGP;

	/* Must end render pass before ending command buffer */
	VULKAN_INTERNAL_MaybeEndRenderPass(renderer);

	if (present)
	{
		SDL_GetCurrentDisplayMode(
			SDL_GetWindowDisplayIndex(
				(SDL_Window*) windowHandle
			),
			&mode
		);
		if (mode.refresh_rate == 0)
		{
			/* Needs to be _something_ */
			mode.refresh_rate = 60;
		}

		swapchainData = (VulkanSwapchainData*) SDL_GetWindowData(windowHandle, WINDOW_SWAPCHAIN_DATA);

		if (swapchainData == NULL)
		{
			createSwapchainResult = VULKAN_INTERNAL_CreateSwapchain(renderer, windowHandle);

			if (createSwapchainResult == CREATE_SWAPCHAIN_FAIL)
			{
				FNA3D_LogError("Failed to create swapchain for window handle: %p", windowHandle);
			}
			else if (createSwapchainResult == CREATE_SWAPCHAIN_SURFACE_ZERO)
			{
				FNA3D_LogInfo("Surface for window handle: %p is size zero, canceling present", windowHandle);
			}
			else
			{
				swapchainData = (VulkanSwapchainData*) SDL_GetWindowData(windowHandle, WINDOW_SWAPCHAIN_DATA);
				validSwapchainExists = 1;
			}
		}
		else
		{
			validSwapchainExists = 1;
		}

		if (validSwapchainExists)
		{
			/* Begin next frame */
			acquireResult = renderer->vkAcquireNextImageKHR(
				renderer->logicalDevice,
				swapchainData->swapchain,
				10000000000 / mode.refresh_rate, /* ~10 frames, so we'll progress even if throttled to zero. */
				swapchainData->imageAvailableSemaphore,
				VK_NULL_HANDLE,
				&swapchainImageIndex
			);

			if (acquireResult == VK_SUCCESS || acquireResult == VK_SUBOPTIMAL_KHR)
			{
				VULKAN_INTERNAL_SwapChainBlit(
					renderer,
					swapchainData,
					sourceRectangle,
					destinationRectangle,
					swapchainImageIndex
				);

				acquireSuccess = 1;
			}
		}
	}

	commandBufferToSubmit = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
		renderer->commandBuffers
	);

	if (renderer->renderPassInProgress)
	{
		VULKAN_INTERNAL_MaybeEndRenderPass(renderer);
		renderer->needNewRenderPass = 1;
	}

	FNA3D_CommandBuffer_EndRecording(renderer->commandBuffers);

	/* TODO: Figure out how to properly submit commands mid-frame */

	fences[fenceCount] = ((VulkanCommandBuffer*) FNA3D_CommandBuffer_GetDefragBuffer(renderer->commandBuffers))->inFlightFence;
	fenceCount += 1;

	if (validSwapchainExists && swapchainData->fence != VK_NULL_HANDLE)
	{
		fences[fenceCount] = swapchainData->fence;
		fenceCount += 1;
	}

	/* Wait for the defrag to complete */
	result = renderer->vkWaitForFences(
		renderer->logicalDevice,
		fenceCount,
		fences,
		VK_TRUE,
		UINT64_MAX
	);

	/* This may return an error on exit, so just quietly bail if waiting
	 * fails for any reason
	 */
	if (result != VK_SUCCESS)
	{
		FNA3D_LogWarn("vkWaitForFences: %s", VkErrorMessages(result));
		return;
	}

	if (validSwapchainExists)
	{
		swapchainData->fence = VK_NULL_HANDLE;
	}

	VULKAN_INTERNAL_CleanDefrag(renderer);

	/* Check if we can perform any cleanups */
	if (FNA3D_CommandBuffer_PerformCleanups(renderer->commandBuffers))
	{
		FNA3D_Memory_FreeEmptyAllocations(
			renderer->allocator
		);
	}

	renderer->bufferDefragInProgress = 0;

	/* Decide if we will be defragmenting */
	if (renderer->resourceFreed)
	{
		renderer->defragTimer = 0;
	}
	renderer->resourceFreed = 0;

	if (renderer->needDefrag)
	{
		renderer->defragTimer += 1;

		if (renderer->defragTimer > 5)
		{
			performDefrag = 1;
		}
	}

	/* Prepare command submission */
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pNext = NULL;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBufferToSubmit->commandBuffer;
	submitInfo.signalSemaphoreCount = 0;

	if (present && acquireSuccess)
	{
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &swapchainData->imageAvailableSemaphore;
		submitInfo.pWaitDstStageMask = &waitStages;
		semaphores[submitInfo.signalSemaphoreCount] = swapchainData->renderFinishedSemaphore;
		submitInfo.signalSemaphoreCount += 1;
	}
	else
	{
		submitInfo.waitSemaphoreCount = 0;
		submitInfo.pWaitSemaphores = NULL;
		submitInfo.pWaitDstStageMask = NULL;
	}

	if (performDefrag)
	{
		semaphores[submitInfo.signalSemaphoreCount] = renderer->defragSemaphore;
		submitInfo.signalSemaphoreCount += 1;
	}

	submitInfo.pSignalSemaphores = semaphores;

	renderer->vkResetFences(
		renderer->logicalDevice,
		1,
		&commandBufferToSubmit->inFlightFence
	);

	/* Submit the commands, finally. */
	result = renderer->vkQueueSubmit(
		renderer->unifiedQueue,
		1,
		&submitInfo,
		commandBufferToSubmit->inFlightFence
	);
	VULKAN_ERROR_CHECK(result, vkQueueSubmit,)

	if (validSwapchainExists)
	{
		swapchainData->fence = commandBufferToSubmit->inFlightFence;
	}

	FNA3D_CommandBuffer_SubmitCurrent(renderer->commandBuffers);

	/* Rotate the UBOs */
	MOJOSHADER_vkEndFrame(renderer->mojoshaderContext);

	/* Present, if applicable */
	if (present && acquireSuccess)
	{
		if (renderer->supports.GGP_frame_token)
		{
			const void* token = SDL_GetWindowData(
				(SDL_Window*) windowHandle,
				"GgpFrameToken"
			);
			presentInfoGGP.sType = VK_STRUCTURE_TYPE_PRESENT_FRAME_TOKEN_GGP;
			presentInfoGGP.pNext = NULL;
			presentInfoGGP.frameToken = (uint64_t) (size_t) token;
			presentInfo.pNext = &presentInfoGGP;
		}
		else
		{
			presentInfo.pNext = NULL;
		}
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.pNext = NULL;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores =
			&swapchainData->renderFinishedSemaphore;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapchainData->swapchain;
		presentInfo.pImageIndices = &swapchainImageIndex;
		presentInfo.pResults = NULL;

		presentResult = renderer->vkQueuePresentKHR(
			renderer->unifiedQueue,
			&presentInfo
		);
	}

	/* Now that commands are totally done, check if we need new swapchain */
	if (present)
	{
		if (	!validSwapchainExists ||
			acquireResult == VK_ERROR_OUT_OF_DATE_KHR ||
			acquireResult == VK_SUBOPTIMAL_KHR ||
			presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
			presentResult == VK_SUBOPTIMAL_KHR	)
		{
			VULKAN_INTERNAL_RecreateSwapchain(renderer, windowHandle);
		}

		/* This can technically happen anywhere, but this is the nicest
		 * place for it to happen so we try to log it here.
		 */
		if (	acquireResult == VK_ERROR_DEVICE_LOST ||
			presentResult == VK_ERROR_DEVICE_LOST	)
		{
			FNA3D_LogError("Vulkan device was lost!");
		}

		if (!acquireSuccess)
		{
			FNA3D_LogInfo("Failed to acquire swapchain image, not presenting");
		}
	}

	if (performDefrag)
	{
		FNA3D_Memory_Defragment(
			renderer->allocator
		);
	}

	/* Activate the next command buffer */
	FNA3D_CommandBuffer_BeginRecording(renderer->commandBuffers);
}

static void VULKAN_INTERNAL_FlushCommands(VulkanRenderer *renderer, uint8_t sync)
{
	VkResult result;

	SDL_LockMutex(renderer->passLock);
	FNA3D_CommandBuffer_LockForSubmit(renderer->commandBuffers);

	VULKAN_INTERNAL_SubmitCommands(renderer, 0, NULL, NULL, NULL);

	if (sync)
	{
		result = renderer->vkDeviceWaitIdle(renderer->logicalDevice);

		if (result != VK_SUCCESS)
		{
			FNA3D_LogWarn("vkDeviceWaitIdle: %s", VkErrorMessages(result));
		}

		renderer->bufferDefragInProgress = 0;
	}

	SDL_UnlockMutex(renderer->passLock);
	FNA3D_CommandBuffer_UnlockFromSubmit(renderer->commandBuffers);
}

static void VULKAN_INTERNAL_FlushCommandsAndPresent(
	VulkanRenderer *renderer,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
	SDL_LockMutex(renderer->passLock);
	FNA3D_CommandBuffer_LockForSubmit(renderer->commandBuffers);

	VULKAN_INTERNAL_SubmitCommands(
		renderer,
		1,
		sourceRectangle,
		destinationRectangle,
		overrideWindowHandle
	);

	SDL_UnlockMutex(renderer->passLock);
	FNA3D_CommandBuffer_UnlockFromSubmit(renderer->commandBuffers);
}

/* Vulkan: Swapchain */

static CreateSwapchainResult VULKAN_INTERNAL_CreateSwapchain(
	VulkanRenderer *renderer,
	void *windowHandle
) {
	VkResult vulkanResult;
	uint32_t i;
	VulkanSwapchainData *swapchainData;
	VkSwapchainCreateInfoKHR swapchainCreateInfo;
	VkImageViewCreateInfo imageViewCreateInfo;
	VkSemaphoreCreateInfo semaphoreCreateInfo;
	SwapChainSupportDetails swapchainSupportDetails;
	uint8_t swapchainSupport;
	int32_t drawableWidth, drawableHeight;

	swapchainData = SDL_malloc(sizeof(VulkanSwapchainData));
	swapchainData->windowHandle = windowHandle;

	/* Each swapchain must have its own surface. */
	if (!SDL_Vulkan_CreateSurface(
		(SDL_Window*) windowHandle,
		renderer->instance,
		&swapchainData->surface
	)) {
		SDL_free(swapchainData);
		FNA3D_LogError(
			"SDL_Vulkan_CreateSurface failed: %s",
			SDL_GetError()
		);
		return CREATE_SWAPCHAIN_FAIL;
	}

	swapchainSupport = VULKAN_INTERNAL_QuerySwapChainSupport(
		renderer,
		renderer->physicalDevice,
		swapchainData->surface,
		&swapchainSupportDetails
	);

	if (swapchainSupport == 0)
	{
		renderer->vkDestroySurfaceKHR(
			renderer->instance,
			swapchainData->surface,
			NULL
		);
		if (swapchainSupportDetails.formatsLength > 0)
		{
			SDL_free(swapchainSupportDetails.formats);
		}
		if (swapchainSupportDetails.presentModesLength > 0)
		{
			SDL_free(swapchainSupportDetails.presentModes);
		}
		SDL_free(swapchainData);
		FNA3D_LogError("Surface does not support swapchain creation!");
		return CREATE_SWAPCHAIN_FAIL;
	}

	if (	swapchainSupportDetails.capabilities.currentExtent.width == 0 ||
		swapchainSupportDetails.capabilities.currentExtent.height == 0)
	{
		renderer->vkDestroySurfaceKHR(
			renderer->instance,
			swapchainData->surface,
			NULL
		);
		if (swapchainSupportDetails.formatsLength > 0)
		{
			SDL_free(swapchainSupportDetails.formats);
		}
		if (swapchainSupportDetails.presentModesLength > 0)
		{
			SDL_free(swapchainSupportDetails.presentModes);
		}
		SDL_free(swapchainData);
		return CREATE_SWAPCHAIN_SURFACE_ZERO;
	}

	swapchainData->swapchainFormat = XNAToVK_SurfaceFormat[renderer->backbufferFormat];
	swapchainData->swapchainSwizzle.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	swapchainData->swapchainSwizzle.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	swapchainData->swapchainSwizzle.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	swapchainData->swapchainSwizzle.a = VK_COMPONENT_SWIZZLE_IDENTITY;

	if (!VULKAN_INTERNAL_ChooseSwapSurfaceFormat(
		swapchainData->swapchainFormat,
		swapchainSupportDetails.formats,
		swapchainSupportDetails.formatsLength,
		&swapchainData->surfaceFormat
	)) {
		FNA3D_LogWarn("RGBA swapchain unsupported, falling back to BGRA with swizzle");
		if (renderer->backbufferFormat == FNA3D_SURFACEFORMAT_RGBA1010102)
		{
			swapchainData->swapchainFormat = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
		}
		else if (renderer->backbufferFormat == FNA3D_SURFACEFORMAT_COLORSRGB_EXT)
		{
			swapchainData->swapchainFormat = VK_FORMAT_B8G8R8A8_SRGB;
		}
		else if (renderer->backbufferFormat == FNA3D_SURFACEFORMAT_COLOR)
		{
			swapchainData->swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
		}
		else
		{
			FNA3D_LogWarn("Unrecognized swapchain format");
		}
		swapchainData->swapchainSwizzle.r = VK_COMPONENT_SWIZZLE_B;
		swapchainData->swapchainSwizzle.g = VK_COMPONENT_SWIZZLE_G;
		swapchainData->swapchainSwizzle.b = VK_COMPONENT_SWIZZLE_R;
		swapchainData->swapchainSwizzle.a = VK_COMPONENT_SWIZZLE_A;

		if (!VULKAN_INTERNAL_ChooseSwapSurfaceFormat(
			swapchainData->swapchainFormat,
			swapchainSupportDetails.formats,
			swapchainSupportDetails.formatsLength,
			&swapchainData->surfaceFormat
		)) {
			renderer->vkDestroySurfaceKHR(
				renderer->instance,
				swapchainData->surface,
				NULL
			);
			if (swapchainSupportDetails.formatsLength > 0)
			{
				SDL_free(swapchainSupportDetails.formats);
			}
			if (swapchainSupportDetails.presentModesLength > 0)
			{
				SDL_free(swapchainSupportDetails.presentModes);
			}
			SDL_free(swapchainData);
			FNA3D_LogError("Device does not support swap chain format");
			return CREATE_SWAPCHAIN_FAIL;
		}
	}

	if (!VULKAN_INTERNAL_ChooseSwapPresentMode(
		renderer->presentInterval,
		swapchainSupportDetails.presentModes,
		swapchainSupportDetails.presentModesLength,
		&swapchainData->presentMode
	)) {
		renderer->vkDestroySurfaceKHR(
			renderer->instance,
			swapchainData->surface,
			NULL
		);
		if (swapchainSupportDetails.formatsLength > 0)
		{
			SDL_free(swapchainSupportDetails.formats);
		}
		if (swapchainSupportDetails.presentModesLength > 0)
		{
			SDL_free(swapchainSupportDetails.presentModes);
		}
		SDL_free(swapchainData);
		FNA3D_LogError("Device does not support swap chain present mode");
		return CREATE_SWAPCHAIN_FAIL;
	}

	SDL_GetWindowSizeInPixels(
		(SDL_Window*) windowHandle,
		&drawableWidth,
		&drawableHeight
	);

	if (	drawableWidth < swapchainSupportDetails.capabilities.minImageExtent.width ||
		drawableWidth > swapchainSupportDetails.capabilities.maxImageExtent.width ||
		drawableHeight < swapchainSupportDetails.capabilities.minImageExtent.height ||
		drawableHeight > swapchainSupportDetails.capabilities.maxImageExtent.height	)
	{
		FNA3D_LogWarn("Drawable size not possible for this VkSurface!");

		if (swapchainSupportDetails.capabilities.currentExtent.width != UINT32_MAX)
		{
			FNA3D_LogWarn("Falling back to an acceptable swapchain extent.");
			drawableWidth = VULKAN_INTERNAL_clamp(
				drawableWidth,
				swapchainSupportDetails.capabilities.minImageExtent.width,
				swapchainSupportDetails.capabilities.maxImageExtent.width
			);
			drawableHeight = VULKAN_INTERNAL_clamp(
				drawableHeight,
				swapchainSupportDetails.capabilities.minImageExtent.height,
				swapchainSupportDetails.capabilities.maxImageExtent.height
			);
		}
		else
		{
			renderer->vkDestroySurfaceKHR(
				renderer->instance,
				swapchainData->surface,
				NULL
			);
			if (swapchainSupportDetails.formatsLength > 0)
			{
				SDL_free(swapchainSupportDetails.formats);
			}
			if (swapchainSupportDetails.presentModesLength > 0)
			{
				SDL_free(swapchainSupportDetails.presentModes);
			}
			SDL_free(swapchainData);
			FNA3D_LogError("No fallback swapchain size available!");
			return CREATE_SWAPCHAIN_FAIL;
		}
	}

	swapchainData->extent.width = drawableWidth;
	swapchainData->extent.height = drawableHeight;

	swapchainData->imageCount = swapchainSupportDetails.capabilities.minImageCount + 1;

	if (	swapchainSupportDetails.capabilities.maxImageCount > 0 &&
		swapchainData->imageCount > swapchainSupportDetails.capabilities.maxImageCount	)
	{
		swapchainData->imageCount = swapchainSupportDetails.capabilities.maxImageCount;
	}

	if (swapchainData->presentMode == VK_PRESENT_MODE_MAILBOX_KHR)
	{
		/* Required for proper triple-buffering.
		 * Note that this is below the above maxImageCount check!
		 * If the driver advertises MAILBOX but does not support 3 swap
		 * images, it's not real mailbox support, so let it fail hard.
		 * -flibit
		 */
		swapchainData->imageCount = SDL_max(swapchainData->imageCount, 3);
	}

	swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainCreateInfo.pNext = NULL;
	swapchainCreateInfo.flags = 0;
	swapchainCreateInfo.surface = swapchainData->surface;
	swapchainCreateInfo.minImageCount = swapchainData->imageCount;
	swapchainCreateInfo.imageFormat = swapchainData->surfaceFormat.format;
	swapchainCreateInfo.imageColorSpace = swapchainData->surfaceFormat.colorSpace;
	swapchainCreateInfo.imageExtent = swapchainData->extent;
	swapchainCreateInfo.imageArrayLayers = 1;
	swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainCreateInfo.queueFamilyIndexCount = 0;
	swapchainCreateInfo.pQueueFamilyIndices = NULL;
	swapchainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainCreateInfo.presentMode = swapchainData->presentMode;
	swapchainCreateInfo.clipped = VK_TRUE;
	swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

	vulkanResult = renderer->vkCreateSwapchainKHR(
		renderer->logicalDevice,
		&swapchainCreateInfo,
		NULL,
		&swapchainData->swapchain
	);

	VULKAN_ERROR_CHECK(vulkanResult, vkCreateSwapchainKHR, CREATE_SWAPCHAIN_FAIL)

	renderer->vkGetSwapchainImagesKHR(
		renderer->logicalDevice,
		swapchainData->swapchain,
		&swapchainData->imageCount,
		NULL
	);

	swapchainData->images = (VkImage*) SDL_malloc(
		sizeof(VkImage) * swapchainData->imageCount
	);
	if (!swapchainData->images)
	{
		SDL_OutOfMemory();
		renderer->vkDestroySurfaceKHR(
			renderer->instance,
			swapchainData->surface,
			NULL
		);
		if (swapchainSupportDetails.formatsLength > 0)
		{
			SDL_free(swapchainSupportDetails.formats);
		}
		if (swapchainSupportDetails.presentModesLength > 0)
		{
			SDL_free(swapchainSupportDetails.presentModes);
		}
		SDL_free(swapchainData);
		return CREATE_SWAPCHAIN_FAIL;
	}

	swapchainData->views = (VkImageView*) SDL_malloc(
		sizeof(VkImageView) * swapchainData->imageCount
	);
	if (!swapchainData->views)
	{
		SDL_OutOfMemory();
		renderer->vkDestroySurfaceKHR(
			renderer->instance,
			swapchainData->surface,
			NULL
		);
		if (swapchainSupportDetails.formatsLength > 0)
		{
			SDL_free(swapchainSupportDetails.formats);
		}
		if (swapchainSupportDetails.presentModesLength > 0)
		{
			SDL_free(swapchainSupportDetails.presentModes);
		}
		SDL_free(swapchainData);
		return CREATE_SWAPCHAIN_FAIL;
	}

	swapchainData->resourceAccessTypes = (VulkanResourceAccessType*) SDL_malloc(
		sizeof(VulkanResourceAccessType) * swapchainData->imageCount
	);
	if (!swapchainData->resourceAccessTypes)
	{
		SDL_OutOfMemory();
		renderer->vkDestroySurfaceKHR(
			renderer->instance,
			swapchainData->surface,
			NULL
		);
		if (swapchainSupportDetails.formatsLength > 0)
		{
			SDL_free(swapchainSupportDetails.formats);
		}
		if (swapchainSupportDetails.presentModesLength > 0)
		{
			SDL_free(swapchainSupportDetails.presentModes);
		}
		SDL_free(swapchainData);
		return CREATE_SWAPCHAIN_FAIL;
	}

	renderer->vkGetSwapchainImagesKHR(
		renderer->logicalDevice,
		swapchainData->swapchain,
		&swapchainData->imageCount,
		swapchainData->images
	);

	imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewCreateInfo.pNext = NULL;
	imageViewCreateInfo.flags = 0;
	imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imageViewCreateInfo.format = swapchainData->surfaceFormat.format;
	imageViewCreateInfo.components = swapchainData->swapchainSwizzle;
	imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
	imageViewCreateInfo.subresourceRange.levelCount = 1;
	imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
	imageViewCreateInfo.subresourceRange.layerCount = 1;
	for (i = 0; i < swapchainData->imageCount; i += 1)
	{
		imageViewCreateInfo.image = swapchainData->images[i];

		vulkanResult = renderer->vkCreateImageView(
			renderer->logicalDevice,
			&imageViewCreateInfo,
			NULL,
			&swapchainData->views[i]
		);

		if (vulkanResult != VK_SUCCESS)
		{
			renderer->vkDestroySurfaceKHR(
				renderer->instance,
				swapchainData->surface,
				NULL
			);
			if (swapchainSupportDetails.formatsLength > 0)
			{
				SDL_free(swapchainSupportDetails.formats);
			}
			if (swapchainSupportDetails.presentModesLength > 0)
			{
				SDL_free(swapchainSupportDetails.presentModes);
			}
			SDL_free(swapchainData);
			FNA3D_LogError("vkCreateImageView: %s", VkErrorMessages(vulkanResult));
			return CREATE_SWAPCHAIN_FAIL;
		}

		swapchainData->resourceAccessTypes[i] = RESOURCE_ACCESS_NONE;
	}

	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = NULL;
	semaphoreCreateInfo.flags = 0;

	renderer->vkCreateSemaphore(
		renderer->logicalDevice,
		&semaphoreCreateInfo,
		NULL,
		&swapchainData->imageAvailableSemaphore
	);

	renderer->vkCreateSemaphore(
		renderer->logicalDevice,
		&semaphoreCreateInfo,
		NULL,
		&swapchainData->renderFinishedSemaphore
	);

	swapchainData->fence = VK_NULL_HANDLE;

	SDL_SetWindowData(windowHandle, WINDOW_SWAPCHAIN_DATA, swapchainData);

	if (renderer->swapchainDataCount >= renderer->swapchainDataCapacity)
	{
		renderer->swapchainDataCapacity *= 2;
		renderer->swapchainDatas = SDL_realloc(
			renderer->swapchainDatas,
			renderer->swapchainDataCapacity * sizeof(VulkanSwapchainData*)
		);
	}
	renderer->swapchainDatas[renderer->swapchainDataCount] = swapchainData;
	renderer->swapchainDataCount += 1;

	if (swapchainSupportDetails.formatsLength > 0)
	{
		SDL_free(swapchainSupportDetails.formats);
	}
	if (swapchainSupportDetails.presentModesLength > 0)
	{
		SDL_free(swapchainSupportDetails.presentModes);
	}

	return CREATE_SWAPCHAIN_SUCCESS;
}

static void VULKAN_INTERNAL_DestroySwapchain(
	VulkanRenderer *renderer,
	void *windowHandle
) {
	uint32_t i;
	VulkanSwapchainData *swapchainData;

	swapchainData = (VulkanSwapchainData*) SDL_GetWindowData(windowHandle, WINDOW_SWAPCHAIN_DATA);

	if (swapchainData == NULL)
	{
		return;
	}

	for (i = 0; i < renderer->framebufferArray.count; i += 1)
	{
		renderer->vkDestroyFramebuffer(
			renderer->logicalDevice,
			renderer->framebufferArray.elements[i].value,
			NULL
		);
	}
	SDL_free(renderer->framebufferArray.elements);
	renderer->framebufferArray.elements = NULL;
	renderer->framebufferArray.count = 0;
	renderer->framebufferArray.capacity = 0;

	for (i = 0; i < swapchainData->imageCount; i += 1)
	{
		renderer->vkDestroyImageView(
			renderer->logicalDevice,
			swapchainData->views[i],
			NULL
		);
	}

	SDL_free(swapchainData->images);
	SDL_free(swapchainData->views);
	SDL_free(swapchainData->resourceAccessTypes);

	renderer->vkDestroySwapchainKHR(
		renderer->logicalDevice,
		swapchainData->swapchain,
		NULL
	);

	renderer->vkDestroySurfaceKHR(
		renderer->instance,
		swapchainData->surface,
		NULL
	);

	renderer->vkDestroySemaphore(
		renderer->logicalDevice,
		swapchainData->imageAvailableSemaphore,
		NULL
	);

	renderer->vkDestroySemaphore(
		renderer->logicalDevice,
		swapchainData->renderFinishedSemaphore,
		NULL
	);

	for (i = 0; i < renderer->swapchainDataCount; i += 1)
	{
		if (windowHandle == renderer->swapchainDatas[i]->windowHandle)
		{
			renderer->swapchainDatas[i] = renderer->swapchainDatas[renderer->swapchainDataCount - 1];
			renderer->swapchainDataCount -= 1;
			break;
		}
	}

	SDL_SetWindowData(windowHandle, WINDOW_SWAPCHAIN_DATA, NULL);
	SDL_free(swapchainData);
}

static void VULKAN_INTERNAL_RecreateSwapchain(
	VulkanRenderer *renderer,
	void *windowHandle
) {
	CreateSwapchainResult createSwapchainResult;

	renderer->vkDeviceWaitIdle(renderer->logicalDevice);

	VULKAN_INTERNAL_DestroySwapchain(renderer, windowHandle);
	createSwapchainResult = VULKAN_INTERNAL_CreateSwapchain(renderer, windowHandle);

	if (createSwapchainResult == CREATE_SWAPCHAIN_FAIL)
	{
		return;
	}

	renderer->vkDeviceWaitIdle(renderer->logicalDevice);
}

/* Vulkan: Buffer Objects */

/* This function is EXTREMELY sensitive. Change this at your own peril. -cosmonaut */
static void VULKAN_INTERNAL_SetBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	FNA3D_BufferContainer *container = (FNA3D_BufferContainer*) buffer;
	VulkanCommandBuffer *commandBuffer;
	VulkanBuffer *vulkanBuffer;
	VulkanBuffer *transferBuffer;
	VkDeviceSize transferOffset;
	VkBufferCopy bufferCopy;
	VulkanResourceAccessType accessType;

	vulkanBuffer = (VulkanBuffer*) FNA3D_Memory_GetActiveBuffer(container);
	accessType = vulkanBuffer->resourceAccessType;

	if (	options == FNA3D_SETDATAOPTIONS_NONE &&
		dataLength == vulkanBuffer->size	)
	{
		/* Setting the whole buffer is basically just discard */
		options = FNA3D_SETDATAOPTIONS_DISCARD;
	}

	if (options == FNA3D_SETDATAOPTIONS_NONE)
	{
		/* If NONE is set, we need to do a buffered copy.
		 * The barriers will synchronize on the GPU so the data isn't overwritten
		 * before it needs to be used.
		 */

		VULKAN_INTERNAL_MaybeEndRenderPass(renderer);

		SDL_LockMutex(renderer->passLock);

		FNA3D_CommandBuffer_LockForTransfer(renderer->commandBuffers);

		VULKAN_INTERNAL_CopyToTransferBuffer(
			renderer,
			data,
			dataLength,
			dataLength,
			&transferBuffer,
			&transferOffset,
			renderer->physicalDeviceProperties.properties.limits.optimalBufferCopyOffsetAlignment
		);

		VULKAN_INTERNAL_BufferMemoryBarrier(
			renderer,
			RESOURCE_ACCESS_TRANSFER_READ,
			transferBuffer->buffer,
			&transferBuffer->resourceAccessType
		);

		VULKAN_INTERNAL_BufferMemoryBarrier(
			renderer,
			RESOURCE_ACCESS_TRANSFER_WRITE,
			vulkanBuffer->buffer,
			&vulkanBuffer->resourceAccessType
		);

		bufferCopy.srcOffset = transferOffset;
		bufferCopy.dstOffset = offsetInBytes;
		bufferCopy.size = (VkDeviceSize)dataLength;

		commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
			renderer->commandBuffers
		);
		RECORD_CMD(renderer->vkCmdCopyBuffer(
			commandBuffer->commandBuffer,
			transferBuffer->buffer,
			vulkanBuffer->buffer,
			1,
			&bufferCopy
		));

		VULKAN_INTERNAL_BufferMemoryBarrier(
			renderer,
			accessType,
			vulkanBuffer->buffer,
			&vulkanBuffer->resourceAccessType
		);

		FNA3D_CommandBuffer_UnlockFromTransfer(renderer->commandBuffers);
		SDL_UnlockMutex(renderer->passLock);
	}
	else
	{
		if (options == FNA3D_SETDATAOPTIONS_DISCARD && SDL_AtomicGet(&vulkanBuffer->refcount) > 0)
		{
			/* If DISCARD is set and the buffer was bound,
			 * we have to replace the buffer pointer.
			 */

			vulkanBuffer = (VulkanBuffer*) FNA3D_Memory_DiscardActiveBuffer(
				renderer->allocator,
				container
			);
		}

		/* If this is a defrag frame and NoOverwrite is set, wait for defrag to avoid data race */
		if (options == FNA3D_SETDATAOPTIONS_NOOVERWRITE && renderer->bufferDefragInProgress)
		{
			commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetDefragBuffer(
				renderer->commandBuffers
			);
			renderer->vkWaitForFences(
				renderer->logicalDevice,
				1,
				&commandBuffer->inFlightFence,
				VK_TRUE,
				UINT64_MAX
			);

			renderer->bufferDefragInProgress = 0;
		}

		SDL_memcpy(
			FNA3D_Memory_GetHostPointer(
				vulkanBuffer->usedRegion,
				offsetInBytes
			),
			data,
			dataLength
		);
	}
}

/* Vulkan: Texture Objects */

static uint8_t VULKAN_INTERNAL_CreateTexture(
	VulkanRenderer *renderer,
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	uint8_t isCube,
	uint8_t isRenderTarget,
	VkSampleCountFlagBits samples,
	uint32_t levelCount,
	VkFormat format,
	VkComponentMapping swizzle,
	VkImageAspectFlags aspectMask,
	VkImageType imageType,
	VkImageUsageFlags usage,
	VulkanTexture *texture
) {
	VkResult result;
	uint8_t bindResult = 0;
	VkImageCreateInfo imageCreateInfo;
	VkImageViewCreateInfo imageViewCreateInfo;
	uint8_t layerCount = isCube ? 6 : 1;
	uint32_t i;

	imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCreateInfo.pNext = NULL;
	imageCreateInfo.flags = isCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
	imageCreateInfo.imageType = imageType;
	imageCreateInfo.format = format;
	imageCreateInfo.extent.width = width;
	imageCreateInfo.extent.height = height;
	imageCreateInfo.extent.depth = depth;
	imageCreateInfo.mipLevels = levelCount;
	imageCreateInfo.arrayLayers = layerCount;
	imageCreateInfo.samples = samples;
	imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCreateInfo.usage = usage;
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCreateInfo.queueFamilyIndexCount = 0;
	imageCreateInfo.pQueueFamilyIndices = NULL;
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	result = renderer->vkCreateImage(
		renderer->logicalDevice,
		&imageCreateInfo,
		NULL,
		&texture->image
	);
	VULKAN_ERROR_CHECK(result, vkCreateImage, 0)

	texture->isRenderTarget = isRenderTarget;
	texture->imageCreateInfo = imageCreateInfo;

	bindResult = VULKAN_INTERNAL_BindMemoryForImage(
		renderer,
		texture->image,
		texture,
		isRenderTarget,
		&texture->usedRegion
	);

	/* Binding failed, bail out! */
	if (bindResult != 1)
	{
		renderer->vkDestroyImage(
			renderer->logicalDevice,
			texture->image,
			NULL
		);

		return bindResult;
	}

	imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewCreateInfo.pNext = NULL;
	imageViewCreateInfo.flags = 0;
	imageViewCreateInfo.image = texture->image;
	imageViewCreateInfo.format = format;
	imageViewCreateInfo.components = swizzle;
	imageViewCreateInfo.subresourceRange.aspectMask = aspectMask;
	imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
	imageViewCreateInfo.subresourceRange.levelCount = levelCount;
	imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
	imageViewCreateInfo.subresourceRange.layerCount = layerCount;

	if (isCube)
	{
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
	}
	else if (imageType == VK_IMAGE_TYPE_2D)
	{
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	}
	else if (imageType == VK_IMAGE_TYPE_3D)
	{
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
	}
	else
	{
		FNA3D_LogError("Invalid image type: %u", imageType);
	}

	result = renderer->vkCreateImageView(
		renderer->logicalDevice,
		&imageViewCreateInfo,
		NULL,
		&texture->view
	);
	VULKAN_ERROR_CHECK(result, vkCreateImageView, 0)

	texture->viewCreateInfo = imageViewCreateInfo;

	/* FIXME: Reduce this memset to the minimum necessary for init! */
	SDL_memset(texture->rtViews, '\0', sizeof(texture->rtViews));
	if (isRenderTarget)
	{
		if (!isCube)
		{
			/* Framebuffer views don't like swizzling */
			imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

			/* The view should only contain one level */
			imageViewCreateInfo.subresourceRange.levelCount = 1;

			result = renderer->vkCreateImageView(
				renderer->logicalDevice,
				&imageViewCreateInfo,
				NULL,
				&texture->rtViews[0]
			);
			VULKAN_ERROR_CHECK(result, vkCreateImageView, 0)
		}
		else
		{
			/* Create a framebuffer-compatible view for each layer */
			for (i = 0; i < layerCount; i += 1)
			{
				imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
				imageViewCreateInfo.subresourceRange.levelCount = 1;
				imageViewCreateInfo.subresourceRange.layerCount = 1;
				imageViewCreateInfo.subresourceRange.baseArrayLayer = i;
				imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
				imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
				imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
				imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

				result = renderer->vkCreateImageView(
					renderer->logicalDevice,
					&imageViewCreateInfo,
					NULL,
					&texture->rtViews[i]
				);
				VULKAN_ERROR_CHECK(result, vkCreateImageView, 0)
			}
		}
	}

	texture->dimensions.width = width;
	texture->dimensions.height = height;
	texture->depth = depth;
	texture->surfaceFormat = format;
	texture->levelCount = levelCount;
	texture->layerCount = layerCount;
	texture->resourceAccessType = RESOURCE_ACCESS_NONE;
	texture->external = 0;

	return 1;
}

static void VULKAN_INTERNAL_GetTextureData(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	int32_t level,
	int32_t layer,
	void* data,
	int32_t dataLength
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *vulkanTexture = (VulkanTexture*) texture;
	VulkanCommandBuffer *commandBuffer;
	FNA3D_TransferBuffer *transferBuffer;
	VulkanResourceAccessType prevResourceAccess;
	VkBufferImageCopy imageCopy;
	uint8_t *dataPtr = (uint8_t*) data;
	uint8_t *transferBufferPointer;

	VULKAN_INTERNAL_MaybeEndRenderPass(renderer);

	SDL_LockMutex(renderer->passLock);
	FNA3D_CommandBuffer_LockForTransfer(renderer->commandBuffers);

	VULKAN_INTERNAL_PrepareCopyFromTransferBuffer(
		renderer,
		dataLength,
		/* The Vulkan spec states: bufferOffset must be a multiple of 4
		 * https://vulkan.lunarg.com/doc/view/1.2.141.0/windows/1.2-extensions/vkspec.html#VUID-VkBufferImageCopy-bufferOffset-00194
		 */
		(VkDeviceSize)SDL_max(Texture_GetFormatSize(vulkanTexture->colorFormat), 4),
		&transferBuffer,
		(void**) &transferBufferPointer
	);

	/* Cache this so we can restore it later */
	prevResourceAccess = vulkanTexture->resourceAccessType;

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_READ,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		vulkanTexture->layerCount,
		0,
		vulkanTexture->levelCount,
		0,
		vulkanTexture->image,
		&vulkanTexture->resourceAccessType
	);

	/* Save texture data to transfer buffer */

	imageCopy.imageExtent.width = w;
	imageCopy.imageExtent.height = h;
	imageCopy.imageExtent.depth = 1;
	imageCopy.imageOffset.x = x;
	imageCopy.imageOffset.y = y;
	imageCopy.imageOffset.z = 0;
	imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageCopy.imageSubresource.baseArrayLayer = layer;
	imageCopy.imageSubresource.layerCount = 1;
	imageCopy.imageSubresource.mipLevel = level;
	imageCopy.bufferOffset = transferBuffer->offset;
	imageCopy.bufferRowLength = w;
	imageCopy.bufferImageHeight = h;

	commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
		renderer->commandBuffers
	);
	RECORD_CMD(renderer->vkCmdCopyImageToBuffer(
		commandBuffer->commandBuffer,
		vulkanTexture->image,
		AccessMap[vulkanTexture->resourceAccessType].imageLayout,
		((VulkanBuffer*) transferBuffer->buffer)->buffer,
		1,
		&imageCopy
	));

	/* Restore the image layout and wait for completion of the render pass */

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		prevResourceAccess,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		vulkanTexture->layerCount,
		0,
		vulkanTexture->levelCount,
		0,
		vulkanTexture->image,
		&vulkanTexture->resourceAccessType
	);

	VULKAN_INTERNAL_FlushCommands(renderer, 1);

	/* Read from transfer buffer */

	SDL_memcpy(
		dataPtr,
		transferBufferPointer,
		BytesPerImage(w, h, vulkanTexture->colorFormat)
	);

	FNA3D_CommandBuffer_UnlockFromTransfer(renderer->commandBuffers);
	SDL_UnlockMutex(renderer->passLock);
}

/* Vulkan: Mutable State Commands */

static void VULKAN_INTERNAL_SetViewportCommand(VulkanRenderer *renderer)
{
	VulkanCommandBuffer *commandBuffer;
	VkViewport vulkanViewport;

	/* Flipping the viewport for compatibility with D3D */
	vulkanViewport.x = (float) renderer->viewport.x;
	vulkanViewport.width = (float) renderer->viewport.w;
	vulkanViewport.minDepth = renderer->viewport.minDepth;
	vulkanViewport.maxDepth = renderer->viewport.maxDepth;
#ifdef __APPLE__
	/* For MoltenVK we just disable viewport flipping, bypassing the Vulkan spec */
	vulkanViewport.y = (float) renderer->viewport.y;
	vulkanViewport.height = (float) renderer->viewport.h;
#else
	/* For everyone else, we have KHR_maintenance1 to do the flipping for us */
	vulkanViewport.y = (float) (renderer->viewport.y + renderer->viewport.h);
	vulkanViewport.height = (float) -renderer->viewport.h;
#endif

	commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
		renderer->commandBuffers
	);
	RECORD_CMD(renderer->vkCmdSetViewport(
		commandBuffer->commandBuffer,
		0,
		1,
		&vulkanViewport
	));
}

static void VULKAN_INTERNAL_SetScissorRectCommand(VulkanRenderer *renderer)
{
	VulkanCommandBuffer *commandBuffer;
	VkOffset2D offset;
	VkExtent2D extent;
	VkRect2D vulkanScissorRect;

	if (renderer->renderPassInProgress)
	{
		if (!renderer->rasterizerState.scissorTestEnable)
		{
			offset.x = 0;
			offset.y = 0;
			extent = renderer->colorAttachments[0]->dimensions;
		}
		else
		{
			offset.x = renderer->scissorRect.x;
			offset.y = renderer->scissorRect.y;
			extent.width = renderer->scissorRect.w;
			extent.height = renderer->scissorRect.h;
		}

		vulkanScissorRect.offset = offset;
		vulkanScissorRect.extent = extent;

		commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
			renderer->commandBuffers
		);
		RECORD_CMD(renderer->vkCmdSetScissor(
			commandBuffer->commandBuffer,
			0,
			1,
			&vulkanScissorRect
		));
	}
}

static void VULKAN_INTERNAL_SetStencilReferenceValueCommand(
	VulkanRenderer *renderer
) {
	VulkanCommandBuffer *commandBuffer;
	if (renderer->renderPassInProgress)
	{
		commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
			renderer->commandBuffers
		);
		RECORD_CMD(renderer->vkCmdSetStencilReference(
			commandBuffer->commandBuffer,
			VK_STENCIL_FRONT_AND_BACK,
			renderer->stencilRef
		));
	}
}

static void VULKAN_INTERNAL_SetDepthBiasCommand(VulkanRenderer *renderer)
{
	VulkanCommandBuffer *commandBuffer;
	if (renderer->renderPassInProgress)
	{
		commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
			renderer->commandBuffers
		);
		RECORD_CMD(renderer->vkCmdSetDepthBias(
			commandBuffer->commandBuffer,
			renderer->rasterizerState.depthBias,
			0.0f, /* no clamp */
			renderer->rasterizerState.slopeScaleDepthBias
		));
	}
}

/* Vulkan: Pipeline State Objects */

static VkPipelineLayout VULKAN_INTERNAL_FetchPipelineLayout(
	VulkanRenderer *renderer,
	MOJOSHADER_vkShader *vertShader,
	MOJOSHADER_vkShader *fragShader
) {
	VkDescriptorSetLayout setLayouts[4];

	PipelineLayoutHash pipelineLayoutHash;
	VkPipelineLayoutCreateInfo layoutCreateInfo;
	VkPipelineLayout layout;
	VkResult vulkanResult;

	pipelineLayoutHash.vertexSamplerLayout = VULKAN_INTERNAL_FetchSamplerDescriptorSetLayout(renderer, vertShader, VK_SHADER_STAGE_VERTEX_BIT);
	pipelineLayoutHash.fragSamplerLayout = VULKAN_INTERNAL_FetchSamplerDescriptorSetLayout(renderer, fragShader, VK_SHADER_STAGE_FRAGMENT_BIT);
	pipelineLayoutHash.vertexUniformLayout = renderer->vertexUniformBufferDescriptorSetLayout;
	pipelineLayoutHash.fragUniformLayout = renderer->fragUniformBufferDescriptorSetLayout;

	layout = PipelineLayoutHashArray_Fetch(
		&renderer->pipelineLayoutTable,
		pipelineLayoutHash
	);

	if (layout != VK_NULL_HANDLE)
	{
		return layout;
	}

	setLayouts[0] = pipelineLayoutHash.vertexSamplerLayout;
	setLayouts[1] = pipelineLayoutHash.fragSamplerLayout;
	setLayouts[2] = renderer->vertexUniformBufferDescriptorSetLayout;
	setLayouts[3] = renderer->fragUniformBufferDescriptorSetLayout;

	layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutCreateInfo.pNext = NULL;
	layoutCreateInfo.flags = 0;
	layoutCreateInfo.setLayoutCount = 4;
	layoutCreateInfo.pSetLayouts = setLayouts;
	layoutCreateInfo.pushConstantRangeCount = 0;
	layoutCreateInfo.pPushConstantRanges = NULL;

	vulkanResult = renderer->vkCreatePipelineLayout(
		renderer->logicalDevice,
		&layoutCreateInfo,
		NULL,
		&layout
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkCreatePipelineLayout, VK_NULL_HANDLE)

	PipelineLayoutHashArray_Insert(
		&renderer->pipelineLayoutTable,
		pipelineLayoutHash,
		layout
	);
	return layout;
}

static void VULKAN_INTERNAL_GenerateVertexInputInfo(
	VulkanRenderer *renderer,
	VkVertexInputBindingDescription *bindingDescriptions,
	VkVertexInputAttributeDescription* attributeDescriptions,
	uint32_t *attributeDescriptionCount,
	VkVertexInputBindingDivisorDescriptionEXT *divisorDescriptions,
	uint32_t *divisorDescriptionCount
) {
	MOJOSHADER_vkShader *vertexShader, *blah;
	uint8_t attrUse[MOJOSHADER_USAGE_TOTAL][16];
	uint32_t attributeDescriptionCounter = 0;
	uint32_t divisorDescriptionCounter = 0;
	int32_t i, j, k;
	FNA3D_VertexDeclaration vertexDeclaration;
	FNA3D_VertexElement element;
	FNA3D_VertexElementUsage usage;
	int32_t index, attribLoc;
	VkVertexInputAttributeDescription vInputAttribDescription;
	VkVertexInputBindingDescription vertexInputBindingDescription;
	VkVertexInputBindingDivisorDescriptionEXT divisorDescription;

	MOJOSHADER_vkGetBoundShaders(renderer->mojoshaderContext, &vertexShader, &blah);

	SDL_memset(attrUse, '\0', sizeof(attrUse));
	for (i = 0; i < (int32_t) renderer->numVertexBindings; i += 1)
	{
		vertexDeclaration =
			renderer->vertexBindings[i].vertexDeclaration;

		for (j = 0; j < vertexDeclaration.elementCount; j += 1)
		{
			element = vertexDeclaration.elements[j];
			usage = element.vertexElementUsage;
			index = element.usageIndex;

			if (attrUse[usage][index])
			{
				index = -1;

				for (k = 0; k < MAX_VERTEX_ATTRIBUTES; k += 1)
				{
					if (!attrUse[usage][k])
					{
						index = k;
						break;
					}
				}

				if (index < 0)
				{
					FNA3D_LogError(
						"Vertex usage collision!"
					);
				}
			}

			attrUse[usage][index] = 1;

			attribLoc = MOJOSHADER_vkGetVertexAttribLocation(
				vertexShader,
				VertexAttribUsage(usage),
				index
			);

			if (attribLoc == -1)
			{
				/* Stream not in use! */
				continue;
			}

			vInputAttribDescription.location = attribLoc;
			vInputAttribDescription.format = XNAToVK_VertexAttribType[
				element.vertexElementFormat
			];
			vInputAttribDescription.offset = element.offset;
			vInputAttribDescription.binding = i;

			attributeDescriptions[attributeDescriptionCounter] =
				vInputAttribDescription;
			attributeDescriptionCounter += 1;
		}

		vertexInputBindingDescription.binding = i;
		vertexInputBindingDescription.stride =
			vertexDeclaration.vertexStride;

		if (renderer->vertexBindings[i].instanceFrequency > 0)
		{
			vertexInputBindingDescription.inputRate =
				VK_VERTEX_INPUT_RATE_INSTANCE;

			divisorDescription.binding = i;
			divisorDescription.divisor =
				renderer->vertexBindings[i].instanceFrequency;

			divisorDescriptions[divisorDescriptionCounter] =
				divisorDescription;
			divisorDescriptionCounter += 1;
		}
		else
		{
			vertexInputBindingDescription.inputRate =
				VK_VERTEX_INPUT_RATE_VERTEX;
		}

		bindingDescriptions[i] = vertexInputBindingDescription;
	}

	*attributeDescriptionCount = attributeDescriptionCounter;
	*divisorDescriptionCount = divisorDescriptionCounter;
}

static VkPipeline VULKAN_INTERNAL_FetchPipeline(VulkanRenderer *renderer)
{
	VkResult vulkanResult;
	VkPipeline pipeline;
	VkPipelineViewportStateCreateInfo viewportStateInfo;
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
	VkVertexInputBindingDescription *bindingDescriptions;
	VkVertexInputAttributeDescription *attributeDescriptions;
	uint32_t attributeDescriptionCount;
	VkVertexInputBindingDivisorDescriptionEXT *divisorDescriptions;
	uint32_t divisorDescriptionCount;
	VkPipelineVertexInputStateCreateInfo vertexInputInfo;
	VkPipelineVertexInputDivisorStateCreateInfoEXT divisorStateInfo;
	VkPipelineRasterizationStateCreateInfo rasterizerInfo;
	VkPipelineMultisampleStateCreateInfo multisamplingInfo;
	VkPipelineColorBlendAttachmentState colorBlendAttachments[MAX_RENDERTARGET_BINDINGS];
	VkPipelineColorBlendStateCreateInfo colorBlendStateInfo;
	VkStencilOpState frontStencilState, backStencilState;
	VkPipelineDepthStencilStateCreateInfo depthStencilStateInfo;
	static const VkDynamicState dynamicStates[] =
	{
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_BLEND_CONSTANTS,
		VK_DYNAMIC_STATE_STENCIL_REFERENCE,
		VK_DYNAMIC_STATE_DEPTH_BIAS
	};
	VkPipelineDynamicStateCreateInfo dynamicStateInfo;
	MOJOSHADER_vkShader *vertShader, *fragShader;
	VkPipelineShaderStageCreateInfo stageInfos[2];
	VkGraphicsPipelineCreateInfo pipelineCreateInfo;

	PipelineHash hash;
	hash.blendState = GetPackedBlendState(renderer->blendState);
	hash.rasterizerState = GetPackedRasterizerState(
		renderer->rasterizerState,
		renderer->rasterizerState.depthBias * XNAToVK_DepthBiasScale(
			XNAToVK_DepthFormat(
				renderer,
				renderer->currentDepthFormat
			)
		)
	);
	hash.depthStencilState = GetPackedDepthStencilState(
		renderer->depthStencilState
	);
	hash.vertexBufferBindingsIndex = renderer->currentVertexBufferBindingsIndex;
	hash.primitiveType = renderer->currentPrimitiveType;
	hash.sampleMask = renderer->multiSampleMask[0];
	MOJOSHADER_vkGetBoundShaders(renderer->mojoshaderContext, &vertShader, &fragShader);
	hash.vertShader = vertShader;
	hash.fragShader = fragShader;
	hash.renderPass = renderer->renderPass;

	renderer->currentPipelineLayout = VULKAN_INTERNAL_FetchPipelineLayout(
		renderer,
		vertShader,
		fragShader
	);

	pipeline = PipelineHashTable_Fetch(&renderer->pipelineHashTable, hash);
	if (pipeline != VK_NULL_HANDLE)
	{
		return pipeline;
	}

	/* Viewport / Scissor */

	/* NOTE: because viewport and scissor are dynamic,
	 * values must be set using the command buffer
	 */
	viewportStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportStateInfo.pNext = NULL;
	viewportStateInfo.flags = 0;
	viewportStateInfo.viewportCount = 1;
	viewportStateInfo.pViewports = NULL;
	viewportStateInfo.scissorCount = 1;
	viewportStateInfo.pScissors = NULL;

	/* Input Assembly */

	inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyInfo.pNext = NULL;
	inputAssemblyInfo.flags = 0;
	inputAssemblyInfo.topology = XNAToVK_Topology[
		renderer->currentPrimitiveType
	];
	inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

	/* Vertex Input */

	bindingDescriptions = (VkVertexInputBindingDescription*) SDL_malloc(
		renderer->numVertexBindings *
		sizeof(VkVertexInputBindingDescription)
	);
	attributeDescriptions = (VkVertexInputAttributeDescription*) SDL_malloc(
		renderer->numVertexBindings *
		MAX_VERTEX_ATTRIBUTES *
		sizeof(VkVertexInputAttributeDescription)
	);
	divisorDescriptions = (VkVertexInputBindingDivisorDescriptionEXT*) SDL_malloc(
		renderer->numVertexBindings *
		sizeof(VkVertexInputBindingDivisorDescriptionEXT)
	);

	VULKAN_INTERNAL_GenerateVertexInputInfo(
		renderer,
		bindingDescriptions,
		attributeDescriptions,
		&attributeDescriptionCount,
		divisorDescriptions,
		&divisorDescriptionCount
	);

	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	if (divisorDescriptionCount > 0)
	{
		divisorStateInfo.sType =
			VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT;
		divisorStateInfo.pNext = NULL;
		divisorStateInfo.vertexBindingDivisorCount =
			divisorDescriptionCount;
		divisorStateInfo.pVertexBindingDivisors = divisorDescriptions;
		vertexInputInfo.pNext = &divisorStateInfo;
	}
	else
	{
		vertexInputInfo.pNext = NULL;
	}

	vertexInputInfo.flags = 0;
	vertexInputInfo.vertexBindingDescriptionCount =
		renderer->numVertexBindings;
	vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions;
	vertexInputInfo.vertexAttributeDescriptionCount =
		attributeDescriptionCount;
	vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

	/* Rasterizer */

	rasterizerInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizerInfo.pNext = NULL;
	rasterizerInfo.flags = 0;
	rasterizerInfo.depthClampEnable = VK_FALSE;
	rasterizerInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterizerInfo.polygonMode = XNAToVK_PolygonMode[
		renderer->rasterizerState.fillMode
	];
	rasterizerInfo.cullMode = XNAToVK_CullMode[
		renderer->rasterizerState.cullMode
	];
	/* This is reversed because we are flipping the viewport -cosmonaut */
	rasterizerInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizerInfo.depthBiasEnable = VK_TRUE;
	rasterizerInfo.depthBiasConstantFactor = 0.0f;
	rasterizerInfo.depthBiasClamp = 0.0f;
	rasterizerInfo.depthBiasSlopeFactor = 0.0f;
	rasterizerInfo.lineWidth = 1.0f;

	/* Multisample */

	multisamplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisamplingInfo.pNext = NULL;
	multisamplingInfo.flags = 0;
	multisamplingInfo.rasterizationSamples = XNAToVK_SampleCount(
		renderer->multiSampleCount
	);
	multisamplingInfo.sampleShadingEnable = VK_FALSE;
	multisamplingInfo.minSampleShading = 1.0f;
	multisamplingInfo.pSampleMask = renderer->multiSampleMask;
	multisamplingInfo.alphaToCoverageEnable = VK_FALSE;
	multisamplingInfo.alphaToOneEnable = VK_FALSE;

	/* Blend */

	colorBlendAttachments[0].blendEnable = !(
		renderer->blendState.colorSourceBlend == FNA3D_BLEND_ONE &&
		renderer->blendState.colorDestinationBlend == FNA3D_BLEND_ZERO &&
		renderer->blendState.alphaSourceBlend == FNA3D_BLEND_ONE &&
		renderer->blendState.alphaDestinationBlend == FNA3D_BLEND_ZERO
	);
	if (colorBlendAttachments[0].blendEnable)
	{
		colorBlendAttachments[0].srcColorBlendFactor = XNAToVK_BlendFactor[
			renderer->blendState.colorSourceBlend
		];
		colorBlendAttachments[0].srcAlphaBlendFactor = XNAToVK_BlendFactor[
			renderer->blendState.alphaSourceBlend
		];
		colorBlendAttachments[0].dstColorBlendFactor = XNAToVK_BlendFactor[
			renderer->blendState.colorDestinationBlend
		];
		colorBlendAttachments[0].dstAlphaBlendFactor = XNAToVK_BlendFactor[
			renderer->blendState.alphaDestinationBlend
		];

		colorBlendAttachments[0].colorBlendOp = XNAToVK_BlendOp[
			renderer->blendState.colorBlendFunction
		];
		colorBlendAttachments[0].alphaBlendOp = XNAToVK_BlendOp[
			renderer->blendState.alphaBlendFunction
		];
	}
	else
	{
		colorBlendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		colorBlendAttachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorBlendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
		colorBlendAttachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		colorBlendAttachments[0].colorBlendOp = VK_BLEND_OP_ADD;
		colorBlendAttachments[0].alphaBlendOp = VK_BLEND_OP_ADD;
	}
	colorBlendAttachments[1] = colorBlendAttachments[0];
	colorBlendAttachments[2] = colorBlendAttachments[0];
	colorBlendAttachments[3] = colorBlendAttachments[0];

	colorBlendAttachments[0].colorWriteMask =
		renderer->blendState.colorWriteEnable;
	colorBlendAttachments[1].colorWriteMask =
		renderer->blendState.colorWriteEnable1;
	colorBlendAttachments[2].colorWriteMask =
		renderer->blendState.colorWriteEnable2;
	colorBlendAttachments[3].colorWriteMask =
		renderer->blendState.colorWriteEnable3;

	colorBlendStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendStateInfo.pNext = NULL;
	colorBlendStateInfo.flags = 0;
	colorBlendStateInfo.logicOpEnable = VK_FALSE;
	colorBlendStateInfo.logicOp = VK_LOGIC_OP_COPY;
	colorBlendStateInfo.attachmentCount = renderer->colorAttachmentCount;
	colorBlendStateInfo.pAttachments = colorBlendAttachments;
	colorBlendStateInfo.blendConstants[0] = 0.0f;
	colorBlendStateInfo.blendConstants[1] = 0.0f;
	colorBlendStateInfo.blendConstants[2] = 0.0f;
	colorBlendStateInfo.blendConstants[3] = 0.0f;

	/* Stencil */

	frontStencilState.failOp = XNAToVK_StencilOp[
		renderer->depthStencilState.stencilFail
	];
	frontStencilState.passOp = XNAToVK_StencilOp[
		renderer->depthStencilState.stencilPass
	];
	frontStencilState.depthFailOp = XNAToVK_StencilOp[
		renderer->depthStencilState.stencilDepthBufferFail
	];
	frontStencilState.compareOp = XNAToVK_CompareOp[
		renderer->depthStencilState.stencilFunction
	];
	frontStencilState.compareMask = renderer->depthStencilState.stencilMask;
	frontStencilState.writeMask = renderer->depthStencilState.stencilWriteMask;
	frontStencilState.reference = renderer->depthStencilState.referenceStencil;

	if (renderer->depthStencilState.twoSidedStencilMode)
	{
		backStencilState.failOp = XNAToVK_StencilOp[
			renderer->depthStencilState.ccwStencilFail
		];
		backStencilState.passOp = XNAToVK_StencilOp[
			renderer->depthStencilState.ccwStencilPass
		];
		backStencilState.depthFailOp = XNAToVK_StencilOp[
			renderer->depthStencilState.ccwStencilDepthBufferFail
		];
		backStencilState.compareOp = XNAToVK_CompareOp[
			renderer->depthStencilState.ccwStencilFunction
		];
		backStencilState.compareMask = renderer->depthStencilState.stencilMask;
		backStencilState.writeMask = renderer->depthStencilState.stencilWriteMask;
		backStencilState.reference = renderer->depthStencilState.referenceStencil;
	}
	else
	{
		backStencilState = frontStencilState;
	}

	/* Depth Stencil */

	depthStencilStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilStateInfo.pNext = NULL;
	depthStencilStateInfo.flags = 0; /* Unused */
	depthStencilStateInfo.depthTestEnable =
		renderer->depthStencilState.depthBufferEnable;
	depthStencilStateInfo.depthWriteEnable =
		renderer->depthStencilState.depthBufferWriteEnable;
	depthStencilStateInfo.depthCompareOp = XNAToVK_CompareOp[
		renderer->depthStencilState.depthBufferFunction
	];
	depthStencilStateInfo.depthBoundsTestEnable = 0; /* Unused */
	depthStencilStateInfo.stencilTestEnable =
		renderer->depthStencilState.stencilEnable;
	depthStencilStateInfo.front = frontStencilState;
	depthStencilStateInfo.back = backStencilState;
	depthStencilStateInfo.minDepthBounds = 0; /* Unused */
	depthStencilStateInfo.maxDepthBounds = 0; /* Unused */

	/* Dynamic State */

	dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateInfo.pNext = NULL;
	dynamicStateInfo.flags = 0;
	dynamicStateInfo.dynamicStateCount = SDL_arraysize(dynamicStates);
	dynamicStateInfo.pDynamicStates = dynamicStates;

	/* Shaders */

	stageInfos[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageInfos[0].pNext = NULL;
	stageInfos[0].flags = 0;
	stageInfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stageInfos[0].pName = MOJOSHADER_vkGetShaderParseData(vertShader)->mainfn;
	stageInfos[0].pSpecializationInfo = NULL;

	stageInfos[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageInfos[1].pNext = NULL;
	stageInfos[1].flags = 0;
	stageInfos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stageInfos[1].pName = MOJOSHADER_vkGetShaderParseData(fragShader)->mainfn;
	stageInfos[1].pSpecializationInfo = NULL;

	MOJOSHADER_vkGetShaderModules(
		renderer->mojoshaderContext,
		&stageInfos[0].module,
		&stageInfos[1].module
	);

	/* Pipeline */

	pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineCreateInfo.pNext = NULL;
	pipelineCreateInfo.flags = 0;
	pipelineCreateInfo.stageCount = 2;
	pipelineCreateInfo.pStages = stageInfos;
	pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyInfo;
	pipelineCreateInfo.pTessellationState = NULL;
	pipelineCreateInfo.pViewportState = &viewportStateInfo;
	pipelineCreateInfo.pRasterizationState = &rasterizerInfo;
	pipelineCreateInfo.pMultisampleState = &multisamplingInfo;
	pipelineCreateInfo.pDepthStencilState = &depthStencilStateInfo;
	pipelineCreateInfo.pColorBlendState = &colorBlendStateInfo;
	pipelineCreateInfo.pDynamicState = &dynamicStateInfo;
	pipelineCreateInfo.layout = renderer->currentPipelineLayout;
	pipelineCreateInfo.renderPass = renderer->renderPass;
	pipelineCreateInfo.subpass = 0;
	pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineCreateInfo.basePipelineIndex = 0;

	vulkanResult = renderer->vkCreateGraphicsPipelines(
		renderer->logicalDevice,
		renderer->pipelineCache,
		1,
		&pipelineCreateInfo,
		NULL,
		&pipeline
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkCreateGraphicsPipelines, VK_NULL_HANDLE)

	SDL_free(bindingDescriptions);
	SDL_free(attributeDescriptions);
	SDL_free(divisorDescriptions);

	PipelineHashTable_Insert(&renderer->pipelineHashTable, hash, pipeline);
	return pipeline;
}

static void VULKAN_INTERNAL_BindPipeline(VulkanRenderer *renderer)
{
	VulkanCommandBuffer *commandBuffer;
	VkShaderModule vertShader, fragShader;
	MOJOSHADER_vkGetShaderModules(renderer->mojoshaderContext, &vertShader, &fragShader);

	if (	renderer->needNewPipeline ||
		renderer->currentVertShader != vertShader ||
		renderer->currentFragShader != fragShader	)
	{
		VkPipeline pipeline = VULKAN_INTERNAL_FetchPipeline(renderer);

		if (pipeline != renderer->currentPipeline)
		{
			commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
				renderer->commandBuffers
			);
			RECORD_CMD(renderer->vkCmdBindPipeline(
				commandBuffer->commandBuffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipeline
			));
			renderer->currentPipeline = pipeline;
			renderer->fragSamplerDescriptorSetDataNeedsUpdate = 1;
			renderer->vertexSamplerDescriptorSetDataNeedsUpdate = 1;
		}

		renderer->needNewPipeline = 0;
		renderer->currentVertShader = vertShader;
		renderer->currentFragShader = fragShader;
	}
}

/* Vulkan: The Faux-Backbuffer */

static uint8_t VULKAN_INTERNAL_CreateFauxBackbuffer(
	VulkanRenderer *renderer,
	FNA3D_PresentationParameters *presentationParameters
) {
	VkFormat vulkanDepthStencilFormat;
	VkImageAspectFlags depthAspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
	VkFormat format;
	VkComponentMapping swizzle;

	renderer->backbufferFormat = presentationParameters->backBufferFormat;
	renderer->presentInterval = presentationParameters->presentationInterval;

	format = XNAToVK_SurfaceFormat[renderer->backbufferFormat];

	swizzle.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	swizzle.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	swizzle.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	swizzle.a = VK_COMPONENT_SWIZZLE_IDENTITY;

	renderer->fauxBackbufferColor.handle = (VulkanTexture*) SDL_malloc(sizeof(VulkanTexture));

	if (!VULKAN_INTERNAL_CreateTexture(
		renderer,
		presentationParameters->backBufferWidth,
		presentationParameters->backBufferHeight,
		1,
		0,
		1,
		VK_SAMPLE_COUNT_1_BIT,
		1,
		format,
		swizzle,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_TYPE_2D,
		/* FIXME: Transfer bit probably only needs to be set on 0? */
		(
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT
		),
		renderer->fauxBackbufferColor.handle
	)) {
		FNA3D_LogError("Failed to create faux backbuffer colorbuffer");
		return 0;
	}
	renderer->fauxBackbufferColor.handle->colorFormat = renderer->backbufferFormat;

	renderer->fauxBackbufferWidth = presentationParameters->backBufferWidth;
	renderer->fauxBackbufferHeight = presentationParameters->backBufferHeight;

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		renderer->fauxBackbufferColor.handle->layerCount,
		0,
		renderer->fauxBackbufferColor.handle->levelCount,
		0,
		renderer->fauxBackbufferColor.handle->image,
		&renderer->fauxBackbufferColor.handle->resourceAccessType
	);

	renderer->fauxBackbufferMultiSampleCount =
		presentationParameters->multiSampleCount;
	renderer->fauxBackbufferMultiSampleColor = NULL;

	if (renderer->fauxBackbufferMultiSampleCount > 0)
	{
		renderer->fauxBackbufferMultiSampleColor = (VulkanTexture*) SDL_malloc(
			sizeof(VulkanTexture)
		);

		VULKAN_INTERNAL_CreateTexture(
			renderer,
			presentationParameters->backBufferWidth,
			presentationParameters->backBufferHeight,
			1,
			0,
			1,
			XNAToVK_SampleCount(presentationParameters->multiSampleCount),
			1,
			format,
			swizzle,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			renderer->fauxBackbufferMultiSampleColor
		);
		/* FIXME: Swapchain format may not be an FNA3D_SurfaceFormat! */
		renderer->fauxBackbufferMultiSampleColor->colorFormat = renderer->backbufferFormat;

		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			renderer->fauxBackbufferMultiSampleColor->layerCount,
			0,
			renderer->fauxBackbufferMultiSampleColor->levelCount,
			0,
			renderer->fauxBackbufferMultiSampleColor->image,
			&renderer->fauxBackbufferMultiSampleColor->resourceAccessType
		);
	}

	/* Create faux backbuffer depth stencil image */

	renderer->fauxBackbufferDepthStencil.handle = NULL;
	if (presentationParameters->depthStencilFormat != FNA3D_DEPTHFORMAT_NONE)
	{
		renderer->fauxBackbufferDepthStencil.handle = (VulkanTexture*) SDL_malloc(
			sizeof(VulkanTexture)
		);

		vulkanDepthStencilFormat = XNAToVK_DepthFormat(
			renderer,
			presentationParameters->depthStencilFormat
		);

		if (DepthFormatContainsStencil(vulkanDepthStencilFormat))
		{
			depthAspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}

		if (!VULKAN_INTERNAL_CreateTexture(
			renderer,
			presentationParameters->backBufferWidth,
			presentationParameters->backBufferHeight,
			1,
			0,
			1,
			XNAToVK_SampleCount(
				presentationParameters->multiSampleCount
			),
			1,
			vulkanDepthStencilFormat,
			RGBA_SWIZZLE,
			depthAspectFlags,
			VK_IMAGE_TYPE_2D,
			(
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
				VK_IMAGE_USAGE_TRANSFER_DST_BIT
			),
			renderer->fauxBackbufferDepthStencil.handle
		)) {
			FNA3D_LogError("Failed to create depth stencil image");
			return 0;
		}
		renderer->fauxBackbufferDepthStencil.handle->depthStencilFormat =
			presentationParameters->depthStencilFormat;

		/* Layout transition if required */

		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_WRITE,
			depthAspectFlags,
			0,
			renderer->fauxBackbufferDepthStencil.handle->layerCount,
			0,
			renderer->fauxBackbufferDepthStencil.handle->levelCount,
			0,
			renderer->fauxBackbufferDepthStencil.handle->image,
			&renderer->fauxBackbufferDepthStencil.handle->resourceAccessType
		);

		if (!renderer->renderTargetBound)
		{
			renderer->nextRenderPassDepthStencilAttachment =
				renderer->fauxBackbufferDepthStencil.handle;

			renderer->nextRenderPassDepthFormat =
				presentationParameters->depthStencilFormat;
		}
	}

	if (!renderer->renderTargetBound)
	{
		renderer->nextRenderPassColorAttachments[0] =
			renderer->fauxBackbufferColor.handle;
		renderer->nextRenderPassColorAttachmentCount = 1;

		if (renderer->fauxBackbufferMultiSampleCount > 0)
		{
			renderer->nextRenderPassColorMultiSampleAttachments[0] =
				renderer->fauxBackbufferMultiSampleColor;
			renderer->nextRenderPassMultiSampleCount =
				renderer->fauxBackbufferMultiSampleCount;
		}
	}

	return 1;
}

static void VULKAN_INTERNAL_DestroyFauxBackbuffer(VulkanRenderer *renderer)
{
	renderer->vkDestroyFramebuffer(
		renderer->logicalDevice,
		renderer->fauxBackbufferFramebuffer,
		NULL
	);

	VULKAN_INTERNAL_DestroyTexture(
		renderer,
		renderer->fauxBackbufferColor.handle
	);

	if (renderer->fauxBackbufferMultiSampleColor != NULL)
	{
		VULKAN_INTERNAL_DestroyTexture(
			renderer,
			renderer->fauxBackbufferMultiSampleColor
		);
	}

	if (renderer->fauxBackbufferDepthStencil.handle != NULL)
	{
		VULKAN_INTERNAL_DestroyTexture(
			renderer,
			renderer->fauxBackbufferDepthStencil.handle
		);
	}
}

/* Vulkan: Render Passes */

static VkRenderPass VULKAN_INTERNAL_FetchRenderPass(VulkanRenderer *renderer)
{
	VkResult vulkanResult;
	VkRenderPass renderPass;
	VkAttachmentDescription attachmentDescriptions[2 * MAX_RENDERTARGET_BINDINGS + 1];
	uint32_t attachmentDescriptionsCount = 0;
	uint32_t i;
	VkAttachmentReference colorAttachmentReferences[MAX_RENDERTARGET_BINDINGS];
	uint32_t colorAttachmentReferenceCount = 0;
	VkAttachmentReference resolveReferences[MAX_RENDERTARGET_BINDINGS + 1];
	uint32_t resolveReferenceCount = 0;
	VkAttachmentReference depthStencilAttachmentReference;
	VkSubpassDescription subpass;
	VkRenderPassCreateInfo renderPassCreateInfo;

	RenderPassHash hash;
	hash.colorAttachmentFormatOne = renderer->colorAttachments[0]->surfaceFormat;
	hash.colorAttachmentFormatTwo = renderer->colorAttachments[1] != NULL ?
		renderer->colorAttachments[1]->surfaceFormat :
		VK_FORMAT_UNDEFINED;
	hash.colorAttachmentFormatThree = renderer->colorAttachments[2] != NULL ?
		renderer->colorAttachments[2]->surfaceFormat :
		VK_FORMAT_UNDEFINED;
	hash.colorAttachmentFormatFour = renderer->colorAttachments[3] != NULL ?
		renderer->colorAttachments[3]->surfaceFormat :
		VK_FORMAT_UNDEFINED;
	hash.depthStencilAttachmentFormat = renderer->depthStencilAttachment != NULL ?
		renderer->depthStencilAttachment->surfaceFormat :
		VK_FORMAT_UNDEFINED;
	hash.clearColor = renderer->shouldClearColorOnBeginPass;
	hash.clearDepth = renderer->shouldClearDepthOnBeginPass;
	hash.clearStencil = renderer->shouldClearStencilOnBeginPass;
	hash.preserveTargetContents = renderer->nextRenderPassPreserveTargetContents;

	hash.width = renderer->colorAttachments[0]->dimensions.width;
	hash.height = renderer->colorAttachments[0]->dimensions.height;
	hash.multiSampleCount = renderer->multiSampleCount;

	/* The render pass is already cached, can return it */
	renderPass = RenderPassHashArray_Fetch(
		&renderer->renderPassArray,
		hash
	);
	if (renderPass != VK_NULL_HANDLE)
	{
		return renderPass;
	}

	/*
	 * FIXME: We have to always store just in case changing render state
	 * breaks the render pass. Otherwise we risk discarding necessary data.
	 * The only way to avoid this would be to buffer draw calls so we can
	 * ensure that only the final render pass before switching render targets
	 * may be discarded.
	 */

	for (i = 0; i < renderer->colorAttachmentCount; i += 1)
	{
		if (renderer->multiSampleCount > 0)
		{
			/* Resolve attachment and multisample attachment */

			attachmentDescriptions[attachmentDescriptionsCount].flags = 0;
			attachmentDescriptions[attachmentDescriptionsCount].format =
				renderer->colorAttachments[i]->surfaceFormat;
			attachmentDescriptions[attachmentDescriptionsCount].samples =
				VK_SAMPLE_COUNT_1_BIT;
			attachmentDescriptions[attachmentDescriptionsCount].loadOp =
				hash.clearColor ?
					VK_ATTACHMENT_LOAD_OP_CLEAR :
					VK_ATTACHMENT_LOAD_OP_LOAD;
			attachmentDescriptions[attachmentDescriptionsCount].storeOp =
				VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescriptions[attachmentDescriptionsCount].stencilLoadOp =
				VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionsCount].stencilStoreOp =
				VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionsCount].initialLayout =
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachmentDescriptions[attachmentDescriptionsCount].finalLayout =
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			resolveReferences[resolveReferenceCount].attachment =
				attachmentDescriptionsCount;
			resolveReferences[resolveReferenceCount].layout =
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			attachmentDescriptionsCount += 1;
			resolveReferenceCount += 1;

			attachmentDescriptions[attachmentDescriptionsCount].flags = 0;
			attachmentDescriptions[attachmentDescriptionsCount].format =
				renderer->colorMultiSampleAttachments[i]->surfaceFormat;
			attachmentDescriptions[attachmentDescriptionsCount].samples =
				XNAToVK_SampleCount(renderer->multiSampleCount);
			attachmentDescriptions[attachmentDescriptionsCount].loadOp =
				hash.clearColor ?
					VK_ATTACHMENT_LOAD_OP_CLEAR :
					VK_ATTACHMENT_LOAD_OP_LOAD;
			attachmentDescriptions[attachmentDescriptionsCount].storeOp =
				VK_ATTACHMENT_STORE_OP_STORE;
				/*
				 * Once above FIXME is resolved:
				 * 	hash.preserveTargetContents ?
				 *		VK_ATTACHMENT_STORE_OP_STORE :
				 *		VK_ATTACHMENT_STORE_OP_DONT_CARE;
				 */
			attachmentDescriptions[attachmentDescriptionsCount].stencilLoadOp =
				VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionsCount].stencilStoreOp =
				VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionsCount].initialLayout =
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachmentDescriptions[attachmentDescriptionsCount].finalLayout =
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			colorAttachmentReferences[colorAttachmentReferenceCount].attachment =
				attachmentDescriptionsCount;
			colorAttachmentReferences[colorAttachmentReferenceCount].layout =
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			attachmentDescriptionsCount += 1;
			colorAttachmentReferenceCount += 1;
		}
		else
		{
			/* Regular old attachment */

			attachmentDescriptions[attachmentDescriptionsCount].flags = 0;
			attachmentDescriptions[attachmentDescriptionsCount].format =
				renderer->colorAttachments[i]->surfaceFormat;
			attachmentDescriptions[attachmentDescriptionsCount].samples =
				VK_SAMPLE_COUNT_1_BIT;
			attachmentDescriptions[attachmentDescriptionsCount].loadOp =
				hash.clearColor ?
					VK_ATTACHMENT_LOAD_OP_CLEAR :
					VK_ATTACHMENT_LOAD_OP_LOAD;
			attachmentDescriptions[attachmentDescriptionsCount].storeOp =
				VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescriptions[attachmentDescriptionsCount].stencilLoadOp =
				VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionsCount].stencilStoreOp =
				VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionsCount].initialLayout =
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachmentDescriptions[attachmentDescriptionsCount].finalLayout =
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			attachmentDescriptionsCount += 1;

			colorAttachmentReferences[colorAttachmentReferenceCount].attachment = i;
			colorAttachmentReferences[colorAttachmentReferenceCount].layout =
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			colorAttachmentReferenceCount += 1;
		}
	}

	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.flags = 0;
	subpass.inputAttachmentCount = 0;
	subpass.pInputAttachments = NULL;
	subpass.colorAttachmentCount = renderer->colorAttachmentCount;
	subpass.pColorAttachments = colorAttachmentReferences;
	subpass.preserveAttachmentCount = 0;
	subpass.pPreserveAttachments = NULL;

	if (renderer->depthStencilAttachment == NULL)
	{
		subpass.pDepthStencilAttachment = NULL;
	}
	else
	{
		attachmentDescriptions[attachmentDescriptionsCount].flags = 0;
		attachmentDescriptions[attachmentDescriptionsCount].format =
			renderer->depthStencilAttachment->surfaceFormat;
		attachmentDescriptions[attachmentDescriptionsCount].samples =
			XNAToVK_SampleCount(renderer->multiSampleCount);
		attachmentDescriptions[attachmentDescriptionsCount].loadOp =
			hash.clearDepth ?
				VK_ATTACHMENT_LOAD_OP_CLEAR :
				VK_ATTACHMENT_LOAD_OP_LOAD;
		attachmentDescriptions[attachmentDescriptionsCount].storeOp =
			VK_ATTACHMENT_STORE_OP_STORE;
			/*
			 * Once above FIXME is resolved:
			 * 	hash.preserveTargetContents ?
			 *		VK_ATTACHMENT_STORE_OP_STORE :
			 *		VK_ATTACHMENT_STORE_OP_DONT_CARE;
			 */
		attachmentDescriptions[attachmentDescriptionsCount].stencilLoadOp =
			hash.clearStencil ?
				VK_ATTACHMENT_LOAD_OP_CLEAR :
				VK_ATTACHMENT_LOAD_OP_LOAD;
		attachmentDescriptions[attachmentDescriptionsCount].stencilStoreOp =
			VK_ATTACHMENT_STORE_OP_STORE;
			/*
			 * Once above FIXME is resolved:
			 * 	hash.preserveTargetContents ?
			 *		VK_ATTACHMENT_STORE_OP_STORE :
			 *		VK_ATTACHMENT_STORE_OP_DONT_CARE;
			 */
		attachmentDescriptions[attachmentDescriptionsCount].initialLayout =
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attachmentDescriptions[attachmentDescriptionsCount].finalLayout =
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		depthStencilAttachmentReference.attachment =
			attachmentDescriptionsCount;
		depthStencilAttachmentReference.layout =
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		subpass.pDepthStencilAttachment =
			&depthStencilAttachmentReference;

		attachmentDescriptionsCount += 1;
	}

	if (renderer->multiSampleCount > 0)
	{
		subpass.pResolveAttachments = resolveReferences;
	}
	else
	{
		subpass.pResolveAttachments = NULL;
	}

	renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassCreateInfo.pNext = NULL;
	renderPassCreateInfo.flags = 0;
	renderPassCreateInfo.attachmentCount = attachmentDescriptionsCount;
	renderPassCreateInfo.pAttachments = attachmentDescriptions;
	renderPassCreateInfo.subpassCount = 1;
	renderPassCreateInfo.pSubpasses = &subpass;
	renderPassCreateInfo.dependencyCount = 0;
	renderPassCreateInfo.pDependencies = NULL;

	vulkanResult = renderer->vkCreateRenderPass(
		renderer->logicalDevice,
		&renderPassCreateInfo,
		NULL,
		&renderPass
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkCreateRenderPass, VK_NULL_HANDLE)

	RenderPassHashArray_Insert(
		&renderer->renderPassArray,
		hash,
		renderPass
	);
	return renderPass;
}

static VkFramebuffer VULKAN_INTERNAL_FetchFramebuffer(
	VulkanRenderer *renderer,
	VkRenderPass renderPass
) {
	VkFramebuffer framebuffer;
	VkImageView imageViewAttachments[2 * MAX_RENDERTARGET_BINDINGS + 1];
	uint32_t i, attachmentCount;
	VkFramebufferCreateInfo framebufferInfo;
	VkResult vulkanResult;

	FramebufferHash hash;
	for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i += 1)
	{
		hash.colorAttachmentViews[i] = (
			renderer->colorAttachments[i] != NULL ?
				renderer->colorAttachments[i]->rtViews[
					renderer->attachmentCubeFaces[i]
				] :
				VK_NULL_HANDLE
		);
		hash.colorMultiSampleAttachmentViews[i] = (
			renderer->colorMultiSampleAttachments[i] != NULL ?
				renderer->colorMultiSampleAttachments[i]->rtViews[
					renderer->attachmentCubeFaces[i]
				] :
				VK_NULL_HANDLE
		);
	}
	hash.depthStencilAttachmentView = (
		renderer->depthStencilAttachment != NULL ?
			renderer->depthStencilAttachment->rtViews[0] :
			VK_NULL_HANDLE
	);
	hash.width = renderer->colorAttachments[0]->dimensions.width;
	hash.height = renderer->colorAttachments[0]->dimensions.height;

	/* Framebuffer is cached, can return it */
	framebuffer = FramebufferHashArray_Fetch(
		&renderer->framebufferArray,
		hash
	);
	if (framebuffer != VK_NULL_HANDLE)
	{
		return framebuffer;
	}

	/* Otherwise make a new one */

	attachmentCount = 0;

	for (i = 0; i < renderer->colorAttachmentCount; i += 1)
	{
		imageViewAttachments[attachmentCount] =
			renderer->colorAttachments[i]->rtViews[
				renderer->attachmentCubeFaces[i]
			];
		attachmentCount += 1;

		if (renderer->multiSampleCount > 0)
		{
			imageViewAttachments[attachmentCount] =
				renderer->colorMultiSampleAttachments[i]->rtViews[
					renderer->attachmentCubeFaces[i]
				];
			attachmentCount += 1;
		}
	}
	if (renderer->depthStencilAttachment != NULL)
	{
		imageViewAttachments[attachmentCount] =
			renderer->depthStencilAttachment->rtViews[0];
		attachmentCount += 1;
	}

	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.pNext = NULL;
	framebufferInfo.flags = 0;
	framebufferInfo.renderPass = renderPass;
	framebufferInfo.attachmentCount = attachmentCount;
	framebufferInfo.pAttachments = imageViewAttachments;
	framebufferInfo.width = renderer->colorAttachments[0]->dimensions.width;
	framebufferInfo.height = renderer->colorAttachments[0]->dimensions.height;
	framebufferInfo.layers = 1;

	vulkanResult = renderer->vkCreateFramebuffer(
		renderer->logicalDevice,
		&framebufferInfo,
		NULL,
		&framebuffer
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkCreateFramebuffer, VK_NULL_HANDLE)

	FramebufferHashArray_Insert(
		&renderer->framebufferArray,
		hash,
		framebuffer
	);
	return framebuffer;
}

static void VULKAN_INTERNAL_MaybeEndRenderPass(
	VulkanRenderer *renderer
) {
	int32_t i;
	VulkanTexture *currentTexture;
	VulkanCommandBuffer *commandBuffer;

	SDL_LockMutex(renderer->passLock);

	if (renderer->renderPassInProgress)
	{
		commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
			renderer->commandBuffers
		);
		RECORD_CMD(renderer->vkCmdEndRenderPass(commandBuffer->commandBuffer));

		renderer->renderPassInProgress = 0;
		renderer->needNewRenderPass = 1;
		renderer->drawCallMadeThisPass = 0;
		renderer->currentPipeline = VK_NULL_HANDLE;
		renderer->needNewPipeline = 1;

		for (i = 0; i < renderer->colorAttachmentCount; i += 1)
		{
			currentTexture = renderer->colorAttachments[i];

			/* If the render target can be sampled, transition to sampler layout */
			if (currentTexture->imageCreateInfo.usage & VK_IMAGE_USAGE_SAMPLED_BIT)
			{
				VULKAN_INTERNAL_ImageMemoryBarrier(
					renderer,
					RESOURCE_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE,
					VK_IMAGE_ASPECT_COLOR_BIT,
					0,
					currentTexture->layerCount,
					0,
					currentTexture->levelCount,
					0,
					currentTexture->image,
					&currentTexture->resourceAccessType
				);
			}
		}

		/* Unlocking long-term lock */
		SDL_UnlockMutex(renderer->passLock);
	}

	SDL_UnlockMutex(renderer->passLock);
}

static void VULKAN_INTERNAL_BeginRenderPass(
	VulkanRenderer *renderer
) {
	VulkanCommandBuffer *commandBuffer;
	VkRenderPassBeginInfo renderPassBeginInfo;
	VkFramebuffer framebuffer;
	VkImageAspectFlags depthAspectFlags;
	float blendConstants[4];
	VkClearValue clearValues[2 * MAX_RENDERTARGET_BINDINGS + 1];
	VkClearColorValue clearColorValue;
	uint32_t clearValueCount = 0;
	uint32_t i;

	if (!renderer->needNewRenderPass)
	{
		return;
	}

	VULKAN_INTERNAL_MaybeEndRenderPass(renderer);

	SDL_LockMutex(renderer->passLock);

	for (i = 0; i < renderer->nextRenderPassColorAttachmentCount; i += 1)
	{
		renderer->colorAttachments[i] = renderer->nextRenderPassColorAttachments[i];
		renderer->attachmentCubeFaces[i] = renderer->nextRenderPassAttachmentCubeFaces[i];
		renderer->colorMultiSampleAttachments[i] = renderer->nextRenderPassColorMultiSampleAttachments[i]; /* may be NULL */
	}
	for (; i < MAX_RENDERTARGET_BINDINGS; i += 1)
	{
		renderer->colorAttachments[i] = NULL;
		renderer->attachmentCubeFaces[i] = 0;
		renderer->colorMultiSampleAttachments[i] = NULL;
	}

	renderer->colorAttachmentCount = renderer->nextRenderPassColorAttachmentCount;
	renderer->multiSampleCount = renderer->nextRenderPassMultiSampleCount;

	renderer->depthStencilAttachment = renderer->nextRenderPassDepthStencilAttachment;
	renderer->currentDepthFormat = renderer->nextRenderPassDepthFormat;

	renderer->renderTargetBound = (renderer->nextRenderPassColorAttachments[0] != renderer->fauxBackbufferColor.handle);

	renderer->renderPass = VULKAN_INTERNAL_FetchRenderPass(renderer);
	framebuffer = VULKAN_INTERNAL_FetchFramebuffer(
		renderer,
		renderer->renderPass
	);

	renderer->needNewPipeline = 1;

	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.pNext = NULL;
	renderPassBeginInfo.renderPass = renderer->renderPass;
	renderPassBeginInfo.framebuffer = framebuffer;
	renderPassBeginInfo.renderArea.offset.x = 0;
	renderPassBeginInfo.renderArea.offset.y = 0;
	renderPassBeginInfo.renderArea.extent.width =
		renderer->colorAttachments[0]->dimensions.width;
	renderPassBeginInfo.renderArea.extent.height =
		renderer->colorAttachments[0]->dimensions.height;

	for (i = 0; i < renderer->colorAttachmentCount; i += 1)
	{
		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			renderer->colorAttachments[i]->layerCount,
			0,
			renderer->colorAttachments[i]->levelCount,
			0,
			renderer->colorAttachments[i]->image,
			&renderer->colorAttachments[i]->resourceAccessType
		);

		/*
			* In MRT scenarios we need to provide as many clearValue structs
			* as RTs in the current framebuffer even if they aren't cleared
			*/

		if (renderer->shouldClearColorOnBeginPass)
		{
			clearColorValue = renderer->clearColorValue;
		}
		else
		{
			clearColorValue.float32[0] = 0;
			clearColorValue.float32[1] = 0;
			clearColorValue.float32[2] = 0;
			clearColorValue.float32[3] = 0;
		}

		clearValues[clearValueCount].color.float32[0] = clearColorValue.float32[0];
		clearValues[clearValueCount].color.float32[1] = clearColorValue.float32[1];
		clearValues[clearValueCount].color.float32[2] = clearColorValue.float32[2];
		clearValues[clearValueCount].color.float32[3] = clearColorValue.float32[3];
		clearValueCount += 1;

		if (renderer->colorMultiSampleAttachments[i] != NULL)
		{
			clearValues[clearValueCount].color.float32[0] = clearColorValue.float32[0];
			clearValues[clearValueCount].color.float32[1] = clearColorValue.float32[1];
			clearValues[clearValueCount].color.float32[2] = clearColorValue.float32[2];
			clearValues[clearValueCount].color.float32[3] = clearColorValue.float32[3];
			clearValueCount += 1;
		}
	}

	if (renderer->depthStencilAttachment != NULL)
	{
		depthAspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (DepthFormatContainsStencil(
			renderer->depthStencilAttachment->surfaceFormat
		)) {
			depthAspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}

		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_WRITE,
			depthAspectFlags,
			0,
			renderer->depthStencilAttachment->layerCount,
			0,
			renderer->depthStencilAttachment->levelCount,
			0,
			renderer->depthStencilAttachment->image,
			&renderer->depthStencilAttachment->resourceAccessType
		);

		if (renderer->shouldClearDepthOnBeginPass || renderer->shouldClearStencilOnBeginPass)
		{
			clearValues[clearValueCount].depthStencil.depth = renderer->clearDepthStencilValue.depth;
			clearValues[clearValueCount].depthStencil.stencil = renderer->clearDepthStencilValue.stencil;
			clearValueCount += 1;
		}
	}

	renderPassBeginInfo.clearValueCount = clearValueCount;
	renderPassBeginInfo.pClearValues = clearValues;

	commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
		renderer->commandBuffers
	);
	RECORD_CMD(renderer->vkCmdBeginRenderPass(
		commandBuffer->commandBuffer,
		&renderPassBeginInfo,
		VK_SUBPASS_CONTENTS_INLINE
	));

	renderer->renderPassInProgress = 1;

	VULKAN_INTERNAL_SetViewportCommand(renderer);
	VULKAN_INTERNAL_SetScissorRectCommand(renderer);
	VULKAN_INTERNAL_SetStencilReferenceValueCommand(renderer);
	VULKAN_INTERNAL_SetDepthBiasCommand(renderer);

	blendConstants[0] = renderer->blendState.blendFactor.r / 255.0f;
	blendConstants[1] = renderer->blendState.blendFactor.g / 255.0f;
	blendConstants[2] = renderer->blendState.blendFactor.b / 255.0f;
	blendConstants[3] = renderer->blendState.blendFactor.a / 255.0f;

	RECORD_CMD(renderer->vkCmdSetBlendConstants(
		commandBuffer->commandBuffer,
		blendConstants
	));

	/* Reset bindings for the current frame in flight */

	for (i = 0; i < MAX_TOTAL_SAMPLERS; i += 1)
	{
		if (renderer->textures[i] != &NullTexture)
		{
			renderer->textureNeedsUpdate[i] = 1;
		}
		if (renderer->samplers[i] != VK_NULL_HANDLE)
		{
			renderer->samplerNeedsUpdate[i] = 1;
		}
	}

	renderer->currentPipeline = VK_NULL_HANDLE;
	renderer->needNewPipeline = 1;

	renderer->needNewRenderPass = 0;
	renderer->shouldClearColorOnBeginPass = 0;
	renderer->shouldClearDepthOnBeginPass = 0;
	renderer->shouldClearStencilOnBeginPass = 0;
}

static void VULKAN_INTERNAL_BeginRenderPassClear(
	VulkanRenderer *renderer,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil,
	uint8_t clearColor,
	uint8_t clearDepth,
	uint8_t clearStencil
) {
	if (!clearColor && !clearDepth && !clearStencil)
	{
		return;
	}

	renderer->shouldClearColorOnBeginPass |= clearColor;
	renderer->shouldClearDepthOnBeginPass |= clearDepth;
	renderer->shouldClearStencilOnBeginPass |= clearStencil;

	if (clearColor)
	{
		renderer->clearColorValue.float32[0] = color->x;
		renderer->clearColorValue.float32[1] = color->y;
		renderer->clearColorValue.float32[2] = color->z;
		renderer->clearColorValue.float32[3] = color->w;
	}

	if (clearDepth)
	{
		if (depth < 0.0f)
		{
			depth = 0.0f;
		}
		else if (depth > 1.0f)
		{
			depth = 1.0f;
		}

		renderer->clearDepthStencilValue.depth = depth;
	}

	if (clearStencil)
	{
		renderer->clearDepthStencilValue.stencil = stencil;
	}

	renderer->needNewRenderPass = 1;
}

static void VULKAN_INTERNAL_MidRenderPassClear(
	VulkanRenderer *renderer,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil,
	uint8_t clearColor,
	uint8_t clearDepth,
	uint8_t clearStencil
) {
	VulkanCommandBuffer *commandBuffer;
	VkClearAttachment clearAttachments[2 * MAX_RENDERTARGET_BINDINGS + 1];
	VkClearRect clearRect;
	VkClearValue clearValue =
	{{{
		color->x,
		color->y,
		color->z,
		color->w
	}}};
	uint8_t shouldClearDepthStencil = (
		(clearDepth || clearStencil) &&
		renderer->depthStencilAttachment != NULL
	);
	uint32_t i, attachmentCount;

	if (!clearColor && !shouldClearDepthStencil)
	{
		return;
	}

	attachmentCount = 0;

	clearRect.baseArrayLayer = 0;
	clearRect.layerCount = 1;
	clearRect.rect.offset.x = 0;
	clearRect.rect.offset.y = 0;
	clearRect.rect.extent.width = renderer->colorAttachments[0]->dimensions.width;
	clearRect.rect.extent.height = renderer->colorAttachments[0]->dimensions.height;

	if (clearColor)
	{
		for (i = 0; i < renderer->colorAttachmentCount; i += 1)
		{
			clearAttachments[attachmentCount].aspectMask =
				VK_IMAGE_ASPECT_COLOR_BIT;
			clearAttachments[attachmentCount].colorAttachment =
				attachmentCount;
			clearAttachments[attachmentCount].clearValue =
				clearValue;
			attachmentCount += 1;

			/* Do NOT clear the multisample image here!
			 * Vulkan treats them both as the same color attachment.
			 * Vulkan is a very good and not confusing at all API.
			 */
		}
	}

	if (shouldClearDepthStencil)
	{
		clearAttachments[attachmentCount].aspectMask = 0;
		clearAttachments[attachmentCount].colorAttachment = 0;
		if (clearDepth)
		{
			if (depth < 0.0f)
			{
				depth = 0.0f;
			}
			else if (depth > 1.0f)
			{
				depth = 1.0f;
			}
			clearAttachments[attachmentCount].aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
			clearAttachments[attachmentCount].clearValue.depthStencil.depth = depth;
		}
		else
		{
			clearAttachments[attachmentCount].clearValue.depthStencil.depth = 0.0f;
		}
		if (clearStencil)
		{
			clearAttachments[attachmentCount].aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			clearAttachments[attachmentCount].clearValue.depthStencil.stencil = stencil;
		}
		else
		{
			clearAttachments[attachmentCount].clearValue.depthStencil.stencil = 0;
		}

		attachmentCount += 1;
	}

	commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
		renderer->commandBuffers
	);
	RECORD_CMD(renderer->vkCmdClearAttachments(
		commandBuffer->commandBuffer,
		attachmentCount,
		clearAttachments,
		1,
		&clearRect
	));
}

/* Vulkan: Sampler State */

static VkSampler VULKAN_INTERNAL_FetchSamplerState(
	VulkanRenderer *renderer,
	FNA3D_SamplerState *samplerState,
	uint32_t levelCount
) {
	VkSamplerCreateInfo createInfo;
	VkSampler state;
	VkResult result;

	PackedState hash = GetPackedSamplerState(*samplerState);
	state = SamplerStateHashArray_Fetch(
		&renderer->samplerStateArray,
		hash
	);
	if (state != VK_NULL_HANDLE)
	{
		return state;
	}

	createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	createInfo.pNext = NULL;
	createInfo.flags = 0;
	createInfo.magFilter = XNAToVK_MagFilter[samplerState->filter];
	createInfo.minFilter = XNAToVK_MinFilter[samplerState->filter];
	createInfo.mipmapMode = XNAToVK_MipFilter[samplerState->filter];
	createInfo.addressModeU = XNAToVK_SamplerAddressMode[
		samplerState->addressU
	];
	createInfo.addressModeV = XNAToVK_SamplerAddressMode[
		samplerState->addressV
	];
	createInfo.addressModeW = XNAToVK_SamplerAddressMode[
		samplerState->addressW
	];
	createInfo.mipLodBias = samplerState->mipMapLevelOfDetailBias;
	createInfo.anisotropyEnable = (samplerState->filter == FNA3D_TEXTUREFILTER_ANISOTROPIC);
	createInfo.maxAnisotropy = SDL_min(
		(float) SDL_max(1, samplerState->maxAnisotropy),
		renderer->physicalDeviceProperties.properties.limits.maxSamplerAnisotropy
	);
	createInfo.compareEnable = 0;
	createInfo.compareOp = 0;
	createInfo.minLod = (float) samplerState->maxMipLevel;
	createInfo.maxLod = VK_LOD_CLAMP_NONE;
	createInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	createInfo.unnormalizedCoordinates = 0;

	result = renderer->vkCreateSampler(
		renderer->logicalDevice,
		&createInfo,
		NULL,
		&state
	);
	VULKAN_ERROR_CHECK(result, vkCreateSampler, 0)

	SamplerStateHashArray_Insert(
		&renderer->samplerStateArray,
		hash,
		state
	);

	return state;
}

/* Renderer Implementation */

/* Quit */

static void VULKAN_DestroyDevice(FNA3D_Device *device)
{
	VulkanRenderer *renderer = (VulkanRenderer*) device->driverData;
	ShaderResources *shaderResources;
	PipelineHashArray hashArray;
	int32_t i, j;
	VkResult pipelineCacheResult;
	size_t pipelineCacheSize;
	uint8_t *pipelineCacheData;
	SDL_RWops *pipelineCacheFile;
	const char *pipelineCacheFileName;

	VULKAN_INTERNAL_FlushCommands(renderer, 1);

	FNA3D_CommandBuffer_Finish(renderer->commandBuffers);

	/* Save the pipeline cache to disk */

	pipelineCacheResult = renderer->vkGetPipelineCacheData(
		renderer->logicalDevice,
		renderer->pipelineCache,
		&pipelineCacheSize,
		NULL
	);

	if (pipelineCacheResult == VK_SUCCESS)
	{
		pipelineCacheFileName = SDL_GetHint("FNA3D_VULKAN_PIPELINE_CACHE_FILE_NAME");
		if (pipelineCacheFileName == NULL)
		{
			pipelineCacheFileName = DEFAULT_PIPELINE_CACHE_FILE_NAME;
		}
		if (pipelineCacheFileName[0] == '\0')
		{
			/* For intentionally empty file names, assume caching is disabled */
			pipelineCacheFile = NULL;
		}
		else
		{
			pipelineCacheFile = SDL_RWFromFile(pipelineCacheFileName, "wb");
		}

		if (pipelineCacheFile != NULL)
		{
			pipelineCacheData = SDL_malloc(pipelineCacheSize);

			renderer->vkGetPipelineCacheData(
				renderer->logicalDevice,
				renderer->pipelineCache,
				&pipelineCacheSize,
				pipelineCacheData
			);

			pipelineCacheFile->write(
				pipelineCacheFile,
				pipelineCacheData,
				sizeof(uint8_t),
				pipelineCacheSize
			);
			pipelineCacheFile->close(pipelineCacheFile);

			SDL_free(pipelineCacheData);
		}
		else
		{
			FNA3D_LogWarn("Could not open pipeline cache file for writing!");
		}
	}
	else
	{
		FNA3D_LogWarn("vkGetPipelineCacheData: %s", VkErrorMessages(pipelineCacheResult));
		FNA3D_LogWarn("Error getting data from pipeline cache, aborting save!");
	}

	/* Clean up! */

	VULKAN_INTERNAL_DestroyBuffer(renderer, renderer->dummyVertUniformBuffer);
	VULKAN_INTERNAL_DestroyBuffer(renderer, renderer->dummyFragUniformBuffer);

	VULKAN_INTERNAL_DestroyTexture(renderer, renderer->dummyVertTexture);
	VULKAN_INTERNAL_DestroyTexture(renderer, renderer->dummyVertTexture3D);
	VULKAN_INTERNAL_DestroyTexture(renderer, renderer->dummyVertTextureCube);

	VULKAN_INTERNAL_DestroyTexture(renderer, renderer->dummyFragTexture);
	VULKAN_INTERNAL_DestroyTexture(renderer, renderer->dummyFragTexture3D);
	VULKAN_INTERNAL_DestroyTexture(renderer, renderer->dummyFragTextureCube);

	MOJOSHADER_vkDestroyContext(renderer->mojoshaderContext);
	VULKAN_INTERNAL_DestroyFauxBackbuffer(renderer);

	renderer->vkDestroySemaphore(
		renderer->logicalDevice,
		renderer->defragSemaphore,
		NULL
	);

	renderer->vkDestroyQueryPool(
		renderer->logicalDevice,
		renderer->queryPool,
		NULL
	);

	FNA3D_DestroyCommandBufferManager(renderer->commandBuffers);
	renderer->vkDestroyCommandPool(
		renderer->logicalDevice,
		renderer->commandPool,
		NULL
	);

	for (i = 0; i < NUM_PIPELINE_HASH_BUCKETS; i += 1)
	{
		hashArray = renderer->pipelineHashTable.buckets[i];
		for (j = 0; j < hashArray.count; j += 1)
		{
			renderer->vkDestroyPipeline(
				renderer->logicalDevice,
				renderer->pipelineHashTable.buckets[i].elements[j].value,
				NULL
			);
		}
		if (hashArray.elements != NULL)
		{
			SDL_free(hashArray.elements);
		}
	}

	for (i = 0; i < NUM_SHADER_RESOURCES_BUCKETS; i += 1)
	{
		for (j = 0; j < renderer->shaderResourcesHashTable.buckets[i].count; j += 1)
		{
			shaderResources = renderer->shaderResourcesHashTable.buckets[i].elements[j].value;
			ShaderResources_Destroy(renderer, shaderResources);
		}

		SDL_free(renderer->shaderResourcesHashTable.buckets[i].elements);
	}

	renderer->vkDestroyDescriptorPool(
		renderer->logicalDevice,
		renderer->uniformBufferDescriptorPool,
		NULL
	);

	for (i = 0; i < NUM_DESCRIPTOR_SET_LAYOUT_BUCKETS; i += 1)
	{
		for (j = 0; j < renderer->descriptorSetLayoutTable.buckets[i].count; j += 1)
		{
			renderer->vkDestroyDescriptorSetLayout(
				renderer->logicalDevice,
				renderer->descriptorSetLayoutTable.buckets[i].elements[j].value,
				NULL
			);
		}

		SDL_free(renderer->descriptorSetLayoutTable.buckets[i].elements);
	}

	renderer->vkDestroyDescriptorSetLayout(
		renderer->logicalDevice,
		renderer->vertexUniformBufferDescriptorSetLayout,
		NULL
	);

	renderer->vkDestroyDescriptorSetLayout(
		renderer->logicalDevice,
		renderer->fragUniformBufferDescriptorSetLayout,
		NULL
	);

	for (i = 0; i < NUM_PIPELINE_LAYOUT_BUCKETS; i += 1)
	{
		for (j = 0; j < renderer->pipelineLayoutTable.buckets[i].count; j += 1)
		{
			renderer->vkDestroyPipelineLayout(
				renderer->logicalDevice,
				renderer->pipelineLayoutTable.buckets[i].elements[j].value,
				NULL
			);
		}

		SDL_free(renderer->pipelineLayoutTable.buckets[i].elements);
	}

	renderer->vkDestroyPipelineCache(
		renderer->logicalDevice,
		renderer->pipelineCache,
		NULL
	);

	for (i = 0; i < renderer->renderPassArray.count; i += 1)
	{
		renderer->vkDestroyRenderPass(
			renderer->logicalDevice,
			renderer->renderPassArray.elements[i].value,
			NULL
		);
	}

	for (i = 0; i < renderer->samplerStateArray.count; i += 1)
	{
		renderer->vkDestroySampler(
			renderer->logicalDevice,
			renderer->samplerStateArray.elements[i].value,
			NULL
		);
	}

	renderer->vkDestroySampler(
		renderer->logicalDevice,
		renderer->dummyVertSamplerState,
		NULL
	);
	renderer->vkDestroySampler(
		renderer->logicalDevice,
		renderer->dummyVertSampler3DState,
		NULL
	);
	renderer->vkDestroySampler(
		renderer->logicalDevice,
		renderer->dummyVertSamplerCubeState,
		NULL
	);

	renderer->vkDestroySampler(
		renderer->logicalDevice,
		renderer->dummyFragSamplerState,
		NULL
	);
	renderer->vkDestroySampler(
		renderer->logicalDevice,
		renderer->dummyFragSampler3DState,
		NULL
	);
	renderer->vkDestroySampler(
		renderer->logicalDevice,
		renderer->dummyFragSamplerCubeState,
		NULL
	);

	for (j = renderer->swapchainDataCount - 1; j >= 0; j -= 1)
	{
		VULKAN_INTERNAL_DestroySwapchain(renderer, renderer->swapchainDatas[j]->windowHandle);
	}
	SDL_free(renderer->swapchainDatas);

	FNA3D_DestroyMemoryAllocator(renderer->allocator);

	SDL_DestroyMutex(renderer->passLock);
	SDL_DestroyMutex(renderer->disposeLock);

	renderer->vkDestroyDevice(renderer->logicalDevice, NULL);
	renderer->vkDestroyInstance(renderer->instance, NULL);

	SDL_free(renderer->defragmentedBuffersToDestroy);
	SDL_free(renderer->defragmentedImagesToDestroy);
	SDL_free(renderer->defragmentedImageViewsToDestroy);

	SDL_free(renderer->renderPassArray.elements);
	SDL_free(renderer->samplerStateArray.elements);
	SDL_free(renderer->vertexBufferBindingsCache.elements);

	SDL_free(renderer);
	SDL_free(device);
}

/* Presentation */

static void VULKAN_SwapBuffers(
	FNA3D_Renderer *driverData,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;

	/* Perform any pending clears before presenting */
	if (	renderer->shouldClearColorOnBeginPass ||
		renderer->shouldClearDepthOnBeginPass ||
		renderer->shouldClearStencilOnBeginPass	)
	{
		VULKAN_INTERNAL_BeginRenderPass(renderer);
	}

	VULKAN_INTERNAL_FlushCommandsAndPresent(
		renderer,
		sourceRectangle,
		destinationRectangle,
		overrideWindowHandle
	);

	renderer->needNewRenderPass = 1;
}

/* Drawing */

static void VULKAN_Clear(
	FNA3D_Renderer *driverData,
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	uint8_t clearColor = (options & FNA3D_CLEAROPTIONS_TARGET) == FNA3D_CLEAROPTIONS_TARGET;
	uint8_t clearDepth = (options & FNA3D_CLEAROPTIONS_DEPTHBUFFER) == FNA3D_CLEAROPTIONS_DEPTHBUFFER;
	uint8_t clearStencil = (options & FNA3D_CLEAROPTIONS_STENCIL) == FNA3D_CLEAROPTIONS_STENCIL;

	if (renderer->renderPassInProgress && renderer->drawCallMadeThisPass && !renderer->needNewRenderPass)
	{
		VULKAN_INTERNAL_MidRenderPassClear(
			renderer,
			color,
			depth,
			stencil,
			clearColor,
			clearDepth,
			clearStencil
		);
	}
	else
	{
		VULKAN_INTERNAL_BeginRenderPassClear(
			renderer,
			color,
			depth,
			stencil,
			clearColor,
			clearDepth,
			clearStencil
		);
	}
}

static void VULKAN_DrawInstancedPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t baseVertex,
	int32_t minVertexIndex,
	int32_t numVertices,
	int32_t startIndex,
	int32_t primitiveCount,
	int32_t instanceCount,
	FNA3D_Buffer *indices,
	FNA3D_IndexElementSize indexElementSize
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *commandBuffer;
	VulkanBuffer *indexBuffer;
	VkDescriptorSet descriptorSets[4];
	MOJOSHADER_vkShader *vertShader, *fragShader;
	ShaderResources *vertShaderResources, *fragShaderResources;
	uint32_t dynamicOffsets[2];

	/* Note that minVertexIndex/numVertices are NOT used! */

	indexBuffer = (VulkanBuffer*) FNA3D_Memory_GetActiveBuffer(
		(FNA3D_BufferContainer*) indices
	);

	FNA3D_CommandBuffer_MarkBufferAsBound(
		renderer->commandBuffers,
		(FNA3D_BufferHandle*) indexBuffer
	);

	if (primitiveType != renderer->currentPrimitiveType)
	{
		renderer->currentPrimitiveType = primitiveType;
		renderer->needNewPipeline = 1;
	}

	VULKAN_INTERNAL_BeginRenderPass(renderer);
	VULKAN_INTERNAL_BindPipeline(renderer);

	commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
		renderer->commandBuffers
	);

	if (renderer->numVertexBindings > 0)
	{
		/* FIXME: State shadowing for vertex buffers? -flibit */
		RECORD_CMD(renderer->vkCmdBindVertexBuffers(
			commandBuffer->commandBuffer,
			0,
			renderer->numVertexBindings,
			renderer->boundVertexBuffers,
			renderer->boundVertexBufferOffsets
		));
	}
	/* FIXME: State shadowing for index buffers? -flibit */
	RECORD_CMD(renderer->vkCmdBindIndexBuffer(
		commandBuffer->commandBuffer,
		indexBuffer->buffer,
		0,
		XNAToVK_IndexType[indexElementSize]
	));

	MOJOSHADER_vkGetBoundShaders(renderer->mojoshaderContext, &vertShader, &fragShader);
	vertShaderResources = VULKAN_INTERNAL_FetchShaderResources(
		renderer,
		vertShader,
		VK_SHADER_STAGE_VERTEX_BIT
	);
	fragShaderResources = VULKAN_INTERNAL_FetchShaderResources(
		renderer,
		fragShader,
		VK_SHADER_STAGE_FRAGMENT_BIT
	);

	if (	renderer->vertexSamplerDescriptorSetDataNeedsUpdate ||
		renderer->fragSamplerDescriptorSetDataNeedsUpdate	)
	{
		VULKAN_INTERNAL_FetchDescriptorSetDataAndOffsets(
			renderer,
			vertShaderResources,
			fragShaderResources,
			descriptorSets,
			dynamicOffsets
		);

		RECORD_CMD(renderer->vkCmdBindDescriptorSets(
			commandBuffer->commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			renderer->currentPipelineLayout,
			0,
			4,
			descriptorSets,
			2,
			dynamicOffsets
		));
	}


	RECORD_CMD(renderer->vkCmdDrawIndexed(
		commandBuffer->commandBuffer,
		PrimitiveVerts(primitiveType, primitiveCount),
		instanceCount,
		startIndex,
		renderer->supportsBaseVertex ? baseVertex : 0,
		0
	));

	renderer->drawCallMadeThisPass = 1;
}

static void VULKAN_DrawIndexedPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t baseVertex,
	int32_t minVertexIndex,
	int32_t numVertices,
	int32_t startIndex,
	int32_t primitiveCount,
	FNA3D_Buffer *indices,
	FNA3D_IndexElementSize indexElementSize
) {
	VULKAN_DrawInstancedPrimitives(
		driverData,
		primitiveType,
		baseVertex,
		minVertexIndex,
		numVertices,
		startIndex,
		primitiveCount,
		1,
		indices,
		indexElementSize
	);
}

static void VULKAN_DrawPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *commandBuffer;
	VkDescriptorSet descriptorSets[4];
	MOJOSHADER_vkShader *vertShader, *fragShader;
	ShaderResources *vertShaderResources, *fragShaderResources;
	uint32_t dynamicOffsets[2];

	if (primitiveType != renderer->currentPrimitiveType)
	{
		renderer->currentPrimitiveType = primitiveType;
		renderer->needNewPipeline = 1;
	}
	VULKAN_INTERNAL_BeginRenderPass(renderer);
	VULKAN_INTERNAL_BindPipeline(renderer);

	commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
		renderer->commandBuffers
	);

	if (renderer->numVertexBindings > 0)
	{
		/* FIXME: State shadowing for vertex buffers? -flibit */
		RECORD_CMD(renderer->vkCmdBindVertexBuffers(
			commandBuffer->commandBuffer,
			0,
			renderer->numVertexBindings,
			renderer->boundVertexBuffers,
			renderer->boundVertexBufferOffsets
		));
	}

	MOJOSHADER_vkGetBoundShaders(renderer->mojoshaderContext, &vertShader, &fragShader);
	vertShaderResources = VULKAN_INTERNAL_FetchShaderResources(
		renderer,
		vertShader,
		VK_SHADER_STAGE_VERTEX_BIT
	);
	fragShaderResources = VULKAN_INTERNAL_FetchShaderResources(
		renderer,
		fragShader,
		VK_SHADER_STAGE_FRAGMENT_BIT
	);

	if (	renderer->vertexSamplerDescriptorSetDataNeedsUpdate ||
		renderer->fragSamplerDescriptorSetDataNeedsUpdate	)
	{
		VULKAN_INTERNAL_FetchDescriptorSetDataAndOffsets(
			renderer,
			vertShaderResources,
			fragShaderResources,
			descriptorSets,
			dynamicOffsets
		);

		RECORD_CMD(renderer->vkCmdBindDescriptorSets(
			commandBuffer->commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			renderer->currentPipelineLayout,
			0,
			4,
			descriptorSets,
			2,
			dynamicOffsets
		));
	}

	RECORD_CMD(renderer->vkCmdDraw(
		commandBuffer->commandBuffer,
		PrimitiveVerts(primitiveType, primitiveCount),
		1,
		vertexStart,
		0
	));

	renderer->drawCallMadeThisPass = 1;
}

/* Mutable Render States */

static void VULKAN_SetViewport(
	FNA3D_Renderer *driverData,
	FNA3D_Viewport *viewport
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;

	if (	viewport->x != renderer->viewport.x ||
		viewport->y != renderer->viewport.y ||
		viewport->w != renderer->viewport.w ||
		viewport->h != renderer->viewport.h ||
		viewport->minDepth != renderer->viewport.minDepth ||
		viewport->maxDepth != renderer->viewport.maxDepth	)
	{
		renderer->viewport = *viewport;
		VULKAN_INTERNAL_SetViewportCommand(renderer);
	}
}

static void VULKAN_SetScissorRect(
	FNA3D_Renderer *driverData,
	FNA3D_Rect *scissor
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;

	if (	scissor->x != renderer->scissorRect.x ||
		scissor->y != renderer->scissorRect.y ||
		scissor->w != renderer->scissorRect.w ||
		scissor->h != renderer->scissorRect.h	)
	{
		renderer->scissorRect = *scissor;
		VULKAN_INTERNAL_SetScissorRectCommand(renderer);
	}
}

static void VULKAN_GetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	SDL_memcpy(
		blendFactor,
		&renderer->blendState.blendFactor,
		sizeof(FNA3D_Color)
	);
}

static void VULKAN_SetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *commandBuffer;
	const float blendConstants[] =
	{
		blendFactor->r,
		blendFactor->g,
		blendFactor->b,
		blendFactor->a
	};

	if (	blendFactor->r != renderer->blendState.blendFactor.r ||
		blendFactor->g != renderer->blendState.blendFactor.g ||
		blendFactor->b != renderer->blendState.blendFactor.b ||
		blendFactor->a != renderer->blendState.blendFactor.a	)
	{
		renderer->blendState.blendFactor = *blendFactor;
		renderer->needNewPipeline = 1;

		commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
			renderer->commandBuffers
		);
		RECORD_CMD(renderer->vkCmdSetBlendConstants(
			commandBuffer->commandBuffer,
			blendConstants
		));
	}
}

static int32_t VULKAN_GetMultiSampleMask(FNA3D_Renderer *driverData)
{
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	return renderer->multiSampleMask[0];
}

static void VULKAN_SetMultiSampleMask(FNA3D_Renderer *driverData, int32_t mask)
{
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	if (renderer->debugMode && renderer->multiSampleCount> 32) {
		FNA3D_LogWarn(
			"Using a 32-bit multisample mask for a 64-sample rasterizer."
			" Last 32 bits of the mask will all be 1."
		);
	}
	if (renderer->multiSampleMask[0] != mask)
	{
		renderer->multiSampleMask[0] = mask;
		renderer->needNewPipeline = 1;
	}
}

static int32_t VULKAN_GetReferenceStencil(FNA3D_Renderer *driverData)
{
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	return renderer->stencilRef;
}

static void VULKAN_SetReferenceStencil(FNA3D_Renderer *driverData, int32_t ref)
{
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	if (renderer->stencilRef != ref)
	{
		renderer->stencilRef = ref;
		VULKAN_INTERNAL_SetStencilReferenceValueCommand(renderer);
	}
}

/* Immutable Render States */

static void VULKAN_SetBlendState(
	FNA3D_Renderer *driverData,
	FNA3D_BlendState *blendState
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;

	VULKAN_SetBlendFactor(
		driverData,
		&blendState->blendFactor
	);

	VULKAN_SetMultiSampleMask(
		driverData,
		blendState->multiSampleMask
	);

	if (SDL_memcmp(&renderer->blendState, blendState, sizeof(FNA3D_BlendState)) != 0)
	{
		SDL_memcpy(&renderer->blendState, blendState, sizeof(FNA3D_BlendState));
		renderer->needNewPipeline = 1;
	}
}

static void VULKAN_SetDepthStencilState(
	FNA3D_Renderer *driverData,
	FNA3D_DepthStencilState *depthStencilState
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;

	/* TODO: Arrange these checks in an optimized priority */
	if (	renderer->depthStencilState.depthBufferEnable != depthStencilState->depthBufferEnable ||
		renderer->depthStencilState.depthBufferWriteEnable != depthStencilState->depthBufferWriteEnable ||
		renderer->depthStencilState.depthBufferFunction != depthStencilState->depthBufferFunction ||
		renderer->depthStencilState.stencilEnable != depthStencilState->stencilEnable ||
		renderer->depthStencilState.stencilMask != depthStencilState->stencilMask ||
		renderer->depthStencilState.stencilWriteMask != depthStencilState->stencilWriteMask ||
		renderer->depthStencilState.twoSidedStencilMode != depthStencilState->twoSidedStencilMode ||
		renderer->depthStencilState.stencilFail != depthStencilState->stencilFail ||
		renderer->depthStencilState.stencilDepthBufferFail != depthStencilState->stencilDepthBufferFail ||
		renderer->depthStencilState.stencilPass != depthStencilState->stencilPass ||
		renderer->depthStencilState.stencilFunction != depthStencilState->stencilFunction ||
		renderer->depthStencilState.ccwStencilFail != depthStencilState->ccwStencilFail ||
		renderer->depthStencilState.ccwStencilDepthBufferFail != depthStencilState->ccwStencilDepthBufferFail ||
		renderer->depthStencilState.ccwStencilPass != depthStencilState->ccwStencilPass ||
		renderer->depthStencilState.ccwStencilFunction != depthStencilState->ccwStencilFunction ||
		renderer->depthStencilState.referenceStencil != depthStencilState->referenceStencil	)
	{
		renderer->needNewPipeline = 1;

		SDL_memcpy(
			&renderer->depthStencilState,
			depthStencilState,
			sizeof(FNA3D_DepthStencilState)
		);
	}

	/* Dynamic state */
	VULKAN_SetReferenceStencil(
		driverData,
		depthStencilState->referenceStencil
	);
}

static void VULKAN_ApplyRasterizerState(
	FNA3D_Renderer *driverData,
	FNA3D_RasterizerState *rasterizerState
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	float realDepthBias;

	if (rasterizerState->scissorTestEnable != renderer->rasterizerState.scissorTestEnable)
	{
		renderer->rasterizerState.scissorTestEnable = rasterizerState->scissorTestEnable;
		VULKAN_INTERNAL_SetScissorRectCommand(renderer);
		renderer->needNewPipeline = 1;
	}

	realDepthBias = rasterizerState->depthBias * XNAToVK_DepthBiasScale(
		XNAToVK_DepthFormat(
			renderer,
			renderer->currentDepthFormat
		)
	);

	if (	realDepthBias != renderer->rasterizerState.depthBias ||
		rasterizerState->slopeScaleDepthBias != renderer->rasterizerState.slopeScaleDepthBias	)
	{
		renderer->rasterizerState.depthBias = realDepthBias;
		renderer->rasterizerState.slopeScaleDepthBias = rasterizerState->slopeScaleDepthBias;
		VULKAN_INTERNAL_SetDepthBiasCommand(renderer);
		renderer->needNewPipeline = 1;
	}

	if (	rasterizerState->cullMode != renderer->rasterizerState.cullMode ||
		rasterizerState->fillMode != renderer->rasterizerState.fillMode ||
		rasterizerState->multiSampleAntiAlias != renderer->rasterizerState.multiSampleAntiAlias	)
	{
		renderer->rasterizerState.cullMode = rasterizerState->cullMode;
		renderer->rasterizerState.fillMode = rasterizerState->fillMode;
		renderer->rasterizerState.multiSampleAntiAlias = rasterizerState->multiSampleAntiAlias;
		renderer->needNewPipeline = 1;
	}
}

static void VULKAN_VerifySampler(
	FNA3D_Renderer *driverData,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *vulkanTexture = (VulkanTexture*) texture;
	VkSampler vkSamplerState;

	if (texture == NULL)
	{
		if (renderer->textures[index] != &NullTexture)
		{
			renderer->textures[index] = &NullTexture;
			renderer->textureNeedsUpdate[index] = 1;
		}

		if (renderer->samplers[index] == VK_NULL_HANDLE)
		{
			vkSamplerState = VULKAN_INTERNAL_FetchSamplerState(
				renderer,
				sampler,
				0
			);

			renderer->samplers[index] = vkSamplerState;
			renderer->samplerNeedsUpdate[index] = 1;
		}

		return;
	}

	if (vulkanTexture != renderer->textures[index])
	{
		renderer->textures[index] = vulkanTexture;
		renderer->textureNeedsUpdate[index] = 1;

		if (index >= MAX_TEXTURE_SAMPLERS)
		{
			renderer->vertexSamplerDescriptorSetDataNeedsUpdate = 1;
		}
		else
		{
			renderer->fragSamplerDescriptorSetDataNeedsUpdate = 1;
		}
	}

	vkSamplerState = VULKAN_INTERNAL_FetchSamplerState(
		renderer,
		sampler,
		vulkanTexture->levelCount
	);

	if (vkSamplerState != renderer->samplers[index])
	{
		renderer->samplers[index] = vkSamplerState;
		renderer->samplerNeedsUpdate[index] = 1;

		if (index >= MAX_TEXTURE_SAMPLERS)
		{
			renderer->vertexSamplerDescriptorSetDataNeedsUpdate = 1;
		}
		else
		{
			renderer->fragSamplerDescriptorSetDataNeedsUpdate = 1;
		}
	}
}

static void VULKAN_VerifyVertexSampler(
	FNA3D_Renderer *driverData,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
	VULKAN_VerifySampler(
		driverData,
		MAX_TEXTURE_SAMPLERS + index,
		texture,
		sampler
	);
}

static void VULKAN_ApplyVertexBufferBindings(
	FNA3D_Renderer *driverData,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings,
	uint8_t bindingsUpdated,
	int32_t baseVertex
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	MOJOSHADER_vkShader *vertexShader, *blah;
	int32_t i, bindingsIndex;
	uint32_t hash;
	void* bindingsResult;
	FNA3D_VertexBufferBinding *src, *dst;
	VulkanBuffer *vertexBuffer;
	VkDeviceSize offset;

	if (renderer->supportsBaseVertex)
	{
		baseVertex = 0;
	}

	/* Check VertexBufferBindings */
	MOJOSHADER_vkGetBoundShaders(renderer->mojoshaderContext, &vertexShader, &blah);
	bindingsResult = PackedVertexBufferBindingsArray_Fetch(
		renderer->vertexBufferBindingsCache,
		bindings,
		numBindings,
		vertexShader,
		&bindingsIndex,
		&hash
	);
	if (bindingsResult == NULL)
	{
		PackedVertexBufferBindingsArray_Insert(
			&renderer->vertexBufferBindingsCache,
			bindings,
			numBindings,
			vertexShader,
			(void*) 69420
		);
	}

	if (bindingsUpdated)
	{
		renderer->numVertexBindings = numBindings;
		for (i = 0; i < renderer->numVertexBindings; i += 1)
		{
			src = &bindings[i];
			dst = &renderer->vertexBindings[i];
			dst->vertexBuffer = src->vertexBuffer;
			dst->vertexOffset = src->vertexOffset;
			dst->instanceFrequency = src->instanceFrequency;
			dst->vertexDeclaration.vertexStride = src->vertexDeclaration.vertexStride;
			dst->vertexDeclaration.elementCount = src->vertexDeclaration.elementCount;
			SDL_memcpy(
				dst->vertexDeclaration.elements,
				src->vertexDeclaration.elements,
				sizeof(FNA3D_VertexElement) * src->vertexDeclaration.elementCount
			);
		}
	}

	if (bindingsIndex != renderer->currentVertexBufferBindingsIndex)
	{
		renderer->currentVertexBufferBindingsIndex = bindingsIndex;
		renderer->needNewPipeline = 1;
	}

	for (i = 0; i < numBindings; i += 1)
	{
		vertexBuffer = (VulkanBuffer*) FNA3D_Memory_GetActiveBuffer(
			(FNA3D_BufferContainer*) bindings[i].vertexBuffer
		);
		if (vertexBuffer == NULL)
		{
			continue;
		}

		offset =
			(bindings[i].vertexOffset + baseVertex) *
			bindings[i].vertexDeclaration.vertexStride
		;

		renderer->boundVertexBuffers[i] = vertexBuffer->buffer;
		renderer->boundVertexBufferOffsets[i] = offset;

		FNA3D_CommandBuffer_MarkBufferAsBound(
			renderer->commandBuffers,
			(FNA3D_BufferHandle*) vertexBuffer
		);
	}
}

/* Render Targets */

static void VULKAN_SetRenderTargets(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *depthStencilBuffer,
	FNA3D_DepthFormat depthFormat,
	uint8_t preserveTargetContents
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanColorBuffer *cb;
	VulkanTexture *tex;
	int32_t i;

	/* Perform any pending clears before switching render targets */
	if (	renderer->shouldClearColorOnBeginPass ||
		renderer->shouldClearDepthOnBeginPass ||
		renderer->shouldClearStencilOnBeginPass	)
	{
		VULKAN_INTERNAL_BeginRenderPass(renderer);
	}

	renderer->nextRenderPassPreserveTargetContents = preserveTargetContents;

	for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i += 1)
	{
		renderer->nextRenderPassColorAttachments[i] = NULL;
		renderer->nextRenderPassColorMultiSampleAttachments[i] = NULL;
	}

	renderer->nextRenderPassDepthStencilAttachment = NULL;
	renderer->nextRenderPassMultiSampleCount = renderer->fauxBackbufferMultiSampleCount;

	if (numRenderTargets <= 0)
	{
		renderer->nextRenderPassColorAttachments[0] = renderer->fauxBackbufferColor.handle;
		renderer->nextRenderPassAttachmentCubeFaces[0] = (FNA3D_CubeMapFace) 0;
		renderer->nextRenderPassColorAttachmentCount = 1;

		if (renderer->fauxBackbufferMultiSampleCount > 1)
		{
			renderer->nextRenderPassColorMultiSampleAttachments[0] =
				renderer->fauxBackbufferMultiSampleColor;
		}
		renderer->nextRenderPassDepthStencilAttachment =
			renderer->fauxBackbufferDepthStencil.handle;
	}
	else
	{
		for (i = 0; i < numRenderTargets; i += 1)
		{
			renderer->nextRenderPassAttachmentCubeFaces[i] = (
				renderTargets[i].type == FNA3D_RENDERTARGET_TYPE_CUBE ?
					renderTargets[i].cube.face :
					(FNA3D_CubeMapFace) 0
			);

			if (renderTargets[i].colorBuffer != NULL)
			{
				cb = ((VulkanRenderbuffer*) renderTargets[i].colorBuffer)->colorBuffer;
				renderer->nextRenderPassColorAttachments[i] = cb->handle;
				renderer->nextRenderPassMultiSampleCount = cb->multiSampleCount;

				if (cb->multiSampleCount > 0)
				{
					renderer->nextRenderPassColorMultiSampleAttachments[i] = cb->multiSampleTexture;
				}
			}
			else
			{
				tex = (VulkanTexture*) renderTargets[i].texture;
				renderer->nextRenderPassColorAttachments[i] = tex;
				renderer->nextRenderPassMultiSampleCount = 0;
			}
		}

		renderer->nextRenderPassColorAttachmentCount = numRenderTargets;

		/* update depth stencil buffer */

		if (depthStencilBuffer != NULL)
		{
			renderer->nextRenderPassDepthStencilAttachment = ((VulkanRenderbuffer*) depthStencilBuffer)->depthBuffer->handle;
			renderer->nextRenderPassDepthFormat = depthFormat;
		}
		else
		{
			renderer->nextRenderPassDepthStencilAttachment = NULL;
		}
	}

	renderer->needNewRenderPass = 1;
}

static void VULKAN_ResolveTarget(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *target
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *vulkanTexture = (VulkanTexture*) target->texture;
	VulkanCommandBuffer *commandBuffer;
	int32_t layerCount = (target->type == FNA3D_RENDERTARGET_TYPE_CUBE) ? 6 : 1;
	int32_t level;
	VulkanResourceAccessType *levelAccessType;
	VkImageBlit blit;

	/* The target is resolved during the render pass */

	/* If the target has mipmaps, regenerate them now */
	if (target->levelCount > 1)
	{
		VULKAN_INTERNAL_MaybeEndRenderPass(renderer);

		/* Store the original image layout... */
		levelAccessType = SDL_stack_alloc(
			VulkanResourceAccessType,
			target->levelCount
		);
		for (level = 0; level < target->levelCount; level += 1)
		{
			levelAccessType[level] = vulkanTexture->resourceAccessType;
		}

		/* Blit each mip sequentially. Barriers, barriers everywhere! */
		for (level = 1; level < target->levelCount; level += 1)
		{
			blit.srcOffsets[0].x = 0;
			blit.srcOffsets[0].y = 0;
			blit.srcOffsets[0].z = 0;

			blit.srcOffsets[1].x = vulkanTexture->dimensions.width >> (level - 1);
			blit.srcOffsets[1].y = vulkanTexture->dimensions.height >> (level - 1);
			blit.srcOffsets[1].z = 1;

			blit.dstOffsets[0].x = 0;
			blit.dstOffsets[0].y = 0;
			blit.dstOffsets[0].z = 0;

			blit.dstOffsets[1].x = vulkanTexture->dimensions.width >> level;
			blit.dstOffsets[1].y = vulkanTexture->dimensions.height >> level;
			blit.dstOffsets[1].z = 1;

			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = layerCount;
			blit.srcSubresource.mipLevel = level - 1;

			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = layerCount;
			blit.dstSubresource.mipLevel = level;

			VULKAN_INTERNAL_ImageMemoryBarrier(
				renderer,
				RESOURCE_ACCESS_TRANSFER_READ,
				VK_IMAGE_ASPECT_COLOR_BIT,
				0,
				layerCount,
				level - 1,
				1,
				0,
				vulkanTexture->image,
				&levelAccessType[level - 1]
			);

			VULKAN_INTERNAL_ImageMemoryBarrier(
				renderer,
				RESOURCE_ACCESS_TRANSFER_WRITE,
				VK_IMAGE_ASPECT_COLOR_BIT,
				0,
				layerCount,
				level,
				1,
				1,
				vulkanTexture->image,
				&levelAccessType[level]
			);

			commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
				renderer->commandBuffers
			);
			RECORD_CMD(renderer->vkCmdBlitImage(
				commandBuffer->commandBuffer,
				vulkanTexture->image,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				vulkanTexture->image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&blit,
				VK_FILTER_LINEAR
			));
		}

		/* Transition final level to READ */
		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			RESOURCE_ACCESS_TRANSFER_READ,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			layerCount,
			target->levelCount - 1,
			1,
			1,
			vulkanTexture->image,
			&levelAccessType[target->levelCount - 1]
		);

		/* The whole texture is in READ layout now, so set the access type on the texture */
		vulkanTexture->resourceAccessType = RESOURCE_ACCESS_TRANSFER_READ;

		/* Transition to sampler read if necessary */
		if (vulkanTexture->imageCreateInfo.usage & VK_IMAGE_USAGE_SAMPLED_BIT)
		{
			VULKAN_INTERNAL_ImageMemoryBarrier(
				renderer,
				RESOURCE_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE,
				VK_IMAGE_ASPECT_COLOR_BIT,
				0,
				layerCount,
				0,
				target->levelCount,
				0,
				vulkanTexture->image,
				&vulkanTexture->resourceAccessType
			);
		}

		SDL_stack_free(levelAccessType);
	}
}

/* Backbuffer Functions */

static void VULKAN_ResetBackbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_PresentationParameters *presentationParameters
) {
	int32_t i;
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	uint8_t recreateSwapchains =
		(presentationParameters->backBufferWidth != renderer->fauxBackbufferWidth ||
		presentationParameters->backBufferHeight != renderer->fauxBackbufferHeight);

	VULKAN_INTERNAL_FlushCommands(renderer, 1);

	VULKAN_INTERNAL_DestroyFauxBackbuffer(renderer);
	VULKAN_INTERNAL_CreateFauxBackbuffer(
		renderer,
		presentationParameters
	);

	VULKAN_INTERNAL_FlushCommands(renderer, 1);

	if (recreateSwapchains)
	{
		for (i = renderer->swapchainDataCount - 1; i >= 0; i -= 1)
		{
			VULKAN_INTERNAL_RecreateSwapchain(renderer, renderer->swapchainDatas[i]->windowHandle);
		}
	}
}

static void VULKAN_ReadBackbuffer(
	FNA3D_Renderer *driverData,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	void* data,
	int32_t dataLength
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;

	VULKAN_INTERNAL_GetTextureData(
		driverData,
		(FNA3D_Texture*) renderer->fauxBackbufferColor.handle,
		x,
		y,
		w,
		h,
		0,
		0,
		data,
		dataLength
	);
}

static void VULKAN_GetBackbufferSize(
	FNA3D_Renderer *driverData,
	int32_t *w,
	int32_t *h
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	*w = renderer->fauxBackbufferWidth;
	*h = renderer->fauxBackbufferHeight;
}

static FNA3D_SurfaceFormat VULKAN_GetBackbufferSurfaceFormat(
	FNA3D_Renderer *driverData
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	return renderer->fauxBackbufferColor.handle->colorFormat;
}

static FNA3D_DepthFormat VULKAN_GetBackbufferDepthFormat(
	FNA3D_Renderer *driverData
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	if (renderer->fauxBackbufferDepthStencil.handle == NULL)
	{
		return FNA3D_DEPTHFORMAT_NONE;
	}
	return renderer->fauxBackbufferDepthStencil.handle->depthStencilFormat;
}

static int32_t VULKAN_GetBackbufferMultiSampleCount(FNA3D_Renderer *driverData)
{
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	return renderer->fauxBackbufferMultiSampleCount;
}

/* Textures */

static FNA3D_Texture* VULKAN_CreateTexture2D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *result;
	uint32_t usageFlags = (
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT
	);

	result = (VulkanTexture*) SDL_malloc(sizeof(VulkanTexture));

	if (isRenderTarget)
	{
		usageFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	VULKAN_INTERNAL_CreateTexture(
		renderer,
		width,
		height,
		1,
		0,
		isRenderTarget,
		VK_SAMPLE_COUNT_1_BIT,
		levelCount,
		XNAToVK_SurfaceFormat[format],
		XNAToVK_SurfaceSwizzle[format],
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_TYPE_2D,
		usageFlags,
		result
	);
	result->colorFormat = format;

	return (FNA3D_Texture*) result;
}

static FNA3D_Texture* VULKAN_CreateTexture3D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t depth,
	int32_t levelCount
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *result;
	uint32_t usageFlags = (
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT
	);

	result = (VulkanTexture*) SDL_malloc(sizeof(VulkanTexture));

	VULKAN_INTERNAL_CreateTexture(
		renderer,
		width,
		height,
		depth,
		0,
		0,
		VK_SAMPLE_COUNT_1_BIT,
		levelCount,
		XNAToVK_SurfaceFormat[format],
		XNAToVK_SurfaceSwizzle[format],
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_TYPE_3D,
		usageFlags,
		result
	);
	result->colorFormat = format;

	return (FNA3D_Texture*) result;
}

static FNA3D_Texture* VULKAN_CreateTextureCube(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t size,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *result;
	uint32_t usageFlags = (
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT
	);

	result = (VulkanTexture*) SDL_malloc(sizeof(VulkanTexture));

	if (isRenderTarget)
	{
		usageFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	VULKAN_INTERNAL_CreateTexture(
		renderer,
		size,
		size,
		1,
		1,
		isRenderTarget,
		VK_SAMPLE_COUNT_1_BIT,
		levelCount,
		XNAToVK_SurfaceFormat[format],
		XNAToVK_SurfaceSwizzle[format],
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_TYPE_2D,
		usageFlags,
		result
	);
	result->colorFormat = format;

	return (FNA3D_Texture*) result;
}

static void VULKAN_AddDisposeTexture(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *vulkanTexture = (VulkanTexture*) texture;
	uint32_t i;

	for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i += 1)
	{
		if (renderer->colorAttachments[i] != NULL)
		{
			if (vulkanTexture->view == renderer->colorAttachments[i]->rtViews[renderer->attachmentCubeFaces[i]])
			{
				renderer->colorAttachments[i] = NULL;
			}
		}
	}

	for (i = 0; i < TEXTURE_COUNT; i += 1)
	{
		if (vulkanTexture == renderer->textures[i])
		{
			renderer->textures[i] = &NullTexture;
			renderer->textureNeedsUpdate[i] = 1;
		}
	}

	/* Queue texture for destruction */
	FNA3D_CommandBuffer_AddDisposeTexture(
		renderer->commandBuffers,
		(FNA3D_Texture*) vulkanTexture
	);
}

static void VULKAN_INTERNAL_SetTextureData(
	VulkanRenderer *renderer,
	VulkanTexture *texture,
	int32_t x,
	int32_t y,
	int32_t z,
	int32_t w,
	int32_t h,
	int32_t d,
	int32_t level,
	int32_t layer,
	void *data,
	int32_t dataLength
) {
	VulkanCommandBuffer *commandBuffer;
	VkBufferImageCopy imageCopy;
	int32_t uploadLength = BytesPerImage(w, h, texture->colorFormat) * d;
	int32_t copyLength = SDL_min(dataLength, uploadLength);
	VulkanBuffer *transferBuffer;
	VkDeviceSize offset;
	int32_t bufferRowLength = w;
	int32_t bufferImageHeight = h;
	int32_t blockSize = Texture_GetBlockSize(texture->colorFormat);

	if (blockSize > 1)
	{
		bufferRowLength = (bufferRowLength + blockSize - 1) & ~(blockSize - 1);
		bufferImageHeight = (bufferImageHeight + blockSize - 1) & ~(blockSize - 1);
	}

	if (dataLength > uploadLength)
	{
		FNA3D_LogWarn(
			"dataLength %i too long for texture upload, w: %i, h: %i, max upload length: %i",
			dataLength,
			w,
			h,
			uploadLength
		);
	}

	VULKAN_INTERNAL_MaybeEndRenderPass(renderer);

	SDL_LockMutex(renderer->passLock);
	FNA3D_CommandBuffer_LockForTransfer(renderer->commandBuffers);

	VULKAN_INTERNAL_CopyToTransferBuffer(
		renderer,
		data,
		uploadLength,
		copyLength,
		&transferBuffer,
		&offset,
		(VkDeviceSize)Texture_GetFormatSize(texture->colorFormat)
	);

	VULKAN_INTERNAL_BufferMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_READ,
		transferBuffer->buffer,
		&transferBuffer->resourceAccessType
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		texture->layerCount,
		0,
		texture->levelCount,
		0,
		texture->image,
		&texture->resourceAccessType
	);

	/* Block compressed texture buffers must be at least 1 block in width and height */
	bufferRowLength = SDL_max(blockSize, bufferRowLength);
	bufferImageHeight = SDL_max(blockSize, bufferImageHeight);

	imageCopy.imageExtent.width = w;
	imageCopy.imageExtent.height = h;
	imageCopy.imageExtent.depth = d;
	imageCopy.imageOffset.x = x;
	imageCopy.imageOffset.y = y;
	imageCopy.imageOffset.z = z;
	imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageCopy.imageSubresource.baseArrayLayer = layer;
	imageCopy.imageSubresource.layerCount = 1;
	imageCopy.imageSubresource.mipLevel = level;
	imageCopy.bufferOffset = offset;
	imageCopy.bufferRowLength = bufferRowLength;
	imageCopy.bufferImageHeight = bufferImageHeight;

	commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
		renderer->commandBuffers
	);
	RECORD_CMD(renderer->vkCmdCopyBufferToImage(
		commandBuffer->commandBuffer,
		transferBuffer->buffer,
		texture->image,
		AccessMap[texture->resourceAccessType].imageLayout,
		1,
		&imageCopy
	));

	if (texture->imageCreateInfo.usage & VK_IMAGE_USAGE_SAMPLED_BIT)
	{
		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			RESOURCE_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			texture->layerCount,
			0,
			texture->levelCount,
			0,
			texture->image,
			&texture->resourceAccessType
		);
	}

	FNA3D_CommandBuffer_UnlockFromTransfer(renderer->commandBuffers);
	SDL_UnlockMutex(renderer->passLock);
}

static void VULKAN_SetTextureData2D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	VULKAN_INTERNAL_SetTextureData(
		(VulkanRenderer*) driverData,
		(VulkanTexture*) texture,
		x,
		y,
		0,
		w,
		h,
		1,
		level,
		0,
		data,
		dataLength
	);
}

static void VULKAN_SetTextureData3D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t z,
	int32_t w,
	int32_t h,
	int32_t d,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	VULKAN_INTERNAL_SetTextureData(
		(VulkanRenderer*) driverData,
		(VulkanTexture*) texture,
		x,
		y,
		z,
		w,
		h,
		d,
		level,
		0,
		data,
		dataLength
	);
}

static void VULKAN_SetTextureDataCube(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	FNA3D_CubeMapFace cubeMapFace,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	VULKAN_INTERNAL_SetTextureData(
		(VulkanRenderer*) driverData,
		(VulkanTexture*) texture,
		x,
		y,
		0,
		w,
		h,
		1,
		level,
		cubeMapFace,
		data,
		dataLength
	);
}

static void VULKAN_SetTextureDataYUV(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *y,
	FNA3D_Texture *u,
	FNA3D_Texture *v,
	int32_t yWidth,
	int32_t yHeight,
	int32_t uvWidth,
	int32_t uvHeight,
	void* data,
	int32_t dataLength
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *commandBuffer;
	VulkanTexture *tex;
	VulkanBuffer *transferBuffer;
	int32_t yDataLength = BytesPerImage(yWidth, yHeight, FNA3D_SURFACEFORMAT_ALPHA8);
	int32_t uvDataLength = BytesPerImage(uvWidth, uvHeight, FNA3D_SURFACEFORMAT_ALPHA8);
	int32_t uploadLength = yDataLength + uvDataLength * 2;
	int32_t copyLength = SDL_min(dataLength, uploadLength);
	VkBufferImageCopy imageCopy;
	VkDeviceSize offset;

	if (dataLength > uploadLength)
	{
		FNA3D_LogWarn(
			"dataLength %i too long for texture upload, max upload length: %i",
			dataLength,
			uploadLength
		);
	}

	VULKAN_INTERNAL_MaybeEndRenderPass(renderer);

	SDL_LockMutex(renderer->passLock);
	FNA3D_CommandBuffer_LockForTransfer(renderer->commandBuffers);

	VULKAN_INTERNAL_CopyToTransferBuffer(
		renderer,
		data,
		uploadLength,
		copyLength,
		&transferBuffer,
		&offset,
		(VkDeviceSize)Texture_GetFormatSize(FNA3D_SURFACEFORMAT_ALPHA8)
	);

	VULKAN_INTERNAL_BufferMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_READ,
		transferBuffer->buffer,
		&transferBuffer->resourceAccessType
	);

	/* Initialize values that are the same for Y, U, and V */

	imageCopy.imageExtent.depth = 1;
	imageCopy.imageOffset.x = 0;
	imageCopy.imageOffset.y = 0;
	imageCopy.imageOffset.z = 0;
	imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageCopy.imageSubresource.baseArrayLayer = 0;
	imageCopy.imageSubresource.layerCount = 1;
	imageCopy.imageSubresource.mipLevel = 0;

	/* Y */

	tex = (VulkanTexture*) y;

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		tex->layerCount,
		0,
		tex->levelCount,
		0,
		tex->image,
		&tex->resourceAccessType
	);

	imageCopy.imageExtent.width = yWidth;
	imageCopy.imageExtent.height = yHeight;
	imageCopy.bufferOffset = offset;
	imageCopy.bufferRowLength = yWidth;
	imageCopy.bufferImageHeight = yHeight;

	commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
		renderer->commandBuffers
	);
	RECORD_CMD(renderer->vkCmdCopyBufferToImage(
		commandBuffer->commandBuffer,
		transferBuffer->buffer,
		tex->image,
		AccessMap[tex->resourceAccessType].imageLayout,
		1,
		&imageCopy
	));

	if (tex->imageCreateInfo.usage & VK_IMAGE_USAGE_SAMPLED_BIT)
	{
		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			RESOURCE_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			tex->layerCount,
			0,
			tex->levelCount,
			0,
			tex->image,
			&tex->resourceAccessType
		);
	}

	/* These apply to both U and V */

	imageCopy.imageExtent.width = uvWidth;
	imageCopy.imageExtent.height = uvHeight;
	imageCopy.bufferRowLength = uvWidth;
	imageCopy.bufferImageHeight = uvHeight;

	/* U */

	imageCopy.bufferOffset = offset + yDataLength;

	tex = (VulkanTexture*) u;

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		tex->layerCount,
		0,
		tex->levelCount,
		0,
		tex->image,
		&tex->resourceAccessType
	);

	RECORD_CMD(renderer->vkCmdCopyBufferToImage(
		commandBuffer->commandBuffer,
		transferBuffer->buffer,
		tex->image,
		AccessMap[tex->resourceAccessType].imageLayout,
		1,
		&imageCopy
	));

	if (tex->imageCreateInfo.usage & VK_IMAGE_USAGE_SAMPLED_BIT)
	{
		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			RESOURCE_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			tex->layerCount,
			0,
			tex->levelCount,
			0,
			tex->image,
			&tex->resourceAccessType
		);
	}

	/* V */

	imageCopy.bufferOffset = offset + yDataLength + uvDataLength;

	tex = (VulkanTexture*) v;

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		tex->layerCount,
		0,
		tex->levelCount,
		0,
		tex->image,
		&tex->resourceAccessType
	);

	RECORD_CMD(renderer->vkCmdCopyBufferToImage(
		commandBuffer->commandBuffer,
		transferBuffer->buffer,
		tex->image,
		AccessMap[tex->resourceAccessType].imageLayout,
		1,
		&imageCopy
	));

	if (tex->imageCreateInfo.usage & VK_IMAGE_USAGE_SAMPLED_BIT)
	{
		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			RESOURCE_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			tex->layerCount,
			0,
			tex->levelCount,
			0,
			tex->image,
			&tex->resourceAccessType
		);
	}

	FNA3D_CommandBuffer_UnlockFromTransfer(renderer->commandBuffers);
	SDL_UnlockMutex(renderer->passLock);
}

static void VULKAN_GetTextureData2D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	VULKAN_INTERNAL_GetTextureData(
		driverData,
		texture,
		x,
		y,
		w,
		h,
		level,
		0,
		data,
		dataLength
	);
}

static void VULKAN_GetTextureData3D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t z,
	int32_t w,
	int32_t h,
	int32_t d,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	FNA3D_LogError(
		"GetTextureData3D is unsupported!"
	);
}

static void VULKAN_GetTextureDataCube(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	FNA3D_CubeMapFace cubeMapFace,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	VULKAN_INTERNAL_GetTextureData(
		driverData,
		texture,
		x,
		y,
		w,
		h,
		level,
		cubeMapFace,
		data,
		dataLength
	);
}

/* Renderbuffers */

static FNA3D_Renderbuffer* VULKAN_GenColorRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount,
	FNA3D_Texture *texture
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *vlkTexture = (VulkanTexture*) texture;
	VulkanRenderbuffer *renderbuffer;

	/* Create and return the renderbuffer */
	renderbuffer = (VulkanRenderbuffer*) SDL_malloc(sizeof(VulkanRenderbuffer));
	renderbuffer->depthBuffer = NULL;
	renderbuffer->colorBuffer = (VulkanColorBuffer*) SDL_malloc(
		sizeof(VulkanColorBuffer)
	);
	renderbuffer->colorBuffer->handle = vlkTexture;
	renderbuffer->colorBuffer->multiSampleTexture = NULL;
	renderbuffer->colorBuffer->multiSampleCount = 0;

	if (multiSampleCount > 1)
	{
		renderbuffer->colorBuffer->multiSampleTexture =
			(VulkanTexture*) SDL_malloc(sizeof(VulkanTexture));
		VULKAN_INTERNAL_CreateTexture(
			renderer,
			width,
			height,
			1,
			0,
			1,
			XNAToVK_SampleCount(multiSampleCount),
			1,
			XNAToVK_SurfaceFormat[format],
			XNAToVK_SurfaceSwizzle[format],
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			renderbuffer->colorBuffer->multiSampleTexture
		);
		renderbuffer->colorBuffer->multiSampleTexture->colorFormat = format;

		renderbuffer->colorBuffer->multiSampleCount = multiSampleCount;

		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			renderbuffer->colorBuffer->multiSampleTexture->layerCount,
			0,
			renderbuffer->colorBuffer->multiSampleTexture->levelCount,
			0,
			renderbuffer->colorBuffer->multiSampleTexture->image,
			&renderbuffer->colorBuffer->multiSampleTexture->resourceAccessType
		);
	}

	return (FNA3D_Renderbuffer*) renderbuffer;
}

static FNA3D_Renderbuffer* VULKAN_GenDepthStencilRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_DepthFormat format,
	int32_t multiSampleCount
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanRenderbuffer *renderbuffer;
	VkImageAspectFlags depthAspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
	VkFormat depthFormat = XNAToVK_DepthFormat(renderer, format);

	if (DepthFormatContainsStencil(depthFormat))
	{
		depthAspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}

	renderbuffer = (VulkanRenderbuffer*) SDL_malloc(
		sizeof(VulkanRenderbuffer)
	);
	renderbuffer->colorBuffer = NULL;
	renderbuffer->depthBuffer = (VulkanDepthStencilBuffer*) SDL_malloc(
		sizeof(VulkanDepthStencilBuffer)
	);

	renderbuffer->depthBuffer->handle = (VulkanTexture*) SDL_malloc(
		sizeof(VulkanTexture)
	);
	if (!VULKAN_INTERNAL_CreateTexture(
		renderer,
		width,
		height,
		1,
		0,
		1,
		XNAToVK_SampleCount(multiSampleCount),
		1,
		depthFormat,
		RGBA_SWIZZLE,
		depthAspectFlags,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		renderbuffer->depthBuffer->handle
	)) {
		FNA3D_LogError("Failed to create depth stencil image");
		return NULL;
	}
	renderbuffer->depthBuffer->handle->depthStencilFormat = format;

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_WRITE,
		depthAspectFlags,
		0,
		renderbuffer->depthBuffer->handle->layerCount,
		0,
		renderbuffer->depthBuffer->handle->levelCount,
		0,
		renderbuffer->depthBuffer->handle->image,
		&renderbuffer->depthBuffer->handle->resourceAccessType
	);

	return (FNA3D_Renderbuffer*) renderbuffer;
}

static void VULKAN_AddDisposeRenderbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Renderbuffer *renderbuffer
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanRenderbuffer *vlkRenderBuffer = (VulkanRenderbuffer*) renderbuffer;
	uint8_t isDepthStencil = (vlkRenderBuffer->colorBuffer == NULL);
	uint32_t i;

	if (isDepthStencil)
	{
		if (renderer->depthStencilAttachment == vlkRenderBuffer->depthBuffer->handle)
		{
			renderer->depthStencilAttachment = NULL;
		}
	}
	else
	{
		/* Iterate through color attachments */
		for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i += 1)
		{
			if (renderer->colorAttachments[i] == vlkRenderBuffer->colorBuffer->handle)
			{
				renderer->colorAttachments[i] = NULL;
			}

		}
	}

	FNA3D_CommandBuffer_AddDisposeRenderbuffer(
		renderer->commandBuffers,
		(FNA3D_Renderbuffer*) vlkRenderBuffer
	);
}

/* Buffers */

static FNA3D_Buffer* VULKAN_GenVertexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t sizeInBytes
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	return (FNA3D_Buffer*) FNA3D_Memory_CreateBufferContainer(
		renderer->allocator,
		1,
		sizeInBytes
	);
}

static void VULKAN_AddDisposeVertexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	FNA3D_Memory_DestroyBufferContainer(
		renderer->allocator,
		(FNA3D_BufferContainer*) buffer
	);
}

static void VULKAN_SetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride,
	FNA3D_SetDataOptions options
) {
	/* FIXME: use transfer buffer for elementSizeInBytes < vertexStride */
	VULKAN_INTERNAL_SetBufferData(
		driverData,
		buffer,
		offsetInBytes,
		data,
		elementCount * vertexStride,
		options
	);
}

static void VULKAN_GetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanBuffer *vulkanBuffer;
	uint8_t *dataBytes, *cpy, *src, *dst;
	uint8_t useTransferBuffer;
	int32_t i;

	vulkanBuffer = (VulkanBuffer*) FNA3D_Memory_GetActiveBuffer(
		(FNA3D_BufferContainer*) buffer
	);
	dataBytes = (uint8_t*) data;
	useTransferBuffer = elementSizeInBytes < vertexStride;

	if (useTransferBuffer)
	{
		cpy = (uint8_t*) SDL_malloc(elementCount * vertexStride);
	}
	else
	{
		cpy = dataBytes;
	}

	VULKAN_INTERNAL_BufferMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_READ,
		vulkanBuffer->buffer,
		&vulkanBuffer->resourceAccessType
	);

	SDL_memcpy(
		cpy,
		FNA3D_Memory_GetHostPointer(
			vulkanBuffer->usedRegion,
			offsetInBytes
		),
		elementCount * vertexStride
	);

	if (useTransferBuffer)
	{
		src = cpy;
		dst = dataBytes;
		for (i = 0; i < elementCount; i += 1)
		{
			SDL_memcpy(dst, src, elementSizeInBytes);
			dst += elementSizeInBytes;
			src += vertexStride;
		}
		SDL_free(cpy);
	}

	VULKAN_INTERNAL_BufferMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_VERTEX_BUFFER,
		vulkanBuffer->buffer,
		&vulkanBuffer->resourceAccessType
	);
}

static FNA3D_Buffer* VULKAN_GenIndexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t sizeInBytes
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	return (FNA3D_Buffer*) FNA3D_Memory_CreateBufferContainer(
		renderer->allocator,
		0,
		sizeInBytes
	);
}

static void VULKAN_AddDisposeIndexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	FNA3D_Memory_DestroyBufferContainer(
		renderer->allocator,
		(FNA3D_BufferContainer*) buffer
	);
}

static void VULKAN_SetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	VULKAN_INTERNAL_SetBufferData(
		driverData,
		buffer,
		offsetInBytes,
		data,
		dataLength,
		options
	);
}

static void VULKAN_GetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanBuffer *vulkanBuffer;

	vulkanBuffer = (VulkanBuffer*) FNA3D_Memory_GetActiveBuffer(
		(FNA3D_BufferContainer*) buffer
	);

	VULKAN_INTERNAL_BufferMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_READ,
		vulkanBuffer->buffer,
		&vulkanBuffer->resourceAccessType
	);

	SDL_memcpy(
		data,
		FNA3D_Memory_GetHostPointer(
			vulkanBuffer->usedRegion,
			offsetInBytes
		),
		dataLength
	);

	VULKAN_INTERNAL_BufferMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_INDEX_BUFFER,
		vulkanBuffer->buffer,
		&vulkanBuffer->resourceAccessType
	);
}

/* Effects */

static inline void ShaderResourcesHashTable_Remove(
	VulkanRenderer *renderer,
	MOJOSHADER_vkShader *key
) {
	int32_t i;
	uint64_t hashcode = (uint64_t) (size_t) key;
	ShaderResourcesHashArray *arr =
		&renderer->shaderResourcesHashTable.buckets[hashcode % NUM_SHADER_RESOURCES_BUCKETS];
	ShaderResourcesHashMap *element;

	for (i = arr->count - 1; i >= 0; i -= 1)
	{
		element = &arr->elements[i];
		if (element->key == key)
		{
			ShaderResources_Destroy(renderer, element->value);

			SDL_memmove(
				arr->elements + i,
				arr->elements + i + 1,
				sizeof(ShaderResourcesHashMap) * (arr->count - i - 1)
			);

			arr->count -= 1;
		}
	}
}

static void VULKAN_INTERNAL_DeleteShader(const void *shaderContext, void* shader)
{
	MOJOSHADER_vkShader *vkShader = (MOJOSHADER_vkShader*) shader;
	const MOJOSHADER_parseData *pd;
	VulkanRenderer *renderer;
	PipelineHashArray *pipelineHashArray;
	int32_t i, j;

	pd = MOJOSHADER_vkGetShaderParseData(vkShader);
	renderer = (VulkanRenderer*) pd->malloc_data;

	if (MOJOSHADER_vkGetShaderRefCount(vkShader) > 1)
	{
		/* Other effects are using this shader, just decrement the refcount */
		MOJOSHADER_vkDeleteShader(renderer->mojoshaderContext, vkShader);
		return;
	}

	ShaderResourcesHashTable_Remove(renderer, vkShader);

	/* invalidate any pipeline containing shader */
	for (i = 0; i < NUM_PIPELINE_HASH_BUCKETS; i += 1)
	{
		pipelineHashArray = &renderer->pipelineHashTable.buckets[i];
		for (j = pipelineHashArray->count - 1; j >= 0; j -= 1)
		{
			if (	pipelineHashArray->elements[j].key.vertShader == vkShader ||
				pipelineHashArray->elements[j].key.fragShader == vkShader	)
			{
				renderer->vkDestroyPipeline(
					renderer->logicalDevice,
					pipelineHashArray->elements[j].value,
					NULL
				);

				SDL_memmove(
					pipelineHashArray->elements + j,
					pipelineHashArray->elements + j + 1,
					sizeof(PipelineHashMap) * (pipelineHashArray->count - j - 1)
				);

				pipelineHashArray->count -= 1;
			}
		}
	}

	MOJOSHADER_vkDeleteShader(renderer->mojoshaderContext, vkShader);
}

static void VULKAN_CreateEffect(
	FNA3D_Renderer *driverData,
	uint8_t *effectCode,
	uint32_t effectCodeLength,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **effectData
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	MOJOSHADER_effectShaderContext shaderBackend;
	VulkanEffect *result;
	int32_t i;

	shaderBackend.shaderContext = renderer->mojoshaderContext;
	shaderBackend.compileShader = (MOJOSHADER_compileShaderFunc) MOJOSHADER_vkCompileShader;
	shaderBackend.shaderAddRef = (MOJOSHADER_shaderAddRefFunc) MOJOSHADER_vkShaderAddRef;
	shaderBackend.deleteShader = VULKAN_INTERNAL_DeleteShader;
	shaderBackend.getParseData = (MOJOSHADER_getParseDataFunc) MOJOSHADER_vkGetShaderParseData;
	shaderBackend.bindShaders = (MOJOSHADER_bindShadersFunc) MOJOSHADER_vkBindShaders;
	shaderBackend.getBoundShaders = (MOJOSHADER_getBoundShadersFunc) MOJOSHADER_vkGetBoundShaders;
	shaderBackend.mapUniformBufferMemory = (MOJOSHADER_mapUniformBufferMemoryFunc) MOJOSHADER_vkMapUniformBufferMemory;
	shaderBackend.unmapUniformBufferMemory = (MOJOSHADER_unmapUniformBufferMemoryFunc) MOJOSHADER_vkUnmapUniformBufferMemory;
	shaderBackend.getError = (MOJOSHADER_getErrorFunc) MOJOSHADER_vkGetError;
	shaderBackend.m = NULL;
	shaderBackend.f = NULL;
	shaderBackend.malloc_data = driverData;

	*effectData = MOJOSHADER_compileEffect(
		effectCode,
		effectCodeLength,
		NULL,
		0,
		NULL,
		0,
		&shaderBackend
	);

	for (i = 0; i < (*effectData)->error_count; i += 1)
	{
		FNA3D_LogError(
			"MOJOSHADER_compileEffect Error: %s",
			(*effectData)->errors[i].error
		);
	}

	result = (VulkanEffect*) SDL_malloc(sizeof(VulkanEffect));
	result->effect = *effectData;
	*effect = (FNA3D_Effect*) result;
}

static void VULKAN_CloneEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *cloneSource,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **effectData
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanEffect *vulkanCloneSource = (VulkanEffect*) cloneSource;
	VulkanEffect *result;

	*effectData = MOJOSHADER_cloneEffect(vulkanCloneSource->effect);
	if (*effectData == NULL)
	{
		FNA3D_LogError(MOJOSHADER_vkGetError(renderer->mojoshaderContext));
	}

	result = (VulkanEffect*) SDL_malloc(sizeof(VulkanEffect));
	result->effect = *effectData;
	*effect = (FNA3D_Effect*) result;
}

static void VULKAN_AddDisposeEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;

	FNA3D_CommandBuffer_AddDisposeEffect(renderer->commandBuffers, effect);
}

static void VULKAN_SetEffectTechnique(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique
) {
	VulkanEffect *vkEffect = (VulkanEffect*) effect;
	MOJOSHADER_effectSetTechnique(vkEffect->effect, technique);
}

static void VULKAN_ApplyEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	uint32_t pass,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanEffect *fnaEffect = (VulkanEffect*) effect;
	MOJOSHADER_effect *effectData = fnaEffect->effect;
	const MOJOSHADER_effectTechnique *technique = fnaEffect->effect->current_technique;
	uint32_t numPasses;

	/*
	 * Lock to prevent mojoshader from overwriting resource data
	 * while resources are initialized
	 */
	SDL_LockMutex(renderer->passLock);

	renderer->vertexSamplerDescriptorSetDataNeedsUpdate = 1;
	renderer->fragSamplerDescriptorSetDataNeedsUpdate = 1;
	renderer->needNewPipeline = 1;

	if (effectData == renderer->currentEffect)
	{
		if (	technique == renderer->currentTechnique &&
			pass == renderer->currentPass	)
		{
			MOJOSHADER_effectCommitChanges(
				renderer->currentEffect
			);

			SDL_UnlockMutex(renderer->passLock);
			return;
		}
		MOJOSHADER_effectEndPass(renderer->currentEffect);
		MOJOSHADER_effectBeginPass(renderer->currentEffect, pass);
		renderer->currentTechnique = technique;
		renderer->currentPass = pass;

		SDL_UnlockMutex(renderer->passLock);
		return;
	}
	else if (renderer->currentEffect != NULL)
	{
		MOJOSHADER_effectEndPass(renderer->currentEffect);
		MOJOSHADER_effectEnd(renderer->currentEffect);
	}

	MOJOSHADER_effectBegin(
		effectData,
		&numPasses,
		0,
		stateChanges
	);

	MOJOSHADER_effectBeginPass(effectData, pass);
	renderer->currentEffect = effectData;
	renderer->currentTechnique = technique;
	renderer->currentPass = pass;

	SDL_UnlockMutex(renderer->passLock);
}

static void VULKAN_BeginPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	MOJOSHADER_effect *effectData = ((VulkanEffect *) effect)->effect;
	uint32_t whatever;

	MOJOSHADER_effectBegin(
			effectData,
			&whatever,
			1,
			stateChanges
	);
	MOJOSHADER_effectBeginPass(effectData, 0);
}

static void VULKAN_EndPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	MOJOSHADER_effect *effectData = ((VulkanEffect *) effect)->effect;
	MOJOSHADER_effectEndPass(effectData);
	MOJOSHADER_effectEnd(effectData);
}

/* Queries */

static FNA3D_Query* VULKAN_CreateQuery(FNA3D_Renderer *driverData)
{
	VulkanRenderer *renderer = (VulkanRenderer *) driverData;
	VulkanQuery *query = (VulkanQuery*) SDL_malloc(sizeof(VulkanQuery));

	if (renderer->freeQueryIndexStackHead == -1)
	{
		FNA3D_LogError(
			"Query limit of %d has been exceeded!",
			MAX_QUERIES
		);
		return NULL;
	}

	query->index = renderer->freeQueryIndexStackHead;
	renderer->freeQueryIndexStackHead = renderer->freeQueryIndexStack[renderer->freeQueryIndexStackHead];
	return (FNA3D_Query *) query;
}

static void VULKAN_AddDisposeQuery(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanQuery *vulkanQuery = (VulkanQuery*) query;

	/* Push the now-free index to the stack */
	SDL_LockMutex(renderer->disposeLock);
	renderer->freeQueryIndexStack[vulkanQuery->index] =
		renderer->freeQueryIndexStackHead;
	renderer->freeQueryIndexStackHead = vulkanQuery->index;
	SDL_UnlockMutex(renderer->disposeLock);

	SDL_free(vulkanQuery);
}

static void VULKAN_QueryBegin(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanQuery *vulkanQuery = (VulkanQuery*) query;
	VulkanCommandBuffer *commandBuffer;

	/* Need to do this between passes */
	VULKAN_INTERNAL_MaybeEndRenderPass(renderer);

	commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
		renderer->commandBuffers
	);

	RECORD_CMD(renderer->vkCmdResetQueryPool(
		commandBuffer->commandBuffer,
		renderer->queryPool,
		vulkanQuery->index,
		1
	));

	RECORD_CMD(renderer->vkCmdBeginQuery(
		commandBuffer->commandBuffer,
		renderer->queryPool,
		vulkanQuery->index,
		renderer->supportsPreciseOcclusionQueries ?
			VK_QUERY_CONTROL_PRECISE_BIT :
			0
	));
}

static void VULKAN_QueryEnd(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanQuery *vulkanQuery = (VulkanQuery*) query;
	VulkanCommandBuffer *commandBuffer;

	/* Assume that the user is calling this in
	 * the same pass as they started it
	 */

	commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
		renderer->commandBuffers
	);
	RECORD_CMD(renderer->vkCmdEndQuery(
		commandBuffer->commandBuffer,
		renderer->queryPool,
		vulkanQuery->index
	));
}

static uint8_t VULKAN_QueryComplete(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanQuery *vulkanQuery = (VulkanQuery*) query;
	VkResult vulkanResult;
	uint32_t queryResult;

	vulkanResult = renderer->vkGetQueryPoolResults(
		renderer->logicalDevice,
		renderer->queryPool,
		vulkanQuery->index,
		1,
		sizeof(queryResult),
		&queryResult,
		0,
		0
	);

	return vulkanResult == VK_SUCCESS;
}

static int32_t VULKAN_QueryPixelCount(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanQuery *vulkanQuery = (VulkanQuery*) query;
	VkResult vulkanResult;
	uint32_t queryResult;

	vulkanResult = renderer->vkGetQueryPoolResults(
		renderer->logicalDevice,
		renderer->queryPool,
		vulkanQuery->index,
		1,
		sizeof(queryResult),
		&queryResult,
		0,
		0
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkGetQueryPoolResults, 0)

	/* FIXME maybe signed/unsigned integer problems? */
	return queryResult;
}

/* Feature Queries */

static uint8_t VULKAN_SupportsDXT1(FNA3D_Renderer *driverData)
{
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	return renderer->supportsDxt1;
}

static uint8_t VULKAN_SupportsS3TC(FNA3D_Renderer *driverData)
{
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	return renderer->supportsS3tc;
}

static uint8_t VULKAN_SupportsBC7(FNA3D_Renderer *driverData)
{
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	return renderer->supportsBc7;
}

static uint8_t VULKAN_SupportsHardwareInstancing(FNA3D_Renderer *driverData)
{
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	return renderer->supports.EXT_vertex_attribute_divisor;
}

static uint8_t VULKAN_SupportsNoOverwrite(FNA3D_Renderer *driverData)
{
	return 1;
}

static uint8_t VULKAN_SupportsSRGBRenderTargets(FNA3D_Renderer *driverData)
{
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	return renderer->supportsSRGBRenderTarget;
}

static void VULKAN_GetMaxTextureSlots(
	FNA3D_Renderer *driverData,
	int32_t *textures,
	int32_t *vertexTextures
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	*textures = renderer->numTextureSlots;
	*vertexTextures = renderer->numVertexTextureSlots;
}

static int32_t VULKAN_GetMaxMultiSampleCount(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VkSampleCountFlags flags = renderer->physicalDeviceProperties.properties.limits.framebufferColorSampleCounts;
	int32_t maxSupported = 1;

	if (flags & VK_SAMPLE_COUNT_64_BIT)
	{
		maxSupported = 64;
	}
	else if (flags & VK_SAMPLE_COUNT_32_BIT)
	{
		maxSupported = 32;
	}
	else if (flags & VK_SAMPLE_COUNT_16_BIT)
	{
		maxSupported = 16;
	}
	else if (flags & VK_SAMPLE_COUNT_8_BIT)
	{
		maxSupported = 8;
	}
	else if (flags & VK_SAMPLE_COUNT_4_BIT)
	{
		maxSupported = 4;
	}
	else if (flags & VK_SAMPLE_COUNT_2_BIT)
	{
		maxSupported = 2;
	}

	return SDL_min(multiSampleCount, maxSupported);
}

/* Debugging */

static void VULKAN_SetStringMarker(FNA3D_Renderer *driverData, const char *text)
{
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *commandBuffer;
	VkDebugUtilsLabelEXT labelInfo;

	if (renderer->supportsDebugUtils)
	{
		labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
		labelInfo.pNext = NULL;
		labelInfo.pLabelName = text;

		commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetCurrent(
			renderer->commandBuffers
		);
		RECORD_CMD(renderer->vkCmdInsertDebugUtilsLabelEXT(
			commandBuffer->commandBuffer,
			&labelInfo
		));
	}
}

static void VULKAN_SetTextureName(FNA3D_Renderer* driverData, FNA3D_Texture* texture, const char* text)
{
	VulkanRenderer* renderer = (VulkanRenderer*)driverData;
	VulkanTexture* vkTexture = (VulkanTexture*)texture;
	VkDebugUtilsObjectNameInfoEXT nameInfo;

	if (renderer->supportsDebugUtils)
	{
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		nameInfo.pNext = NULL;
		nameInfo.pObjectName = text;
		nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
		nameInfo.objectHandle = (uint64_t) vkTexture->image;

		renderer->vkSetDebugUtilsObjectNameEXT(
			renderer->logicalDevice,
			&nameInfo
		);
	}
}

/* External Interop */

static void VULKAN_GetSysRenderer(
	FNA3D_Renderer *driverData,
	FNA3D_SysRendererEXT *sysrenderer
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;

	sysrenderer->rendererType = FNA3D_RENDERER_TYPE_VULKAN_EXT;
	sysrenderer->renderer.vulkan.instance = renderer->instance;
	sysrenderer->renderer.vulkan.physicalDevice = renderer->physicalDevice;
	sysrenderer->renderer.vulkan.logicalDevice = renderer->logicalDevice;
	sysrenderer->renderer.vulkan.queueFamilyIndex = renderer->queueFamilyIndex;
}

static FNA3D_Texture* VULKAN_CreateSysTexture(
	FNA3D_Renderer *driverData,
	FNA3D_SysTextureEXT *systexture
) {
	VulkanTexture *texture;

	if (systexture->rendererType != FNA3D_RENDERER_TYPE_VULKAN_EXT)
	{
		return NULL;
	}

	texture = (VulkanTexture*) SDL_malloc(sizeof(VulkanTexture));

	texture->image = (VkImage) systexture->texture.vulkan.image;
	texture->view = (VkImageView) systexture->texture.vulkan.view;
	texture->external = 1;

	/* unused by external */
	texture->usedRegion = NULL;
	texture->colorFormat = 0;
	texture->depth = 0;
	texture->depthStencilFormat = 0;
	texture->dimensions.width = 0;
	texture->dimensions.height = 0;
	texture->layerCount = 0;
	texture->levelCount = 0;
	texture->resourceAccessType = RESOURCE_ACCESS_NONE;
	texture->rtViews[0] = VK_NULL_HANDLE;
	texture->rtViews[1] = VK_NULL_HANDLE;
	texture->rtViews[2] = VK_NULL_HANDLE;
	texture->rtViews[3] = VK_NULL_HANDLE;
	texture->rtViews[4] = VK_NULL_HANDLE;
	texture->rtViews[5] = VK_NULL_HANDLE;
	texture->surfaceFormat = 0;

	return (FNA3D_Texture*) texture;
}

/* Memory Driver */

static uint8_t VULKAN_Memory_AllocDeviceMemory(
	FNA3D_Renderer *driverData,
	size_t subAllocatorIndex,
	size_t memorySize,
	uint8_t deviceLocal,
	uint8_t hostVisible,
	FNA3D_MemoryPlatformHandle* driverMemory,
	uint8_t** mapPointer
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VkMemoryAllocateInfo allocInfo;
	VkResult result;

	if (	deviceLocal &&
		(renderer->deviceLocalHeapUsage + memorySize > renderer->maxDeviceLocalHeapUsage)	)
	{
		/* we are oversubscribing device local memory */
		return 0;
	}

	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.pNext = NULL;
	allocInfo.memoryTypeIndex = subAllocatorIndex;
	allocInfo.allocationSize = memorySize;

	result = renderer->vkAllocateMemory(
		renderer->logicalDevice,
		&allocInfo,
		NULL,
		(VkDeviceMemory*) driverMemory /* FIXME: This should be a struct... */
	);

	if (result != VK_SUCCESS)
	{
		FNA3D_LogWarn("vkAllocateMemory: %s", VkErrorMessages(result));
		return 0;
	}

	/* Tracking device-local usage, mostly for debugging purposes */
	if (deviceLocal)
	{
		renderer->deviceLocalHeapUsage += memorySize;
	}

	/* Persistent mapping for host memory */
	if (hostVisible)
	{
		result = renderer->vkMapMemory(
			renderer->logicalDevice,
			(VkDeviceMemory) *driverMemory,
			0,
			VK_WHOLE_SIZE,
			0,
			(void**) mapPointer
		);
		VULKAN_ERROR_CHECK(result, vkMapMemory, 0)
	}
	else
	{
		*mapPointer = NULL;
	}

	return 1;
}

static void VULKAN_Memory_FreeDeviceMemory(
	FNA3D_Renderer *driverData,
	FNA3D_MemoryPlatformHandle driverMemory,
	size_t memorySize,
	size_t subAllocatorIndex
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	uint8_t isDeviceLocal =
		(renderer->memoryProperties.memoryTypes[subAllocatorIndex].propertyFlags &
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;

	renderer->vkFreeMemory(
		renderer->logicalDevice,
		(VkDeviceMemory) driverMemory, /* FIXME: This should be a struct... */
		NULL
	);

	if (isDeviceLocal)
	{
		renderer->deviceLocalHeapUsage -= memorySize;
	}
}

static uint8_t VULKAN_Memory_BindBufferMemory(
	FNA3D_Renderer *driverData,
	FNA3D_MemoryPlatformHandle deviceMemory,
	size_t alignedOffset,
	FNA3D_MemoryPlatformHandle buffer
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VkResult vulkanResult = renderer->vkBindBufferMemory(
		renderer->logicalDevice,
		(VkBuffer) buffer,
		(VkDeviceMemory) deviceMemory, /* FIXME: This should be a struct... */
		alignedOffset
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkBindBufferMemory, 0)
	return 1;
}

static uint8_t VULKAN_Memory_BindImageMemory(
	FNA3D_Renderer *driverData,
	FNA3D_MemoryPlatformHandle deviceMemory,
	size_t alignedOffset,
	FNA3D_MemoryPlatformHandle image
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VkResult vulkanResult = renderer->vkBindImageMemory(
		renderer->logicalDevice,
		(VkImage) image,
		(VkDeviceMemory) deviceMemory, /* FIXME: This should be a struct... */
		alignedOffset
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkBindBufferMemory, 0)
	return 1;
}

static void VULKAN_Memory_BeginDefragCommands(FNA3D_Renderer *driverData)
{
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *commandBuffer;
	VkCommandBufferBeginInfo beginInfo;

	FNA3D_CommandBuffer_LockForDefrag(renderer->commandBuffers);

	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.pNext = NULL;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	beginInfo.pInheritanceInfo = NULL;

	commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetDefragBuffer(
		renderer->commandBuffers
	);
	renderer->vkBeginCommandBuffer(
		commandBuffer->commandBuffer,
		&beginInfo
	);

	renderer->needDefrag = 0;
}

static void VULKAN_Memory_EndDefragCommands(FNA3D_Renderer *driverData)
{
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *commandBuffer;
	VkPipelineStageFlags waitFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	VkSubmitInfo submitInfo;
	VkResult result;

	commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetDefragBuffer(
		renderer->commandBuffers
	);

	renderer->vkEndCommandBuffer(
		commandBuffer->commandBuffer
	);

	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pNext = NULL;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer->commandBuffer;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &renderer->defragSemaphore;
	submitInfo.pWaitDstStageMask = &waitFlags;
	submitInfo.signalSemaphoreCount = 0;
	submitInfo.pSignalSemaphores = NULL;

	result = renderer->vkResetFences(
		renderer->logicalDevice,
		1,
		&commandBuffer->inFlightFence
	);
	if (result != VK_SUCCESS)
	{
		FNA3D_LogError("vkResetFences: %s", VkErrorMessages(result));
		return;
	}

	result = renderer->vkQueueSubmit(
		renderer->unifiedQueue,
		1,
		&submitInfo,
		commandBuffer->inFlightFence
	);
	if (result != VK_SUCCESS)
	{
		FNA3D_LogError("vkQueueSubmit: %s", VkErrorMessages(result));
		return;
	}

	renderer->defragTimer = 0;

	FNA3D_CommandBuffer_UnlockFromDefrag(renderer->commandBuffers);
}

static uint8_t VULKAN_Memory_DefragBuffer(
	FNA3D_Renderer *driverData,
	void* resource,
	size_t resourceSize
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanBuffer *vulkanBuffer = (VulkanBuffer*) resource;
	VulkanCommandBuffer *commandBuffer;
	VulkanResourceAccessType originalResourceAccessType;
	VulkanResourceAccessType copyResourceAccessType = RESOURCE_ACCESS_NONE;
	FNA3D_MemoryUsedRegion *newRegion;
	VkBufferCopy bufferCopy;
	VkBuffer copyBuffer;
	VkResult result;

	vulkanBuffer->bufferCreateInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	result = renderer->vkCreateBuffer(
		renderer->logicalDevice,
		&vulkanBuffer->bufferCreateInfo,
		NULL,
		&copyBuffer
	);
	VULKAN_ERROR_CHECK(result, vkCreateBuffer, 0)

	if (
		VULKAN_INTERNAL_BindMemoryForBuffer(
			renderer,
			copyBuffer,
			vulkanBuffer,
			resourceSize,
			vulkanBuffer->preferDeviceLocal,
			0,
			&newRegion
		) != 1)
	{
		/* Out of memory, abort */
		renderer->vkDestroyBuffer(
			renderer->logicalDevice,
			copyBuffer,
			NULL
		);
		return 0;
	}

	originalResourceAccessType = vulkanBuffer->resourceAccessType;

	VULKAN_INTERNAL_BufferMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_READ,
		vulkanBuffer->buffer,
		&vulkanBuffer->resourceAccessType
	);

	VULKAN_INTERNAL_BufferMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_WRITE,
		copyBuffer,
		&copyResourceAccessType
	);

	bufferCopy.srcOffset = 0;
	bufferCopy.dstOffset = 0;
	bufferCopy.size = resourceSize;

	commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetDefragBuffer(
		renderer->commandBuffers
	);

	renderer->vkCmdCopyBuffer(
		commandBuffer->commandBuffer,
		vulkanBuffer->buffer,
		copyBuffer,
		1,
		&bufferCopy
	);

	VULKAN_INTERNAL_BufferMemoryBarrier(
		renderer,
		originalResourceAccessType,
		copyBuffer,
		&copyResourceAccessType
	);

	if (renderer->defragmentedBuffersToDestroyCount >= renderer->defragmentedBuffersToDestroyCapacity)
	{
		renderer->defragmentedBuffersToDestroyCapacity *= 2;
		renderer->defragmentedBuffersToDestroy = SDL_realloc(
			renderer->defragmentedBuffersToDestroy,
			sizeof(VkBuffer) * renderer->defragmentedBuffersToDestroyCapacity
		);
	}

	renderer->defragmentedBuffersToDestroy[
		renderer->defragmentedBuffersToDestroyCount
	] = vulkanBuffer->buffer;

	renderer->defragmentedBuffersToDestroyCount += 1;

	vulkanBuffer->usedRegion = newRegion; /* lol */
	vulkanBuffer->buffer = copyBuffer;
	vulkanBuffer->resourceAccessType = copyResourceAccessType;

	/* Binding prevents data race when using SetBufferData with Discard option */
	FNA3D_CommandBuffer_MarkBufferAsBound(
		renderer->commandBuffers,
		(FNA3D_BufferHandle*) vulkanBuffer
	);

	renderer->needDefrag = 1;
	renderer->bufferDefragInProgress = 1;

	return 1;
}

static uint8_t VULKAN_Memory_DefragImage(
	FNA3D_Renderer *driverData,
	void* resource,
	size_t resourceSize
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *vulkanTexture = (VulkanTexture*) resource;
	VulkanCommandBuffer *commandBuffer;
	VulkanResourceAccessType originalResourceAccessType;
	VulkanResourceAccessType copyResourceAccessType = RESOURCE_ACCESS_NONE;
	FNA3D_MemoryUsedRegion *newRegion;
	VkImageAspectFlags aspectFlags;
	VkImageCopy *imageCopyRegions;
	VkImage copyImage;
	VkResult result;
	uint32_t level;

	result = renderer->vkCreateImage(
		renderer->logicalDevice,
		&vulkanTexture->imageCreateInfo,
		NULL,
		&copyImage
	);

	VULKAN_ERROR_CHECK(result, vkCreateImage, 0)

	if (VULKAN_INTERNAL_BindMemoryForImage(
		renderer,
		copyImage,
		vulkanTexture,
		0,
		&newRegion
	) != 1)
	{
		/* Out of memory, abort */
		renderer->vkDestroyImage(
			renderer->logicalDevice,
			copyImage,
			NULL
		);

		return 0;
	}

	if (IsDepthFormat(vulkanTexture->surfaceFormat))
	{
		aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;

		if (DepthFormatContainsStencil(vulkanTexture->surfaceFormat))
		{
			aspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	}
	else
	{
		aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
	}

	originalResourceAccessType = vulkanTexture->resourceAccessType;

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_READ,
		aspectFlags,
		0,
		vulkanTexture->layerCount,
		0,
		vulkanTexture->levelCount,
		0,
		vulkanTexture->image,
		&vulkanTexture->resourceAccessType
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_WRITE,
		aspectFlags,
		0,
		vulkanTexture->layerCount,
		0,
		vulkanTexture->levelCount,
		0,
		copyImage,
		&copyResourceAccessType
	);

	imageCopyRegions = SDL_stack_alloc(VkImageCopy, vulkanTexture->levelCount);

	for (level = 0; level < vulkanTexture->levelCount; level += 1)
	{
		imageCopyRegions[level].srcOffset.x = 0;
		imageCopyRegions[level].srcOffset.y = 0;
		imageCopyRegions[level].srcOffset.z = 0;
		imageCopyRegions[level].srcSubresource.aspectMask = aspectFlags;
		imageCopyRegions[level].srcSubresource.baseArrayLayer = 0;
		imageCopyRegions[level].srcSubresource.layerCount = vulkanTexture->layerCount;
		imageCopyRegions[level].srcSubresource.mipLevel = level;
		imageCopyRegions[level].extent.width = SDL_max(1, vulkanTexture->dimensions.width >> level);
		imageCopyRegions[level].extent.height = SDL_max(1, vulkanTexture->dimensions.height >> level);
		imageCopyRegions[level].extent.depth = vulkanTexture->depth;
		imageCopyRegions[level].dstOffset.x = 0;
		imageCopyRegions[level].dstOffset.y = 0;
		imageCopyRegions[level].dstOffset.z = 0;
		imageCopyRegions[level].dstSubresource.aspectMask = aspectFlags;
		imageCopyRegions[level].dstSubresource.baseArrayLayer = 0;
		imageCopyRegions[level].dstSubresource.layerCount = vulkanTexture->layerCount;
		imageCopyRegions[level].dstSubresource.mipLevel = level;
	}

	commandBuffer = (VulkanCommandBuffer*) FNA3D_CommandBuffer_GetDefragBuffer(
		renderer->commandBuffers
	);
	renderer->vkCmdCopyImage(
		commandBuffer->commandBuffer,
		vulkanTexture->image,
		AccessMap[vulkanTexture->resourceAccessType].imageLayout,
		copyImage,
		AccessMap[copyResourceAccessType].imageLayout,
		vulkanTexture->levelCount,
		imageCopyRegions
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		originalResourceAccessType,
		aspectFlags,
		0,
		vulkanTexture->layerCount,
		0,
		vulkanTexture->levelCount,
		0,
		copyImage,
		&copyResourceAccessType
	);

	SDL_stack_free(imageCopyRegions);

	if (renderer->defragmentedImagesToDestroyCount >= renderer->defragmentedImagesToDestroyCapacity)
	{
		renderer->defragmentedImagesToDestroyCapacity *= 2;
		renderer->defragmentedImagesToDestroy = SDL_realloc(
			renderer->defragmentedImagesToDestroy,
			sizeof(VkImage) * renderer->defragmentedImagesToDestroyCapacity
		);
	}

	if (renderer->defragmentedImageViewsToDestroyCount >= renderer->defragmentedImageViewsToDestroyCapacity)
	{
		renderer->defragmentedImageViewsToDestroyCapacity *= 2;
		renderer->defragmentedImageViewsToDestroy = SDL_realloc(
			renderer->defragmentedImageViewsToDestroy,
			sizeof(VkImageView) * renderer->defragmentedImageViewsToDestroyCapacity
		);
	}

	renderer->defragmentedImagesToDestroy[
		renderer->defragmentedImagesToDestroyCount
	] = vulkanTexture->image;

	renderer->defragmentedImagesToDestroyCount += 1;

	renderer->defragmentedImageViewsToDestroy[
		renderer->defragmentedImageViewsToDestroyCount
	] = vulkanTexture->view;

	renderer->defragmentedImageViewsToDestroyCount += 1;

	vulkanTexture->viewCreateInfo.image = copyImage;

	renderer->vkCreateImageView(
		renderer->logicalDevice,
		&vulkanTexture->viewCreateInfo,
		NULL,
		&vulkanTexture->view
	);

	vulkanTexture->usedRegion = newRegion; /* lol */
	vulkanTexture->image = copyImage;
	vulkanTexture->resourceAccessType = copyResourceAccessType;

	renderer->needDefrag = 1;

	return 1;
}

static FNA3D_BufferHandle* VULKAN_Memory_CreateBufferHandle(
	FNA3D_Renderer *driverData,
	uint8_t isVertexData,
	size_t sizeInBytes
) {
	return (FNA3D_BufferHandle*) VULKAN_INTERNAL_CreateBuffer(
		(VulkanRenderer*) driverData,
		sizeInBytes,
		isVertexData ?
			RESOURCE_ACCESS_VERTEX_BUFFER :
			RESOURCE_ACCESS_INDEX_BUFFER,
		isVertexData ?
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT :
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		0,
		0
	);
}

static FNA3D_BufferHandle* VULKAN_Memory_CloneBufferHandle(
	FNA3D_Renderer *driverData,
	FNA3D_BufferHandle *buffer
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanBuffer *vulkanBuffer = (VulkanBuffer*) buffer;
	VulkanBuffer *result = VULKAN_INTERNAL_CreateBuffer(
		renderer,
		vulkanBuffer->size,
		vulkanBuffer->resourceAccessType,
		vulkanBuffer->usage,
		vulkanBuffer->preferDeviceLocal,
		vulkanBuffer->isTransferBuffer
	);
	if (result != NULL)
	{
		VULKAN_INTERNAL_BufferMemoryBarrier(
			renderer,
			result->resourceAccessType,
			result->buffer,
			&result->resourceAccessType
		);
	}

	return (FNA3D_BufferHandle*) result;
}

static void VULKAN_Memory_MarkBufferHandlesForDestroy(
	FNA3D_Renderer *driverData,
	FNA3D_BufferHandle **buffers,
	size_t bufferCount
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;

	FNA3D_CommandBuffer_AddDisposeBuffers(
		renderer->commandBuffers,
		buffers,
		bufferCount
	);
}

static uint8_t VULKAN_Memory_BufferHandleInUse(
	FNA3D_Renderer *driverData,
	FNA3D_BufferHandle *buffer
) {
	VulkanBuffer *vulkanBuffer = (VulkanBuffer*) buffer;
	return SDL_AtomicGet(&vulkanBuffer->refcount) > 0;
}

/* Command Buffer Driver */

static FNA3D_CommandBuffer* VULKAN_CommandBuffer_AllocCommandBuffer(
	FNA3D_Renderer *driverData,
	uint8_t fenceSignaled
) {
	VkCommandBufferAllocateInfo allocateInfo;
	VkFenceCreateInfo fenceCreateInfo;
	VkResult vulkanResult;
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *commandBuffer = (VulkanCommandBuffer*) SDL_malloc(
		sizeof(VulkanCommandBuffer)
	);

	allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocateInfo.pNext = NULL;
	allocateInfo.commandPool = renderer->commandPool;
	allocateInfo.commandBufferCount = 1;
	allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	vulkanResult = renderer->vkAllocateCommandBuffers(
		renderer->logicalDevice,
		&allocateInfo,
		&commandBuffer->commandBuffer
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkAllocateCommandBuffers, NULL)

	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = NULL;
	fenceCreateInfo.flags = fenceSignaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;

	renderer->vkCreateFence(
		renderer->logicalDevice,
		&fenceCreateInfo,
		NULL,
		&commandBuffer->inFlightFence
	);

	/* Descriptor set tracking */

	commandBuffer->usedDescriptorSetDataCapacity = 16;
	commandBuffer->usedDescriptorSetDataCount = 0;
	commandBuffer->usedDescriptorSetDatas = SDL_malloc(
		commandBuffer->usedDescriptorSetDataCapacity * sizeof(DescriptorSetData)
	);

	return (FNA3D_CommandBuffer*) commandBuffer;
}

static void VULKAN_CommandBuffer_FreeCommandBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_CommandBuffer *handle
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *commandBuffer = (VulkanCommandBuffer*) handle;

	renderer->vkDestroyFence(
		renderer->logicalDevice,
		commandBuffer->inFlightFence,
		NULL
	);
	SDL_free(commandBuffer->usedDescriptorSetDatas);
	SDL_free(commandBuffer);
}

static void VULKAN_CommandBuffer_BeginRecording(
	FNA3D_Renderer *driverData,
	FNA3D_CommandBuffer *handle
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *commandBuffer = (VulkanCommandBuffer*) handle;
	VkCommandBufferBeginInfo beginInfo;
	VkResult result;

	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.pNext = NULL;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	beginInfo.pInheritanceInfo = NULL;

	result = renderer->vkBeginCommandBuffer(
		commandBuffer->commandBuffer,
		&beginInfo
	);

	VULKAN_ERROR_CHECK(result, vkBeginCommandBuffer,)
}

static void VULKAN_CommandBuffer_EndRecording(
	FNA3D_Renderer *driverData,
	FNA3D_CommandBuffer *handle
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *commandBuffer = (VulkanCommandBuffer*) handle;
	VkResult result = renderer->vkEndCommandBuffer(
		commandBuffer->commandBuffer
	);
	VULKAN_ERROR_CHECK(result, vkEndCommandBuffer,)
}

static void VULKAN_CommandBuffer_Reset(
	FNA3D_Renderer *driverData,
	FNA3D_CommandBuffer *handle
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *commandBuffer = (VulkanCommandBuffer*) handle;
	DescriptorSetData *descriptorSetData;
	uint32_t i;

	/* Mark used descriptor sets as inactive */
	for (i = 0; i < commandBuffer->usedDescriptorSetDataCount; i += 1)
	{
		descriptorSetData = &commandBuffer->usedDescriptorSetDatas[i];

		if (descriptorSetData->descriptorSet != VK_NULL_HANDLE)
		{
			descriptorSetData->parent->inactiveDescriptorSets[descriptorSetData->parent->inactiveDescriptorSetCount] = descriptorSetData->descriptorSet;
			descriptorSetData->parent->inactiveDescriptorSetCount += 1;
		}
	}
	commandBuffer->usedDescriptorSetDataCount = 0;

	/* Reset the command buffer */
	renderer->vkResetCommandBuffer(
		commandBuffer->commandBuffer,
		VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT
	);
}

static uint8_t VULKAN_CommandBuffer_QueryFence(
	FNA3D_Renderer *driverData,
	FNA3D_CommandBuffer *handle
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *commandBuffer = (VulkanCommandBuffer*) handle;

	/* If we set a timeout of 0, we can query the command buffer state */
	return renderer->vkWaitForFences(
		renderer->logicalDevice,
		1,
		&commandBuffer->inFlightFence,
		VK_TRUE,
		0
	) == VK_SUCCESS;
}

static void VULKAN_CommandBuffer_WaitForFences(
	FNA3D_Renderer *driverData,
	FNA3D_CommandBuffer **handles,
	size_t handleCount
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer **commandBuffers = (VulkanCommandBuffer**) handles;
	VkFence *fences;
	uint32_t i;

	fences = SDL_stack_alloc(VkFence, handleCount);
	for (i = 0; i < handleCount; i += 1)
	{
		fences[i] = commandBuffers[i]->inFlightFence;
	}

	renderer->vkWaitForFences(
		renderer->logicalDevice,
		handleCount,
		fences,
		VK_TRUE,
		UINT64_MAX
	);

	SDL_stack_free(fences);
}

static FNA3D_BufferHandle* VULKAN_CommandBuffer_CreateTransferBuffer(
	FNA3D_Renderer *driverData,
	size_t size,
	uint8_t preferDeviceLocal
) {
	return (FNA3D_BufferHandle*) VULKAN_INTERNAL_CreateBuffer(
		(VulkanRenderer*) driverData,
		size,
		RESOURCE_ACCESS_MEMORY_TRANSFER_READ_WRITE,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		preferDeviceLocal,
		1
	);
}

static void VULKAN_CommandBuffer_IncBufferRef(
	FNA3D_Renderer *driverData,
	FNA3D_BufferHandle *handle
) {
	VulkanBuffer *vulkanBuffer = (VulkanBuffer*) handle;
	SDL_AtomicIncRef(&vulkanBuffer->refcount);
}

static void VULKAN_CommandBuffer_DecBufferRef(
	FNA3D_Renderer *driverData,
	FNA3D_BufferHandle *handle
) {
	VulkanBuffer *vulkanBuffer = (VulkanBuffer*) handle;
	SDL_AtomicDecRef(&vulkanBuffer->refcount);
}

static size_t VULKAN_CommandBuffer_GetBufferSize(
	FNA3D_Renderer *driverData,
	FNA3D_BufferHandle *handle
) {
	VulkanBuffer *vulkanBuffer = (VulkanBuffer*) handle;
	return vulkanBuffer->size;
}

static void VULKAN_CommandBuffer_DestroyTexture(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture
) {
	VULKAN_INTERNAL_DestroyTexture(
		(VulkanRenderer*) driverData,
		(VulkanTexture*) texture
	);
}

static void VULKAN_CommandBuffer_DestroyBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_BufferHandle *buffer
) {
	VULKAN_INTERNAL_DestroyBuffer(
		(VulkanRenderer*) driverData,
		(VulkanBuffer*) buffer
	);
}

static void VULKAN_CommandBuffer_DestroyRenderbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Renderbuffer *renderbuffer
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanRenderbuffer *vkRenderbuffer = (VulkanRenderbuffer*) renderbuffer;
	uint8_t isDepthStencil = (vkRenderbuffer->colorBuffer == NULL);

	if (isDepthStencil)
	{
		VULKAN_INTERNAL_DestroyTexture(
			renderer,
			vkRenderbuffer->depthBuffer->handle
		);
		SDL_free(vkRenderbuffer->depthBuffer);
	}
	else
	{
		if (vkRenderbuffer->colorBuffer->multiSampleTexture != NULL)
		{
			VULKAN_INTERNAL_DestroyTexture(
				renderer,
				vkRenderbuffer->colorBuffer->multiSampleTexture
			);
		}

		/* The image is owned by the texture it's from,
		 * so we don't free it here.
		 */
		SDL_free(vkRenderbuffer->colorBuffer);
	}

	SDL_free(vkRenderbuffer);
}

static void VULKAN_CommandBuffer_DestroyEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanEffect *vulkanEffect = (VulkanEffect*) effect;
	MOJOSHADER_effect *effectData = vulkanEffect->effect;

	if (effectData == renderer->currentEffect)
	{
		MOJOSHADER_effectEndPass(renderer->currentEffect);
		MOJOSHADER_effectEnd(renderer->currentEffect);
		renderer->currentEffect = NULL;
		renderer->currentTechnique = NULL;
		renderer->currentPass = 0;
	}
	MOJOSHADER_deleteEffect(effectData);
	SDL_free(vulkanEffect);
}

/* Driver */

static uint8_t VULKAN_PrepareWindowAttributes(uint32_t *flags)
{
	SDL_Window *dummyWindowHandle;
	VkSurfaceKHR surface;
	FNA3D_PresentationParameters presentationParameters;
	VulkanRenderer *renderer;
	uint8_t result;

	/* Required for MoltenVK support */
	SDL_setenv("MVK_CONFIG_FULL_IMAGE_VIEW_SWIZZLE", "1", 1);
	SDL_setenv("MVK_CONFIG_SHADER_CONVERSION_FLIP_VERTEX_Y", "0", 1);

	if (SDL_Vulkan_LoadLibrary(NULL) < 0)
	{
		FNA3D_LogWarn("Vulkan: SDL_Vulkan_LoadLibrary failed!");
		return 0;
	}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
	vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) SDL_Vulkan_GetVkGetInstanceProcAddr();
#pragma GCC diagnostic pop
	if (vkGetInstanceProcAddr == NULL)
	{
		FNA3D_LogWarn(
			"SDL_Vulkan_GetVkGetInstanceProcAddr(): %s",
			SDL_GetError()
		);
		return 0;
	}

	#define VULKAN_GLOBAL_FUNCTION(name) \
		name = (PFN_##name) vkGetInstanceProcAddr(VK_NULL_HANDLE, #name); \
		if (name == NULL) \
		{ \
			FNA3D_LogWarn("vkGetInstanceProcAddr(VK_NULL_HANDLE, \"" #name "\") failed"); \
			return 0; \
		}
	#include "FNA3D_Driver_Vulkan_vkfuncs.h"

	/* Test if we can create a vulkan device */

	/* Create a dummy window, otherwise we cannot query swapchain support */
	dummyWindowHandle = SDL_CreateWindow(
		"FNA3D Vulkan",
		0, 0,
		128, 128,
		SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN
	);

	if (dummyWindowHandle == NULL)
	{
		FNA3D_LogWarn("Vulkan: Could not create dummy window");
		return 0;
	}

	presentationParameters.deviceWindowHandle = dummyWindowHandle;

	/* partially set up VulkanRenderer so we can fall back in case of device non-compliance */
	renderer = (VulkanRenderer*) SDL_malloc(sizeof(VulkanRenderer));
	SDL_memset(renderer, '\0', sizeof(VulkanRenderer));

	if (!VULKAN_INTERNAL_CreateInstance(renderer, &presentationParameters))
	{
		SDL_DestroyWindow(dummyWindowHandle);
		SDL_free(renderer);
		FNA3D_LogWarn("Vulkan: Could not create Vulkan instance");
		return 0;
	}

	if (!SDL_Vulkan_CreateSurface(
		(SDL_Window*) presentationParameters.deviceWindowHandle,
		renderer->instance,
		&surface
	)) {
		SDL_DestroyWindow(dummyWindowHandle);
		SDL_free(renderer);
		FNA3D_LogWarn(
			"SDL_Vulkan_CreateSurface failed: %s",
			SDL_GetError()
		);
		return 0;
	}

	#define VULKAN_INSTANCE_FUNCTION(name) \
		renderer->name = (PFN_##name) vkGetInstanceProcAddr(renderer->instance, #name);
	#include "FNA3D_Driver_Vulkan_vkfuncs.h"

	result = VULKAN_INTERNAL_DeterminePhysicalDevice(renderer, surface);

	renderer->vkDestroySurfaceKHR(
		renderer->instance,
		surface,
		NULL
	);
	renderer->vkDestroyInstance(renderer->instance, NULL);
	SDL_DestroyWindow(dummyWindowHandle);
	SDL_free(renderer);

	if (!result)
	{
		FNA3D_LogWarn("Vulkan: Failed to determine a suitable physical device");
	}
	else
	{
		*flags = SDL_WINDOW_VULKAN;
	}
	return result;
}

static FNA3D_Device* VULKAN_CreateDevice(
	FNA3D_PresentationParameters *presentationParameters,
	uint8_t debugMode
) {
	uint32_t i;
	VkResult vulkanResult;

	/* Variables: Create the FNA3D_Device */
	FNA3D_Device *result;
	VulkanRenderer *renderer;

	/* Variables: Initialize memory allocator */
	FNA3D_MemoryDriver memoryDriver;

	/* Variables: Choose depth formats */
	VkImageFormatProperties imageFormatProperties;

	/* Variables: Create command pool and command buffer */
	VkCommandPoolCreateInfo commandPoolCreateInfo;
	FNA3D_CommandBufferDriver commandBufferDriver;

	/* Variables: Create semaphores */
	VkSemaphoreCreateInfo semaphoreInfo;

	/* Variables: Create pipeline cache */
	VkPipelineCacheCreateInfo pipelineCacheCreateInfo;
	const char *pipelineCacheFileName;
	size_t pipelineCacheSize;
	uint8_t *pipelineCacheBytes;

	/* Variables: Create descriptor set layouts */
	VkDescriptorSetLayoutBinding layoutBinding;
	VkDescriptorSetLayoutCreateInfo layoutCreateInfo;

	/* Variables: Check for DXT1/S3TC Support */
	VkFormatProperties formatPropsBC1, formatPropsBC2, formatPropsBC3, formatPropsBC7;

	/* Variables: Check for SRGB Render Target Support */
	VkFormatProperties formatPropsSrgbRT;

	/* Variables: Check for Precise Occlusion Query Support */
	VkPhysicalDeviceFeatures physicalDeviceFeatures;

	/* Variables: Create query pool */
	VkQueryPoolCreateInfo queryPoolCreateInfo;

	/* Variables: Create dummy data */
	VkSamplerCreateInfo samplerCreateInfo;

	/* Variables: Create UBO pool and dummy UBO descriptor sets */
	VkDescriptorPoolSize descriptorPoolSize;
	VkDescriptorPoolCreateInfo descriptorPoolInfo;
	VkDescriptorSetAllocateInfo descriptorSetAllocateInfo;
	VkWriteDescriptorSet writeDescriptorSets[2];
	VkDescriptorBufferInfo bufferInfos[2];

	/* Variables: Create dummy surface for initialization */
	VkSurfaceKHR surface;

	/*
	 * Create the FNA3D_Device
	 */

	result = (FNA3D_Device*) SDL_malloc(sizeof(FNA3D_Device));
	ASSIGN_DRIVER(VULKAN)

	renderer = (VulkanRenderer*) SDL_malloc(sizeof(VulkanRenderer));
	SDL_memset(renderer, '\0', sizeof(VulkanRenderer));
	renderer->debugMode = debugMode;
	renderer->parentDevice = result;
	result->driverData = (FNA3D_Renderer*) renderer;

	/*
	 * Initialize memory allocator
	 */

	ASSIGN_MEMORY_DRIVER(VULKAN)
	renderer->allocator = FNA3D_CreateMemoryAllocator(
		&memoryDriver,
		VK_MAX_MEMORY_TYPES
	);

	/*
	 * Create the vkInstance
	 */

	if (!VULKAN_INTERNAL_CreateInstance(renderer, presentationParameters))
	{
		FNA3D_LogError("Error creating vulkan instance");
		return NULL;
	}

	/*
	 * Create the dummy surface
	 */

	if (!SDL_Vulkan_CreateSurface(
		(SDL_Window*) presentationParameters->deviceWindowHandle,
		renderer->instance,
		&surface
	)) {
		FNA3D_LogError(
			"SDL_Vulkan_CreateSurface failed: %s",
			SDL_GetError()
		);
		return NULL;
	}

	/*
	 * Get vkInstance entry points
	 */

	#define VULKAN_INSTANCE_FUNCTION(name) \
		renderer->name = (PFN_##name) vkGetInstanceProcAddr(renderer->instance, #name);
	#include "FNA3D_Driver_Vulkan_vkfuncs.h"

	/*
	 * Choose/Create vkDevice
	 */

	if (!VULKAN_INTERNAL_DeterminePhysicalDevice(renderer, surface))
	{
		FNA3D_LogError("Failed to determine a suitable physical device");
		return NULL;
	}

	renderer->vkDestroySurfaceKHR(renderer->instance, surface, NULL);

	FNA3D_LogInfo("FNA3D Driver: Vulkan");
	FNA3D_LogInfo(
		"Vulkan Device: %s",
		renderer->physicalDeviceProperties.properties.deviceName
	);
	if (	renderer->supportsDeviceProperties2 &&
		renderer->supports.KHR_driver_properties	)
	{
		FNA3D_LogInfo(
			"Vulkan Driver: %s %s",
			renderer->physicalDeviceDriverProperties.driverName,
			renderer->physicalDeviceDriverProperties.driverInfo
		);
		FNA3D_LogInfo(
			"Vulkan Conformance: %u.%u.%u",
			renderer->physicalDeviceDriverProperties.conformanceVersion.major,
			renderer->physicalDeviceDriverProperties.conformanceVersion.minor,
			renderer->physicalDeviceDriverProperties.conformanceVersion.patch
		);
	}
	else
	{
		FNA3D_LogInfo("KHR_driver_properties unsupported! Bother your vendor about this!");
	}

	if (!VULKAN_INTERNAL_CreateLogicalDevice(renderer))
	{
		FNA3D_LogError("Failed to create logical device");
		return NULL;
	}

	/*
	 * Choose depth formats
	 */

	vulkanResult = renderer->vkGetPhysicalDeviceImageFormatProperties(
		renderer->physicalDevice,
		VK_FORMAT_D16_UNORM,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_ASPECT_DEPTH_BIT,
		0,
		&imageFormatProperties
	);

	if (vulkanResult == VK_ERROR_FORMAT_NOT_SUPPORTED)
	{
		renderer->D16Format = VK_FORMAT_D32_SFLOAT;
	}
	else
	{
		renderer->D16Format = VK_FORMAT_D16_UNORM;
	}

	/* Vulkan doesn't even have plain D24 in the spec! */
	renderer->D24Format = VK_FORMAT_D32_SFLOAT;

	vulkanResult = renderer->vkGetPhysicalDeviceImageFormatProperties(
		renderer->physicalDevice,
		VK_FORMAT_D24_UNORM_S8_UINT,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
		0,
		&imageFormatProperties
	);

	if (vulkanResult == VK_ERROR_FORMAT_NOT_SUPPORTED)
	{
		renderer->D24S8Format = VK_FORMAT_D32_SFLOAT_S8_UINT;
	}
	else
	{
		renderer->D24S8Format = VK_FORMAT_D24_UNORM_S8_UINT;
	}

	/*
	 * Create MojoShader context
	 */

	renderer->mojoshaderContext = MOJOSHADER_vkCreateContext(
		&renderer->instance,
		&renderer->physicalDevice,
		&renderer->logicalDevice,
		(PFN_MOJOSHADER_vkGetInstanceProcAddr) vkGetInstanceProcAddr,
		(PFN_MOJOSHADER_vkGetDeviceProcAddr) renderer->vkGetDeviceProcAddr,
		renderer->queueFamilyIndex,
		renderer->physicalDeviceProperties.properties.limits.maxUniformBufferRange,
		renderer->physicalDeviceProperties.properties.limits.minUniformBufferOffsetAlignment,
		NULL,
		NULL,
		renderer
	);
	if (renderer->mojoshaderContext == NULL)
	{
		FNA3D_LogError("Failed to create MojoShader context");
		return NULL;
	}

	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreInfo.pNext = NULL;
	semaphoreInfo.flags = 0;

	vulkanResult = renderer->vkCreateSemaphore(
		renderer->logicalDevice,
		&semaphoreInfo,
		NULL,
		&renderer->defragSemaphore
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkCreateSemaphore, NULL)

	/*
	 * Create command pool and buffers
	 */

	commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolCreateInfo.pNext = NULL;
	commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	commandPoolCreateInfo.queueFamilyIndex = renderer->queueFamilyIndex;
	vulkanResult = renderer->vkCreateCommandPool(
		renderer->logicalDevice,
		&commandPoolCreateInfo,
		NULL,
		&renderer->commandPool
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkCreateCommandPool, NULL)

	ASSIGN_COMMANDBUFFER_DRIVER(VULKAN)
	renderer->commandBuffers = FNA3D_CreateCommandBufferManager(&commandBufferDriver);
	FNA3D_CommandBuffer_BeginRecording(renderer->commandBuffers);

	/*
	 * Create the initial faux-backbuffer
	 */

	if (!VULKAN_INTERNAL_CreateFauxBackbuffer(renderer, presentationParameters))
	{
		FNA3D_LogError("Failed to create faux backbuffer");
		return NULL;
	}

	/*
	 * Create initial swapchain
	 */

	renderer->swapchainDataCapacity = 1;
	renderer->swapchainDataCount = 0;
	renderer->swapchainDatas = SDL_malloc(renderer->swapchainDataCapacity * sizeof(VulkanSwapchainData*));

	if (VULKAN_INTERNAL_CreateSwapchain(renderer, presentationParameters->deviceWindowHandle) != CREATE_SWAPCHAIN_SUCCESS)
	{
		FNA3D_LogError("Failed to create swap chain");
		return NULL;
	}

	/*
	 * Create the pipeline cache
	 */

	pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	pipelineCacheCreateInfo.pNext = NULL;
	pipelineCacheCreateInfo.flags = 0;

	pipelineCacheFileName = SDL_GetHint("FNA3D_VULKAN_PIPELINE_CACHE_FILE_NAME");
	if (pipelineCacheFileName == NULL)
	{
		pipelineCacheFileName = DEFAULT_PIPELINE_CACHE_FILE_NAME;
	}
	if (pipelineCacheFileName[0] == '\0')
	{
		/* For intentionally empty file names, assume caching is disabled */
		pipelineCacheBytes = NULL;
	}
	else
	{
		pipelineCacheBytes = SDL_LoadFile(pipelineCacheFileName, &pipelineCacheSize);
	}

	if (pipelineCacheBytes != NULL)
	{
		FNA3D_LogInfo("Pipeline cache found, loading...");
		pipelineCacheCreateInfo.pInitialData = pipelineCacheBytes;
		pipelineCacheCreateInfo.initialDataSize = pipelineCacheSize;
	}
	else
	{
		pipelineCacheCreateInfo.initialDataSize = 0;
		pipelineCacheCreateInfo.pInitialData = NULL;
	}

	vulkanResult = renderer->vkCreatePipelineCache(
		renderer->logicalDevice,
		&pipelineCacheCreateInfo,
		NULL,
		&renderer->pipelineCache
	);

	if (pipelineCacheBytes != NULL)
	{
		SDL_free(pipelineCacheBytes);

		/* The pipeline cache was invalid, try again with no input data */
		if (vulkanResult != VK_SUCCESS)
		{
			FNA3D_LogWarn("Pipeline cache preload failed, ignoring");
			pipelineCacheCreateInfo.initialDataSize = 0;
			pipelineCacheCreateInfo.pInitialData = NULL;
			vulkanResult = renderer->vkCreatePipelineCache(
				renderer->logicalDevice,
				&pipelineCacheCreateInfo,
				NULL,
				&renderer->pipelineCache
			);
		}
	}

	VULKAN_ERROR_CHECK(vulkanResult, vkCreatePipelineCache, NULL)

	/*
	 * Define sampler counts
	 */

	renderer->numTextureSlots = SDL_min(
		renderer->physicalDeviceProperties.properties.limits.maxPerStageDescriptorSamplers,
		MAX_TEXTURE_SAMPLERS
	);
	renderer->numVertexTextureSlots = SDL_min(
		renderer->physicalDeviceProperties.properties.limits.maxPerStageDescriptorSamplers,
		MAX_VERTEXTEXTURE_SAMPLERS
	);

	/* Define vertex UBO set layout */
	layoutBinding.binding = 0;
	layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	layoutBinding.descriptorCount = 1;
	layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	layoutBinding.pImmutableSamplers = NULL;

	layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutCreateInfo.pNext = NULL;
	layoutCreateInfo.flags = 0;
	layoutCreateInfo.bindingCount = 1;
	layoutCreateInfo.pBindings = &layoutBinding;

	vulkanResult = renderer->vkCreateDescriptorSetLayout(
		renderer->logicalDevice,
		&layoutCreateInfo,
		NULL,
		&renderer->vertexUniformBufferDescriptorSetLayout
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkCreateDescriptorSetLayout, NULL)

	/* Define frag UBO set layout */
	layoutBinding.binding = 0;
	layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	layoutBinding.descriptorCount = 1;
	layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	layoutBinding.pImmutableSamplers = NULL;

	layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutCreateInfo.pNext = NULL;
	layoutCreateInfo.flags = 0;
	layoutCreateInfo.bindingCount = 1;
	layoutCreateInfo.pBindings = &layoutBinding;

	vulkanResult = renderer->vkCreateDescriptorSetLayout(
		renderer->logicalDevice,
		&layoutCreateInfo,
		NULL,
		&renderer->fragUniformBufferDescriptorSetLayout
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkCreateDescriptorSetLayout, NULL)

	renderer->vertexSamplerDescriptorSetDataNeedsUpdate = 1;
	renderer->fragSamplerDescriptorSetDataNeedsUpdate = 1;

	/*
	 * Init various renderer properties
	 */

	renderer->currentDepthFormat = presentationParameters->depthStencilFormat;
	renderer->currentPipeline = VK_NULL_HANDLE;
	renderer->needNewRenderPass = 1;
	renderer->needNewPipeline = 1;

	/*
	 * Check for DXT1/S3TC support
	 */

	renderer->vkGetPhysicalDeviceFormatProperties(
		renderer->physicalDevice,
		XNAToVK_SurfaceFormat[FNA3D_SURFACEFORMAT_DXT1],
		&formatPropsBC1
	);
	renderer->vkGetPhysicalDeviceFormatProperties(
		renderer->physicalDevice,
		XNAToVK_SurfaceFormat[FNA3D_SURFACEFORMAT_DXT3],
		&formatPropsBC2
	);
	renderer->vkGetPhysicalDeviceFormatProperties(
		renderer->physicalDevice,
		XNAToVK_SurfaceFormat[FNA3D_SURFACEFORMAT_DXT5],
		&formatPropsBC3
	);
	renderer->vkGetPhysicalDeviceFormatProperties(
		renderer->physicalDevice,
		XNAToVK_SurfaceFormat[FNA3D_SURFACEFORMAT_COLORSRGB_EXT],
		&formatPropsSrgbRT
	);
	renderer->vkGetPhysicalDeviceFormatProperties(
		renderer->physicalDevice,
		XNAToVK_SurfaceFormat[FNA3D_SURFACEFORMAT_BC7_EXT],
		&formatPropsBC7
	);

	#define SUPPORTED_FORMAT(fmt) \
		((fmt.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) && \
		(fmt.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT))
	renderer->supportsDxt1 = SUPPORTED_FORMAT(formatPropsBC1);
	renderer->supportsS3tc = (
		SUPPORTED_FORMAT(formatPropsBC2) ||
		SUPPORTED_FORMAT(formatPropsBC3)
	);
	renderer->supportsBc7 = SUPPORTED_FORMAT(formatPropsBC7);

	renderer->supportsSRGBRenderTarget = (
		SUPPORTED_FORMAT(formatPropsSrgbRT) && (formatPropsSrgbRT.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
	);

	renderer->vkGetPhysicalDeviceFeatures(
		renderer->physicalDevice,
		&physicalDeviceFeatures
	);
	renderer->supportsPreciseOcclusionQueries = physicalDeviceFeatures.occlusionQueryPrecise;

#ifdef __APPLE__
	/* The iOS/tvOS simulator and some older (~A8) devices don't support base vertex,
	 * and unfortunately that's not a queryable Vulkan device feature/property.
	 * However, according to the Metal Feature Set tables, any device that supports
	 * "counting occlusion queries" also supports base vertex. So we'll check for that.
	 * -caleb
	 */
	renderer->supportsBaseVertex = renderer->supportsPreciseOcclusionQueries;
#else
	renderer->supportsBaseVertex = 1;
#endif

	/*
	 * Initialize renderer members not covered by SDL_memset('\0')
	 */

	SDL_memset(
		renderer->multiSampleMask,
		-1,
		sizeof(renderer->multiSampleMask) /* AKA 0xFFFFFFFF */
	);
	for (i = 0; i < MAX_BOUND_VERTEX_BUFFERS; i += 1)
	{
		renderer->vertexBindings[i].vertexDeclaration.elements =
			renderer->vertexElements[i];
	}

	/*
	 * Create query pool
	 */

	queryPoolCreateInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	queryPoolCreateInfo.pNext = NULL;
	queryPoolCreateInfo.flags = 0;
	queryPoolCreateInfo.queryType = VK_QUERY_TYPE_OCCLUSION;
	queryPoolCreateInfo.queryCount = MAX_QUERIES;
	queryPoolCreateInfo.pipelineStatistics = 0;
	vulkanResult = renderer->vkCreateQueryPool(
		renderer->logicalDevice,
		&queryPoolCreateInfo,
		NULL,
		&renderer->queryPool
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkCreateQueryPool, NULL)

	/* Set up the stack, the value at each index is the next available
	 * index, or -1 if no such index exists.
	 */
	for (i = 0; i < MAX_QUERIES - 1; i += 1)
	{
		renderer->freeQueryIndexStack[i] = i + 1;
	}
	renderer->freeQueryIndexStack[MAX_QUERIES - 1] = -1;

	/* Initialize hash tables */

	for (i = 0; i < NUM_SHADER_RESOURCES_BUCKETS; i += 1)
	{
		renderer->shaderResourcesHashTable.buckets[i].elements = NULL;
		renderer->shaderResourcesHashTable.buckets[i].count = 0;
		renderer->shaderResourcesHashTable.buckets[i].capacity = 0;
	}

	for (i = 0; i < NUM_DESCRIPTOR_SET_LAYOUT_BUCKETS; i += 1)
	{
		renderer->descriptorSetLayoutTable.buckets[i].elements = NULL;
		renderer->descriptorSetLayoutTable.buckets[i].count = 0;
		renderer->descriptorSetLayoutTable.buckets[i].capacity = 0;
	}
	/*
	 * Create dummy data
	 */

	renderer->dummyVertTexture = (VulkanTexture*) VULKAN_CreateTexture2D(
		(FNA3D_Renderer*) renderer,
		FNA3D_SURFACEFORMAT_COLOR,
		1,
		1,
		1,
		1
	);
	renderer->dummyVertTexture3D = (VulkanTexture*) VULKAN_CreateTexture3D(
		(FNA3D_Renderer*) renderer,
		FNA3D_SURFACEFORMAT_COLOR,
		1,
		1,
		1,
		1
	);
	renderer->dummyVertTextureCube = (VulkanTexture*) VULKAN_CreateTextureCube(
		(FNA3D_Renderer*) renderer,
		FNA3D_SURFACEFORMAT_COLOR,
		1,
		1,
		0
	);
	renderer->dummyFragTexture = (VulkanTexture*) VULKAN_CreateTexture2D(
		(FNA3D_Renderer*) renderer,
		FNA3D_SURFACEFORMAT_COLOR,
		1,
		1,
		1,
		1
	);
	renderer->dummyFragTexture3D = (VulkanTexture*) VULKAN_CreateTexture3D(
		(FNA3D_Renderer*) renderer,
		FNA3D_SURFACEFORMAT_COLOR,
		1,
		1,
		1,
		1
	);
	renderer->dummyFragTextureCube = (VulkanTexture*) VULKAN_CreateTextureCube(
		(FNA3D_Renderer*) renderer,
		FNA3D_SURFACEFORMAT_COLOR,
		1,
		1,
		0
	);
	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_VERTEX_SHADER_READ_SAMPLED_IMAGE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		1,
		0,
		1,
		1,
		renderer->dummyVertTexture->image,
		&renderer->dummyVertTexture->resourceAccessType
	);
	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_VERTEX_SHADER_READ_SAMPLED_IMAGE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		1,
		0,
		1,
		1,
		renderer->dummyVertTexture3D->image,
		&renderer->dummyVertTexture3D->resourceAccessType
	);
	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_VERTEX_SHADER_READ_SAMPLED_IMAGE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		6,
		0,
		1,
		1,
		renderer->dummyVertTextureCube->image,
		&renderer->dummyVertTextureCube->resourceAccessType
	);
	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_FRAGMENT_SHADER_READ_SAMPLED_IMAGE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		1,
		0,
		1,
		1,
		renderer->dummyFragTexture->image,
		&renderer->dummyFragTexture->resourceAccessType
	);
	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_FRAGMENT_SHADER_READ_SAMPLED_IMAGE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		1,
		0,
		1,
		1,
		renderer->dummyFragTexture3D->image,
		&renderer->dummyFragTexture3D->resourceAccessType
	);
	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_FRAGMENT_SHADER_READ_SAMPLED_IMAGE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		6,
		0,
		1,
		1,
		renderer->dummyFragTextureCube->image,
		&renderer->dummyFragTextureCube->resourceAccessType
	);

	renderer->dummyVertUniformBuffer = (VulkanBuffer*) VULKAN_INTERNAL_CreateBuffer(
		renderer,
		1,
		RESOURCE_ACCESS_VERTEX_SHADER_READ_UNIFORM_BUFFER,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		0,
		0
	);
	SDL_memset(
		FNA3D_Memory_GetHostPointer(
			renderer->dummyVertUniformBuffer->usedRegion,
			0
		),
		0,
		1
	);

	renderer->dummyFragUniformBuffer = (VulkanBuffer*) VULKAN_INTERNAL_CreateBuffer(
		renderer,
		1,
		RESOURCE_ACCESS_FRAGMENT_SHADER_READ_UNIFORM_BUFFER,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		0,
		0
	);
	SDL_memset(
		FNA3D_Memory_GetHostPointer(
			renderer->dummyFragUniformBuffer->usedRegion,
			0
		),
		0,
		1
	);

	samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerCreateInfo.pNext = NULL;
	samplerCreateInfo.flags = 0;
	samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.mipLodBias = 0;
	samplerCreateInfo.anisotropyEnable = 0;
	samplerCreateInfo.maxAnisotropy = 1;
	samplerCreateInfo.compareEnable = 0;
	samplerCreateInfo.compareOp = 0;
	samplerCreateInfo.minLod = 0;
	samplerCreateInfo.maxLod = 1;
	samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	samplerCreateInfo.unnormalizedCoordinates = 0;
	renderer->vkCreateSampler(
		renderer->logicalDevice,
		&samplerCreateInfo,
		NULL,
		&renderer->dummyVertSamplerState
	);
	renderer->vkCreateSampler(
		renderer->logicalDevice,
		&samplerCreateInfo,
		NULL,
		&renderer->dummyVertSampler3DState
	);
	renderer->vkCreateSampler(
		renderer->logicalDevice,
		&samplerCreateInfo,
		NULL,
		&renderer->dummyVertSamplerCubeState
	);
	renderer->vkCreateSampler(
		renderer->logicalDevice,
		&samplerCreateInfo,
		NULL,
		&renderer->dummyFragSamplerState
	);
	renderer->vkCreateSampler(
		renderer->logicalDevice,
		&samplerCreateInfo,
		NULL,
		&renderer->dummyFragSampler3DState
	);
	renderer->vkCreateSampler(
		renderer->logicalDevice,
		&samplerCreateInfo,
		NULL,
		&renderer->dummyFragSamplerCubeState
	);

	descriptorPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	descriptorPoolSize.descriptorCount = MAX_UNIFORM_DESCRIPTOR_SETS;

	descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolInfo.pNext = NULL;
	descriptorPoolInfo.flags = 0;
	descriptorPoolInfo.maxSets = MAX_UNIFORM_DESCRIPTOR_SETS;
	descriptorPoolInfo.poolSizeCount = 1;
	descriptorPoolInfo.pPoolSizes = &descriptorPoolSize;

	vulkanResult = renderer->vkCreateDescriptorPool(
		renderer->logicalDevice,
		&descriptorPoolInfo,
		NULL,
		&renderer->uniformBufferDescriptorPool
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkCreateDescriptorPool, 0)

	descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAllocateInfo.pNext = NULL;
	descriptorSetAllocateInfo.descriptorPool = renderer->uniformBufferDescriptorPool;
	descriptorSetAllocateInfo.descriptorSetCount = 1;
	descriptorSetAllocateInfo.pSetLayouts = &renderer->vertexUniformBufferDescriptorSetLayout;

	vulkanResult = renderer->vkAllocateDescriptorSets(
		renderer->logicalDevice,
		&descriptorSetAllocateInfo,
		&renderer->dummyVertexUniformBufferDescriptorSet
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkAllocateDescriptorSets, 0)

	descriptorSetAllocateInfo.pSetLayouts = &renderer->fragUniformBufferDescriptorSetLayout;

	vulkanResult = renderer->vkAllocateDescriptorSets(
		renderer->logicalDevice,
		&descriptorSetAllocateInfo,
		&renderer->dummyFragUniformBufferDescriptorSet
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkAllocateDescriptorSets, 0)

	bufferInfos[0].buffer = renderer->dummyVertUniformBuffer->buffer;
	bufferInfos[0].offset = 0;
	bufferInfos[0].range = renderer->dummyVertUniformBuffer->size;

	writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeDescriptorSets[0].pNext = NULL;
	writeDescriptorSets[0].descriptorCount = 1;
	writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	writeDescriptorSets[0].dstArrayElement = 0;
	writeDescriptorSets[0].dstBinding = 0;
	writeDescriptorSets[0].dstSet = renderer->dummyVertexUniformBufferDescriptorSet;
	writeDescriptorSets[0].pBufferInfo = &bufferInfos[0];
	writeDescriptorSets[0].pImageInfo = NULL;
	writeDescriptorSets[0].pTexelBufferView = NULL;

	bufferInfos[1].buffer = renderer->dummyFragUniformBuffer->buffer;
	bufferInfos[1].offset = 0;
	bufferInfos[1].range = renderer->dummyFragUniformBuffer->size;

	writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeDescriptorSets[1].pNext = NULL;
	writeDescriptorSets[1].descriptorCount = 1;
	writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	writeDescriptorSets[1].dstArrayElement = 0;
	writeDescriptorSets[1].dstBinding = 0;
	writeDescriptorSets[1].dstSet = renderer->dummyFragUniformBufferDescriptorSet;
	writeDescriptorSets[1].pBufferInfo = &bufferInfos[1];
	writeDescriptorSets[1].pImageInfo = NULL;
	writeDescriptorSets[1].pTexelBufferView = NULL;

	renderer->vkUpdateDescriptorSets(
		renderer->logicalDevice,
		2,
		writeDescriptorSets,
		0,
		NULL
	);

	/* init texture storage */

	for (i = 0; i < MAX_TEXTURE_SAMPLERS; i += 1)
	{
		renderer->textures[i] = &NullTexture;
		renderer->samplers[i] = renderer->dummyFragSamplerState;
	}

	for (i = 0; i < MAX_VERTEXTEXTURE_SAMPLERS; i += 1)
	{
		renderer->textures[MAX_TEXTURE_SAMPLERS + i] = &NullTexture;
		renderer->samplers[MAX_TEXTURE_SAMPLERS + i] = renderer->dummyVertSamplerState;
	}

	renderer->bufferDefragInProgress = 0;
	renderer->needDefrag = 0;
	renderer->defragTimer = 0;
	renderer->resourceFreed = 0;

	renderer->defragmentedBuffersToDestroyCapacity = 16;
	renderer->defragmentedBuffersToDestroyCount = 0;

	renderer->defragmentedBuffersToDestroy = (VkBuffer*) SDL_malloc(
		sizeof(VkBuffer) *
		renderer->defragmentedBuffersToDestroyCapacity
	);

	renderer->defragmentedImagesToDestroyCapacity = 16;
	renderer->defragmentedImagesToDestroyCount = 0;

	renderer->defragmentedImagesToDestroy = (VkImage*) SDL_malloc(
		sizeof(VkImage) *
		renderer->defragmentedImagesToDestroyCapacity
	);

	renderer->defragmentedImageViewsToDestroyCapacity = 16;
	renderer->defragmentedImageViewsToDestroyCount = 0;

	renderer->defragmentedImageViewsToDestroy = (VkImageView*) SDL_malloc(
		sizeof(VkImageView) *
		renderer->defragmentedImageViewsToDestroyCapacity
	);

	renderer->passLock = SDL_CreateMutex();
	renderer->disposeLock = SDL_CreateMutex();

	return result;
}

FNA3D_Driver VulkanDriver = {
	"Vulkan",
	VULKAN_PrepareWindowAttributes,
	VULKAN_CreateDevice
};

#else

extern int this_tu_is_empty;

#endif /* FNA3D_DRIVER_VULKAN */
