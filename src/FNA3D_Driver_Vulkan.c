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

#if FNA3D_DRIVER_VULKAN

#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"

#include "FNA3D_Driver.h"
#include "FNA3D_PipelineCache.h"
#include "stb_ds.h"

#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_vulkan.h>

/* Global Vulkan Loader Entry Points */

static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = NULL;

#define VULKAN_GLOBAL_FUNCTION(name) \
	static PFN_##name name = NULL;
#include "FNA3D_Driver_Vulkan_vkfuncs.h"

/* vkInstance/vkDevice function typedefs */

#define VULKAN_INSTANCE_FUNCTION(ext, ret, func, params) \
	typedef ret (VKAPI_CALL *vkfntype_##func) params;
#define VULKAN_DEVICE_FUNCTION(ext, ret, func, params) \
	typedef ret (VKAPI_CALL *vkfntype_##func) params;
#include "FNA3D_Driver_Vulkan_vkfuncs.h"

/* Constants/Limits */

#define TEXTURE_COUNT MAX_TOTAL_SAMPLERS
#define MAX_MULTISAMPLE_MASK_SIZE 2
#define MAX_QUERIES 16

/* Should be equivalent to the number of values in FNA3D_PrimitiveType */
#define PRIMITIVE_TYPES_COUNT 5

#define SAMPLER_DESCRIPTOR_POOL_SIZE 256
#define UNIFORM_BUFFER_DESCRIPTOR_POOL_SIZE 32

#define NULL_BUFFER (VkBuffer) 0
#define NULL_DESC_SET (VkDescriptorSet) 0
#define NULL_FENCE (VkFence) 0
#define NULL_FRAMEBUFFER (VkFramebuffer) 0
#define NULL_IMAGE_VIEW (VkImageView) 0
#define NULL_PIPELINE (VkPipeline) 0
#define NULL_PIPELINE_LAYOUT (VkPipelineLayout) 0
#define NULL_SAMPLER (VkSampler) 0
#define NULL_RENDER_PASS (VkRenderPass) 0

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

/* Command Encoding */

typedef enum VulkanCommandType
{
	CMDTYPE_BIND_DESCRIPTOR_SETS,
	CMDTYPE_BIND_INDEX_BUFFER,
	CMDTYPE_BIND_PIPELINE,
	CMDTYPE_BIND_VERTEX_BUFFERS,
	CMDTYPE_CLEAR_ATTACHMENTS,
	CMDTYPE_CLEAR_COLOR_IMAGE,
	CMDTYPE_CLEAR_DEPTH_STENCIL_IMAGE,
	CMDTYPE_DRAW,
	CMDTYPE_DRAW_INDEXED,
	CMDTYPE_SET_BLEND_CONSTANTS,
	CMDTYPE_SET_DEPTH_BIAS,
	CMDTYPE_BLIT_IMAGE,
	CMDTYPE_COPY_BUFFER_TO_IMAGE,
	CMDTYPE_COPY_IMAGE_TO_BUFFER,
	CMDTYPE_PIPELINE_BARRIER,
	CMDTYPE_SET_SCISSOR,
	CMDTYPE_SET_VIEWPORT,
	CMDTYPE_SET_STENCIL_REFERENCE,
	CMDTYPE_INSERT_DEBUG_UTILS_LABEL,
	CMDTYPE_RESET_QUERY_POOL,
	CMDTYPE_BEGIN_QUERY,
	CMDTYPE_END_QUERY,
	CMDTYPE_BEGIN_RENDER_PASS,
	CMDTYPE_END_RENDER_PASS
} VulkanCommandType;

typedef struct VulkanCommand
{
	VulkanCommandType type;
	FNA3DNAMELESS union
	{
		struct
		{
			VkPipelineLayout layout;
			VkDescriptorSet descriptorSets[4];
			uint32_t dynamicOffsetCount;
			uint32_t dynamicOffsets[4];
		} bindDescriptorSets;

		struct
		{
			VkBuffer buffer;
			VkDeviceSize offset;
			VkIndexType indexType;
		} bindIndexBuffer;

		struct
		{
			VkPipeline pipeline;
		} bindPipeline;

		struct
		{
			uint32_t bindingCount;
			VkBuffer buffers[MAX_BOUND_VERTEX_BUFFERS];
			VkDeviceSize offsets[MAX_BOUND_VERTEX_BUFFERS];
		} bindVertexBuffers;

		struct
		{
			uint32_t attachmentCount;
			VkClearAttachment attachments[MAX_RENDERTARGET_BINDINGS + 1];
			VkClearRect rect;
		} clearAttachments;

		struct
		{
			VkImage image;
			VkClearColorValue color;
			VkImageSubresourceRange range;
		} clearColorImage;

		struct
		{
			VkImage image;
			VkClearDepthStencilValue depthStencil;
			VkImageSubresourceRange range;
		} clearDepthStencilImage;

		struct
		{
			uint32_t vertexCount;
			uint32_t firstVertex;
		} draw;

		struct
		{
			uint32_t indexCount;
			uint32_t instanceCount;
			uint32_t firstIndex;
			int32_t vertexOffset;
		} drawIndexed;

		struct
		{
			float blendConstants[4];
		} setBlendConstants;

		struct
		{
			float depthBiasConstantFactor;
			float depthBiasSlopeFactor;
		} setDepthBias;

		struct
		{
			VkImage srcImage;
			VkImage dstImage;
			VkImageBlit region;
			VkFilter filter;
		} blitImage;

		struct
		{
			VkBuffer srcBuffer;
			VkImage dstImage;
			VkImageLayout dstImageLayout;
			VkBufferImageCopy region;
		} copyBufferToImage;

		struct
		{
			VkImage srcImage;
			VkImageLayout srcImageLayout;
			VkBuffer dstBuffer;
			VkBufferImageCopy region;
		} copyImageToBuffer;

		struct
		{
			VkPipelineStageFlags srcStageMask;
			VkPipelineStageFlags dstStageMask;
			uint32_t bufferMemoryBarrierCount;
			VkBufferMemoryBarrier bufferMemoryBarrier;
			uint32_t imageMemoryBarrierCount;
			VkImageMemoryBarrier imageMemoryBarrier;
		} pipelineBarrier;

		struct
		{
			VkRect2D scissor;
		} setScissor;

		struct
		{
			VkViewport viewport;
		} setViewport;

		struct
		{
			uint32_t reference;
		} setStencilReference;

		struct
		{
			VkDebugUtilsLabelEXT labelInfo;
		} insertDebugUtilsLabel;

		struct
		{
			VkQueryPool queryPool;
			uint32_t firstQuery;
		} resetQueryPool;

		struct
		{
			VkQueryPool queryPool;
			uint32_t query;
		} beginQuery;

		struct
		{
			VkQueryPool queryPool;
			uint32_t query;
		} endQuery;

		struct
		{
			VkRenderPassBeginInfo beginInfo;
		} beginRenderPass;

		struct
		{
			/* Nothing to do here... */
			uint8_t dummy;
		} endRenderPass;
	};
} VulkanCommand;

/* Internal Structures */

typedef struct QueueFamilyIndices
{
	uint32_t graphicsFamily;
	uint32_t presentFamily;
} QueueFamilyIndices;

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
	StateHash blendState;
	StateHash rasterizerState;
	StateHash depthStencilState;
	uint64_t vertexBufferBindingsHash;
	FNA3D_PrimitiveType primitiveType;
	VkSampleMask sampleMask;
	uint64_t vertShader;
	uint64_t fragShader;
	/* Pipelines have to be compatible with a render pass */
	VkRenderPass renderPass;
} PipelineHash;

typedef struct PipelineHashMap
{
	PipelineHash key;
	VkPipeline value;
} PipelineHashMap;

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
} RenderPassHash;

typedef struct RenderPassHashMap
{
	RenderPassHash key;
	VkRenderPass value;
} RenderPassHashMap;

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

typedef struct SamplerStateHashMap
{
	StateHash key;
	VkSampler value;
} SamplerStateHashMap;

/* FIXME: This can be packed better */
typedef struct PipelineLayoutHash
{
	uint8_t vertUniformBufferCount;
	uint8_t vertSamplerCount;
	uint8_t fragUniformBufferCount;
	uint8_t fragSamplerCount;
} PipelineLayoutHash;

typedef struct PipelineLayoutHashMap
{
	PipelineLayoutHash key;
	VkPipelineLayout value;
} PipelineLayoutHashMap;

/* Used to delay destruction until command buffer completes */
typedef struct BufferMemoryWrapper
{
	VkBuffer buffer;
	VkDeviceMemory deviceMemory;
} BufferMemoryWrapper;

typedef struct VulkanBuffer VulkanBuffer;

typedef struct VulkanTexture /* Cast from FNA3D_Texture* */
{
	VkImage image;
	VkImageView view;
	VkImageView rtViews[6];
	VkDeviceMemory memory;
	VkExtent2D dimensions;
	uint32_t depth;
	VkDeviceSize memorySize;
	VkFormat surfaceFormat;
	uint32_t layerCount;
	uint32_t levelCount;
	VulkanResourceAccessType resourceAccessType;
	FNA3DNAMELESS union
	{
		FNA3D_SurfaceFormat colorFormat;
		FNA3D_DepthFormat depthStencilFormat;
	};
} VulkanTexture;

static VulkanTexture NullTexture =
{
	(VkImage) 0,
	(VkImageView) 0,
	{ 0, 0, 0, 0, 0, 0 },
	(VkDeviceMemory) 0,
	{0, 0},
	0,
	0,
	VK_FORMAT_UNDEFINED,
	0,
	RESOURCE_ACCESS_NONE
};

typedef struct VulkanPhysicalBuffer
{
	VkBuffer buffer;
	VkDeviceMemory deviceMemory;
	VkDeviceSize size;
	uint8_t *mapPointer;
} VulkanPhysicalBuffer;

typedef struct VulkanSubBuffer
{
	VulkanPhysicalBuffer *physicalBuffer;
	uint8_t *ptr;
	VkDeviceSize offset;
	VulkanResourceAccessType resourceAccessType;
} VulkanSubBuffer;

struct VulkanBuffer /* cast from FNA3D_Buffer */
{
	VkDeviceSize size;
	int32_t subBufferCount;
	int32_t subBufferCapacity;
	int32_t currentSubBufferIndex;
	uint8_t bound;
	VulkanResourceAccessType resourceAccessType;
	VulkanSubBuffer *subBuffers;
};

typedef struct VulkanBufferAllocator
{
	#define PHYSICAL_BUFFER_BASE_SIZE 4000000
	#define PHYSICAL_BUFFER_MAX_COUNT 7
	VulkanPhysicalBuffer *physicalBuffers[PHYSICAL_BUFFER_MAX_COUNT];
	VkDeviceSize totalAllocated[PHYSICAL_BUFFER_MAX_COUNT];
} VulkanBufferAllocator;

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

typedef struct VulkanRenderer
{
	FNA3D_Device *parentDevice;
	VkInstance instance;
	VkPhysicalDevice physicalDevice;
	VkPhysicalDeviceProperties2 physicalDeviceProperties;
	VkPhysicalDeviceDriverPropertiesKHR physicalDeviceDriverProperties;
	VkDevice logicalDevice;

	FNA3D_PresentInterval presentInterval;
	void* deviceWindowHandle;

	QueueFamilyIndices queueFamilyIndices;
	VkQueue graphicsQueue;
	VkQueue presentQueue;

	VkSurfaceKHR surface;
	VkSwapchainKHR swapChain;
	VkImage *swapChainImages;
	VulkanResourceAccessType *swapChainResourceAccessTypes;
	VkImageView *swapChainImageViews;
	uint32_t swapChainImageCount;
	VkExtent2D swapChainExtent;

	VkPipelineCache pipelineCache;

	VkRenderPass renderPass;
	VkPipeline currentPipeline;
	VkPipelineLayout currentPipelineLayout;
	uint64_t currentVertexBufferBindingHash;

	/* Command Buffers */
	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;
	uint32_t commandCapacity;
	uint32_t numActiveCommands;
	VulkanCommand *commands;
	SDL_mutex *commandLock;
	SDL_mutex *passLock;

	/* Queries */
	VkQueryPool queryPool;
	int8_t freeQueryIndexStack[MAX_QUERIES];
	int8_t freeQueryIndexStackHead;

	VkFormat swapchainFormat;
	VkComponentMapping swapchainSwizzle;
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

	FNA3D_Viewport viewport;
	FNA3D_Rect scissorRect;

	VkSampleMask multiSampleMask[MAX_MULTISAMPLE_MASK_SIZE];
	FNA3D_BlendState blendState;

	FNA3D_DepthStencilState depthStencilState;
	FNA3D_RasterizerState rasterizerState;
	FNA3D_PrimitiveType currentPrimitiveType;

	VulkanBufferAllocator *bufferAllocator;
	VulkanBuffer **buffersInUse;
	uint32_t numBuffersInUse;
	uint32_t maxBuffersInUse;

	VulkanPhysicalBuffer *textureStagingBuffer;

	uint32_t numVertexBindings;
	FNA3D_VertexBufferBinding *vertexBindings;
	VkBuffer buffers[MAX_BOUND_VERTEX_BUFFERS];
	VkDeviceSize offsets[MAX_BOUND_VERTEX_BUFFERS];
	uint32_t bufferCount;

	/* Should be equal to swap chain count */
	VkBuffer ldVertUniformBuffer;
	VkBuffer ldFragUniformBuffer;
	VkDeviceSize ldVertUniformOffset;
	VkDeviceSize ldFragUniformOffset;
	VkDeviceSize ldVertUniformSize;
	VkDeviceSize ldFragUniformSize;

	/* Needs to be dynamic because of swap chain count */
	VkBuffer ldVertexBuffers[MAX_BOUND_VERTEX_BUFFERS];
	VkDeviceSize ldVertexBufferOffsets[MAX_BOUND_VERTEX_BUFFERS];

	int32_t stencilRef;

	int32_t numSamplers;
	int32_t numTextureSlots;
	int32_t numVertexTextureSlots;

	VulkanTexture *textures[TEXTURE_COUNT];
	VkSampler samplers[TEXTURE_COUNT];
	uint8_t textureNeedsUpdate[TEXTURE_COUNT];
	uint8_t samplerNeedsUpdate[TEXTURE_COUNT];

	VkDescriptorSetLayout vertUniformBufferDescriptorSetLayout;
	VkDescriptorSetLayout vertSamplerDescriptorSetLayouts[MAX_VERTEXTEXTURE_SAMPLERS];
	VkDescriptorSetLayout fragUniformBufferDescriptorSetLayout;
	VkDescriptorSetLayout fragSamplerDescriptorSetLayouts[MAX_TEXTURE_SAMPLERS];

	VkDescriptorPool *samplerDescriptorPools;
	uint32_t activeSamplerDescriptorPoolIndex;
	uint32_t samplerDescriptorPoolCapacity;

	VkDescriptorPool *uniformBufferDescriptorPools;
	uint32_t activeUniformBufferDescriptorPoolIndex;
	uint32_t uniformBufferDescriptorPoolCapacity;

	VkDescriptorSet currentVertSamplerDescriptorSet;
	VkDescriptorSet currentFragSamplerDescriptorSet;
	VkDescriptorSet currentVertUniformBufferDescriptorSet;
	VkDescriptorSet currentFragUniformBufferDescriptorSet;

	PipelineLayoutHash currentPipelineLayoutHash;

	VulkanBuffer *dummyVertUniformBuffer;
	VulkanBuffer *dummyFragUniformBuffer;
	VkSampler dummyFragSamplerState;
	VkSampler dummyVertSamplerState;
	VulkanTexture *dummyVertTexture;
	VulkanTexture *dummyFragTexture;

	PipelineLayoutHashMap *pipelineLayoutHashMap;
	PipelineHashMap *pipelineHashMap;
	RenderPassHashMap *renderPassHashMap;
	FramebufferHashMap *framebufferHashMap;
	SamplerStateHashMap *samplerStateHashMap;

	VkFence inFlightFence;
	VkSemaphore imageAvailableSemaphore;
	VkSemaphore renderFinishedSemaphore;
	VkFence *imagesInFlight;

	/* MojoShader Interop */
	MOJOSHADER_vkContext *mojoshaderContext;
	MOJOSHADER_effect *currentEffect;
	const MOJOSHADER_effectTechnique *currentTechnique;
	uint32_t currentPass;

	VkShaderModule currentVertShader;
	VkShaderModule currentFragShader;

	/* Storing references to objects that need to be destroyed
	 * so we don't have to stall or invalidate the command buffer
	 */

	VulkanRenderbuffer **renderbuffersToDestroy;
	uint32_t renderbuffersToDestroyCount;
	uint32_t renderbuffersToDestroyCapacity;

	VulkanBuffer **buffersToDestroy;
	uint32_t buffersToDestroyCount;
	uint32_t buffersToDestroyCapacity;

	VulkanEffect **effectsToDestroy;
	uint32_t effectsToDestroyCount;
	uint32_t effectsToDestroyCapacity;

	VulkanTexture **texturesToDestroy;
	uint32_t texturesToDestroyCount;
	uint32_t texturesToDestroyCapacity;

	uint8_t renderPassInProgress;
	uint8_t needNewRenderPass;
	uint8_t renderTargetBound;
	uint8_t needNewPipeline;

	/* Depth Formats (may not match the format implied by the name!) */
	VkFormat D16Format;
	VkFormat D24Format;
	VkFormat D24S8Format;

	/* Capabilities */
	uint8_t supportsDxt1;
	uint8_t supportsS3tc;
	uint8_t supportsDebugUtils;
	uint8_t debugMode;

	#define VULKAN_INSTANCE_FUNCTION(ext, ret, func, params) \
		vkfntype_##func func;
	#define VULKAN_DEVICE_FUNCTION(ext, ret, func, params) \
		vkfntype_##func func;
	#include "FNA3D_Driver_Vulkan_vkfuncs.h"
} VulkanRenderer;

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
	IDENTITY_SWIZZLE,	/* SurfaceFormat.Bgr565 */
	IDENTITY_SWIZZLE,	/* SurfaceFormat.Bgra5551 */
	{			/* SurfaceFormat.Bgra4444 */
		VK_COMPONENT_SWIZZLE_G,
		VK_COMPONENT_SWIZZLE_R,
		VK_COMPONENT_SWIZZLE_A,
		VK_COMPONENT_SWIZZLE_B
	},
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
	IDENTITY_SWIZZLE	/* SurfaceFormat.ColorBgraEXT */
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
	VK_FORMAT_R8G8B8A8_UNORM		/* SurfaceFormat.ColorBgraEXT */
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

static inline void LogVulkanResult(
	const char* vulkanFunctionName,
	VkResult result
) {
	if (result != VK_SUCCESS)
	{
		FNA3D_LogError(
			"%s: %s",
			vulkanFunctionName,
			VkErrorMessages(result)
		);
	}
}

/* Forward Declarations */

static void VULKAN_GetBackbufferSize(
	FNA3D_Renderer *driverData,
	int32_t *w,
	int32_t *h
);

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
);

static void VULKAN_INTERNAL_RecreateSwapchain(VulkanRenderer *renderer);

static void VULKAN_INTERNAL_MaybeEndRenderPass(VulkanRenderer *renderer);

/* Vulkan: Internal Implementation */

/* Vulkan: Extensions */

static inline uint8_t SupportsExtension(
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
	const char **requiredExtensions,
	uint32_t requiredExtensionsLength,
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
	availableExtensions = SDL_stack_alloc(
		VkExtensionProperties,
		extensionCount
	);
	vkEnumerateInstanceExtensionProperties(
		NULL,
		&extensionCount,
		availableExtensions
	);

	for (i = 0; i < requiredExtensionsLength; i += 1)
	{
		if (!SupportsExtension(
			requiredExtensions[i],
			availableExtensions,
			extensionCount
		)) {
			allExtensionsSupported = 0;
			break;
		}
	}

	/* This is optional, but nice to have! */
	*supportsDebugUtils = SupportsExtension(
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
		availableExtensions,
		extensionCount
	);

	SDL_stack_free(availableExtensions);
	return allExtensionsSupported;
}

static uint8_t VULKAN_INTERNAL_CheckDeviceExtensions(
	VulkanRenderer *renderer,
	VkPhysicalDevice physicalDevice,
	const char** requiredExtensions,
	uint32_t requiredExtensionsLength
) {
	uint32_t extensionCount, i;
	VkExtensionProperties *availableExtensions;
	uint8_t allExtensionsSupported = 1;

	renderer->vkEnumerateDeviceExtensionProperties(
		physicalDevice,
		NULL,
		&extensionCount,
		NULL
	);
	availableExtensions = SDL_stack_alloc(
		VkExtensionProperties,
		extensionCount
	);
	renderer->vkEnumerateDeviceExtensionProperties(
		physicalDevice,
		NULL,
		&extensionCount,
		availableExtensions
	);

	for (i = 0; i < requiredExtensionsLength; i += 1)
	{
		if (!SupportsExtension(
			requiredExtensions[i],
			availableExtensions,
			extensionCount
		)) {
			allExtensionsSupported = 0;
			break;
		}
	}

	SDL_stack_free(availableExtensions);
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
	availableLayers = SDL_stack_alloc(VkLayerProperties, layerCount);
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

	SDL_stack_free(availableLayers);
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
	uint32_t formatCount;
	uint32_t presentModeCount;

	result = renderer->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
		physicalDevice,
		surface,
		&outputDetails->capabilities
	);
	if (result != VK_SUCCESS)
	{
		FNA3D_LogError(
			"vkGetPhysicalDeviceSurfaceCapabilitiesKHR: %s",
			VkErrorMessages(result)
		);

		return 0;
	}

	renderer->vkGetPhysicalDeviceSurfaceFormatsKHR(
		physicalDevice,
		surface,
		&formatCount,
		NULL
	);

	if (formatCount != 0)
	{
		outputDetails->formats = (VkSurfaceFormatKHR*) SDL_malloc(
			sizeof(VkSurfaceFormatKHR) * formatCount
		);
		outputDetails->formatsLength = formatCount;

		if (!outputDetails->formats)
		{
			SDL_OutOfMemory();
			return 0;
		}

		result = renderer->vkGetPhysicalDeviceSurfaceFormatsKHR(
			physicalDevice,
			surface,
			&formatCount,
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

	renderer->vkGetPhysicalDeviceSurfacePresentModesKHR(
		physicalDevice,
		surface,
		&presentModeCount,
		NULL
	);

	if (presentModeCount != 0)
	{
		outputDetails->presentModes = (VkPresentModeKHR*) SDL_malloc(
			sizeof(VkPresentModeKHR) * presentModeCount
		);
		outputDetails->presentModesLength = presentModeCount;

		if (!outputDetails->presentModes)
		{
			SDL_OutOfMemory();
			return 0;
		}

		result = renderer->vkGetPhysicalDeviceSurfacePresentModesKHR(
			physicalDevice,
			surface,
			&presentModeCount,
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

	return 1;
}

static uint8_t VULKAN_INTERNAL_ChooseSwapSurfaceFormat(
	VkFormat desiredFormat,
	VkSurfaceFormatKHR *availableFormats,
	uint32_t availableFormatsLength,
	VkSurfaceFormatKHR *outputFormat
) {
	uint32_t i;
	for (i = 0; i < availableFormatsLength; i += 1)
	{
		if (	availableFormats[i].format == desiredFormat &&
			availableFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR	)
		{
			*outputFormat = availableFormats[i];
			return 1;
		}
	}

	FNA3D_LogError("Desired surface format is unavailable.");
	return 0;
}

static uint8_t VULKAN_INTERNAL_ChooseSwapPresentMode(
	FNA3D_PresentInterval desiredPresentInterval,
	VkPresentModeKHR *availablePresentModes,
	uint32_t availablePresentModesLength,
	VkPresentModeKHR *outputPresentMode
) {
	uint32_t i;
	if (	desiredPresentInterval == FNA3D_PRESENTINTERVAL_DEFAULT ||
		desiredPresentInterval == FNA3D_PRESENTINTERVAL_ONE	)
	{
		if (SDL_GetHintBoolean("FNA3D_DISABLE_LATESWAPTEAR", 0))
		{
			*outputPresentMode = VK_PRESENT_MODE_FIFO_KHR;
			return 1;
		}
		for (i = 0; i < availablePresentModesLength; i += 1)
		{
			if (availablePresentModes[i] == VK_PRESENT_MODE_FIFO_RELAXED_KHR)
			{
				*outputPresentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
				FNA3D_LogInfo("Using VK_PRESENT_MODE_FIFO_RELAXED_KHR!");
				return 1;
			}
		}
		FNA3D_LogInfo("VK_PRESENT_MODE_FIFO_RELAXED_KHR unsupported.");
	}
	else if (desiredPresentInterval ==  FNA3D_PRESENTINTERVAL_IMMEDIATE)
	{
		for (i = 0; i < availablePresentModesLength; i += 1)
		{
			if (availablePresentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
			{
				*outputPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
				return 1;
			}
		}
		FNA3D_LogInfo("VK_PRESENT_MODE_IMMEDIATE_KHR unsupported.");
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

	FNA3D_LogInfo("Fall back to VK_PRESENT_MODE_FIFO_KHR.");
	*outputPresentMode = VK_PRESENT_MODE_FIFO_KHR;
	return 1;
}

static uint8_t VULKAN_INTERNAL_FindMemoryType(
	VulkanRenderer *renderer,
	uint32_t typeFilter,
	VkMemoryPropertyFlags properties,
	uint32_t *result
) {
	VkPhysicalDeviceMemoryProperties memoryProperties;
	uint32_t i;

	renderer->vkGetPhysicalDeviceMemoryProperties(
		renderer->physicalDevice,
		&memoryProperties
	);

	for (i = 0; i < memoryProperties.memoryTypeCount; i += 1)
	{
		if (	(typeFilter & (1 << i)) &&
			(memoryProperties.memoryTypes[i].propertyFlags & properties) == properties	)
		{
			*result = i;
			return 1;
		}
	}

	FNA3D_LogError("Failed to find suitable memory type");

	return 0;
}

static uint8_t VULKAN_INTERNAL_IsDeviceSuitable(
	VulkanRenderer *renderer,
	VkPhysicalDevice physicalDevice,
	const char** requiredExtensionNames,
	uint32_t requiredExtensionNamesLength,
	VkSurfaceKHR surface,
	QueueFamilyIndices *queueFamilyIndices,
	uint8_t *isIdeal
) {
	uint32_t queueFamilyCount, i;
	SwapChainSupportDetails swapChainSupportDetails;
	VkQueueFamilyProperties *queueProps;
	VkBool32 supportsPresent;
	uint8_t querySuccess, foundSuitableDevice = 0;
	VkPhysicalDeviceProperties deviceProperties;

	queueFamilyIndices->graphicsFamily = UINT32_MAX;
	queueFamilyIndices->presentFamily = UINT32_MAX;
	*isIdeal = 0;

	/* Note: If no dedicated device exists,
	 * one that supports our features would be fine
	 */

	if (!VULKAN_INTERNAL_CheckDeviceExtensions(
		renderer,
		physicalDevice,
		requiredExtensionNames,
		requiredExtensionNamesLength
	)) {
		return 0;
	}

	renderer->vkGetPhysicalDeviceQueueFamilyProperties(
		physicalDevice,
		&queueFamilyCount,
		NULL
	);

	/* FIXME: Need better structure for checking vs storing support details */
	querySuccess = VULKAN_INTERNAL_QuerySwapChainSupport(
		renderer,
		physicalDevice,
		surface,
		&swapChainSupportDetails
	);
	SDL_free(swapChainSupportDetails.formats);
	SDL_free(swapChainSupportDetails.presentModes);
	if (	querySuccess == 0 ||
		swapChainSupportDetails.formatsLength == 0 ||
		swapChainSupportDetails.presentModesLength == 0	)
	{
		return 0;
	}

	queueProps = (VkQueueFamilyProperties*) SDL_stack_alloc(
		VkQueueFamilyProperties,
		queueFamilyCount
	);
	renderer->vkGetPhysicalDeviceQueueFamilyProperties(
		physicalDevice,
		&queueFamilyCount,
		queueProps
	);

	for (i = 0; i < queueFamilyCount; i += 1)
	{
		renderer->vkGetPhysicalDeviceSurfaceSupportKHR(
			physicalDevice,
			i,
			surface,
			&supportsPresent
		);
		if (	supportsPresent &&
			(queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0	)
		{
			queueFamilyIndices->graphicsFamily = i;
			queueFamilyIndices->presentFamily = i;
			foundSuitableDevice = 1;
			break;
		}
	}

	SDL_stack_free(queueProps);

	if (foundSuitableDevice)
	{
		/* We'd really like a discrete GPU, but it's OK either way! */
		renderer->vkGetPhysicalDeviceProperties(
			physicalDevice,
			&deviceProperties
		);
		if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			*isIdeal = 1;
		}
		return 1;
	}

	/* This device is useless for us, next! */
	return 0;
}

/* Vulkan: vkInstance/vkDevice Creation */

static uint8_t VULKAN_INTERNAL_CreateInstance(
	VulkanRenderer *renderer,
	FNA3D_PresentationParameters *presentationParameters
) {
	VkResult vulkanResult;
	VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	const char **instanceExtensionNames;
	uint32_t instanceExtensionCount;
	VkInstanceCreateInfo createInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	const char *layerNames[] = { "VK_LAYER_KHRONOS_validation" };

	appInfo.pEngineName = "FNA3D";
	appInfo.engineVersion = FNA3D_COMPILED_VERSION;
	appInfo.apiVersion = VK_MAKE_VERSION(1, 0, 0);

	if (!SDL_Vulkan_GetInstanceExtensions(
		(SDL_Window*) presentationParameters->deviceWindowHandle,
		&instanceExtensionCount,
		NULL
	)) {
		FNA3D_LogError(
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
		FNA3D_LogError(
			"SDL_Vulkan_GetInstanceExtensions(): %s",
			SDL_GetError()
		);
		goto create_instance_fail;
	}

	instanceExtensionNames[instanceExtensionCount++] =
		VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;

	if (!VULKAN_INTERNAL_CheckInstanceExtensions(
		instanceExtensionNames,
		instanceExtensionCount,
		&renderer->supportsDebugUtils
	)) {
		FNA3D_LogError(
			"Required Vulkan instance extensions not supported"
		);
		goto create_instance_fail;
	}

	if (renderer->supportsDebugUtils)
	{
		/* Append the debug extension to the end */
		instanceExtensionNames[instanceExtensionCount++] =
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	}
	else
	{
		FNA3D_LogWarn(
			"%s is not supported!",
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME
		);
	}

	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledExtensionCount = instanceExtensionCount;
	createInfo.ppEnabledExtensionNames = instanceExtensionNames;
	createInfo.ppEnabledLayerNames = layerNames;
	if (renderer->debugMode)
	{
		createInfo.enabledLayerCount = SDL_arraysize(layerNames);
		if (!VULKAN_INTERNAL_CheckValidationLayers(
			layerNames,
			createInfo.enabledLayerCount
		)) {
			FNA3D_LogWarn("Validation layers not found, continuing without validation");
			createInfo.enabledLayerCount = 0;
		}
	}
	else
	{
		createInfo.enabledLayerCount = 0;
	}

	vulkanResult = vkCreateInstance(&createInfo, NULL, &renderer->instance);
	if (vulkanResult != VK_SUCCESS)
	{
		FNA3D_LogError(
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

static uint8_t VULKAN_INTERNAL_DeterminePhysicalDevice(
	VulkanRenderer *renderer,
	const char **deviceExtensionNames,
	uint32_t deviceExtensionCount
) {
	VkResult vulkanResult;
	VkPhysicalDevice *physicalDevices;
	uint32_t physicalDeviceCount, i, suitableIndex;
	VkPhysicalDevice physicalDevice;
	QueueFamilyIndices queueFamilyIndices;
	uint8_t isIdeal;

	vulkanResult = renderer->vkEnumeratePhysicalDevices(
		renderer->instance,
		&physicalDeviceCount,
		NULL
	);

	if (vulkanResult != VK_SUCCESS)
	{
		FNA3D_LogError(
			"vkEnumeratePhysicalDevices failed: %s",
			VkErrorMessages(vulkanResult)
		);
		return 0;
	}

	if (physicalDeviceCount == 0)
	{
		FNA3D_LogError("Failed to find any GPUs with Vulkan support");
		return 0;
	}

	physicalDevices = SDL_stack_alloc(VkPhysicalDevice, physicalDeviceCount);

	vulkanResult = renderer->vkEnumeratePhysicalDevices(
		renderer->instance,
		&physicalDeviceCount,
		physicalDevices
	);

	if (vulkanResult != VK_SUCCESS)
	{
		FNA3D_LogError(
			"vkEnumeratePhysicalDevices failed: %s",
			VkErrorMessages(vulkanResult)
		);
		SDL_stack_free(physicalDevices);
		return 0;
	}

	/* Any suitable device will do, but we'd like the best */
	suitableIndex = -1;
	for (i = 0; i < physicalDeviceCount; i += 1)
	{
		if (VULKAN_INTERNAL_IsDeviceSuitable(
			renderer,
			physicalDevices[i],
			deviceExtensionNames,
			deviceExtensionCount,
			renderer->surface,
			&queueFamilyIndices,
			&isIdeal
		)) {
			suitableIndex = i;
			if (isIdeal)
			{
				/* This is the one we want! */
				break;
			}
		}
	}

	if (suitableIndex != -1)
	{
		physicalDevice = physicalDevices[suitableIndex];
	}
	else
	{
		FNA3D_LogError("No suitable physical devices found");
		SDL_stack_free(physicalDevices);
		return 0;
	}

	renderer->physicalDevice = physicalDevice;
	renderer->queueFamilyIndices = queueFamilyIndices;

	renderer->physicalDeviceDriverProperties.sType =
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR;

	renderer->physicalDeviceProperties.sType =
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	renderer->physicalDeviceProperties.pNext =
		&renderer->physicalDeviceDriverProperties;

	renderer->vkGetPhysicalDeviceProperties2KHR(
		renderer->physicalDevice,
		&renderer->physicalDeviceProperties
	);

	FNA3D_LogInfo("FNA3D Driver: Vulkan");
	FNA3D_LogInfo(
		"Vulkan Device: %s",
		renderer->physicalDeviceProperties.properties.deviceName
	);
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

	SDL_stack_free(physicalDevices);
	return 1;
}

static uint8_t VULKAN_INTERNAL_CreateLogicalDevice(
	VulkanRenderer *renderer,
	const char **deviceExtensionNames,
	uint32_t deviceExtensionCount
) {
	VkResult vulkanResult;
	VkDeviceCreateInfo deviceCreateInfo;
	VkPhysicalDeviceFeatures deviceFeatures;

	VkDeviceQueueCreateInfo *queueCreateInfos = SDL_stack_alloc(
		VkDeviceQueueCreateInfo,
		2
	);
	VkDeviceQueueCreateInfo queueCreateInfoGraphics;
	VkDeviceQueueCreateInfo queueCreateInfoPresent;

	int32_t queueInfoCount = 1;
	float queuePriority = 1.0f;

	SDL_zero(deviceCreateInfo);
	SDL_zero(deviceFeatures);
	SDL_zero(queueCreateInfoGraphics);
	SDL_zero(queueCreateInfoPresent);

	queueCreateInfoGraphics.sType =
		VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfoGraphics.queueFamilyIndex =
		renderer->queueFamilyIndices.graphicsFamily;
	queueCreateInfoGraphics.queueCount = 1;
	queueCreateInfoGraphics.pQueuePriorities = &queuePriority;

	queueCreateInfos[0] = queueCreateInfoGraphics;

	if (renderer->queueFamilyIndices.presentFamily != renderer->queueFamilyIndices.graphicsFamily)
	{
		queueCreateInfoPresent.sType =
			VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfoPresent.queueFamilyIndex =
			renderer->queueFamilyIndices.presentFamily;
		queueCreateInfoPresent.queueCount = 1;
		queueCreateInfoPresent.pQueuePriorities = &queuePriority;

		queueCreateInfos[1] = queueCreateInfoPresent;
		queueInfoCount += 1;
	}

	/* specifying used device features */

	deviceFeatures.occlusionQueryPrecise = VK_TRUE;
	deviceFeatures.fillModeNonSolid = VK_TRUE;

	/* creating the logical device */

	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.queueCreateInfoCount = queueInfoCount;
	deviceCreateInfo.pQueueCreateInfos = queueCreateInfos;
	deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
	deviceCreateInfo.ppEnabledExtensionNames = deviceExtensionNames;
	deviceCreateInfo.enabledExtensionCount = deviceExtensionCount;

	vulkanResult = renderer->vkCreateDevice(
		renderer->physicalDevice,
		&deviceCreateInfo,
		NULL,
		&renderer->logicalDevice
	);
	if (vulkanResult != VK_SUCCESS)
	{
		FNA3D_LogError(
			"vkCreateDevice failed: %s",
			VkErrorMessages(vulkanResult)
		);
		return 0;
	}

	/* Load vkDevice entry points */

	#define VULKAN_DEVICE_FUNCTION(ext, ret, func, params) \
		renderer->func = (vkfntype_##func) \
			renderer->vkGetDeviceProcAddr( \
				renderer->logicalDevice, \
				#func \
			);
	#include "FNA3D_Driver_Vulkan_vkfuncs.h"

	renderer->vkGetDeviceQueue(
		renderer->logicalDevice,
		renderer->queueFamilyIndices.graphicsFamily,
		0,
		&renderer->graphicsQueue
	);

	renderer->vkGetDeviceQueue(
		renderer->logicalDevice,
		renderer->queueFamilyIndices.presentFamily,
		0,
		&renderer->presentQueue
	);

	SDL_stack_free(queueCreateInfos);
	return 1;
}

/* Vulkan: Command Buffers */

static void VULKAN_INTERNAL_EncodeCommand(
	VulkanRenderer *renderer,
	VulkanCommand cmd
) {
	SDL_LockMutex(renderer->commandLock);

	/* Grow the array if needed */
	if (renderer->numActiveCommands == renderer->commandCapacity)
	{
		renderer->commandCapacity *= 2;
		renderer->commands = (VulkanCommand*) SDL_realloc(
			renderer->commands,
			sizeof(VulkanCommand) * renderer->commandCapacity
		);
	}

	/* Add the command to the array */
	renderer->commands[renderer->numActiveCommands] = cmd;
	renderer->numActiveCommands += 1;

	SDL_UnlockMutex(renderer->commandLock);
}

static void VULKAN_INTERNAL_RecordCommands(VulkanRenderer *renderer)
{
	VkCommandBufferBeginInfo beginInfo =
	{
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
	};
	uint32_t i;
	VulkanCommand *cmd;
	VkResult result;

	/* renderer->commandLock must be LOCKED at this point! */

	/* Begin recording */
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = renderer->vkBeginCommandBuffer(
		renderer->commandBuffer,
		&beginInfo
	);
	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkBeginCommandBuffer", result);
	}

	/* Record the commands, in order! */
	for (i = 0; i < renderer->numActiveCommands; i += 1)
	{
		cmd = &renderer->commands[i];
		switch (cmd->type)
		{
			case CMDTYPE_BIND_DESCRIPTOR_SETS:
				renderer->vkCmdBindDescriptorSets(
					renderer->commandBuffer,
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					cmd->bindDescriptorSets.layout,
					0,
					4, /* FIXME: When we batch them, this will need to change. */
					cmd->bindDescriptorSets.descriptorSets,
					cmd->bindDescriptorSets.dynamicOffsetCount,
					cmd->bindDescriptorSets.dynamicOffsets
				);
				break;

			case CMDTYPE_BIND_INDEX_BUFFER:
				renderer->vkCmdBindIndexBuffer(
					renderer->commandBuffer,
					cmd->bindIndexBuffer.buffer,
					cmd->bindIndexBuffer.offset,
					cmd->bindIndexBuffer.indexType
				);
				break;

			case CMDTYPE_BIND_PIPELINE:
				renderer->vkCmdBindPipeline(
					renderer->commandBuffer,
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					cmd->bindPipeline.pipeline
				);
				break;

			case CMDTYPE_BIND_VERTEX_BUFFERS:
				renderer->vkCmdBindVertexBuffers(
					renderer->commandBuffer,
					0,
					cmd->bindVertexBuffers.bindingCount,
					cmd->bindVertexBuffers.buffers,
					cmd->bindVertexBuffers.offsets
				);
				break;

			case CMDTYPE_CLEAR_ATTACHMENTS:
				renderer->vkCmdClearAttachments(
					renderer->commandBuffer,
					cmd->clearAttachments.attachmentCount,
					cmd->clearAttachments.attachments,
					1,
					&cmd->clearAttachments.rect
				);
				break;

			case CMDTYPE_CLEAR_COLOR_IMAGE:
				renderer->vkCmdClearColorImage(
					renderer->commandBuffer,
					cmd->clearColorImage.image,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					&cmd->clearColorImage.color,
					1,
					&cmd->clearColorImage.range
				);
				break;

			case CMDTYPE_CLEAR_DEPTH_STENCIL_IMAGE:
				renderer->vkCmdClearDepthStencilImage(
					renderer->commandBuffer,
					cmd->clearDepthStencilImage.image,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					&cmd->clearDepthStencilImage.depthStencil,
					1,
					&cmd->clearDepthStencilImage.range
				);
				break;

			case CMDTYPE_DRAW:
				renderer->vkCmdDraw(
					renderer->commandBuffer,
					cmd->draw.vertexCount,
					1,
					cmd->draw.firstVertex,
					0
				);
				break;

			case CMDTYPE_DRAW_INDEXED:
				renderer->vkCmdDrawIndexed(
					renderer->commandBuffer,
					cmd->drawIndexed.indexCount,
					cmd->drawIndexed.instanceCount,
					cmd->drawIndexed.firstIndex,
					cmd->drawIndexed.vertexOffset,
					0
				);
				break;

			case CMDTYPE_SET_BLEND_CONSTANTS:
				renderer->vkCmdSetBlendConstants(
					renderer->commandBuffer,
					cmd->setBlendConstants.blendConstants
				);
				break;

			case CMDTYPE_SET_DEPTH_BIAS:
				renderer->vkCmdSetDepthBias(
					renderer->commandBuffer,
					cmd->setDepthBias.depthBiasConstantFactor,
					0.0f, /* no clamp */
					cmd->setDepthBias.depthBiasSlopeFactor
				);
				break;

			case CMDTYPE_BLIT_IMAGE:
				renderer->vkCmdBlitImage(
					renderer->commandBuffer,
					cmd->blitImage.srcImage,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					cmd->blitImage.dstImage,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1,
					&cmd->blitImage.region,
					cmd->blitImage.filter
				);
				break;

			case CMDTYPE_COPY_BUFFER_TO_IMAGE:
				renderer->vkCmdCopyBufferToImage(
					renderer->commandBuffer,
					cmd->copyBufferToImage.srcBuffer,
					cmd->copyBufferToImage.dstImage,
					cmd->copyBufferToImage.dstImageLayout,
					1,
					&cmd->copyBufferToImage.region
				);
				break;

			case CMDTYPE_COPY_IMAGE_TO_BUFFER:
				renderer->vkCmdCopyImageToBuffer(
					renderer->commandBuffer,
					cmd->copyImageToBuffer.srcImage,
					cmd->copyImageToBuffer.srcImageLayout,
					cmd->copyImageToBuffer.dstBuffer,
					1,
					&cmd->copyImageToBuffer.region
				);
				break;

			case CMDTYPE_PIPELINE_BARRIER:
				renderer->vkCmdPipelineBarrier(
					renderer->commandBuffer,
					cmd->pipelineBarrier.srcStageMask,
					cmd->pipelineBarrier.dstStageMask,
					0,
					0,
					NULL,
					cmd->pipelineBarrier.bufferMemoryBarrierCount,
					&cmd->pipelineBarrier.bufferMemoryBarrier,
					cmd->pipelineBarrier.imageMemoryBarrierCount,
					&cmd->pipelineBarrier.imageMemoryBarrier
				);
				break;

			case CMDTYPE_SET_SCISSOR:
				renderer->vkCmdSetScissor(
					renderer->commandBuffer,
					0,
					1,
					&cmd->setScissor.scissor
				);
				break;

			case CMDTYPE_SET_VIEWPORT:
				renderer->vkCmdSetViewport(
					renderer->commandBuffer,
					0,
					1,
					&cmd->setViewport.viewport
				);
				break;

			case CMDTYPE_SET_STENCIL_REFERENCE:
				renderer->vkCmdSetStencilReference(
					renderer->commandBuffer,
					VK_STENCIL_FACE_FRONT_AND_BACK,
					cmd->setStencilReference.reference
				);
				break;

			case CMDTYPE_INSERT_DEBUG_UTILS_LABEL:
				renderer->vkCmdInsertDebugUtilsLabelEXT(
					renderer->commandBuffer,
					&cmd->insertDebugUtilsLabel.labelInfo
				);
				break;

			case CMDTYPE_RESET_QUERY_POOL:
				renderer->vkCmdResetQueryPool(
					renderer->commandBuffer,
					cmd->resetQueryPool.queryPool,
					cmd->resetQueryPool.firstQuery,
					1
				);
				break;

			case CMDTYPE_BEGIN_QUERY:
				renderer->vkCmdBeginQuery(
					renderer->commandBuffer,
					cmd->beginQuery.queryPool,
					cmd->beginQuery.query,
					VK_QUERY_CONTROL_PRECISE_BIT
				);
				break;

			case CMDTYPE_END_QUERY:
				renderer->vkCmdEndQuery(
					renderer->commandBuffer,
					cmd->endQuery.queryPool,
					cmd->endQuery.query
				);
				break;

			case CMDTYPE_BEGIN_RENDER_PASS:
				renderer->vkCmdBeginRenderPass(
					renderer->commandBuffer,
					&cmd->beginRenderPass.beginInfo,
					VK_SUBPASS_CONTENTS_INLINE
				);
				break;

			case CMDTYPE_END_RENDER_PASS:
				renderer->vkCmdEndRenderPass(renderer->commandBuffer);
				break;
		}
	}

	/* End command recording */
	result = renderer->vkEndCommandBuffer(
		renderer->commandBuffer
	);
	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkEndCommandBuffer", result);
	}
}

static void VULKAN_INTERNAL_WaitAndReset(VulkanRenderer *renderer)
{
	uint32_t i;
	VkResult result = renderer->vkWaitForFences(
		renderer->logicalDevice,
		1,
		&renderer->inFlightFence,
		VK_TRUE,
		UINT64_MAX
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkWaitForFences", result);
		return;
	}

	/* Reset buffer and subbuffer binding info */
	for (i = 0; i < renderer->numBuffersInUse; i += 1)
	{
		if (renderer->buffersInUse[i] != NULL)
		{
			renderer->buffersInUse[i]->bound = 0;
			renderer->buffersInUse[i]->currentSubBufferIndex = 0;
			renderer->buffersInUse[i] = NULL;
		}
	}
	renderer->numBuffersInUse = 0;

	/* Reset the command buffer */
	result = renderer->vkResetCommandBuffer(
		renderer->commandBuffer,
		VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT
	);
	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkResetCommandBuffer", result);
	}
}

static void VULKAN_INTERNAL_Flush(VulkanRenderer *renderer)
{
	VkResult result;
	VkSubmitInfo submitInfo =
	{
		VK_STRUCTURE_TYPE_SUBMIT_INFO
	};

	/* Keep these locked until the end of the function! */
	SDL_LockMutex(renderer->passLock);
	SDL_LockMutex(renderer->commandLock);

	/* TODO: Batch descriptor set updates */

	/* Record commands into the command buffer */
	VULKAN_INTERNAL_RecordCommands(renderer);
	renderer->numActiveCommands = 0;

	/* Submit the command buffer */
	submitInfo.waitSemaphoreCount = 0;
	submitInfo.pWaitSemaphores = NULL;
	submitInfo.pWaitDstStageMask = NULL;
	submitInfo.signalSemaphoreCount = 0;
	submitInfo.pSignalSemaphores = NULL;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &renderer->commandBuffer;

	renderer->vkResetFences(
		renderer->logicalDevice,
		1,
		&renderer->inFlightFence
	);

	result = renderer->vkQueueSubmit(
		renderer->graphicsQueue,
		1,
		&submitInfo,
		renderer->inFlightFence
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkQueueSubmit", result);
		return;
	}

	/* Wait for completion */
	VULKAN_INTERNAL_WaitAndReset(renderer);

	/* It should be safe to unlock now */
	SDL_UnlockMutex(renderer->commandLock);
	SDL_UnlockMutex(renderer->passLock);
}

static void VULKAN_INTERNAL_FlushAndPresent(
	VulkanRenderer *renderer,
	void* overrideWindowHandle,
	uint32_t swapChainImageIndex
) {
	VkResult result;
	VkSubmitInfo submitInfo =
	{
		VK_STRUCTURE_TYPE_SUBMIT_INFO
	};
	VkPipelineStageFlags waitStages[] =
	{
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
	};
	VkPresentInfoKHR presentInfo =
	{
		VK_STRUCTURE_TYPE_PRESENT_INFO_KHR
	};
	struct
	{
		VkStructureType sType;
		const void *pNext;
		uint64_t frameToken;
	} presentInfoGGP;

	SDL_LockMutex(renderer->passLock);
	SDL_LockMutex(renderer->commandLock);

	/* TODO: Batch descriptor set updates */

	/* Record commands into the command buffer */
	VULKAN_INTERNAL_RecordCommands(renderer);
	renderer->numActiveCommands = 0;

	/* Prepare the command buffer for submission */
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &renderer->imageAvailableSemaphore;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &renderer->renderFinishedSemaphore;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &renderer->commandBuffer;

	renderer->vkResetFences(
		renderer->logicalDevice,
		1,
		&renderer->inFlightFence
	);

	result = renderer->vkQueueSubmit(
		renderer->graphicsQueue,
		1,
		&submitInfo,
		renderer->inFlightFence
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkQueueSubmit", result);
		return;
	}

	if (renderer->physicalDeviceDriverProperties.driverID == VK_DRIVER_ID_GGP_PROPRIETARY)
	{
		const void* token = SDL_GetWindowData(
			(SDL_Window*) overrideWindowHandle,
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
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores =
		&renderer->renderFinishedSemaphore;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &renderer->swapChain;
	presentInfo.pImageIndices = &swapChainImageIndex;
	presentInfo.pResults = NULL;

	result = renderer->vkQueuePresentKHR(
		renderer->presentQueue,
		&presentInfo
	);

	/* Wait for frame completion */
	VULKAN_INTERNAL_WaitAndReset(renderer);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
	{
		VULKAN_INTERNAL_RecreateSwapchain(renderer);
		SDL_UnlockMutex(renderer->commandLock);
		SDL_UnlockMutex(renderer->passLock);
		return;
	}
	else if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkQueuePresentKHR", result);
		FNA3D_LogError("failed to present image");
	}

	/* It should be safe to unlock now */
	SDL_UnlockMutex(renderer->commandLock);
	SDL_UnlockMutex(renderer->passLock);
}

/* Vulkan: Memory Barriers */

static void VULKAN_INTERNAL_BufferMemoryBarrier(
	VulkanRenderer *renderer,
	VulkanResourceAccessType nextResourceAccessType,
	VulkanBuffer *buffer,
	VulkanSubBuffer *subBuffer
) {
	VkPipelineStageFlags srcStages = 0;
	VkPipelineStageFlags dstStages = 0;
	VkBufferMemoryBarrier memoryBarrier =
	{
		VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER
	};
	VulkanResourceAccessType prevAccess, nextAccess;
	const VulkanResourceAccessInfo *prevAccessInfo, *nextAccessInfo;
	VulkanCommand pipelineBarrierCmd =
	{
		CMDTYPE_PIPELINE_BARRIER
	};

	if (subBuffer->resourceAccessType == nextResourceAccessType)
	{
		return;
	}

	memoryBarrier.srcAccessMask = 0;
	memoryBarrier.dstAccessMask = 0;
	memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrier.buffer = subBuffer->physicalBuffer->buffer;
	memoryBarrier.offset = subBuffer->offset;
	memoryBarrier.size = buffer->size;

	prevAccess = subBuffer->resourceAccessType;
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

	pipelineBarrierCmd.pipelineBarrier.srcStageMask = srcStages;
	pipelineBarrierCmd.pipelineBarrier.dstStageMask = dstStages;
	pipelineBarrierCmd.pipelineBarrier.bufferMemoryBarrierCount = 1;
	pipelineBarrierCmd.pipelineBarrier.bufferMemoryBarrier = memoryBarrier;
	pipelineBarrierCmd.pipelineBarrier.imageMemoryBarrierCount = 0;
	VULKAN_INTERNAL_EncodeCommand(renderer, pipelineBarrierCmd);

	subBuffer->resourceAccessType = nextResourceAccessType;
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
	VkPipelineStageFlags srcStages = 0;
	VkPipelineStageFlags dstStages = 0;
	VkImageMemoryBarrier memoryBarrier =
	{
		VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER
	};
	VulkanResourceAccessType prevAccess;
	const VulkanResourceAccessInfo *pPrevAccessInfo, *pNextAccessInfo;
	VulkanCommand pipelineBarrierCmd =
	{
		CMDTYPE_PIPELINE_BARRIER
	};

	if (*resourceAccessType == nextAccess)
	{
		return;
	}

	memoryBarrier.srcAccessMask = 0;
	memoryBarrier.dstAccessMask = 0;
	memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrier.image = image;
	memoryBarrier.subresourceRange.aspectMask = aspectMask;
	memoryBarrier.subresourceRange.baseArrayLayer = baseLayer;
	memoryBarrier.subresourceRange.layerCount = layerCount;
	memoryBarrier.subresourceRange.baseMipLevel = baseLevel;
	memoryBarrier.subresourceRange.levelCount = levelCount;
	memoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	memoryBarrier.newLayout = VK_IMAGE_LAYOUT_UNDEFINED;

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

	pipelineBarrierCmd.pipelineBarrier.srcStageMask = srcStages;
	pipelineBarrierCmd.pipelineBarrier.dstStageMask = dstStages;
	pipelineBarrierCmd.pipelineBarrier.bufferMemoryBarrierCount = 0;
	pipelineBarrierCmd.pipelineBarrier.imageMemoryBarrierCount = 1;
	pipelineBarrierCmd.pipelineBarrier.imageMemoryBarrier = memoryBarrier;
	VULKAN_INTERNAL_EncodeCommand(renderer, pipelineBarrierCmd);

	*resourceAccessType = nextAccess;
}

/* Vulkan: Swapchain */

static inline VkExtent2D ChooseSwapExtent(
	void* windowHandle,
	const VkSurfaceCapabilitiesKHR capabilities
) {
	VkExtent2D actualExtent;
	int32_t drawableWidth, drawableHeight;

	if (capabilities.currentExtent.width != UINT32_MAX)
	{
		return capabilities.currentExtent;
	}
	else
	{
		SDL_Vulkan_GetDrawableSize(
			(SDL_Window*) windowHandle,
			&drawableWidth,
			&drawableHeight
		);

		actualExtent.width = drawableWidth;
		actualExtent.height = drawableHeight;

		return actualExtent;
	}
}

static CreateSwapchainResult VULKAN_INTERNAL_CreateSwapchain(
	VulkanRenderer *renderer
) {
	VkResult vulkanResult;
	SwapChainSupportDetails swapChainSupportDetails;
	VkSurfaceFormatKHR surfaceFormat;
	VkPresentModeKHR presentMode;
	VkExtent2D extent = { 0, 0 };
	uint32_t imageCount, swapChainImageCount, i;
	VkSwapchainCreateInfoKHR swapChainCreateInfo =
	{
		VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR
	};
	VkImage *swapChainImages;
	VkImageViewCreateInfo createInfo =
	{
		VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
	};
	VkImageView swapChainImageView;

	if (!VULKAN_INTERNAL_QuerySwapChainSupport(
		renderer,
		renderer->physicalDevice,
		renderer->surface,
		&swapChainSupportDetails
	)) {
		FNA3D_LogError("Device does not support swap chain creation");
		return CREATE_SWAPCHAIN_FAIL;
	}

	renderer->swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
	renderer->swapchainSwizzle.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	renderer->swapchainSwizzle.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	renderer->swapchainSwizzle.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	renderer->swapchainSwizzle.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	if (!VULKAN_INTERNAL_ChooseSwapSurfaceFormat(
		renderer->swapchainFormat,
		swapChainSupportDetails.formats,
		swapChainSupportDetails.formatsLength,
		&surfaceFormat
	)) {
		SDL_free(swapChainSupportDetails.formats);
		SDL_free(swapChainSupportDetails.presentModes);
		FNA3D_LogError("Device does not support swap chain format");
		return CREATE_SWAPCHAIN_FAIL;
	}

	if (!VULKAN_INTERNAL_ChooseSwapPresentMode(
		renderer->presentInterval,
		swapChainSupportDetails.presentModes,
		swapChainSupportDetails.presentModesLength,
		&presentMode
	)) {
		SDL_free(swapChainSupportDetails.formats);
		SDL_free(swapChainSupportDetails.presentModes);
		FNA3D_LogError("Device does not support swap chain present mode");
		return CREATE_SWAPCHAIN_FAIL;
	}

	extent = ChooseSwapExtent(
		renderer->deviceWindowHandle,
		swapChainSupportDetails.capabilities
	);

	if (extent.width == 0 || extent.height == 0)
	{
		return CREATE_SWAPCHAIN_SURFACE_ZERO;
	}

	imageCount = swapChainSupportDetails.capabilities.minImageCount + 1;

	if (	swapChainSupportDetails.capabilities.maxImageCount > 0 &&
		imageCount > swapChainSupportDetails.capabilities.maxImageCount	)
	{
		imageCount = swapChainSupportDetails.capabilities.maxImageCount;
	}

	swapChainCreateInfo.surface = renderer->surface;
	swapChainCreateInfo.minImageCount = imageCount;
	swapChainCreateInfo.imageFormat = surfaceFormat.format;
	swapChainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
	swapChainCreateInfo.imageExtent = extent;
	swapChainCreateInfo.imageArrayLayers = 1;
	swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	swapChainCreateInfo.preTransform = swapChainSupportDetails.capabilities.currentTransform;
	swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapChainCreateInfo.presentMode = presentMode;
	swapChainCreateInfo.clipped = VK_TRUE;
	swapChainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

	vulkanResult = renderer->vkCreateSwapchainKHR(
		renderer->logicalDevice,
		&swapChainCreateInfo,
		NULL,
		&renderer->swapChain
	);

	SDL_free(swapChainSupportDetails.formats);
	SDL_free(swapChainSupportDetails.presentModes);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateSwapchainKHR", vulkanResult);

		return CREATE_SWAPCHAIN_FAIL;
	}

	renderer->vkGetSwapchainImagesKHR(
		renderer->logicalDevice,
		renderer->swapChain,
		&swapChainImageCount,
		NULL
	);

	renderer->swapChainImages = (VkImage*) SDL_malloc(
		sizeof(VkImage) * swapChainImageCount
	);
	if (!renderer->swapChainImages)
	{
		SDL_OutOfMemory();
		return CREATE_SWAPCHAIN_FAIL;
	}

	renderer->swapChainImageViews = (VkImageView*) SDL_malloc(
		sizeof(VkImageView) * swapChainImageCount
	);
	if (!renderer->swapChainImageViews)
	{
		SDL_OutOfMemory();
		return CREATE_SWAPCHAIN_FAIL;
	}

	renderer->swapChainResourceAccessTypes = (VulkanResourceAccessType*) SDL_malloc(
		sizeof(VulkanResourceAccessType) * swapChainImageCount
	);
	if (!renderer->swapChainResourceAccessTypes)
	{
		SDL_OutOfMemory();
		return CREATE_SWAPCHAIN_FAIL;
	}

	swapChainImages = SDL_stack_alloc(VkImage, swapChainImageCount);
	renderer->vkGetSwapchainImagesKHR(
		renderer->logicalDevice,
		renderer->swapChain,
		&swapChainImageCount,
		swapChainImages
	);
	renderer->swapChainImageCount = swapChainImageCount;
	renderer->swapChainExtent = extent;

	for (i = 0; i < swapChainImageCount; i += 1)
	{
		createInfo.image = swapChainImages[i];
		createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		createInfo.format = surfaceFormat.format;
		createInfo.components = renderer->swapchainSwizzle;
		createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		createInfo.subresourceRange.baseMipLevel = 0;
		createInfo.subresourceRange.levelCount = 1;
		createInfo.subresourceRange.baseArrayLayer = 0;
		createInfo.subresourceRange.layerCount = 1;

		vulkanResult = renderer->vkCreateImageView(
			renderer->logicalDevice,
			&createInfo,
			NULL,
			&swapChainImageView
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateImageView", vulkanResult);
			SDL_stack_free(swapChainImages);
			return CREATE_SWAPCHAIN_FAIL;
		}

		renderer->swapChainImages[i] = swapChainImages[i];
		renderer->swapChainImageViews[i] = swapChainImageView;
		renderer->swapChainResourceAccessTypes[i] = RESOURCE_ACCESS_NONE;
	}

	SDL_stack_free(swapChainImages);
	return CREATE_SWAPCHAIN_SUCCESS;
}

static void VULKAN_INTERNAL_DestroySwapchain(VulkanRenderer *renderer)
{
	uint32_t i;

	for (i = 0; i < hmlenu(renderer->framebufferHashMap); i += 1)
	{
		renderer->vkDestroyFramebuffer(
			renderer->logicalDevice,
			renderer->framebufferHashMap[i].value,
			NULL
		);
	}
	hmfree(renderer->framebufferHashMap);

	for (i = 0; i < renderer->swapChainImageCount; i += 1)
	{
		renderer->vkDestroyImageView(
			renderer->logicalDevice,
			renderer->swapChainImageViews[i],
			NULL
		);
	}

	SDL_free(renderer->swapChainImages);
	renderer->swapChainImages = NULL;
	SDL_free(renderer->swapChainImageViews);
	renderer->swapChainImageViews = NULL;
	SDL_free(renderer->swapChainResourceAccessTypes);
	renderer->swapChainResourceAccessTypes = NULL;

	renderer->vkDestroySwapchainKHR(
		renderer->logicalDevice,
		renderer->swapChain,
		NULL
	);
}

static void VULKAN_INTERNAL_RecreateSwapchain(VulkanRenderer *renderer)
{
	CreateSwapchainResult createSwapchainResult;
	SwapChainSupportDetails swapChainSupportDetails;
	VkExtent2D extent;

	VULKAN_INTERNAL_MaybeEndRenderPass(renderer);
	VULKAN_INTERNAL_Flush(renderer);

	renderer->vkDeviceWaitIdle(renderer->logicalDevice);

	VULKAN_INTERNAL_QuerySwapChainSupport(
		renderer,
		renderer->physicalDevice,
		renderer->surface,
		&swapChainSupportDetails
	);

	extent = ChooseSwapExtent(
		renderer->deviceWindowHandle,
		swapChainSupportDetails.capabilities
	);

	if (extent.width == 0 || extent.height == 0)
	{
		return;
	}

	VULKAN_INTERNAL_DestroySwapchain(renderer);
	createSwapchainResult = VULKAN_INTERNAL_CreateSwapchain(renderer);

	if (createSwapchainResult == CREATE_SWAPCHAIN_FAIL)
	{
		FNA3D_LogError("Failed to recreate swapchain");
		return;
	}

	renderer->vkDeviceWaitIdle(renderer->logicalDevice);
}

/* Vulkan: Buffer Objects */

static void VULKAN_INTERNAL_RemoveBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanBuffer *vulkanBuffer = (VulkanBuffer*) buffer;

	/* Queue buffer for destruction */
	if (renderer->buffersToDestroyCount + 1 >= renderer->buffersToDestroyCapacity)
	{
		renderer->buffersToDestroyCapacity *= 2;

		renderer->buffersToDestroy = SDL_realloc(
			renderer->buffersToDestroy,
			sizeof(VulkanBuffer*) * renderer->buffersToDestroyCapacity
		);
	}

	renderer->buffersToDestroy[
		renderer->buffersToDestroyCount
	] = vulkanBuffer;
	renderer->buffersToDestroyCount += 1;
}

static inline VkDeviceSize VULKAN_INTERNAL_NextHighestAlignment(
	VkDeviceSize n,
	VkDeviceSize align
) {
	return align * ((n + align - 1) / align);
}

static VulkanPhysicalBuffer *VULKAN_INTERNAL_NewPhysicalBuffer(
	VulkanRenderer *renderer,
	VkDeviceSize size,
	VkBufferUsageFlags usage
) {
	VkResult vulkanResult;
	VkBufferCreateInfo bufferCreateInfo =
	{
		VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO
	};
	VkMemoryDedicatedAllocateInfoKHR dedicatedInfo =
	{
		VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR,
		NULL,
		VK_NULL_HANDLE,
		VK_NULL_HANDLE
	};
	VkMemoryAllocateInfo allocInfo =
	{
		VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
	};
	VkMemoryDedicatedRequirementsKHR dedicatedRequirements =
	{
		VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR,
		NULL
	};
	VkMemoryRequirements2KHR memoryRequirements =
	{
		VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR,
		&dedicatedRequirements
	};
	VkBufferMemoryRequirementsInfo2KHR bufferRequirementsInfo =
	{
		VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2_KHR,
		NULL,
		VK_NULL_HANDLE
	};

	VulkanPhysicalBuffer *result = (VulkanPhysicalBuffer*) SDL_malloc(sizeof(VulkanPhysicalBuffer));
	SDL_zerop(result);

	bufferCreateInfo.size = size;
	bufferCreateInfo.usage = usage;

	bufferCreateInfo.flags = 0;
	bufferCreateInfo.queueFamilyIndexCount = 1;
	bufferCreateInfo.pQueueFamilyIndices = &renderer->queueFamilyIndices.graphicsFamily;

	vulkanResult = renderer->vkCreateBuffer(
		renderer->logicalDevice,
		&bufferCreateInfo,
		NULL,
		&result->buffer
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateBuffer", vulkanResult);
		FNA3D_LogError("Failed to create buffer");
		return (VulkanPhysicalBuffer*) NULL;
	}

	bufferRequirementsInfo.buffer = result->buffer;

	renderer->vkGetBufferMemoryRequirements2KHR(
		renderer->logicalDevice,
		&bufferRequirementsInfo,
		&memoryRequirements
	);

	allocInfo.allocationSize = memoryRequirements.memoryRequirements.size;

	if (!VULKAN_INTERNAL_FindMemoryType(
		renderer,
		memoryRequirements.memoryRequirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&allocInfo.memoryTypeIndex
	)) {
		FNA3D_LogError("Failed to allocate VkBuffer!");
		return NULL;
	}

	if (dedicatedRequirements.prefersDedicatedAllocation)
	{
		dedicatedInfo.buffer = result->buffer;
		allocInfo.pNext = &dedicatedInfo;
	}

	vulkanResult = renderer->vkAllocateMemory(
		renderer->logicalDevice,
		&allocInfo,
		NULL,
		&result->deviceMemory
	);

	if (vulkanResult != VK_SUCCESS)
	{
		FNA3D_LogError("Failed to allocate VkDeviceMemory!");
		return NULL;
	}

	vulkanResult = renderer->vkBindBufferMemory(
		renderer->logicalDevice,
		result->buffer,
		result->deviceMemory,
		0
	);

	if (vulkanResult != VK_SUCCESS)
	{
		FNA3D_LogError("Failed to bind buffer memory!");
		return NULL;
	}

	vulkanResult = renderer->vkMapMemory(
		renderer->logicalDevice,
		result->deviceMemory,
		0,
		size,
		0,
		(void**) &result->mapPointer
	);

	if (vulkanResult != VK_SUCCESS)
	{
		FNA3D_LogError("Failed to map memory!");
		return NULL;
	}

	result->size = size;

	return result;
}

/* Modified from the allocation strategy utilized by the Metal driver */
static void VULKAN_INTERNAL_AllocateSubBuffer(
	VulkanRenderer *renderer,
	VulkanBuffer *buffer
) {
	VkDeviceSize totalPhysicalSize, totalAllocated, alignment, alignedAlloc;
	uint32_t i;
	VulkanSubBuffer *subBuffer;

	if (	buffer->resourceAccessType == RESOURCE_ACCESS_FRAGMENT_SHADER_READ_UNIFORM_BUFFER ||
		buffer->resourceAccessType == RESOURCE_ACCESS_VERTEX_SHADER_READ_UNIFORM_BUFFER		)
	{
		alignment = renderer->physicalDeviceProperties.properties.limits.minUniformBufferOffsetAlignment;
	}
	else if (buffer->resourceAccessType == RESOURCE_ACCESS_VERTEX_BUFFER)
	{
		/* TODO: this is the max texel size.
		 * For efficiency we could query texel size by format.
		 */
		alignment = 16;
	}
	else
	{
		alignment = renderer->physicalDeviceProperties.properties.limits.minMemoryMapAlignment;
	}

	/* Which physical buffer should we suballocate from? */
	for (i = 0; i < PHYSICAL_BUFFER_MAX_COUNT; i += 1)
	{
		totalPhysicalSize = PHYSICAL_BUFFER_BASE_SIZE << i;
		totalAllocated = renderer->bufferAllocator->totalAllocated[i];
		alignedAlloc = VULKAN_INTERNAL_NextHighestAlignment(
			totalAllocated + buffer->size,
			alignment
		);

		if (alignedAlloc <= totalPhysicalSize)
		{
			/* It fits! */
			break;
		}
	}
	if (i == PHYSICAL_BUFFER_MAX_COUNT)
	{
		FNA3D_LogError("Oh crap, out of buffer room!!");
		return;
	}

	/* Create physical buffer */
	if (renderer->bufferAllocator->physicalBuffers[i] == NULL)
	{
		renderer->bufferAllocator->physicalBuffers[i] = VULKAN_INTERNAL_NewPhysicalBuffer(
			renderer,
			totalPhysicalSize,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT |
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
		);
	}

	/* Reallocate the subbuffer array if we're at max capacity */
	if (buffer->subBufferCount == buffer->subBufferCapacity)
	{
		buffer->subBufferCapacity *= 2;
		buffer->subBuffers = SDL_realloc(
			buffer->subBuffers,
			sizeof(VulkanSubBuffer) * buffer->subBufferCapacity
		);
	}

	/* Populate the given VulkanSubBuffer */
	subBuffer = &buffer->subBuffers[buffer->subBufferCount];
	subBuffer->physicalBuffer = renderer->bufferAllocator->physicalBuffers[i];
	subBuffer->offset = totalAllocated;
	subBuffer->resourceAccessType = buffer->resourceAccessType;
	subBuffer->ptr = (
		subBuffer->physicalBuffer->mapPointer +
		subBuffer->offset
	);

	/* Mark how much we've just allocated, rounding up for alignment */
	renderer->bufferAllocator->totalAllocated[i] = alignedAlloc;
	buffer->subBufferCount += 1;

	VULKAN_INTERNAL_BufferMemoryBarrier(
		renderer,
		buffer->resourceAccessType,
		buffer,
		subBuffer
	);
}

static void VULKAN_INTERNAL_MarkAsBound(
	VulkanRenderer *renderer,
	VulkanBuffer *buf
) {
	/* Don't rebind a bound buffer */
	if (buf->bound) return;

	buf->bound = 1;

	if (renderer->numBuffersInUse == renderer->maxBuffersInUse)
	{
		renderer->maxBuffersInUse *= 2;
		renderer->buffersInUse = SDL_realloc(
			renderer->buffersInUse,
			sizeof(VulkanBuffer*) * renderer->maxBuffersInUse
		);
	}

	renderer->buffersInUse[renderer->numBuffersInUse] = buf;
	renderer->numBuffersInUse += 1;
}

static void VULKAN_INTERNAL_SetBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanBuffer *vulkanBuffer = (VulkanBuffer*) buffer;
	uint32_t prevIndex;

	#define CURIDX vulkanBuffer->currentSubBufferIndex
	#define SUBBUF vulkanBuffer->subBuffers[CURIDX]

	prevIndex = CURIDX;

	if (vulkanBuffer->bound)
	{
		if (options == FNA3D_SETDATAOPTIONS_NONE)
		{
			VULKAN_INTERNAL_Flush(renderer);
			vulkanBuffer->bound = 1;
		}
		else if (options == FNA3D_SETDATAOPTIONS_DISCARD)
		{
			CURIDX += 1;
		}
	}

	/* Create a new SubBuffer if needed */
	if (CURIDX == vulkanBuffer->subBufferCount)
	{
		VULKAN_INTERNAL_AllocateSubBuffer(renderer, vulkanBuffer);
	}

	/* Copy over previous contents when needed */
	if (	options == FNA3D_SETDATAOPTIONS_NONE &&
		dataLength < vulkanBuffer->size &&
		CURIDX != prevIndex			)
	{
		SDL_memcpy(
			SUBBUF.ptr,
			vulkanBuffer->subBuffers[prevIndex].ptr,
			vulkanBuffer->size
		);
	}

	/* Copy the data! */
	SDL_memcpy(
		SUBBUF.ptr + offsetInBytes,
		data,
		dataLength
	);

	#undef SUBBUF
	#undef CURIDX
}

static FNA3D_Buffer* VULKAN_INTERNAL_CreateBuffer(
	VkDeviceSize size,
	VulkanResourceAccessType resourceAccessType
) {
	VulkanBuffer *result = SDL_malloc(sizeof(VulkanBuffer));
	SDL_memset(result, '\0', sizeof(VulkanBuffer));

	result->size = size;
	result->resourceAccessType = resourceAccessType;
	result->subBufferCapacity = 4;
	result->subBuffers = SDL_malloc(
		sizeof(VulkanSubBuffer) * result->subBufferCapacity
	);

	return (FNA3D_Buffer*) result;
}

static void VULKAN_INTERNAL_DestroyTextureStagingBuffer(
	VulkanRenderer *renderer
) {
	renderer->vkFreeMemory(
		renderer->logicalDevice,
		renderer->textureStagingBuffer->deviceMemory,
		NULL
	);

	renderer->vkDestroyBuffer(
		renderer->logicalDevice,
		renderer->textureStagingBuffer->buffer,
		NULL
	);

	SDL_free(renderer->textureStagingBuffer);
}

static void VULKAN_INTERNAL_MaybeExpandStagingBuffer(
	VulkanRenderer *renderer,
	VkDeviceSize size
) {
	if (size <= renderer->textureStagingBuffer->size)
	{
		return;
	}

	VULKAN_INTERNAL_DestroyTextureStagingBuffer(renderer);

	renderer->textureStagingBuffer = VULKAN_INTERNAL_NewPhysicalBuffer(
		renderer,
		size,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
	);
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
	VkImageTiling tiling,
	VkImageType imageType,
	VkImageUsageFlags usage,
	VkMemoryPropertyFlags memoryProperties,
	VulkanTexture *texture
) {
	VkResult result;
	VkImageCreateInfo imageCreateInfo =
	{
		VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO
	};
	VkMemoryDedicatedAllocateInfoKHR dedicatedInfo =
	{
		VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR,
		NULL,
		VK_NULL_HANDLE,
		VK_NULL_HANDLE
	};
	VkMemoryAllocateInfo allocInfo =
	{
		VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
	};
	VkMemoryDedicatedRequirementsKHR dedicatedRequirements =
	{
		VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR,
		NULL
	};
	VkMemoryRequirements2KHR memoryRequirements =
	{
		VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR,
		&dedicatedRequirements
	};
	VkImageMemoryRequirementsInfo2KHR imageRequirementsInfo =
	{
		VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2_KHR,
		NULL,
		VK_NULL_HANDLE
	};
	VkImageViewCreateInfo imageViewCreateInfo =
	{
		VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
	};
	uint8_t layerCount = isCube ? 6 : 1;
	uint32_t i;

	imageCreateInfo.flags = isCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
	imageCreateInfo.imageType = imageType;
	imageCreateInfo.format = format;
	imageCreateInfo.extent.width = width;
	imageCreateInfo.extent.height = height;
	imageCreateInfo.extent.depth = depth;
	imageCreateInfo.mipLevels = levelCount;
	imageCreateInfo.arrayLayers = layerCount;
	imageCreateInfo.samples = samples;
	imageCreateInfo.tiling = tiling;
	imageCreateInfo.usage = usage;
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCreateInfo.queueFamilyIndexCount = 0;
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	result = renderer->vkCreateImage(
		renderer->logicalDevice,
		&imageCreateInfo,
		NULL,
		&texture->image
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateImage", result);
		FNA3D_LogError("Failed to create image");
		return 0;
	}

	imageRequirementsInfo.image = texture->image;

	renderer->vkGetImageMemoryRequirements2KHR(
		renderer->logicalDevice,
		&imageRequirementsInfo,
		&memoryRequirements
	);

	texture->memorySize = memoryRequirements.memoryRequirements.size;
	allocInfo.allocationSize = memoryRequirements.memoryRequirements.size;

	if (!VULKAN_INTERNAL_FindMemoryType(
		renderer,
		memoryRequirements.memoryRequirements.memoryTypeBits,
		memoryProperties,
		&allocInfo.memoryTypeIndex
	)) {
		FNA3D_LogError(
			"Could not find valid memory type for image creation"
		);
		return 0;
	}

	if (dedicatedRequirements.prefersDedicatedAllocation)
	{
		dedicatedInfo.image = texture->image;
		allocInfo.pNext = &dedicatedInfo;
	}

	result = renderer->vkAllocateMemory(
		renderer->logicalDevice,
		&allocInfo,
		NULL,
		&texture->memory
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkAllocateMemory", result);
		return 0;
	}

	result = renderer->vkBindImageMemory(
		renderer->logicalDevice,
		texture->image,
		texture->memory,
		0
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkBindImageMemory", result);
		return 0;
	}

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
		FNA3D_LogError("invalid image type: %u", imageType);
	}

	result = renderer->vkCreateImageView(
		renderer->logicalDevice,
		&imageViewCreateInfo,
		NULL,
		&texture->view
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateImageView", result);
		FNA3D_LogError("Failed to create texture image view");
		return 0;
	}

	if (isRenderTarget)
	{
		if (!isCube && levelCount == 1)
		{
			/* Skip a step and just use the view directly */
			texture->rtViews[0] = texture->view;
		}
		else
		{
			/* Create a framebuffer-compatible view for each layer */
			for (i = 0; i < layerCount; i += 1)
			{
				imageViewCreateInfo.subresourceRange.levelCount = 1;
				imageViewCreateInfo.subresourceRange.layerCount = 1;
				imageViewCreateInfo.subresourceRange.baseArrayLayer = i;

				result = renderer->vkCreateImageView(
					renderer->logicalDevice,
					&imageViewCreateInfo,
					NULL,
					&texture->rtViews[i]
				);

				if (result != VK_SUCCESS)
				{
					LogVulkanResult(
						"vkCreateImageView",
						result
					);
					FNA3D_LogError("Failed to create color attachment image view");
					return 0;
				}
			}
		}
	}

	texture->dimensions.width = width;
	texture->dimensions.height = height;
	texture->depth = depth;
	texture->resourceAccessType = RESOURCE_ACCESS_NONE;
	texture->surfaceFormat = format;
	texture->levelCount = levelCount;
	texture->layerCount = layerCount;

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
	VulkanResourceAccessType prevResourceAccess;
	VkBufferImageCopy imageCopy;
	uint8_t *dataPtr = (uint8_t*) data;
	VulkanCommand copyImageCmd =
	{
		CMDTYPE_COPY_IMAGE_TO_BUFFER
	};

	SDL_LockMutex(renderer->passLock);

	VULKAN_INTERNAL_MaybeExpandStagingBuffer(renderer, dataLength);

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

	/* Save texture data to staging buffer */

	imageCopy.imageExtent.width = w;
	imageCopy.imageExtent.height = h;
	imageCopy.imageExtent.depth = 1;
	imageCopy.bufferRowLength = w;
	imageCopy.bufferImageHeight = h;
	imageCopy.imageOffset.x = x;
	imageCopy.imageOffset.y = y;
	imageCopy.imageOffset.z = 0;
	imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageCopy.imageSubresource.baseArrayLayer = layer;
	imageCopy.imageSubresource.layerCount = 1;
	imageCopy.imageSubresource.mipLevel = level;
	imageCopy.bufferOffset = 0;

	copyImageCmd.copyImageToBuffer.srcImage = vulkanTexture->image;
	copyImageCmd.copyImageToBuffer.srcImageLayout = AccessMap[vulkanTexture->resourceAccessType].imageLayout;
	copyImageCmd.copyImageToBuffer.dstBuffer = renderer->textureStagingBuffer->buffer;
	copyImageCmd.copyImageToBuffer.region = imageCopy;
	VULKAN_INTERNAL_EncodeCommand(
		renderer,
		copyImageCmd
	);

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

	VULKAN_INTERNAL_Flush(renderer);

	/* Read from staging buffer */

	SDL_memcpy(
		dataPtr,
		renderer->textureStagingBuffer->mapPointer,
		BytesPerImage(w, h, vulkanTexture->colorFormat)
	);

	SDL_UnlockMutex(renderer->passLock);
}

/* Vulkan: Mutable State Commands */

static void VULKAN_INTERNAL_SetViewportCommand(VulkanRenderer *renderer)
{
	int32_t targetHeight, meh;
	VkViewport vulkanViewport;
	VulkanCommand setViewportCmd =
	{
		CMDTYPE_SET_VIEWPORT
	};

	/* Flipping the viewport for compatibility with D3D */
	vulkanViewport.x = (float) renderer->viewport.x;
	if (!renderer->renderTargetBound)
	{
		VULKAN_GetBackbufferSize(
			(FNA3D_Renderer*) renderer,
			&meh,
			&targetHeight
		);
	}
	else
	{
		targetHeight = renderer->viewport.h;
	}

	vulkanViewport.y = (float) (targetHeight - renderer->viewport.y);
	vulkanViewport.width = (float) renderer->viewport.w;
	vulkanViewport.height = (float) -renderer->viewport.h;
	vulkanViewport.minDepth = renderer->viewport.minDepth;
	vulkanViewport.maxDepth = renderer->viewport.maxDepth;

	setViewportCmd.setViewport.viewport = vulkanViewport;
	VULKAN_INTERNAL_EncodeCommand(
		renderer,
		setViewportCmd
	);
}

static void VULKAN_INTERNAL_SetScissorRectCommand(VulkanRenderer *renderer)
{
	VkOffset2D offset;
	VkExtent2D extent;
	VkRect2D vulkanScissorRect;
	VulkanCommand setScissorCmd =
	{
		CMDTYPE_SET_SCISSOR
	};

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

		setScissorCmd.setScissor.scissor = vulkanScissorRect;
		VULKAN_INTERNAL_EncodeCommand(
			renderer,
			setScissorCmd
		);
	}
}

static void VULKAN_INTERNAL_SetStencilReferenceValueCommand(
	VulkanRenderer *renderer
) {
	VulkanCommand setRefCmd =
	{
		CMDTYPE_SET_STENCIL_REFERENCE
	};

	if (renderer->renderPassInProgress)
	{
		setRefCmd.setStencilReference.reference = renderer->stencilRef;
		VULKAN_INTERNAL_EncodeCommand(
			renderer,
			setRefCmd
		);
	}
}

static void VULKAN_INTERNAL_SetDepthBiasCommand(VulkanRenderer *renderer)
{
	VulkanCommand setDepthBiasCmd =
	{
		CMDTYPE_SET_DEPTH_BIAS
	};

	if (renderer->renderPassInProgress)
	{
		setDepthBiasCmd.setDepthBias.depthBiasConstantFactor =
			renderer->rasterizerState.depthBias;
		setDepthBiasCmd.setDepthBias.depthBiasSlopeFactor =
			renderer->rasterizerState.slopeScaleDepthBias;
		VULKAN_INTERNAL_EncodeCommand(
			renderer,
			setDepthBiasCmd
		);
	}
}

/* Vulkan: Pipeline State Objects */

static VkPipelineLayout VULKAN_INTERNAL_FetchPipelineLayout(
	VulkanRenderer *renderer,
	MOJOSHADER_vkShader *vertShader,
	MOJOSHADER_vkShader *fragShader
) {
	PipelineLayoutHash hash;
	VkDescriptorSetLayout setLayouts[4];
	VkPipelineLayoutCreateInfo layoutCreateInfo =
	{
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
	};
	VkPipelineLayout layout;
	VkResult vulkanResult;

	/* Get pipeline layout hash */
	hash.vertUniformBufferCount =
		MOJOSHADER_vkGetShaderParseData(vertShader)->uniform_count > 0;
	hash.vertSamplerCount =
		MOJOSHADER_vkGetShaderParseData(vertShader)->sampler_count;
	hash.fragUniformBufferCount =
		MOJOSHADER_vkGetShaderParseData(fragShader)->uniform_count > 0;
	hash.fragSamplerCount =
		MOJOSHADER_vkGetShaderParseData(fragShader)->sampler_count;

	renderer->currentPipelineLayoutHash = hash;

	if (hmgeti(renderer->pipelineLayoutHashMap, hash) != -1)
	{
		return hmget(renderer->pipelineLayoutHashMap, hash);
	}

	setLayouts[0] = renderer->vertSamplerDescriptorSetLayouts[
		hash.vertSamplerCount
	];
	setLayouts[1] = renderer->fragSamplerDescriptorSetLayouts[
		hash.fragSamplerCount
	];
	setLayouts[2] = renderer->vertUniformBufferDescriptorSetLayout;
	setLayouts[3] = renderer->fragUniformBufferDescriptorSetLayout;

	layoutCreateInfo.setLayoutCount = 4;
	layoutCreateInfo.pSetLayouts = setLayouts;

	vulkanResult = renderer->vkCreatePipelineLayout(
		renderer->logicalDevice,
		&layoutCreateInfo,
		NULL,
		&layout
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreatePipelineLayout", vulkanResult);
		return NULL_PIPELINE_LAYOUT;
	}

	hmput(renderer->pipelineLayoutHashMap, hash, layout);
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

	MOJOSHADER_vkGetBoundShaders(&vertexShader, &blah);

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
	VkPipelineViewportStateCreateInfo viewportStateInfo =
	{
		VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
	};
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo =
	{
		VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
	};
	VkVertexInputBindingDescription *bindingDescriptions;
	VkVertexInputAttributeDescription *attributeDescriptions;
	uint32_t attributeDescriptionCount;
	VkVertexInputBindingDivisorDescriptionEXT *divisorDescriptions;
	uint32_t divisorDescriptionCount;
	VkPipelineVertexInputStateCreateInfo vertexInputInfo =
	{
		VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
	};
	VkPipelineVertexInputDivisorStateCreateInfoEXT divisorStateInfo =
	{
		VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT
	};
	VkPipelineRasterizationStateCreateInfo rasterizerInfo =
	{
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
	};
	VkPipelineMultisampleStateCreateInfo multisamplingInfo =
	{
		VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
	};
	VkPipelineColorBlendAttachmentState colorBlendAttachments[MAX_RENDERTARGET_BINDINGS];
	VkPipelineColorBlendStateCreateInfo colorBlendStateInfo =
	{
		VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
	};
	VkStencilOpState frontStencilState, backStencilState;
	VkPipelineDepthStencilStateCreateInfo depthStencilStateInfo =
	{
		VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
	};
	VkDynamicState dynamicStates[] =
	{
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_BLEND_CONSTANTS,
		VK_DYNAMIC_STATE_STENCIL_REFERENCE,
		VK_DYNAMIC_STATE_DEPTH_BIAS
	};
	VkPipelineDynamicStateCreateInfo dynamicStateInfo =
	{
		VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
	};
	MOJOSHADER_vkShader *vertShader, *fragShader;
	VkPipelineShaderStageCreateInfo vertShaderStageInfo =
	{
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
	};
	VkPipelineShaderStageCreateInfo fragShaderStageInfo =
	{
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
	};
	VkPipelineShaderStageCreateInfo stageInfos[2];
	VkGraphicsPipelineCreateInfo pipelineCreateInfo =
	{
		VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
	};

	PipelineHash hash;
	hash.blendState = GetBlendStateHash(renderer->blendState);
	hash.rasterizerState = GetRasterizerStateHash(
		renderer->rasterizerState,
		renderer->rasterizerState.depthBias * XNAToVK_DepthBiasScale(
			XNAToVK_DepthFormat(
				renderer,
				renderer->currentDepthFormat
			)
		)
	);
	hash.depthStencilState = GetDepthStencilStateHash(
		renderer->depthStencilState
	);
	hash.vertexBufferBindingsHash = renderer->currentVertexBufferBindingHash;
	hash.primitiveType = renderer->currentPrimitiveType;
	hash.sampleMask = renderer->multiSampleMask[0];
	MOJOSHADER_vkGetBoundShaders(&vertShader, &fragShader);
	hash.vertShader = (uint64_t) (size_t) vertShader;
	hash.fragShader = (uint64_t) (size_t) fragShader;
	hash.renderPass = renderer->renderPass;

	renderer->currentPipelineLayout = VULKAN_INTERNAL_FetchPipelineLayout(
		renderer,
		vertShader,
		fragShader
	);

	if (hmgeti(renderer->pipelineHashMap, hash) != -1)
	{
		return hmget(renderer->pipelineHashMap, hash);
	}

	/* Viewport / Scissor */

	/* NOTE: because viewport and scissor are dynamic,
	 * values must be set using the command buffer
	 */
	viewportStateInfo.viewportCount = 1;
	viewportStateInfo.scissorCount = 1;

	/* Input Assembly */

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

	if (divisorDescriptionCount > 0)
	{
		divisorStateInfo.vertexBindingDivisorCount =
			divisorDescriptionCount;
		divisorStateInfo.pVertexBindingDivisors = divisorDescriptions;
		vertexInputInfo.pNext = &divisorStateInfo;
	}

	vertexInputInfo.vertexBindingDescriptionCount =
		renderer->numVertexBindings;
	vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions;
	vertexInputInfo.vertexAttributeDescriptionCount =
		attributeDescriptionCount;
	vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

	/* Rasterizer */

	rasterizerInfo.depthClampEnable = VK_FALSE;
	rasterizerInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterizerInfo.polygonMode = XNAToVK_PolygonMode[
		renderer->rasterizerState.fillMode
	];
	rasterizerInfo.lineWidth = 1.0f;
	rasterizerInfo.cullMode = XNAToVK_CullMode[
		renderer->rasterizerState.cullMode
	];
	/* This is reversed because we are flipping the viewport -cosmonaut */
	rasterizerInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizerInfo.depthBiasEnable = VK_TRUE;

	/* Multisample */

	multisamplingInfo.sampleShadingEnable = VK_FALSE;
	multisamplingInfo.minSampleShading = 1.0f;
	multisamplingInfo.pSampleMask = renderer->multiSampleMask;
	multisamplingInfo.rasterizationSamples = XNAToVK_SampleCount(
		renderer->multiSampleCount
	);
	multisamplingInfo.alphaToCoverageEnable = VK_FALSE;
	multisamplingInfo.alphaToOneEnable = VK_FALSE;

	/* Blend */

	SDL_zero(colorBlendAttachments);
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

	colorBlendStateInfo.logicOpEnable = VK_FALSE;
	colorBlendStateInfo.logicOp = VK_LOGIC_OP_COPY;
	colorBlendStateInfo.attachmentCount = renderer->colorAttachmentCount;
	colorBlendStateInfo.pAttachments = colorBlendAttachments;

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
		renderer->depthStencilState.stencilFunction
	];
	backStencilState.compareMask = renderer->depthStencilState.stencilMask;
	backStencilState.writeMask = renderer->depthStencilState.stencilWriteMask;
	backStencilState.reference = renderer->depthStencilState.referenceStencil;

	/* Depth Stencil */

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

	dynamicStateInfo.dynamicStateCount = SDL_arraysize(dynamicStates);
	dynamicStateInfo.pDynamicStates = dynamicStates;

	/* Shaders */

	vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertShaderStageInfo.pName = MOJOSHADER_vkGetShaderParseData(vertShader)->mainfn;

	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.pName = MOJOSHADER_vkGetShaderParseData(fragShader)->mainfn;

	MOJOSHADER_vkGetShaderModules(
		&vertShaderStageInfo.module,
		&fragShaderStageInfo.module
	);

	stageInfos[0] = vertShaderStageInfo;
	stageInfos[1] = fragShaderStageInfo;

	/* Pipeline */

	pipelineCreateInfo.stageCount = 2;
	pipelineCreateInfo.pStages = stageInfos;
	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyInfo;
	pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
	pipelineCreateInfo.pViewportState = &viewportStateInfo;
	pipelineCreateInfo.pRasterizationState = &rasterizerInfo;
	pipelineCreateInfo.pMultisampleState = &multisamplingInfo;
	pipelineCreateInfo.pDepthStencilState = &depthStencilStateInfo;
	pipelineCreateInfo.pColorBlendState = &colorBlendStateInfo;
	pipelineCreateInfo.pDynamicState = &dynamicStateInfo;
	pipelineCreateInfo.layout = renderer->currentPipelineLayout;
	pipelineCreateInfo.renderPass = renderer->renderPass;

	vulkanResult = renderer->vkCreateGraphicsPipelines(
		renderer->logicalDevice,
		renderer->pipelineCache,
		1,
		&pipelineCreateInfo,
		NULL,
		&pipeline
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateGraphicsPipelines", vulkanResult);
		return NULL_PIPELINE;
	}

	SDL_free(bindingDescriptions);
	SDL_free(attributeDescriptions);
	SDL_free(divisorDescriptions);

	hmput(renderer->pipelineHashMap, hash, pipeline);
	return pipeline;
}

static void VULKAN_INTERNAL_BindPipeline(VulkanRenderer *renderer)
{
	VulkanCommand bindPipelineCmd =
	{
		CMDTYPE_BIND_PIPELINE
	};
	VkShaderModule vertShader, fragShader;
	MOJOSHADER_vkGetShaderModules(&vertShader, &fragShader);

	if (	renderer->needNewPipeline ||
		renderer->currentVertShader != vertShader ||
		renderer->currentFragShader != fragShader	)
	{
		VkPipeline pipeline = VULKAN_INTERNAL_FetchPipeline(renderer);

		if (pipeline != renderer->currentPipeline)
		{
			bindPipelineCmd.bindPipeline.pipeline = pipeline;
			VULKAN_INTERNAL_EncodeCommand(
				renderer,
				bindPipelineCmd
			);

			renderer->currentPipeline = pipeline;
		}

		renderer->needNewPipeline = 0;
		renderer->currentVertShader = vertShader;
		renderer->currentFragShader = fragShader;
	}
}

/* Vulkan: Resource Disposal */

static void VULKAN_INTERNAL_DestroyBuffer(
	VulkanRenderer *renderer,
	VulkanBuffer *buffer
) {
	uint32_t i;

	if (buffer->bound)
	{
		for (i = 0; i < renderer->numBuffersInUse; i += 1)
		{
			if (renderer->buffersInUse[i] == buffer)
			{
				renderer->buffersInUse[i] = NULL;
			}
		}
	}

	SDL_free(buffer->subBuffers);
	buffer->subBuffers = NULL;

	SDL_free(buffer);
}

static void VULKAN_INTERNAL_DestroyTexture(
	VulkanRenderer *renderer,
	VulkanTexture *texture
) {
	int32_t i;

	renderer->vkFreeMemory(
		renderer->logicalDevice,
		texture->memory,
		NULL
	);

	renderer->vkDestroyImageView(
		renderer->logicalDevice,
		texture->view,
		NULL
	);

	if (texture->rtViews[0] != texture->view)
	{
		renderer->vkDestroyImageView(
			renderer->logicalDevice,
			texture->rtViews[0],
			NULL
		);
	}

	if (texture->rtViews[1] != NULL_IMAGE_VIEW)
	{
		/* Free all the other cube RT views */
		for (i = 1; i < 6; i += 1)
		{
			renderer->vkDestroyImageView(
				renderer->logicalDevice,
				texture->rtViews[i],
				NULL
			);
		}
	}

	renderer->vkDestroyImage(
		renderer->logicalDevice,
		texture->image,
		NULL
	);

	SDL_free(texture);
}

static void VULKAN_INTERNAL_DestroyRenderbuffer(
	VulkanRenderer *renderer,
	VulkanRenderbuffer *renderbuffer
) {
	uint8_t isDepthStencil = (renderbuffer->colorBuffer == NULL);

	if (isDepthStencil)
	{
		VULKAN_INTERNAL_DestroyTexture(
			renderer,
			renderbuffer->depthBuffer->handle
		);
		SDL_free(renderbuffer->depthBuffer);
	}
	else
	{
		if (renderbuffer->colorBuffer->multiSampleTexture != NULL)
		{
			VULKAN_INTERNAL_DestroyTexture(
				renderer,
				renderbuffer->colorBuffer->multiSampleTexture
			);
		}

		/* The image is owned by the texture it's from,
		 * so we don't free it here.
		 */
		SDL_free(renderbuffer->colorBuffer);
	}

	SDL_free(renderbuffer);
}

static void VULKAN_INTERNAL_DestroyEffect(
	VulkanRenderer *renderer,
	VulkanEffect *vulkanEffect
) {
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

static void VULKAN_INTERNAL_PerformDeferredDestroys(VulkanRenderer *renderer)
{
	uint32_t i;

	for (i = 0; i < renderer->renderbuffersToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyRenderbuffer(
			renderer,
			renderer->renderbuffersToDestroy[i]
		);
	}
	renderer->renderbuffersToDestroyCount = 0;

	for (i = 0; i < renderer->buffersToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyBuffer(
			renderer,
			renderer->buffersToDestroy[i]
		);
	}
	renderer->buffersToDestroyCount = 0;

	for (i = 0; i < renderer->effectsToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyEffect(
			renderer,
			renderer->effectsToDestroy[i]
		);
	}
	renderer->effectsToDestroyCount = 0;

	for (i = 0; i < renderer->texturesToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyTexture(
			renderer,
			renderer->texturesToDestroy[i]
		);
	}
	renderer->texturesToDestroyCount = 0;
}

/* Vulkan: The Faux-Backbuffer */

static uint8_t VULKAN_INTERNAL_CreateFauxBackbuffer(
	VulkanRenderer *renderer,
	FNA3D_PresentationParameters *presentationParameters
) {
	VkFormat vulkanDepthStencilFormat;
	VkImageAspectFlags depthAspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;

	renderer->fauxBackbufferColor.handle = (VulkanTexture*) SDL_malloc(sizeof(VulkanTexture));
	SDL_memset(renderer->fauxBackbufferColor.handle, '\0', sizeof(VulkanTexture));

	if (!VULKAN_INTERNAL_CreateTexture(
		renderer,
		presentationParameters->backBufferWidth,
		presentationParameters->backBufferHeight,
		1,
		0,
		1,
		VK_SAMPLE_COUNT_1_BIT,
		1,
		renderer->swapchainFormat,
		renderer->swapchainSwizzle,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_TYPE_2D,
		/* FIXME: Transfer bit probably only needs to be set on 0? */
		(
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT
		),
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		renderer->fauxBackbufferColor.handle
	)) {
		FNA3D_LogError("Failed to create faux backbuffer colorbuffer");
		return 0;
	}
	renderer->fauxBackbufferColor.handle->colorFormat =
		presentationParameters->backBufferFormat;

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
		SDL_memset(
			renderer->fauxBackbufferMultiSampleColor,
			'\0',
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
			renderer->swapchainFormat,
			renderer->swapchainSwizzle,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			renderer->fauxBackbufferMultiSampleColor
		);
		/* FIXME: Swapchain format may not be an FNA3D_SurfaceFormat! */
		renderer->fauxBackbufferMultiSampleColor->colorFormat = FNA3D_SURFACEFORMAT_COLOR;

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
		SDL_memset(
			renderer->fauxBackbufferDepthStencil.handle,
			'\0',
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
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_TYPE_2D,
			(
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
				VK_IMAGE_USAGE_TRANSFER_DST_BIT
			),
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
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
			renderer->depthStencilAttachment =
				renderer->fauxBackbufferDepthStencil.handle;
		}
	}

	if (!renderer->renderTargetBound)
	{
		renderer->colorAttachments[0] =
			renderer->fauxBackbufferColor.handle;
		renderer->colorAttachmentCount = 1;

		if (renderer->fauxBackbufferMultiSampleCount > 0)
		{
			renderer->colorMultiSampleAttachments[0] =
				renderer->fauxBackbufferMultiSampleColor;
			renderer->multiSampleCount =
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
	VkRenderPass renderPass = NULL_RENDER_PASS;
	VkAttachmentDescription attachmentDescriptions[MAX_RENDERTARGET_BINDINGS + 1];
	uint32_t attachmentDescriptionsCount = 0;
	uint32_t i;
	VkAttachmentReference colorAttachmentReferences[MAX_RENDERTARGET_BINDINGS];
	uint32_t colorAttachmentReferenceCount = 0;
	VkAttachmentReference resolveReferences[MAX_RENDERTARGET_BINDINGS + 1];
	uint32_t resolveReferenceCount = 0;
	VkAttachmentReference depthStencilAttachmentReference;
	VkSubpassDescription subpass;
	VkRenderPassCreateInfo renderPassCreateInfo =
	{
		VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO
	};

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

	hash.width = renderer->colorAttachments[0]->dimensions.width;
	hash.height = renderer->colorAttachments[0]->dimensions.height;
	hash.multiSampleCount = renderer->multiSampleCount;

	/* The render pass is already cached, can return it */
	if (hmgeti(renderer->renderPassHashMap, hash) != -1)
	{
		return hmget(renderer->renderPassHashMap, hash);
	}

	/* Otherwise lets make a new one */

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
				VK_ATTACHMENT_LOAD_OP_LOAD;
			attachmentDescriptions[attachmentDescriptionsCount].storeOp =
				VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescriptions[attachmentDescriptionsCount].stencilLoadOp =
				VK_ATTACHMENT_LOAD_OP_LOAD;
			attachmentDescriptions[attachmentDescriptionsCount].stencilStoreOp =
				VK_ATTACHMENT_STORE_OP_STORE;
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
				VK_ATTACHMENT_LOAD_OP_LOAD;
			attachmentDescriptions[attachmentDescriptionsCount].storeOp =
				VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescriptions[attachmentDescriptionsCount].stencilLoadOp =
				VK_ATTACHMENT_LOAD_OP_LOAD;
			attachmentDescriptions[attachmentDescriptionsCount].stencilStoreOp =
				VK_ATTACHMENT_STORE_OP_STORE;
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
				VK_ATTACHMENT_LOAD_OP_LOAD;
			attachmentDescriptions[attachmentDescriptionsCount].storeOp =
				VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescriptions[attachmentDescriptionsCount].stencilLoadOp =
				VK_ATTACHMENT_LOAD_OP_LOAD;
			attachmentDescriptions[attachmentDescriptionsCount].stencilStoreOp =
				VK_ATTACHMENT_STORE_OP_STORE;
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
			VK_ATTACHMENT_LOAD_OP_LOAD;
		attachmentDescriptions[attachmentDescriptionsCount].storeOp =
			VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescriptions[attachmentDescriptionsCount].stencilLoadOp =
			VK_ATTACHMENT_LOAD_OP_LOAD;
		attachmentDescriptions[attachmentDescriptionsCount].stencilStoreOp =
			VK_ATTACHMENT_STORE_OP_STORE;
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

	renderPassCreateInfo.attachmentCount = attachmentDescriptionsCount;
	renderPassCreateInfo.pAttachments = attachmentDescriptions;
	renderPassCreateInfo.subpassCount = 1;
	renderPassCreateInfo.pSubpasses = &subpass;
	renderPassCreateInfo.dependencyCount = 0;
	renderPassCreateInfo.pDependencies = NULL;
	renderPassCreateInfo.flags = 0;

	vulkanResult = renderer->vkCreateRenderPass(
		renderer->logicalDevice,
		&renderPassCreateInfo,
		NULL,
		&renderPass
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateRenderPass", vulkanResult);
		return NULL_RENDER_PASS;
	}

	hmput(renderer->renderPassHashMap, hash, renderPass);
	return renderPass;
}

static VkFramebuffer VULKAN_INTERNAL_FetchFramebuffer(
	VulkanRenderer *renderer,
	VkRenderPass renderPass
) {
	VkFramebuffer framebuffer;
	VkImageView imageViewAttachments[2 * MAX_RENDERTARGET_BINDINGS + 1];
	uint32_t i, attachmentCount;
	VkFramebufferCreateInfo framebufferInfo =
	{
		VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
	};
	VkResult vulkanResult;

	FramebufferHash hash;
	for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i += 1)
	{
		hash.colorAttachmentViews[i] = (
			renderer->colorAttachments[i] != NULL ?
				renderer->colorAttachments[i]->rtViews[
					renderer->attachmentCubeFaces[i]
				] :
				NULL_IMAGE_VIEW
		);
		hash.colorMultiSampleAttachmentViews[i] = (
			renderer->colorMultiSampleAttachments[i] != NULL ?
				renderer->colorMultiSampleAttachments[i]->rtViews[
					renderer->attachmentCubeFaces[i]
				] :
				NULL_IMAGE_VIEW
		);
	}
	hash.depthStencilAttachmentView = (
		renderer->depthStencilAttachment != NULL ?
			renderer->depthStencilAttachment->rtViews[0] :
			NULL_IMAGE_VIEW
	);
	hash.width = renderer->colorAttachments[0]->dimensions.width;
	hash.height = renderer->colorAttachments[0]->dimensions.height;

	/* Framebuffer is cached, can return it */
	if (hmgeti(renderer->framebufferHashMap, hash) != -1)
	{
		return hmget(renderer->framebufferHashMap, hash);
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

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateFramebuffer", vulkanResult);
	}

	hmput(renderer->framebufferHashMap, hash, framebuffer);

	return framebuffer;
}

static void VULKAN_INTERNAL_MaybeEndRenderPass(
	VulkanRenderer *renderer
) {
	VulkanCommand endPassCmd =
	{
		CMDTYPE_END_RENDER_PASS
	};

	SDL_LockMutex(renderer->passLock);
	if (renderer->renderPassInProgress)
	{
		VULKAN_INTERNAL_EncodeCommand(
			renderer,
			endPassCmd
		);
		renderer->renderPassInProgress = 0;

		/* This was locked in BeginRenderPass! */
		SDL_UnlockMutex(renderer->passLock);
	}
	SDL_UnlockMutex(renderer->passLock);
}

static void VULKAN_INTERNAL_BeginRenderPass(
	VulkanRenderer *renderer
) {
	VkRenderPassBeginInfo renderPassBeginInfo =
	{
		VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
	};
	VulkanCommand beginPassCmd =
	{
		CMDTYPE_BEGIN_RENDER_PASS
	};
	VkFramebuffer framebuffer;
	VkImageAspectFlags depthAspectFlags;
	float blendConstants[4];
	VulkanCommand setBlendConstantsCmd =
	{
		CMDTYPE_SET_BLEND_CONSTANTS
	};
	uint32_t i;

	if (!renderer->needNewRenderPass)
	{
		return;
	}

	VULKAN_INTERNAL_MaybeEndRenderPass(renderer);

	SDL_LockMutex(renderer->passLock);

	renderer->renderPass = VULKAN_INTERNAL_FetchRenderPass(renderer);
	framebuffer = VULKAN_INTERNAL_FetchFramebuffer(
		renderer,
		renderer->renderPass
	);

	renderer->needNewPipeline = 1;

	renderPassBeginInfo.renderArea.offset.x = 0;
	renderPassBeginInfo.renderArea.extent.width =
		renderer->colorAttachments[0]->dimensions.width;
	renderPassBeginInfo.renderArea.extent.height =
		renderer->colorAttachments[0]->dimensions.height;

	renderPassBeginInfo.renderPass = renderer->renderPass;
	renderPassBeginInfo.framebuffer = framebuffer;

	for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i += 1)
	{
		if (renderer->colorAttachments[i] != NULL)
		{
			VULKAN_INTERNAL_ImageMemoryBarrier(
				renderer,
				RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE,
				VK_IMAGE_ASPECT_COLOR_BIT,
				0,
				renderer->colorAttachments[i]->layerCount,
				0,
				1,
				0,
				renderer->colorAttachments[i]->image,
				&renderer->colorAttachments[i]->resourceAccessType
			);
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
			1,
			0,
			renderer->depthStencilAttachment->image,
			&renderer->depthStencilAttachment->resourceAccessType
		);
	}

	beginPassCmd.beginRenderPass.beginInfo = renderPassBeginInfo;
	VULKAN_INTERNAL_EncodeCommand(
		renderer,
		beginPassCmd
	);

	/* This is a long-term lock that lasts the whole render pass.
	 * It gets unlocked inside MaybeEndRenderPass!
	 */
	SDL_LockMutex(renderer->passLock);

	renderer->renderPassInProgress = 1;

	VULKAN_INTERNAL_SetViewportCommand(renderer);
	VULKAN_INTERNAL_SetScissorRectCommand(renderer);
	VULKAN_INTERNAL_SetStencilReferenceValueCommand(renderer);
	VULKAN_INTERNAL_SetDepthBiasCommand(renderer);

	blendConstants[0] = renderer->blendState.blendFactor.r / 255.0f;
	blendConstants[1] = renderer->blendState.blendFactor.g / 255.0f;
	blendConstants[2] = renderer->blendState.blendFactor.b / 255.0f;
	blendConstants[3] = renderer->blendState.blendFactor.a / 255.0f;

	SDL_memcpy(
		setBlendConstantsCmd.setBlendConstants.blendConstants,
		blendConstants,
		sizeof(blendConstants)
	);

	VULKAN_INTERNAL_EncodeCommand(
		renderer,
		setBlendConstantsCmd
	);

	/* Reset bindings for the current frame in flight */

	for (i = 0; i < MAX_TOTAL_SAMPLERS; i += 1)
	{
		if (renderer->textures[i] != &NullTexture)
		{
			renderer->textureNeedsUpdate[i] = 1;
		}
		if (renderer->samplers[i] != NULL_SAMPLER)
		{
			renderer->samplerNeedsUpdate[i] = 1;
		}
	}

	renderer->ldFragUniformBuffer = NULL_BUFFER;
	renderer->ldFragUniformOffset = 0;
	renderer->ldFragUniformSize = 0;
	renderer->ldVertUniformBuffer = NULL_BUFFER;
	renderer->ldVertUniformOffset = 0;
	renderer->ldVertUniformSize = 0;
	renderer->currentPipeline = NULL_PIPELINE;

	for (i = 0; i < MAX_BOUND_VERTEX_BUFFERS; i += 1)
	{
		renderer->ldVertexBuffers[i] = NULL_BUFFER;
		renderer->ldVertexBufferOffsets[i] = 0;
	}

	renderer->needNewRenderPass = 0;

	SDL_UnlockMutex(renderer->passLock);
}

static void VULKAN_INTERNAL_RenderPassClear(
	VulkanRenderer *renderer,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil,
	uint8_t clearColor,
	uint8_t clearDepth,
	uint8_t clearStencil
) {
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
	VulkanCommand clearAttachmentsCmd =
	{
		CMDTYPE_CLEAR_ATTACHMENTS
	};

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
			SDL_zero(clearAttachments[attachmentCount]);
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
		SDL_zero(clearAttachments[attachmentCount]);

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
		if (clearStencil)
		{
			clearAttachments[attachmentCount].aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			clearAttachments[attachmentCount].clearValue.depthStencil.stencil = stencil;
		}

		attachmentCount += 1;
	}

	clearAttachmentsCmd.clearAttachments.attachmentCount = attachmentCount;
	SDL_memcpy(
		clearAttachmentsCmd.clearAttachments.attachments,
		clearAttachments,
		sizeof(clearAttachments)
	);
	clearAttachmentsCmd.clearAttachments.rect = clearRect;
	VULKAN_INTERNAL_EncodeCommand(
		renderer,
		clearAttachmentsCmd
	);
}

static void VULKAN_INTERNAL_OutsideRenderPassClear(
	VulkanRenderer *renderer,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil,
	uint8_t clearColor,
	uint8_t clearDepth,
	uint8_t clearStencil
) {
	uint32_t i;
	VkClearColorValue clearValue =
	{{
		color->x,
		color->y,
		color->z,
		color->w
	}};
	VulkanCommand clearColorCmd =
	{
		CMDTYPE_CLEAR_COLOR_IMAGE
	};
	uint8_t shouldClearDepthStencil = (
		(clearDepth || clearStencil) &&
		renderer->depthStencilAttachment != NULL
	);
	VulkanCommand clearDepthStencilCmd =
	{
		CMDTYPE_CLEAR_DEPTH_STENCIL_IMAGE
	};
	VkImageAspectFlags depthAspectMask = 0;
	VkClearDepthStencilValue clearDepthStencilValue;
	VkImageSubresourceRange subresourceRange;

	SDL_LockMutex(renderer->passLock);

	if (clearColor)
	{
		for (i = 0; i < renderer->colorAttachmentCount; i += 1)
		{
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseArrayLayer = 0;
			subresourceRange.layerCount = renderer->colorAttachments[i]->layerCount;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = renderer->colorAttachments[i]->levelCount;

			VULKAN_INTERNAL_ImageMemoryBarrier(
				renderer,
				RESOURCE_ACCESS_TRANSFER_WRITE,
				VK_IMAGE_ASPECT_COLOR_BIT,
				0,
				renderer->colorAttachments[i]->layerCount,
				0,
				renderer->colorAttachments[i]->levelCount,
				0,
				renderer->colorAttachments[i]->image,
				&renderer->colorAttachments[i]->resourceAccessType
			);

			clearColorCmd.clearColorImage.image =
				renderer->colorAttachments[i]->image;
			clearColorCmd.clearColorImage.color = clearValue;
			clearColorCmd.clearColorImage.range = subresourceRange;
			VULKAN_INTERNAL_EncodeCommand(
				renderer,
				clearColorCmd
			);

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

			if (renderer->multiSampleCount > 0)
			{
				subresourceRange.layerCount =
					renderer->colorMultiSampleAttachments[i]->layerCount;
				subresourceRange.levelCount =
					renderer->colorMultiSampleAttachments[i]->levelCount;

				VULKAN_INTERNAL_ImageMemoryBarrier(
					renderer,
					RESOURCE_ACCESS_TRANSFER_WRITE,
					VK_IMAGE_ASPECT_COLOR_BIT,
					0,
					renderer->colorMultiSampleAttachments[i]->layerCount,
					0,
					renderer->colorMultiSampleAttachments[i]->levelCount,
					0,
					renderer->colorMultiSampleAttachments[i]->image,
					&renderer->colorMultiSampleAttachments[i]->resourceAccessType
				);

				clearColorCmd.clearColorImage.image =
					renderer->colorMultiSampleAttachments[i]->image;
				/* clearValue is already set */
				clearColorCmd.clearColorImage.range = subresourceRange;
				VULKAN_INTERNAL_EncodeCommand(
					renderer,
					clearColorCmd
				);

				VULKAN_INTERNAL_ImageMemoryBarrier(
					renderer,
					RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE,
					VK_IMAGE_ASPECT_COLOR_BIT,
					0,
					renderer->colorMultiSampleAttachments[i]->layerCount,
					0,
					renderer->colorMultiSampleAttachments[i]->levelCount,
					0,
					renderer->colorMultiSampleAttachments[i]->image,
					&renderer->colorMultiSampleAttachments[i]->resourceAccessType
				);
			}
		}
	}

	if (shouldClearDepthStencil)
	{
		if (clearDepth)
		{
			depthAspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
		}
		if (clearStencil)
		{
			depthAspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}

		clearDepthStencilValue.stencil = stencil;
		if (depth < 0.0f)
		{
			clearDepthStencilValue.depth = 0.0f;
		}
		else if (depth > 1.0f)
		{
			clearDepthStencilValue.depth = 1.0f;
		}
		else
		{
			clearDepthStencilValue.depth = depth;
		}

		subresourceRange.aspectMask = depthAspectMask;
		subresourceRange.baseArrayLayer = 0;
		subresourceRange.layerCount =
			renderer->depthStencilAttachment->layerCount;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount =
			renderer->depthStencilAttachment->levelCount;

		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			RESOURCE_ACCESS_TRANSFER_WRITE,
			depthAspectMask,
			0,
			renderer->depthStencilAttachment->layerCount,
			0,
			renderer->depthStencilAttachment->levelCount,
			0,
			renderer->depthStencilAttachment->image,
			&renderer->depthStencilAttachment->resourceAccessType
		);

		clearDepthStencilCmd.clearDepthStencilImage.image =
			renderer->depthStencilAttachment->image;
		clearDepthStencilCmd.clearDepthStencilImage.depthStencil =
			clearDepthStencilValue;
		clearDepthStencilCmd.clearDepthStencilImage.range =
			subresourceRange;
		VULKAN_INTERNAL_EncodeCommand(
			renderer,
			clearDepthStencilCmd
		);

		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_WRITE,
			depthAspectMask,
			0,
			renderer->depthStencilAttachment->layerCount,
			0,
			renderer->depthStencilAttachment->levelCount,
			0,
			renderer->depthStencilAttachment->image,
			&renderer->depthStencilAttachment->resourceAccessType
		);
	}

	SDL_UnlockMutex(renderer->passLock);
}

/* Vulkan: Descriptor Sets */

static void VULKAN_INTERNAL_IncrementSamplerDescriptorPool(
	VulkanRenderer *renderer,
	uint32_t additionalCount
) {
	VkDescriptorPoolSize samplerPoolSize;
	VkDescriptorPoolCreateInfo samplerPoolInfo =
	{
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
	};
	VkResult vulkanResult;

	renderer->activeSamplerDescriptorPoolIndex += 1;

	/* if we have used all the pools, allocate a new one */
	if (renderer->activeSamplerDescriptorPoolIndex >= renderer->samplerDescriptorPoolCapacity)
	{
		renderer->samplerDescriptorPoolCapacity += 1;

		renderer->samplerDescriptorPools = SDL_realloc(
			renderer->samplerDescriptorPools,
			sizeof(VkDescriptorPool) * renderer->samplerDescriptorPoolCapacity
		);

		samplerPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerPoolSize.descriptorCount = SAMPLER_DESCRIPTOR_POOL_SIZE;

		samplerPoolInfo.poolSizeCount = 1;
		samplerPoolInfo.pPoolSizes = &samplerPoolSize;
		samplerPoolInfo.maxSets = SAMPLER_DESCRIPTOR_POOL_SIZE;

		vulkanResult = renderer->vkCreateDescriptorPool(
			renderer->logicalDevice,
			&samplerPoolInfo,
			NULL,
			&renderer->samplerDescriptorPools[
				renderer->activeSamplerDescriptorPoolIndex
			]
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateDescriptorPool", vulkanResult);
			return;
		}
	}
}

static void VULKAN_INTERNAL_IncrementUniformBufferDescriptorPool(
	VulkanRenderer *renderer,
	uint32_t additionalCount
) {
	VkDescriptorPoolSize bufferPoolSize;
	VkDescriptorPoolCreateInfo bufferPoolInfo =
	{
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
	};
	VkResult vulkanResult;

	renderer->activeUniformBufferDescriptorPoolIndex += 1;

	/* if we have used all the pools, allocate a new one */
	if (renderer->activeUniformBufferDescriptorPoolIndex >= renderer->uniformBufferDescriptorPoolCapacity)
	{
		renderer->uniformBufferDescriptorPoolCapacity += 1;

		renderer->uniformBufferDescriptorPools = SDL_realloc(
			renderer->uniformBufferDescriptorPools,
			sizeof(VkDescriptorPool) * renderer->uniformBufferDescriptorPoolCapacity
		);

		bufferPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bufferPoolSize.descriptorCount = UNIFORM_BUFFER_DESCRIPTOR_POOL_SIZE;

		bufferPoolInfo.poolSizeCount = 1;
		bufferPoolInfo.pPoolSizes = &bufferPoolSize;
		bufferPoolInfo.maxSets = UNIFORM_BUFFER_DESCRIPTOR_POOL_SIZE;

		vulkanResult = renderer->vkCreateDescriptorPool(
			renderer->logicalDevice,
			&bufferPoolInfo,
			NULL,
			&renderer->uniformBufferDescriptorPools[
				renderer->activeUniformBufferDescriptorPoolIndex
			]
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateDescriptorPool", vulkanResult);
			return;
		}
	}
}

static void VULKAN_INTERNAL_BindResources(VulkanRenderer *renderer)
{
	uint8_t	vertexSamplerDescriptorSetNeedsUpdate,
		fragSamplerDescriptorSetNeedsUpdate;
	uint8_t	vertUniformBufferDescriptorSetNeedsUpdate,
		fragUniformBufferDescriptorSetNeedsUpdate;
	uint32_t vertArrayOffset, fragArrayOffset, i;
	VkWriteDescriptorSet writeDescriptorSets[MAX_TOTAL_SAMPLERS + 2];
	uint32_t writeDescriptorSetCount = 0;
	VkDescriptorSet samplerDescriptorSets[2];
	VkDescriptorSetLayout samplerLayoutsToUpdate[2];
	uint32_t samplerLayoutsToUpdateCount = 0;
	uint32_t vertSamplerIndex = -1;
	uint32_t fragSamplerIndex = -1;
	VkDescriptorSetAllocateInfo samplerAllocateInfo =
	{
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
	};
	VkWriteDescriptorSet vertSamplerWrite =
	{
		VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
	};
	VkWriteDescriptorSet fragSamplerWrite =
	{
		VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
	};
	VkWriteDescriptorSet vertUniformBufferWrite =
	{
		VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
	};
	VkBuffer vUniform, fUniform;
	VulkanSubBuffer *vSubbuf, *fSubbuf;
	unsigned long long vOff, fOff, vSize, fSize; /* MojoShader type... */
	VkDescriptorSetLayout uniformBufferLayouts[2];
	VkDescriptorSet uniformBufferDescriptorSets[2];
	uint32_t uniformBufferLayoutCount = 0;
	int32_t vertUniformBufferIndex = -1;
	int32_t fragUniformBufferIndex = -1;
	VkDescriptorSetAllocateInfo uniformBufferAllocateInfo =
	{
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
	};
	VkWriteDescriptorSet fragUniformBufferWrite =
	{
		VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
	};
	VkDescriptorImageInfo vertSamplerImageInfos[MAX_VERTEXTEXTURE_SAMPLERS];
	VkDescriptorImageInfo fragSamplerImageInfos[MAX_TEXTURE_SAMPLERS];
	VkDescriptorBufferInfo vertUniformBufferInfo;
	VkDescriptorBufferInfo fragUniformBufferInfo;
	uint32_t dynamicOffsets[2];
	uint32_t dynamicOffsetsCount = 0;
	VkDescriptorSet descriptorSetsToBind[4];
	uint32_t vertSamplerDescriptorsToAllocate = 0;
	uint32_t fragSamplerDescriptorsToAllocate = 0;
	uint32_t totalSamplerDescriptorsToAllocate = 0;
	VulkanCommand bindDescriptorSetsCmd =
	{
		CMDTYPE_BIND_DESCRIPTOR_SETS
	};
	VkResult vulkanResult;

	vertexSamplerDescriptorSetNeedsUpdate =
		(renderer->currentVertSamplerDescriptorSet == NULL_DESC_SET);
	fragSamplerDescriptorSetNeedsUpdate =
		(renderer->currentFragSamplerDescriptorSet == NULL_DESC_SET);
	vertUniformBufferDescriptorSetNeedsUpdate =
		(renderer->currentVertUniformBufferDescriptorSet == NULL_DESC_SET);
	fragUniformBufferDescriptorSetNeedsUpdate =
		(renderer->currentFragUniformBufferDescriptorSet == NULL_DESC_SET);

	vertArrayOffset = MAX_TEXTURE_SAMPLERS;
	fragArrayOffset = 0;

	for (i = 0; i < renderer->currentPipelineLayoutHash.vertSamplerCount; i += 1)
	{
#if 0 /* FIXME: vertSamplerImageInfos in Renderer! */
		if (	renderer->textureNeedsUpdate[vertArrayOffset + i] ||
			renderer->samplerNeedsUpdate[vertArrayOffset + i]	)
#endif
		{
			vertSamplerImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			if (renderer->textures[vertArrayOffset + i] != &NullTexture)
			{
				vertSamplerImageInfos[i].imageView = renderer->textures[vertArrayOffset + i]->view;
			}
			else
			{
				vertSamplerImageInfos[i].imageView = renderer->dummyVertTexture->view;
			}
			vertSamplerImageInfos[i].sampler = renderer->samplers[vertArrayOffset + i];

			vertexSamplerDescriptorSetNeedsUpdate = 1;
			vertSamplerDescriptorsToAllocate += 1;
			renderer->textureNeedsUpdate[vertArrayOffset + i] = 0;
			renderer->samplerNeedsUpdate[vertArrayOffset + i] = 0;
		}
	}

	/* use dummy data if sampler count is 0 */
	if (renderer->currentPipelineLayoutHash.vertSamplerCount == 0)
	{
#if 0 /* FIXME: vertSamplerImageInfos in Renderer! */
		if (	renderer->currentVertSamplerDescriptorSet == NULL ||
			renderer->textureNeedsUpdate[vertArrayOffset] ||
			renderer->samplerNeedsUpdate[vertArrayOffset]	)
#endif
		{
			vertSamplerImageInfos[0].imageView = renderer->dummyVertTexture->view;
			vertSamplerImageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			vertSamplerImageInfos[0].sampler = renderer->dummyVertSamplerState;

			vertexSamplerDescriptorSetNeedsUpdate = 1;
			fragSamplerDescriptorsToAllocate += 1;
			renderer->textureNeedsUpdate[vertArrayOffset] = 0;
			renderer->samplerNeedsUpdate[vertArrayOffset] = 0;
		}
	}

	for (i = 0; i < renderer->currentPipelineLayoutHash.fragSamplerCount; i += 1)
	{
#if 0 /* FIXME: fragSamplerImageInfos in Renderer! */
		if (	renderer->textureNeedsUpdate[fragArrayOffset + i] ||
			renderer->samplerNeedsUpdate[fragArrayOffset + i]	)
#endif
		{
			fragSamplerImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			if (renderer->textures[fragArrayOffset + i] != &NullTexture)
			{
				fragSamplerImageInfos[i].imageView = renderer->textures[fragArrayOffset + i]->view;
			}
			else
			{
				fragSamplerImageInfos[i].imageView = renderer->dummyFragTexture->view;
			}
			fragSamplerImageInfos[i].sampler = renderer->samplers[fragArrayOffset + i];

			fragSamplerDescriptorSetNeedsUpdate = 1;
			renderer->textureNeedsUpdate[fragArrayOffset + i] = 0;
			renderer->samplerNeedsUpdate[fragArrayOffset + i] = 0;
		}
	}

	/* use dummy data if sampler count is 0 */
	if (renderer->currentPipelineLayoutHash.fragSamplerCount == 0)
	{
#if 0 /* FIXME: fragSamplerImageInfos in Renderer! */
		if (	renderer->currentFragSamplerDescriptorSet == NULL ||
			renderer->textureNeedsUpdate[fragArrayOffset] ||
			renderer->samplerNeedsUpdate[fragArrayOffset]	)
#endif
		{
			fragSamplerImageInfos[0].imageView = renderer->dummyFragTexture->view;
			fragSamplerImageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			fragSamplerImageInfos[0].sampler = renderer->dummyFragSamplerState;

			fragSamplerDescriptorSetNeedsUpdate = 1;
			renderer->textureNeedsUpdate[fragArrayOffset] = 0;
			renderer->samplerNeedsUpdate[fragArrayOffset] = 0;
		}
	}

	if (vertexSamplerDescriptorSetNeedsUpdate)
	{
		samplerLayoutsToUpdate[samplerLayoutsToUpdateCount] = renderer->vertSamplerDescriptorSetLayouts[
			renderer->currentPipelineLayoutHash.vertSamplerCount
		];

		vertSamplerIndex = samplerLayoutsToUpdateCount;
		totalSamplerDescriptorsToAllocate += vertSamplerDescriptorsToAllocate;
		samplerLayoutsToUpdateCount += 1;
	}

	if (fragSamplerDescriptorSetNeedsUpdate)
	{
		samplerLayoutsToUpdate[samplerLayoutsToUpdateCount] = renderer->fragSamplerDescriptorSetLayouts[
			renderer->currentPipelineLayoutHash.fragSamplerCount
		];

		fragSamplerIndex = samplerLayoutsToUpdateCount;
		totalSamplerDescriptorsToAllocate += fragSamplerDescriptorsToAllocate;
		samplerLayoutsToUpdateCount += 1;
	}

	if (samplerLayoutsToUpdateCount > 0)
	{
		samplerAllocateInfo.descriptorPool = renderer->samplerDescriptorPools[
			renderer->activeSamplerDescriptorPoolIndex
		];
		samplerAllocateInfo.descriptorSetCount = samplerLayoutsToUpdateCount;
		samplerAllocateInfo.pSetLayouts = samplerLayoutsToUpdate;

		vulkanResult = renderer->vkAllocateDescriptorSets(
			renderer->logicalDevice,
			&samplerAllocateInfo,
			samplerDescriptorSets
		);

		if (	vulkanResult == VK_ERROR_OUT_OF_POOL_MEMORY ||
			vulkanResult == VK_ERROR_FRAGMENTED_POOL	)
		{
			VULKAN_INTERNAL_IncrementSamplerDescriptorPool(
				renderer,
				totalSamplerDescriptorsToAllocate
			);

			samplerAllocateInfo.descriptorPool = renderer->samplerDescriptorPools[
				renderer->activeSamplerDescriptorPoolIndex
			];

			vulkanResult = renderer->vkAllocateDescriptorSets(
				renderer->logicalDevice,
				&samplerAllocateInfo,
				samplerDescriptorSets
			);
		}

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkAllocateDescriptorSets", vulkanResult);
		}
	}

	if (vertSamplerIndex != -1)
	{
		if (renderer->currentPipelineLayoutHash.vertSamplerCount > 0)
		{
			for (i = 0; i < renderer->currentPipelineLayoutHash.vertSamplerCount; i += 1)
			{
				vertSamplerWrite.dstSet = samplerDescriptorSets[vertSamplerIndex];
				vertSamplerWrite.dstBinding = i;
				vertSamplerWrite.dstArrayElement = 0;
				vertSamplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				vertSamplerWrite.descriptorCount = 1;
				vertSamplerWrite.pBufferInfo = NULL;
				vertSamplerWrite.pImageInfo = &vertSamplerImageInfos[i];
				vertSamplerWrite.pTexelBufferView = NULL;

				writeDescriptorSets[writeDescriptorSetCount] = vertSamplerWrite;
				writeDescriptorSetCount += 1;
			}
		}
		else if (vertexSamplerDescriptorSetNeedsUpdate)
		{
			vertSamplerWrite.dstSet = samplerDescriptorSets[vertSamplerIndex];
			vertSamplerWrite.dstBinding = 0;
			vertSamplerWrite.dstArrayElement = 0;
			vertSamplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			vertSamplerWrite.descriptorCount = 1;
			vertSamplerWrite.pBufferInfo = NULL;
			vertSamplerWrite.pImageInfo = &vertSamplerImageInfos[0];
			vertSamplerWrite.pTexelBufferView = NULL;

			writeDescriptorSets[writeDescriptorSetCount] = vertSamplerWrite;
			writeDescriptorSetCount += 1;
		}
	}

	if (fragSamplerIndex != -1)
	{
		if (renderer->currentPipelineLayoutHash.fragSamplerCount > 0)
		{
			for (i = 0; i < renderer->currentPipelineLayoutHash.fragSamplerCount; i += 1)
			{
				fragSamplerWrite.dstSet = samplerDescriptorSets[fragSamplerIndex];
				fragSamplerWrite.dstBinding = i;
				fragSamplerWrite.dstArrayElement = 0;
				fragSamplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				fragSamplerWrite.descriptorCount = 1;
				fragSamplerWrite.pBufferInfo = NULL;
				fragSamplerWrite.pImageInfo = &fragSamplerImageInfos[i];
				fragSamplerWrite.pTexelBufferView = NULL;

				writeDescriptorSets[writeDescriptorSetCount] = fragSamplerWrite;
				writeDescriptorSetCount += 1;
			}
		}
		else if (fragSamplerDescriptorSetNeedsUpdate)
		{
			fragSamplerWrite.dstSet = samplerDescriptorSets[fragSamplerIndex];
			fragSamplerWrite.dstBinding = 0;
			fragSamplerWrite.dstArrayElement = 0;
			fragSamplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			fragSamplerWrite.descriptorCount = 1;
			fragSamplerWrite.pBufferInfo = NULL;
			fragSamplerWrite.pImageInfo = &fragSamplerImageInfos[0];
			fragSamplerWrite.pTexelBufferView = NULL;

			writeDescriptorSets[writeDescriptorSetCount] = fragSamplerWrite;
			writeDescriptorSetCount += 1;
		}
	}

	MOJOSHADER_vkGetUniformBuffers(
		&vUniform,
		&vOff,
		&vSize,
		&fUniform,
		&fOff,
		&fSize
	);

	/* we can't bind a NULL UBO so we have dummy data instead */
	if (vUniform == NULL_BUFFER)
	{
		vSubbuf = &renderer->dummyVertUniformBuffer->subBuffers[renderer->dummyVertUniformBuffer->currentSubBufferIndex];
		vUniform = vSubbuf->physicalBuffer->buffer;
		vOff = vSubbuf->offset;
		vSize = renderer->dummyVertUniformBuffer->size;
	}

	if (	renderer->currentVertUniformBufferDescriptorSet == NULL_DESC_SET ||
		vUniform != renderer->ldVertUniformBuffer ||
		vSize != renderer->ldVertUniformSize	)
	{
		vertUniformBufferInfo.buffer = vUniform;
		vertUniformBufferInfo.offset = 0;
		vertUniformBufferInfo.range = vSize;

		vertUniformBufferDescriptorSetNeedsUpdate = 1;
		renderer->ldVertUniformBuffer = vUniform;
		renderer->ldVertUniformOffset = vOff;
		renderer->ldVertUniformSize = vSize;
	}
	else if (vOff != renderer->ldVertUniformOffset)
	{
		renderer->ldVertUniformOffset = vOff;
	}

	/* we can't bind a NULL UBO so we have dummy data instead */
	if (fUniform == NULL_BUFFER)
	{
		fSubbuf = &renderer->dummyFragUniformBuffer->subBuffers[renderer->dummyFragUniformBuffer->currentSubBufferIndex];
		fUniform = fSubbuf->physicalBuffer->buffer;
		fOff = fSubbuf->offset;
		fSize = renderer->dummyFragUniformBuffer->size;
	}

	if (	renderer->currentFragUniformBufferDescriptorSet == NULL_DESC_SET ||
		fUniform != renderer->ldFragUniformBuffer ||
		fSize != renderer->ldFragUniformSize	)
	{
		fragUniformBufferInfo.buffer = fUniform;
		fragUniformBufferInfo.offset = 0;
		fragUniformBufferInfo.range = fSize;

		fragUniformBufferDescriptorSetNeedsUpdate = 1;
		renderer->ldFragUniformBuffer = fUniform;
		renderer->ldFragUniformOffset = fOff;
		renderer->ldFragUniformSize = fSize;
	}
	else if (fOff != renderer->ldFragUniformOffset)
	{
		renderer->ldFragUniformOffset = fOff;
	}

	if (vertUniformBufferDescriptorSetNeedsUpdate)
	{
		uniformBufferLayouts[uniformBufferLayoutCount] =
			renderer->vertUniformBufferDescriptorSetLayout;
		vertUniformBufferIndex = uniformBufferLayoutCount;
		uniformBufferLayoutCount += 1;
	}

	if (fragUniformBufferDescriptorSetNeedsUpdate)
	{
		uniformBufferLayouts[uniformBufferLayoutCount] =
			renderer->fragUniformBufferDescriptorSetLayout;
		fragUniformBufferIndex = uniformBufferLayoutCount;
		uniformBufferLayoutCount += 1;
	}

	if (uniformBufferLayoutCount > 0)
	{
		uniformBufferAllocateInfo.descriptorPool = renderer->uniformBufferDescriptorPools[
			renderer->activeUniformBufferDescriptorPoolIndex
		];
		uniformBufferAllocateInfo.descriptorSetCount = uniformBufferLayoutCount;
		uniformBufferAllocateInfo.pSetLayouts = uniformBufferLayouts;

		vulkanResult = renderer->vkAllocateDescriptorSets(
			renderer->logicalDevice,
			&uniformBufferAllocateInfo,
			uniformBufferDescriptorSets
		);

		if (	vulkanResult == VK_ERROR_OUT_OF_POOL_MEMORY ||
			vulkanResult == VK_ERROR_FRAGMENTED_POOL	)
		{
			VULKAN_INTERNAL_IncrementUniformBufferDescriptorPool(
				renderer,
				uniformBufferLayoutCount
			);

			uniformBufferAllocateInfo.descriptorPool = renderer->uniformBufferDescriptorPools[
				renderer->activeUniformBufferDescriptorPoolIndex
			];

			vulkanResult = renderer->vkAllocateDescriptorSets(
				renderer->logicalDevice,
				&uniformBufferAllocateInfo,
				uniformBufferDescriptorSets
			);
		}

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkAllocateDescriptorSets", vulkanResult);
		}
	}

	if (vertUniformBufferDescriptorSetNeedsUpdate)
	{
		vertUniformBufferWrite.dstSet = uniformBufferDescriptorSets[vertUniformBufferIndex];
		vertUniformBufferWrite.dstBinding = 0;
		vertUniformBufferWrite.dstArrayElement = 0;
		vertUniformBufferWrite.descriptorCount = 1;
		vertUniformBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		vertUniformBufferWrite.pBufferInfo = &vertUniformBufferInfo;
		vertUniformBufferWrite.pImageInfo = NULL;
		vertUniformBufferWrite.pTexelBufferView = NULL;

		writeDescriptorSets[writeDescriptorSetCount] = vertUniformBufferWrite;
		writeDescriptorSetCount += 1;
	}

	if (fragUniformBufferDescriptorSetNeedsUpdate)
	{
		fragUniformBufferWrite.dstSet = uniformBufferDescriptorSets[fragUniformBufferIndex];
		fragUniformBufferWrite.dstBinding = 0;
		fragUniformBufferWrite.dstArrayElement = 0;
		fragUniformBufferWrite.descriptorCount = 1;
		fragUniformBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		fragUniformBufferWrite.pBufferInfo = &fragUniformBufferInfo;
		fragUniformBufferWrite.pImageInfo = NULL;
		fragUniformBufferWrite.pTexelBufferView = NULL;

		writeDescriptorSets[writeDescriptorSetCount] = fragUniformBufferWrite;
		writeDescriptorSetCount += 1;
	}

	/* vert samplers */
	if (vertSamplerIndex != -1)
	{
		renderer->currentVertSamplerDescriptorSet = samplerDescriptorSets[vertSamplerIndex];
	}

	/* frag samplers */
	if (fragSamplerIndex != -1)
	{
		renderer->currentFragSamplerDescriptorSet = samplerDescriptorSets[fragSamplerIndex];
	}

	/* vert ubo */
	if (vertUniformBufferIndex != -1)
	{
		renderer->currentVertUniformBufferDescriptorSet =
			uniformBufferDescriptorSets[vertUniformBufferIndex];
	}

	dynamicOffsets[dynamicOffsetsCount] =
		(uint32_t) renderer->ldVertUniformOffset;
	dynamicOffsetsCount += 1;

	/* frag ubo */
	if (fragUniformBufferIndex != -1)
	{
		renderer->currentFragUniformBufferDescriptorSet =
			uniformBufferDescriptorSets[fragUniformBufferIndex];
	}

	dynamicOffsets[dynamicOffsetsCount] =
		(uint32_t) renderer->ldFragUniformOffset;
	dynamicOffsetsCount += 1;

	renderer->vkUpdateDescriptorSets(
		renderer->logicalDevice,
		writeDescriptorSetCount,
		writeDescriptorSets,
		0,
		NULL
	);

	descriptorSetsToBind[0] = renderer->currentVertSamplerDescriptorSet;
	descriptorSetsToBind[1] = renderer->currentFragSamplerDescriptorSet;
	descriptorSetsToBind[2] = renderer->currentVertUniformBufferDescriptorSet;
	descriptorSetsToBind[3] = renderer->currentFragUniformBufferDescriptorSet;

	bindDescriptorSetsCmd.bindDescriptorSets.layout = renderer->currentPipelineLayout;
	SDL_memcpy(
		bindDescriptorSetsCmd.bindDescriptorSets.descriptorSets,
		descriptorSetsToBind,
		sizeof(descriptorSetsToBind)
	);
	bindDescriptorSetsCmd.bindDescriptorSets.dynamicOffsetCount = dynamicOffsetsCount;
	SDL_memcpy(
		bindDescriptorSetsCmd.bindDescriptorSets.dynamicOffsets,
		dynamicOffsets,
		sizeof(dynamicOffsets)
	);
	VULKAN_INTERNAL_EncodeCommand(
		renderer,
		bindDescriptorSetsCmd
	);
}

/* Vulkan: Sampler State */

static VkSampler VULKAN_INTERNAL_FetchSamplerState(
	VulkanRenderer *renderer,
	FNA3D_SamplerState *samplerState,
	uint32_t levelCount
) {
	VkSamplerCreateInfo createInfo =
	{
		VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO
	};
	VkSampler state;
	VkResult result;

	StateHash hash = GetSamplerStateHash(*samplerState);
	if (hmgeti(renderer->samplerStateHashMap, hash) != -1)
	{
		return hmget(renderer->samplerStateHashMap, hash);
	}

	createInfo.addressModeU = XNAToVK_SamplerAddressMode[
		samplerState->addressU
	];
	createInfo.addressModeV = XNAToVK_SamplerAddressMode[
		samplerState->addressV
	];
	createInfo.addressModeW = XNAToVK_SamplerAddressMode[
		samplerState->addressW
	];
	createInfo.magFilter = XNAToVK_MagFilter[samplerState->filter];
	createInfo.minFilter = XNAToVK_MinFilter[samplerState->filter];
	if (levelCount > 1)
	{
		createInfo.mipmapMode = XNAToVK_MipFilter[samplerState->filter];
	}
	createInfo.mipLodBias = samplerState->mipMapLevelOfDetailBias;
	createInfo.minLod = (float) samplerState->maxMipLevel;
	createInfo.maxLod = VK_LOD_CLAMP_NONE;
	createInfo.maxAnisotropy = (samplerState->filter == FNA3D_TEXTUREFILTER_ANISOTROPIC) ?
		(float) SDL_max(1, samplerState->maxAnisotropy) :
		1.0f;

	result = renderer->vkCreateSampler(
		renderer->logicalDevice,
		&createInfo,
		NULL,
		&state
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateSampler", result);
		return 0;
	}

	hmput(renderer->samplerStateHashMap, hash, state);

	return state;
}

/* Renderer Implementation */

/* Quit */

static void VULKAN_DestroyDevice(FNA3D_Device *device)
{
	VulkanRenderer *renderer = (VulkanRenderer*) device->driverData;
	uint32_t i;
	VkResult waitResult;

	VULKAN_INTERNAL_Flush(renderer);

	waitResult = renderer->vkDeviceWaitIdle(renderer->logicalDevice);

	if (waitResult != VK_SUCCESS)
	{
		LogVulkanResult("vkDeviceWaitIdle", waitResult);
	}

	VULKAN_INTERNAL_DestroyBuffer(renderer, renderer->dummyVertUniformBuffer);
	VULKAN_INTERNAL_DestroyBuffer(renderer, renderer->dummyFragUniformBuffer);

	VULKAN_INTERNAL_DestroyTexture(renderer, renderer->dummyVertTexture);
	VULKAN_INTERNAL_DestroyTexture(renderer, renderer->dummyFragTexture);

	VULKAN_INTERNAL_PerformDeferredDestroys(renderer);

	VULKAN_INTERNAL_DestroyTextureStagingBuffer(renderer);

	MOJOSHADER_vkDestroyContext(renderer->mojoshaderContext);
	VULKAN_INTERNAL_DestroyFauxBackbuffer(renderer);

	renderer->vkDestroySemaphore(
		renderer->logicalDevice,
		renderer->imageAvailableSemaphore,
		NULL
	);

	renderer->vkDestroySemaphore(
		renderer->logicalDevice,
		renderer->renderFinishedSemaphore,
		NULL
	);

	renderer->vkDestroyFence(
		renderer->logicalDevice,
		renderer->inFlightFence,
		NULL
	);

	renderer->vkDestroyQueryPool(
		renderer->logicalDevice,
		renderer->queryPool,
		NULL
	);

	renderer->vkDestroyCommandPool(
		renderer->logicalDevice,
		renderer->commandPool,
		NULL
	);

	SDL_free(renderer->commands);

	for (i = 0; i < hmlenu(renderer->pipelineHashMap); i += 1)
	{
		renderer->vkDestroyPipeline(
			renderer->logicalDevice,
			renderer->pipelineHashMap[i].value,
			NULL
		);
	}

	for (i = 0; i < MAX_VERTEXTEXTURE_SAMPLERS; i += 1)
	{
		renderer->vkDestroyDescriptorSetLayout(
			renderer->logicalDevice,
			renderer->vertSamplerDescriptorSetLayouts[i],
			NULL
		);
	}

	for (i = 0; i < MAX_TEXTURE_SAMPLERS; i += 1)
	{
		renderer->vkDestroyDescriptorSetLayout(
			renderer->logicalDevice,
			renderer->fragSamplerDescriptorSetLayouts[i],
			NULL
		);
	}

	renderer->vkDestroyDescriptorSetLayout(
		renderer->logicalDevice,
		renderer->vertUniformBufferDescriptorSetLayout,
		NULL
	);

	renderer->vkDestroyDescriptorSetLayout(
		renderer->logicalDevice,
		renderer->fragUniformBufferDescriptorSetLayout,
		NULL
	);

	for (i = 0; i < hmlenu(renderer->pipelineLayoutHashMap); i += 1)
	{
		renderer->vkDestroyPipelineLayout(
			renderer->logicalDevice,
			renderer->pipelineLayoutHashMap[i].value,
			NULL
		);
	}

	renderer->vkDestroyPipelineCache(
		renderer->logicalDevice,
		renderer->pipelineCache,
		NULL
	);

	for (i = 0; i < renderer->uniformBufferDescriptorPoolCapacity; i += 1)
	{
		renderer->vkDestroyDescriptorPool(
			renderer->logicalDevice,
			renderer->uniformBufferDescriptorPools[i],
			NULL
		);
	}

	for (i = 0; i < renderer->samplerDescriptorPoolCapacity; i += 1)
	{
		renderer->vkDestroyDescriptorPool(
			renderer->logicalDevice,
			renderer->samplerDescriptorPools[i],
			NULL
		);
	}

	for (i = 0; i < hmlenu(renderer->renderPassHashMap); i += 1)
	{
		renderer->vkDestroyRenderPass(
			renderer->logicalDevice,
			renderer->renderPassHashMap[i].value,
			NULL
		);
	}

	for (i = 0; i < hmlenu(renderer->samplerStateHashMap); i += 1)
	{
		renderer->vkDestroySampler(
			renderer->logicalDevice,
			renderer->samplerStateHashMap[i].value,
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
		renderer->dummyFragSamplerState,
		NULL
	);

	VULKAN_INTERNAL_DestroySwapchain(renderer);

	renderer->vkDestroySurfaceKHR(
		renderer->instance,
		renderer->surface,
		NULL
	);

	for (i = 0; i < PHYSICAL_BUFFER_MAX_COUNT; i += 1)
	{
		if (renderer->bufferAllocator->physicalBuffers[i] != NULL)
		{
			renderer->vkDestroyBuffer(
				renderer->logicalDevice,
				renderer->bufferAllocator->physicalBuffers[i]->buffer,
				NULL
			);
			renderer->vkFreeMemory(
				renderer->logicalDevice,
				renderer->bufferAllocator->physicalBuffers[i]->deviceMemory,
				NULL
			);
		}
	}

	SDL_free(renderer->bufferAllocator);
	SDL_free(renderer->buffersInUse);

	renderer->vkDestroyDevice(renderer->logicalDevice, NULL);
	renderer->vkDestroyInstance(renderer->instance, NULL);

	hmfree(renderer->pipelineLayoutHashMap);
	hmfree(renderer->pipelineHashMap);
	hmfree(renderer->renderPassHashMap);
	hmfree(renderer->framebufferHashMap);
	hmfree(renderer->samplerStateHashMap);

	SDL_free(renderer->imagesInFlight);

	SDL_free(renderer->renderbuffersToDestroy);
	SDL_free(renderer->buffersToDestroy);
	SDL_free(renderer->effectsToDestroy);
	SDL_free(renderer->texturesToDestroy);

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
	VkResult result;
	VkImageBlit blit;
	FNA3D_Rect srcRect;
	FNA3D_Rect dstRect;
	VulkanCommand blitCmd =
	{
		CMDTYPE_BLIT_IMAGE
	};
	uint32_t swapChainImageIndex, i;

	/* Begin next frame */
	result = renderer->vkAcquireNextImageKHR(
		renderer->logicalDevice,
		renderer->swapChain,
		UINT64_MAX,
		renderer->imageAvailableSemaphore,
		VK_NULL_HANDLE,
		&swapChainImageIndex
	);

	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		VULKAN_INTERNAL_RecreateSwapchain(renderer);
		return;
	}

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkAcquireNextImageKHR", result);
		FNA3D_LogError("failed to acquire swapchain image");
	}

	if (renderer->imagesInFlight[swapChainImageIndex] != VK_NULL_HANDLE)
	{
		renderer->vkWaitForFences(
			renderer->logicalDevice,
			1,
			&renderer->imagesInFlight[swapChainImageIndex],
			VK_TRUE,
			UINT64_MAX
		);
	}

	renderer->imagesInFlight[swapChainImageIndex] = renderer->inFlightFence;

	/* Must end render pass before blitting */
	VULKAN_INTERNAL_MaybeEndRenderPass(renderer);

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
		dstRect.w = renderer->swapChainExtent.width;
		dstRect.h = renderer->swapChainExtent.height;
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
		renderer->swapChainImages[swapChainImageIndex],
		&renderer->swapChainResourceAccessTypes[swapChainImageIndex]
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

	blitCmd.blitImage.srcImage = renderer->fauxBackbufferColor.handle->image;
	blitCmd.blitImage.dstImage = renderer->swapChainImages[swapChainImageIndex];
	blitCmd.blitImage.region = blit;
	blitCmd.blitImage.filter = VK_FILTER_LINEAR; /* FIXME: Where is the final blit filter defined? -cosmonaut */
	VULKAN_INTERNAL_EncodeCommand(
		renderer,
		blitCmd
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_PRESENT,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		1,
		0,
		1,
		0,
		renderer->swapChainImages[swapChainImageIndex],
		&renderer->swapChainResourceAccessTypes[swapChainImageIndex]
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

	/* Record and submit the commands, then present! */

	VULKAN_INTERNAL_FlushAndPresent(
		renderer,
		overrideWindowHandle,
		swapChainImageIndex
	);

	/* Cleanup */

	VULKAN_INTERNAL_PerformDeferredDestroys(renderer);

	for (i = 0; i < renderer->samplerDescriptorPoolCapacity; i += 1)
	{
		renderer->vkResetDescriptorPool(
			renderer->logicalDevice,
			renderer->samplerDescriptorPools[i],
			0
		);
	}

	renderer->activeSamplerDescriptorPoolIndex = 0;

	for (i = 0; i < renderer->uniformBufferDescriptorPoolCapacity; i += 1)
	{
		renderer->vkResetDescriptorPool(
			renderer->logicalDevice,
			renderer->uniformBufferDescriptorPools[i],
			0
		);
	}

	renderer->activeUniformBufferDescriptorPoolIndex = 0;

	renderer->currentVertSamplerDescriptorSet = NULL_DESC_SET;
	renderer->currentFragSamplerDescriptorSet = NULL_DESC_SET;
	renderer->currentVertUniformBufferDescriptorSet = NULL_DESC_SET;
	renderer->currentFragUniformBufferDescriptorSet = NULL_DESC_SET;

	MOJOSHADER_vkEndFrame();
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

	if (renderer->renderPassInProgress)
	{
		/* May need a new render pass! */
		VULKAN_INTERNAL_BeginRenderPass(renderer);

		VULKAN_INTERNAL_RenderPassClear(
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
		VULKAN_INTERNAL_OutsideRenderPassClear(
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
	VulkanBuffer *indexBuffer = (VulkanBuffer*) indices;
	VulkanSubBuffer subbuf = indexBuffer->subBuffers[
		indexBuffer->currentSubBufferIndex
	];
	VulkanCommand bindBuffersCmd =
	{
		CMDTYPE_BIND_VERTEX_BUFFERS
	};
	VulkanCommand bindIndexCmd =
	{
		CMDTYPE_BIND_INDEX_BUFFER
	};
	VulkanCommand drawIndexedCmd =
	{
		CMDTYPE_DRAW_INDEXED
	};

	/* Note that minVertexIndex/numVertices are NOT used! */

	VULKAN_INTERNAL_MarkAsBound(renderer, indexBuffer);

	if (primitiveType != renderer->currentPrimitiveType)
	{
		renderer->currentPrimitiveType = primitiveType;
		renderer->needNewRenderPass = 1;
	}
	VULKAN_INTERNAL_BeginRenderPass(renderer);
	VULKAN_INTERNAL_BindPipeline(renderer);
	VULKAN_INTERNAL_BindResources(renderer);

	if (renderer->bufferCount > 0)
	{
		bindBuffersCmd.bindVertexBuffers.bindingCount =
			renderer->bufferCount;
		SDL_memcpy(
			bindBuffersCmd.bindVertexBuffers.buffers,
			renderer->buffers,
			sizeof(renderer->buffers)
		);
		SDL_memcpy(
			bindBuffersCmd.bindVertexBuffers.offsets,
			renderer->offsets,
			sizeof(renderer->offsets)
		);
		VULKAN_INTERNAL_EncodeCommand(
			renderer,
			bindBuffersCmd
		);
	}
	bindIndexCmd.bindIndexBuffer.buffer = subbuf.physicalBuffer->buffer;
	bindIndexCmd.bindIndexBuffer.offset = subbuf.offset;
	bindIndexCmd.bindIndexBuffer.indexType =
		XNAToVK_IndexType[indexElementSize];
	VULKAN_INTERNAL_EncodeCommand(
		renderer,
		bindIndexCmd
	);

	drawIndexedCmd.drawIndexed.indexCount = PrimitiveVerts(
		primitiveType,
		primitiveCount
	);
	drawIndexedCmd.drawIndexed.instanceCount = instanceCount;
	drawIndexedCmd.drawIndexed.firstIndex = startIndex;
	drawIndexedCmd.drawIndexed.vertexOffset = baseVertex;
	VULKAN_INTERNAL_EncodeCommand(
		renderer,
		drawIndexedCmd
	);
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
	VulkanCommand bindBuffersCmd =
	{
		CMDTYPE_BIND_VERTEX_BUFFERS
	};
	VulkanCommand drawCmd =
	{
		CMDTYPE_DRAW
	};

	if (primitiveType != renderer->currentPrimitiveType)
	{
		renderer->currentPrimitiveType = primitiveType;
		renderer->needNewRenderPass = 1;
	}
	VULKAN_INTERNAL_BeginRenderPass(renderer);
	VULKAN_INTERNAL_BindPipeline(renderer);
	VULKAN_INTERNAL_BindResources(renderer);

	if (renderer->bufferCount > 0)
	{
		bindBuffersCmd.bindVertexBuffers.bindingCount =
			renderer->bufferCount;
		SDL_memcpy(
			bindBuffersCmd.bindVertexBuffers.buffers,
			renderer->buffers,
			sizeof(renderer->buffers)
		);
		SDL_memcpy(
			bindBuffersCmd.bindVertexBuffers.offsets,
			renderer->offsets,
			sizeof(renderer->offsets)
		);
		VULKAN_INTERNAL_EncodeCommand(
			renderer,
			bindBuffersCmd
		);
	}
	drawCmd.draw.vertexCount = PrimitiveVerts(
		primitiveType,
		primitiveCount
	);
	drawCmd.draw.firstVertex = vertexStart;
	VULKAN_INTERNAL_EncodeCommand(
		renderer,
		drawCmd
	);
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
	const float blendConstants[] =
	{
		blendFactor->r,
		blendFactor->g,
		blendFactor->b,
		blendFactor->a
	};
	VulkanCommand setBlendConstantsCmd =
	{
		CMDTYPE_SET_BLEND_CONSTANTS
	};

	if (	blendFactor->r != renderer->blendState.blendFactor.r ||
		blendFactor->g != renderer->blendState.blendFactor.g ||
		blendFactor->b != renderer->blendState.blendFactor.b ||
		blendFactor->a != renderer->blendState.blendFactor.a	)
	{
		renderer->blendState.blendFactor = *blendFactor;
		renderer->needNewPipeline = 1;

		SDL_memcpy(
			setBlendConstantsCmd.setBlendConstants.blendConstants,
			blendConstants,
			sizeof(blendConstants)
		);
		VULKAN_INTERNAL_EncodeCommand(
			renderer,
			setBlendConstantsCmd
		);
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

	if (SDL_memcmp(&renderer->depthStencilState, depthStencilState, sizeof(FNA3D_DepthStencilState)) != 0)
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

		if (renderer->samplers[index] == NULL_SAMPLER)
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

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_FRAGMENT_SHADER_READ_SAMPLED_IMAGE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		vulkanTexture->layerCount,
		0,
		vulkanTexture->levelCount,
		0,
		vulkanTexture->image,
		&vulkanTexture->resourceAccessType
	);

	if (vulkanTexture != renderer->textures[index])
	{
		renderer->textures[index] = vulkanTexture;
		renderer->textureNeedsUpdate[index] = 1;
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
	VulkanBuffer *vertexBuffer;
	VulkanSubBuffer subbuf;
	VkDeviceSize offset;
	int32_t i;
	MOJOSHADER_vkShader *vertexShader, *blah;
	uint64_t hash;

	/* Check VertexBufferBindings */
	MOJOSHADER_vkGetBoundShaders(&vertexShader, &blah);
	hash = GetVertexBufferBindingsHash(
		bindings,
		numBindings,
		vertexShader
	);
	renderer->vertexBindings = bindings;
	renderer->numVertexBindings = numBindings;

	if (hash != renderer->currentVertexBufferBindingHash)
	{
		renderer->currentVertexBufferBindingHash = hash;
		renderer->needNewPipeline = 1;
	}

	renderer->bufferCount = 0;
	for (i = 0; i < numBindings; i += 1)
	{
		vertexBuffer = (VulkanBuffer*) bindings[i].vertexBuffer;
		if (vertexBuffer == NULL)
		{
			continue;
		}

		subbuf = vertexBuffer->subBuffers[
			vertexBuffer->currentSubBufferIndex
		];

		offset = subbuf.offset + (
			bindings[i].vertexOffset *
			bindings[i].vertexDeclaration.vertexStride
		);

		VULKAN_INTERNAL_MarkAsBound(renderer, vertexBuffer);
		if (	renderer->ldVertexBuffers[i] != subbuf.physicalBuffer->buffer ||
			renderer->ldVertexBufferOffsets[i] != offset	)
		{
			renderer->ldVertexBuffers[i] = subbuf.physicalBuffer->buffer;
			renderer->ldVertexBufferOffsets[i] = offset;
		}

		renderer->buffers[renderer->bufferCount] = subbuf.physicalBuffer->buffer;
		renderer->offsets[renderer->bufferCount] = offset;
		renderer->bufferCount++;
	}
}

/* Render Targets */

static void VULKAN_SetRenderTargets(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *depthStencilBuffer,
	FNA3D_DepthFormat depthFormat,
	uint8_t preserveDepthStencilContents
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanColorBuffer *cb;
	VulkanTexture *tex;
	int32_t i;

	for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i += 1)
	{
		renderer->colorAttachments[i] = NULL;
		renderer->colorMultiSampleAttachments[i] = NULL;
	}
	renderer->depthStencilAttachment = NULL;
	renderer->multiSampleCount = renderer->fauxBackbufferMultiSampleCount;

	if (renderTargets == NULL)
	{
		renderer->colorAttachments[0] = renderer->fauxBackbufferColor.handle;
		renderer->attachmentCubeFaces[0] = (FNA3D_CubeMapFace) 0;
		renderer->colorAttachmentCount = 1;

		if (renderer->fauxBackbufferMultiSampleCount > 1)
		{
			renderer->colorMultiSampleAttachments[0] =
				renderer->fauxBackbufferMultiSampleColor;
		}
		renderer->depthStencilAttachment =
			renderer->fauxBackbufferDepthStencil.handle;

		renderer->renderTargetBound = 0;
	}
	else
	{
		for (i = 0; i < numRenderTargets; i += 1)
		{
			renderer->attachmentCubeFaces[i] = (
				renderTargets[i].type == FNA3D_RENDERTARGET_TYPE_CUBE ?
					renderTargets[i].cube.face :
					(FNA3D_CubeMapFace) 0
			);

			if (renderTargets[i].colorBuffer != NULL)
			{
				cb = ((VulkanRenderbuffer*) renderTargets[i].colorBuffer)->colorBuffer;
				renderer->colorAttachments[i] = cb->handle;
				renderer->multiSampleCount = cb->multiSampleCount;

				if (cb->multiSampleCount > 0)
				{
					renderer->colorMultiSampleAttachments[i] = cb->multiSampleTexture;
				}
			}
			else
			{
				tex = (VulkanTexture*) renderTargets[i].texture;
				renderer->colorAttachments[i] = tex;
				renderer->multiSampleCount = 0;
			}
		}

		renderer->colorAttachmentCount = numRenderTargets;

		/* update depth stencil buffer */

		if (depthStencilBuffer != NULL)
		{
			renderer->depthStencilAttachment = ((VulkanRenderbuffer*) depthStencilBuffer)->depthBuffer->handle;
			renderer->currentDepthFormat = depthFormat;
		}
		else
		{
			renderer->depthStencilAttachment = NULL;
		}

		renderer->renderTargetBound = 1;
	}

	renderer->needNewRenderPass = 1;
}

static void VULKAN_ResolveTarget(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *target
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *vulkanTexture = (VulkanTexture*) target->texture;
	int32_t layerCount = (target->type == FNA3D_RENDERTARGET_TYPE_CUBE) ? 6 : 1;
	int32_t level;
	VulkanResourceAccessType origAccessType;
	VkImageBlit blit;
	VulkanCommand blitCmd =
	{
		CMDTYPE_BLIT_IMAGE
	};

	/* The target is resolved during the render pass. */

	/* If the target has mipmaps, regenerate them now */
	if (target->levelCount > 1)
	{
		VULKAN_INTERNAL_MaybeEndRenderPass(renderer);

		origAccessType = vulkanTexture->resourceAccessType;

		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			RESOURCE_ACCESS_TRANSFER_READ,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			layerCount,
			0,
			1,
			0,
			vulkanTexture->image,
			&vulkanTexture->resourceAccessType
		);

		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			RESOURCE_ACCESS_TRANSFER_WRITE,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			layerCount,
			1,
			target->levelCount - 1,
			1,
			vulkanTexture->image,
			&vulkanTexture->resourceAccessType
		);

		for (level = 1; level < target->levelCount; level += 1)
		{
			blit.srcOffsets[0].x = 0;
			blit.srcOffsets[0].y = 0;
			blit.srcOffsets[0].z = 0;

			blit.srcOffsets[1].x = vulkanTexture->dimensions.width;
			blit.srcOffsets[1].y = vulkanTexture->dimensions.height;
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
			blit.srcSubresource.mipLevel = 0;

			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = layerCount;
			blit.dstSubresource.mipLevel = level;

			blitCmd.blitImage.srcImage = vulkanTexture->image;
			blitCmd.blitImage.dstImage = vulkanTexture->image;
			blitCmd.blitImage.region = blit;
			blitCmd.blitImage.filter = VK_FILTER_LINEAR;
			VULKAN_INTERNAL_EncodeCommand(
				renderer,
				blitCmd
			);
		}

		/* Transition level >= 1 back to the original access type */
		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			origAccessType,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			layerCount,
			1,
			target->levelCount - 1,
			0,
			vulkanTexture->image,
			&vulkanTexture->resourceAccessType
		);

		/* The 0th mip requires a little access type switcheroo... */
		vulkanTexture->resourceAccessType = RESOURCE_ACCESS_TRANSFER_READ;
		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			origAccessType,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			layerCount,
			0,
			1,
			0,
			vulkanTexture->image,
			&vulkanTexture->resourceAccessType
		);
	}
}

/* Backbuffer Functions */

static void VULKAN_ResetBackbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_PresentationParameters *presentationParameters
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	renderer->presentInterval = presentationParameters->presentationInterval;
	renderer->deviceWindowHandle = presentationParameters->deviceWindowHandle;

	VULKAN_INTERNAL_RecreateSwapchain(renderer);
	VULKAN_INTERNAL_DestroyFauxBackbuffer(renderer);
	VULKAN_INTERNAL_CreateFauxBackbuffer(
		renderer,
		presentationParameters
	);
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

	VULKAN_GetTextureData2D(
		driverData,
		(FNA3D_Texture*) renderer->fauxBackbufferColor.handle,
		x,
		y,
		w,
		h,
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
	SDL_memset(result, '\0', sizeof(VulkanTexture));

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
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_TYPE_2D,
		usageFlags,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
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
	SDL_memset(result, '\0', sizeof(VulkanTexture));

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
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_TYPE_3D,
		usageFlags,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
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
	SDL_memset(result, '\0', sizeof(VulkanTexture));

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
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_TYPE_2D,
		usageFlags,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
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
	if (renderer->texturesToDestroyCount + 1 >= renderer->texturesToDestroyCapacity)
	{
		renderer->texturesToDestroyCapacity *= 2;

		renderer->texturesToDestroy = SDL_realloc(
			renderer->texturesToDestroy,
			sizeof(VulkanTexture*) * renderer->texturesToDestroyCapacity
		);
	}
	renderer->texturesToDestroy[renderer->texturesToDestroyCount] = vulkanTexture;
	renderer->texturesToDestroyCount += 1;
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
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *vulkanTexture = (VulkanTexture*) texture;
	VkBufferImageCopy imageCopy;
	VulkanCommand copyCmd =
	{
		CMDTYPE_COPY_BUFFER_TO_IMAGE
	};

	SDL_LockMutex(renderer->passLock);

	VULKAN_INTERNAL_MaybeExpandStagingBuffer(renderer, dataLength);

	SDL_memcpy(renderer->textureStagingBuffer->mapPointer, data, dataLength);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		vulkanTexture->layerCount,
		0,
		vulkanTexture->levelCount,
		0,
		vulkanTexture->image,
		&vulkanTexture->resourceAccessType
	);

	imageCopy.imageExtent.width = w;
	imageCopy.imageExtent.height = h;
	imageCopy.imageExtent.depth = 1;
	imageCopy.imageOffset.x = x;
	imageCopy.imageOffset.y = y;
	imageCopy.imageOffset.z = 0;
	imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageCopy.imageSubresource.baseArrayLayer = 0;
	imageCopy.imageSubresource.layerCount = 1;
	imageCopy.imageSubresource.mipLevel = level;
	imageCopy.bufferOffset = 0;
	imageCopy.bufferRowLength = 0;
	imageCopy.bufferImageHeight = 0;

	copyCmd.copyBufferToImage.srcBuffer = renderer->textureStagingBuffer->buffer;
	copyCmd.copyBufferToImage.dstImage = vulkanTexture->image;
	copyCmd.copyBufferToImage.dstImageLayout = AccessMap[vulkanTexture->resourceAccessType].imageLayout;
	copyCmd.copyBufferToImage.region = imageCopy;
	VULKAN_INTERNAL_EncodeCommand(
		renderer,
		copyCmd
	);

	VULKAN_INTERNAL_Flush(renderer);

	SDL_UnlockMutex(renderer->passLock);
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
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *vulkanTexture = (VulkanTexture*) texture;
	VkBufferImageCopy imageCopy;
	VulkanCommand copyCmd =
	{
		CMDTYPE_COPY_BUFFER_TO_IMAGE
	};

	SDL_LockMutex(renderer->passLock);

	VULKAN_INTERNAL_MaybeExpandStagingBuffer(renderer, dataLength);

	SDL_memcpy(renderer->textureStagingBuffer->mapPointer, data, dataLength);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		vulkanTexture->layerCount,
		0,
		vulkanTexture->levelCount,
		0,
		vulkanTexture->image,
		&vulkanTexture->resourceAccessType
	);

	imageCopy.imageExtent.width = w;
	imageCopy.imageExtent.height = h;
	imageCopy.imageExtent.depth = d;
	imageCopy.imageOffset.x = x;
	imageCopy.imageOffset.y = y;
	imageCopy.imageOffset.z = z;
	imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageCopy.imageSubresource.baseArrayLayer = 0;
	imageCopy.imageSubresource.layerCount = 1;
	imageCopy.imageSubresource.mipLevel = level;
	imageCopy.bufferOffset = 0;
	imageCopy.bufferRowLength = 0;
	imageCopy.bufferImageHeight = 0;

	copyCmd.copyBufferToImage.srcBuffer = renderer->textureStagingBuffer->buffer;
	copyCmd.copyBufferToImage.dstImage = vulkanTexture->image;
	copyCmd.copyBufferToImage.dstImageLayout = AccessMap[vulkanTexture->resourceAccessType].imageLayout;
	copyCmd.copyBufferToImage.region = imageCopy;
	VULKAN_INTERNAL_EncodeCommand(
		renderer,
		copyCmd
	);

	VULKAN_INTERNAL_Flush(renderer);

	SDL_UnlockMutex(renderer->passLock);
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
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *vulkanTexture = (VulkanTexture*) texture;
	VkBufferImageCopy imageCopy;
	VulkanCommand copyCmd =
	{
		CMDTYPE_COPY_BUFFER_TO_IMAGE
	};

	SDL_LockMutex(renderer->passLock);

	VULKAN_INTERNAL_MaybeExpandStagingBuffer(renderer, dataLength);

	SDL_memcpy(renderer->textureStagingBuffer->mapPointer, data, dataLength);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		vulkanTexture->layerCount,
		0,
		vulkanTexture->levelCount,
		0,
		vulkanTexture->image,
		&vulkanTexture->resourceAccessType
	);

	imageCopy.imageExtent.width = w;
	imageCopy.imageExtent.height = h;
	imageCopy.imageExtent.depth = 1;
	imageCopy.imageOffset.x = x;
	imageCopy.imageOffset.y = y;
	imageCopy.imageOffset.z = 0;
	imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageCopy.imageSubresource.baseArrayLayer = cubeMapFace;
	imageCopy.imageSubresource.layerCount = 1;
	imageCopy.imageSubresource.mipLevel = level;
	imageCopy.bufferOffset = 0;
	imageCopy.bufferRowLength = 0; /* assumes tightly packed data */
	imageCopy.bufferImageHeight = 0; /* assumes tightly packed data */

	copyCmd.copyBufferToImage.srcBuffer = renderer->textureStagingBuffer->buffer;
	copyCmd.copyBufferToImage.dstImage = vulkanTexture->image;
	copyCmd.copyBufferToImage.dstImageLayout = AccessMap[vulkanTexture->resourceAccessType].imageLayout;
	copyCmd.copyBufferToImage.region = imageCopy;
	VULKAN_INTERNAL_EncodeCommand(
		renderer,
		copyCmd
	);

	VULKAN_INTERNAL_Flush(renderer);

	SDL_UnlockMutex(renderer->passLock);
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
	VulkanTexture *tex;
	uint8_t *dataPtr = (uint8_t*) data;
	int32_t yDataLength = BytesPerImage(yWidth, yHeight, FNA3D_SURFACEFORMAT_ALPHA8);
	int32_t uvDataLength = BytesPerImage(uvWidth, uvHeight, FNA3D_SURFACEFORMAT_ALPHA8);
	VkBufferImageCopy imageCopy;
	VulkanCommand copyCmd =
	{
		CMDTYPE_COPY_BUFFER_TO_IMAGE
	};

	SDL_LockMutex(renderer->passLock);

	VULKAN_INTERNAL_MaybeExpandStagingBuffer(renderer, yDataLength + uvDataLength);

	/* Initialize values that are the same for Y, U, and V */

	imageCopy.imageExtent.depth = 1;
	imageCopy.imageOffset.x = 0;
	imageCopy.imageOffset.y = 0;
	imageCopy.imageOffset.z = 0;
	imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageCopy.imageSubresource.baseArrayLayer = 0;
	imageCopy.imageSubresource.layerCount = 1;
	imageCopy.imageSubresource.mipLevel = 0;
	imageCopy.bufferOffset = 0;

	/* Y */

	tex = (VulkanTexture*) y;

	SDL_memcpy(
		renderer->textureStagingBuffer->mapPointer,
		dataPtr,
		yDataLength
	);

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
	imageCopy.bufferRowLength = yWidth;
	imageCopy.bufferImageHeight = yHeight;

	copyCmd.copyBufferToImage.srcBuffer = renderer->textureStagingBuffer->buffer;
	copyCmd.copyBufferToImage.dstImage = tex->image;
	copyCmd.copyBufferToImage.dstImageLayout = AccessMap[tex->resourceAccessType].imageLayout;
	copyCmd.copyBufferToImage.region = imageCopy;
	VULKAN_INTERNAL_EncodeCommand(
		renderer,
		copyCmd
	);

	/* These apply to both U and V */

	imageCopy.imageExtent.width = uvWidth;
	imageCopy.imageExtent.height = uvHeight;
	imageCopy.bufferRowLength = uvWidth;
	imageCopy.bufferImageHeight = uvHeight;

	/* U */

	imageCopy.bufferOffset = yDataLength;

	tex = (VulkanTexture*) u;

	SDL_memcpy(
		renderer->textureStagingBuffer->mapPointer + yDataLength,
		dataPtr + yDataLength,
		uvDataLength
	);

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

	copyCmd.copyBufferToImage.srcBuffer = renderer->textureStagingBuffer->buffer;
	copyCmd.copyBufferToImage.dstImage = tex->image;
	copyCmd.copyBufferToImage.dstImageLayout = AccessMap[tex->resourceAccessType].imageLayout;
	copyCmd.copyBufferToImage.region = imageCopy;
	VULKAN_INTERNAL_EncodeCommand(
		renderer,
		copyCmd
	);

	/* V */

	imageCopy.bufferOffset = yDataLength + uvDataLength;

	tex = (VulkanTexture*) v;

	SDL_memcpy(
		renderer->textureStagingBuffer->mapPointer + yDataLength + uvDataLength,
		dataPtr + yDataLength + uvDataLength,
		uvDataLength
	);

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

	copyCmd.copyBufferToImage.srcBuffer = renderer->textureStagingBuffer->buffer;
	copyCmd.copyBufferToImage.dstImage = tex->image;
	copyCmd.copyBufferToImage.dstImageLayout = AccessMap[tex->resourceAccessType].imageLayout;
	copyCmd.copyBufferToImage.region = imageCopy;
	VULKAN_INTERNAL_EncodeCommand(
		renderer,
		copyCmd
	);

	VULKAN_INTERNAL_Flush(renderer);

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
		SDL_memset(
			renderbuffer->colorBuffer->multiSampleTexture,
			'\0',
			sizeof(VulkanTexture)
		);

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
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
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
	SDL_memset(
		renderbuffer->depthBuffer->handle,
		'\0',
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
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
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

	if (renderer->renderbuffersToDestroyCount + 1 >= renderer->renderbuffersToDestroyCapacity)
	{
		renderer->renderbuffersToDestroyCapacity *= 2;

		renderer->renderbuffersToDestroy = SDL_realloc(
			renderer->renderbuffersToDestroy,
			sizeof(VulkanRenderbuffer*) * renderer->renderbuffersToDestroyCapacity
		);
	}

	renderer->renderbuffersToDestroy[renderer->renderbuffersToDestroyCount] = vlkRenderBuffer;
	renderer->renderbuffersToDestroyCount += 1;
}

/* Vertex Buffers */

static FNA3D_Buffer* VULKAN_GenVertexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t sizeInBytes
) {
	return (FNA3D_Buffer*) VULKAN_INTERNAL_CreateBuffer(
		sizeInBytes,
		RESOURCE_ACCESS_VERTEX_BUFFER
	);
}

static void VULKAN_AddDisposeVertexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	VULKAN_INTERNAL_RemoveBuffer(driverData, buffer);
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
	/* FIXME: use staging buffer for elementSizeInBytes < vertexStride */
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
	VulkanBuffer *vulkanBuffer = (VulkanBuffer*) buffer;
	VulkanSubBuffer *subbuf = &vulkanBuffer->subBuffers[vulkanBuffer->currentSubBufferIndex];
	uint8_t *dataBytes, *cpy, *src, *dst;
	uint8_t useStagingBuffer;
	int32_t i;

	dataBytes = (uint8_t*) data;
	useStagingBuffer = elementSizeInBytes < vertexStride;

	if (useStagingBuffer)
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
		vulkanBuffer,
		subbuf
	);

	SDL_memcpy(
		cpy,
		subbuf->physicalBuffer->mapPointer + subbuf->offset + offsetInBytes,
		elementCount * vertexStride
	);

	if (useStagingBuffer)
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
		vulkanBuffer,
		subbuf
	);
}

/* Index Buffers */

static FNA3D_Buffer* VULKAN_GenIndexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t sizeInBytes
) {
	return (FNA3D_Buffer*) VULKAN_INTERNAL_CreateBuffer(
		sizeInBytes,
		RESOURCE_ACCESS_INDEX_BUFFER
	);
}

static void VULKAN_AddDisposeIndexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	VULKAN_INTERNAL_RemoveBuffer(driverData, buffer);
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
	VulkanBuffer *vulkanBuffer = (VulkanBuffer*) buffer;
	VulkanSubBuffer *subbuf = &vulkanBuffer->subBuffers[vulkanBuffer->currentSubBufferIndex];

	VULKAN_INTERNAL_BufferMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_READ,
		vulkanBuffer,
		subbuf
	);

	SDL_memcpy(
		data,
		subbuf->physicalBuffer->mapPointer + subbuf->offset + offsetInBytes,
		dataLength
	);

	VULKAN_INTERNAL_BufferMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_INDEX_BUFFER,
		vulkanBuffer,
		subbuf
	);
}

/* Effects */

static void VULKAN_CreateEffect(
	FNA3D_Renderer *driverData,
	uint8_t *effectCode,
	uint32_t effectCodeLength,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **effectData
) {
	MOJOSHADER_effectShaderContext shaderBackend;
	VulkanEffect *result;
	int32_t i;

	shaderBackend.compileShader = (MOJOSHADER_compileShaderFunc) MOJOSHADER_vkCompileShader;
	shaderBackend.shaderAddRef = (MOJOSHADER_shaderAddRefFunc) MOJOSHADER_vkShaderAddRef;
	shaderBackend.deleteShader = (MOJOSHADER_deleteShaderFunc) MOJOSHADER_vkDeleteShader;
	shaderBackend.getParseData = (MOJOSHADER_getParseDataFunc) MOJOSHADER_vkGetShaderParseData;
	shaderBackend.bindShaders = (MOJOSHADER_bindShadersFunc) MOJOSHADER_vkBindShaders;
	shaderBackend.getBoundShaders = (MOJOSHADER_getBoundShadersFunc) MOJOSHADER_vkGetBoundShaders;
	shaderBackend.mapUniformBufferMemory = MOJOSHADER_vkMapUniformBufferMemory;
	shaderBackend.unmapUniformBufferMemory = MOJOSHADER_vkUnmapUniformBufferMemory;
	shaderBackend.m = NULL;
	shaderBackend.f = NULL;
	shaderBackend.malloc_data = NULL;

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
	VulkanEffect *vulkanCloneSource = (VulkanEffect*) cloneSource;
	VulkanEffect *result;

	*effectData = MOJOSHADER_cloneEffect(vulkanCloneSource->effect);
	if (*effectData == NULL)
	{
		FNA3D_LogError(MOJOSHADER_vkGetError());
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
	VulkanEffect *vulkanEffect = (VulkanEffect*) effect;

	if (renderer->effectsToDestroyCount + 1 >= renderer->effectsToDestroyCapacity)
	{
		renderer->effectsToDestroyCapacity *= 2;

		renderer->effectsToDestroy = SDL_realloc(
			renderer->effectsToDestroy,
			sizeof(VulkanEffect*) * renderer->effectsToDestroyCapacity
		);
	}

	renderer->effectsToDestroy[renderer->effectsToDestroyCount] = vulkanEffect;
	renderer->effectsToDestroyCount += 1;
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

	if (effectData == renderer->currentEffect)
	{
		if (	technique == renderer->currentTechnique &&
			pass == renderer->currentPass	)
		{
			MOJOSHADER_effectCommitChanges(
				renderer->currentEffect
			);
			return;
		}
		MOJOSHADER_effectEndPass(renderer->currentEffect);
		MOJOSHADER_effectBeginPass(renderer->currentEffect, pass);
		renderer->currentTechnique = technique;
		renderer->currentPass = pass;
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

	if (renderer->freeQueryIndexStackHead == -1) {
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
	renderer->freeQueryIndexStack[vulkanQuery->index] =
		renderer->freeQueryIndexStackHead;
	renderer->freeQueryIndexStackHead = vulkanQuery->index;

	SDL_free(vulkanQuery);
}

static void VULKAN_QueryBegin(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanQuery *vulkanQuery = (VulkanQuery*) query;
	VulkanCommand resetQueryPoolCmd =
	{
		CMDTYPE_RESET_QUERY_POOL
	};
	VulkanCommand beginQueryCmd =
	{
		CMDTYPE_BEGIN_QUERY
	};

	/* Need to do this between passes */
	VULKAN_INTERNAL_MaybeEndRenderPass(renderer);

	resetQueryPoolCmd.resetQueryPool.firstQuery = vulkanQuery->index;
	resetQueryPoolCmd.resetQueryPool.queryPool = renderer->queryPool;
	VULKAN_INTERNAL_EncodeCommand(
		renderer,
		resetQueryPoolCmd
	);

	beginQueryCmd.beginQuery.queryPool = renderer->queryPool;
	beginQueryCmd.beginQuery.query = vulkanQuery->index;
	VULKAN_INTERNAL_EncodeCommand(
		renderer,
		beginQueryCmd
	);
}

static void VULKAN_QueryEnd(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanQuery *vulkanQuery = (VulkanQuery*) query;
	VulkanCommand endQueryCmd =
	{
		CMDTYPE_END_QUERY
	};

	/* Assume that the user is calling this in
	 * the same pass as they started it
	 */

	endQueryCmd.endQuery.queryPool = renderer->queryPool;
	endQueryCmd.endQuery.query = vulkanQuery->index;
	VULKAN_INTERNAL_EncodeCommand(
		renderer,
		endQueryCmd
	);
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

	if (vulkanResult != VK_SUCCESS) {
		LogVulkanResult("vkGetQueryPoolResults", VK_SUCCESS);
		return 0;
	}

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

static uint8_t VULKAN_SupportsHardwareInstancing(FNA3D_Renderer *driverData)
{
	return 1;
}

static uint8_t VULKAN_SupportsNoOverwrite(FNA3D_Renderer *driverData)
{
	return 1;
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
	VkDebugUtilsLabelEXT labelInfo =
	{
		VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT
	};
	VulkanCommand insertLabelCmd =
	{
		CMDTYPE_INSERT_DEBUG_UTILS_LABEL
	};

	if (renderer->supportsDebugUtils)
	{
		labelInfo.pLabelName = text;
		insertLabelCmd.insertDebugUtilsLabel.labelInfo = labelInfo;
		VULKAN_INTERNAL_EncodeCommand(
			renderer,
			insertLabelCmd
		);
	}
}

/* Driver */

static uint8_t VULKAN_PrepareWindowAttributes(uint32_t *flags)
{
	/* FIXME: Look for a suitable device too! */
	if (SDL_Vulkan_LoadLibrary(NULL) < 0)
	{
		return 0;
	}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
	vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) SDL_Vulkan_GetVkGetInstanceProcAddr();
#pragma GCC diagnostic pop
	if (vkGetInstanceProcAddr == NULL)
	{
		FNA3D_LogError(
			"SDL_Vulkan_GetVkGetInstanceProcAddr(): %s",
			SDL_GetError()
		);
		return 0;
	}

	#define VULKAN_GLOBAL_FUNCTION(name)								\
		name = (PFN_##name) vkGetInstanceProcAddr(VK_NULL_HANDLE, #name);			\
		if (name == NULL)									\
		{											\
			FNA3D_LogError("vkGetInstanceProcAddr(VK_NULL_HANDLE, \"" #name "\") failed");	\
			return 0;									\
		}
	#include "FNA3D_Driver_Vulkan_vkfuncs.h"

	*flags = SDL_WINDOW_VULKAN;
	return 1;
}

static void VULKAN_GetDrawableSize(void* window, int32_t *w, int32_t *h)
{
	SDL_Vulkan_GetDrawableSize((SDL_Window*) window, w, h);
}

static FNA3D_Device* VULKAN_CreateDevice(
	FNA3D_PresentationParameters *presentationParameters,
	uint8_t debugMode
) {
	uint32_t i, j;
	VkResult vulkanResult;

	/* Variables: Create the FNA3D_Device */
	FNA3D_Device *result;
	VulkanRenderer *renderer;

	/* Variables: Choose/Create vkDevice */
	const char* deviceExtensionNames[] =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_MAINTENANCE1_EXTENSION_NAME,
		VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME,
		VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME,
		VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
		VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
		"VK_GGP_frame_token"
	};
	uint32_t deviceExtensionCount = SDL_arraysize(deviceExtensionNames);

	/* Variables: Choose depth formats */
	VkImageFormatProperties imageFormatProperties;

	/* Variables: Create command pool and command buffer */
	VkCommandPoolCreateInfo commandPoolCreateInfo =
	{
		VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
	};
	VkCommandBufferAllocateInfo commandBufferAllocateInfo =
	{
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
	};

	/* Variables: Create fence and semaphores */
	VkFenceCreateInfo fenceInfo =
	{
		VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
	};
	VkSemaphoreCreateInfo semaphoreInfo =
	{
		VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
	};

	/* Variables: Create pipeline cache */
	VkPipelineCacheCreateInfo pipelineCacheCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };

	/* Variables: Create descriptor set layouts */
	VkDescriptorSetLayoutBinding *layoutBindings;
	VkDescriptorSetLayoutBinding layoutBinding;
	VkDescriptorSetLayoutCreateInfo layoutCreateInfo;

	/* Variables: Create descriptor pools */
	VkDescriptorPoolSize uniformBufferPoolSize;
	VkDescriptorPoolSize samplerPoolSize;
	VkDescriptorPoolCreateInfo uniformBufferPoolInfo =
	{
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
	};
	VkDescriptorPoolCreateInfo samplerPoolInfo =
	{
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
	};

	/* Variables: Check for DXT1/S3TC Support */
	VkFormatProperties formatPropsBC1, formatPropsBC2, formatPropsBC3;

	/* Variables: Create query pool */
	VkQueryPoolCreateInfo queryPoolCreateInfo =
	{
		VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO
	};

	/* Variables: Create dummy data */
	VkSamplerCreateInfo samplerCreateInfo;
	uint8_t dummyData[] = { 0 };

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

	renderer->presentInterval = presentationParameters->presentationInterval;
	renderer->deviceWindowHandle = presentationParameters->deviceWindowHandle;

	/*
	 * Create the vkInstance
	 */

	if (!VULKAN_INTERNAL_CreateInstance(renderer, presentationParameters))
	{
		FNA3D_LogError("Error creating vulkan instance");
		return NULL;
	}

	/*
	 * Create the WSI vkSurface
	 */

	if (!SDL_Vulkan_CreateSurface(
		(SDL_Window*) presentationParameters->deviceWindowHandle,
		renderer->instance,
		&renderer->surface
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

	#define VULKAN_INSTANCE_FUNCTION(ext, ret, func, params) \
		renderer->func = (vkfntype_##func) vkGetInstanceProcAddr(renderer->instance, #func);
	#include "FNA3D_Driver_Vulkan_vkfuncs.h"

	/*
	 * Choose/Create vkDevice
	 */

	if (SDL_strcmp(SDL_GetPlatform(), "Stadia") != 0)
	{
		deviceExtensionCount -= 1;
	}
	if (!VULKAN_INTERNAL_DeterminePhysicalDevice(
		renderer,
		deviceExtensionNames,
		deviceExtensionCount
	)) {
		FNA3D_LogError("Failed to determine a suitable physical device");
		return NULL;
	}
	if (!VULKAN_INTERNAL_CreateLogicalDevice(
		renderer,
		deviceExtensionNames,
		deviceExtensionCount
	)) {
		FNA3D_LogError("Failed to create logical device");
		return NULL;
	}

	/*
	 * Initialize buffer allocator and buffer space
	 */

	renderer->bufferAllocator = (VulkanBufferAllocator*) SDL_malloc(
		sizeof(VulkanBufferAllocator)
	);
	SDL_zerop(renderer->bufferAllocator);

	renderer->maxBuffersInUse = 32;
	renderer->buffersInUse = (VulkanBuffer**) SDL_malloc(
		sizeof(VulkanBuffer*) * renderer->maxBuffersInUse
	);
	SDL_zerop(renderer->buffersInUse);

	renderer->textureStagingBuffer = VULKAN_INTERNAL_NewPhysicalBuffer(
		renderer,
		8000000,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
	);

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
	 * Create "deferred destroy" storage
	 */

	renderer->renderbuffersToDestroyCapacity = 16;
	renderer->renderbuffersToDestroyCount = 0;

	renderer->renderbuffersToDestroy = (VulkanRenderbuffer**) SDL_malloc(
		sizeof(VulkanRenderbuffer*) *
		renderer->renderbuffersToDestroyCapacity
	);

	renderer->buffersToDestroyCapacity = 16;
	renderer->buffersToDestroyCount = 0;

	renderer->buffersToDestroy = (VulkanBuffer**) SDL_malloc(
		sizeof(VulkanBuffer*) *
		renderer->buffersToDestroyCapacity
	);

	renderer->effectsToDestroyCapacity = 16;
	renderer->effectsToDestroyCount = 0;

	renderer->effectsToDestroy = (VulkanEffect**) SDL_malloc(
		sizeof(VulkanEffect*) *
		renderer->effectsToDestroyCapacity
	);

	renderer->texturesToDestroyCapacity = 16;
	renderer->texturesToDestroyCount = 0;

	renderer->texturesToDestroy = (VulkanTexture**) SDL_malloc(
		sizeof(VulkanTexture*) *
		renderer->texturesToDestroyCapacity
	);

	/*
	 * Create MojoShader context
	 */

	renderer->mojoshaderContext = MOJOSHADER_vkCreateContext(
		&renderer->instance,
		&renderer->physicalDevice,
		&renderer->logicalDevice,
		1,
		(PFN_MOJOSHADER_vkGetInstanceProcAddr) vkGetInstanceProcAddr,
		(PFN_MOJOSHADER_vkGetDeviceProcAddr) renderer->vkGetDeviceProcAddr,
		renderer->queueFamilyIndices.graphicsFamily,
		renderer->physicalDeviceProperties.properties.limits.maxUniformBufferRange,
		renderer->physicalDeviceProperties.properties.limits.minUniformBufferOffsetAlignment,
		NULL,
		NULL,
		NULL
	);
	if (renderer->mojoshaderContext != NULL)
	{
		MOJOSHADER_vkMakeContextCurrent(renderer->mojoshaderContext);
	}
	else
	{
		FNA3D_LogError("Failed to create MojoShader context");
		return NULL;
	}

	/*
	 * Create initial swapchain
	 */

	if (VULKAN_INTERNAL_CreateSwapchain(renderer) != CREATE_SWAPCHAIN_SUCCESS)
	{
		FNA3D_LogError("Failed to create swap chain");
		return NULL;
	}

	/*
	 * Create fence and semaphores
	 */

	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	vulkanResult = renderer->vkCreateSemaphore(
		renderer->logicalDevice,
		&semaphoreInfo,
		NULL,
		&renderer->imageAvailableSemaphore
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateFence", vulkanResult);
		return NULL;
	}

	vulkanResult = renderer->vkCreateSemaphore(
		renderer->logicalDevice,
		&semaphoreInfo,
		NULL,
		&renderer->renderFinishedSemaphore
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateSemaphore", vulkanResult);
		return NULL;
	}

	vulkanResult = renderer->vkCreateFence(
		renderer->logicalDevice,
		&fenceInfo,
		NULL,
		&renderer->inFlightFence
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateSemaphore", vulkanResult);
		return NULL;
	}

	renderer->imagesInFlight = (VkFence*) SDL_malloc(
		sizeof(VkFence) * renderer->swapChainImageCount
	);

	for (i = 0; i < renderer->swapChainImageCount; i += 1)
	{
		renderer->imagesInFlight[i] = VK_NULL_HANDLE;
	}

	/*
	 * Create command pool and buffers
	 */

	commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolCreateInfo.pNext = NULL;
	commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	commandPoolCreateInfo.queueFamilyIndex = renderer->queueFamilyIndices.graphicsFamily;
	vulkanResult = renderer->vkCreateCommandPool(
		renderer->logicalDevice,
		&commandPoolCreateInfo,
		NULL,
		&renderer->commandPool
	);
	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateCommandPool", vulkanResult);
	}

	commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandBufferAllocateInfo.pNext = NULL;
	commandBufferAllocateInfo.commandPool = renderer->commandPool;
	commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandBufferAllocateInfo.commandBufferCount = 1;
	vulkanResult = renderer->vkAllocateCommandBuffers(
		renderer->logicalDevice,
		&commandBufferAllocateInfo,
		&renderer->commandBuffer
	);
	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkAllocateCommandBuffers", vulkanResult);
	}

	/*
	 * Create the dynamic deferred command array
	 */
	renderer->commandCapacity = 128; /* Arbitrary! */
	renderer->commands = (VulkanCommand*) SDL_malloc(
		sizeof(VulkanCommand) * renderer->commandCapacity
	);
	renderer->commandLock = SDL_CreateMutex();
	renderer->passLock = SDL_CreateMutex();

	/*
	 * Create the initial faux-backbuffer
	 */

	if (!VULKAN_INTERNAL_CreateFauxBackbuffer(renderer, presentationParameters))
	{
		FNA3D_LogError("Failed to create faux backbuffer");
		return NULL;
	}

	/*
	 * Create the pipeline cache
	 */

	vulkanResult = renderer->vkCreatePipelineCache(
		renderer->logicalDevice,
		&pipelineCacheCreateInfo,
		NULL,
		&renderer->pipelineCache
	);
	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreatePipelineCache", vulkanResult);
		return NULL;
	}

	/*
	 * Define sampler counts
	 */

	renderer->numSamplers = SDL_min(
		renderer->physicalDeviceProperties.properties.limits.maxSamplerAllocationCount,
		MAX_TEXTURE_SAMPLERS + MAX_VERTEXTEXTURE_SAMPLERS
	);
	renderer->numTextureSlots = SDL_min(
		renderer->numSamplers,
		MAX_TEXTURE_SAMPLERS
	);
	renderer->numVertexTextureSlots = SDL_min(
		SDL_max(renderer->numSamplers - MAX_TEXTURE_SAMPLERS, 0),
		MAX_VERTEXTEXTURE_SAMPLERS
	);

	/*
	 * Create descriptor set layouts
	 */

	/* Define vertex UBO set layout */
	SDL_zero(layoutBinding);
	layoutBinding.binding = 0;
	layoutBinding.descriptorCount = 1;
	layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	layoutBinding.pImmutableSamplers = NULL;

	SDL_zero(layoutCreateInfo);
	layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutCreateInfo.bindingCount = 1;
	layoutCreateInfo.pBindings = &layoutBinding;

	vulkanResult = renderer->vkCreateDescriptorSetLayout(
		renderer->logicalDevice,
		&layoutCreateInfo,
		NULL,
		&renderer->vertUniformBufferDescriptorSetLayout
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateDescriptorSetLayout", vulkanResult);
		return NULL;
	}

	/* Special 0 case for vert sampler layout */
	layoutBinding.binding = 0;
	layoutBinding.descriptorCount = 1;
	layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	layoutBinding.pImmutableSamplers = NULL;

	SDL_zero(layoutCreateInfo);
	layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutCreateInfo.bindingCount = 1;
	layoutCreateInfo.pBindings = &layoutBinding;

	vulkanResult = renderer->vkCreateDescriptorSetLayout(
		renderer->logicalDevice,
		&layoutCreateInfo,
		NULL,
		&renderer->vertSamplerDescriptorSetLayouts[0]
	);

	/* Define all possible vert sampler layouts */
	for (i = 1; i < MAX_VERTEXTEXTURE_SAMPLERS; i += 1)
	{
		layoutBindings = SDL_stack_alloc(VkDescriptorSetLayoutBinding, i);

		for (j = 0; j < i; j += 1)
		{
			SDL_zero(layoutBinding);
			layoutBinding.binding = j;
			layoutBinding.descriptorCount = 1;
			layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			layoutBinding.pImmutableSamplers = NULL;

			layoutBindings[j] = layoutBinding;
		}

		SDL_zero(layoutCreateInfo);
		layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutCreateInfo.bindingCount = i;
		layoutCreateInfo.pBindings = layoutBindings;

		vulkanResult = renderer->vkCreateDescriptorSetLayout(
			renderer->logicalDevice,
			&layoutCreateInfo,
			NULL,
			&renderer->vertSamplerDescriptorSetLayouts[i]
		);

		SDL_stack_free(layoutBindings);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateDescriptorSetLayout", vulkanResult);
			return NULL;
		}
	}

	/* Define frag UBO set layout */
	SDL_zero(layoutBinding);
	layoutBinding.binding = 0;
	layoutBinding.descriptorCount = 1;
	layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	layoutBinding.pImmutableSamplers = NULL;

	SDL_zero(layoutCreateInfo);
	layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutCreateInfo.bindingCount = 1;
	layoutCreateInfo.pBindings = &layoutBinding;

	vulkanResult = renderer->vkCreateDescriptorSetLayout(
		renderer->logicalDevice,
		&layoutCreateInfo,
		NULL,
		&renderer->fragUniformBufferDescriptorSetLayout
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateDescriptorSetLayout", vulkanResult);
		return NULL;
	}

	/* Special 0 case for frag sampler layout */
	layoutBinding.binding = 0;
	layoutBinding.descriptorCount = 1;
	layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	layoutBinding.pImmutableSamplers = NULL;

	SDL_zero(layoutCreateInfo);
	layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutCreateInfo.bindingCount = 1;
	layoutCreateInfo.pBindings = &layoutBinding;

	vulkanResult = renderer->vkCreateDescriptorSetLayout(
		renderer->logicalDevice,
		&layoutCreateInfo,
		NULL,
		&renderer->fragSamplerDescriptorSetLayouts[0]
	);

	/* Define all possible frag sampler layouts */
	for (i = 1; i < MAX_TEXTURE_SAMPLERS; i += 1)
	{
		layoutBindings = SDL_stack_alloc(VkDescriptorSetLayoutBinding, i);

		for (j = 0; j < i; j += 1)
		{
			SDL_zero(layoutBinding);
			layoutBinding.binding = j;
			layoutBinding.descriptorCount = 1;
			layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			layoutBinding.pImmutableSamplers = NULL;

			layoutBindings[j] = layoutBinding;
		}

		SDL_zero(layoutCreateInfo);
		layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutCreateInfo.bindingCount = i;
		layoutCreateInfo.pBindings = layoutBindings;

		vulkanResult = renderer->vkCreateDescriptorSetLayout(
			renderer->logicalDevice,
			&layoutCreateInfo,
			NULL,
			&renderer->fragSamplerDescriptorSetLayouts[i]
		);

		SDL_stack_free(layoutBindings);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateDescriptorSetLayout", vulkanResult);
			return NULL;
		}
	}

	/*
	 * Create descriptor pools
	 */

	renderer->samplerDescriptorPools = (VkDescriptorPool*) SDL_malloc(
		sizeof(VkDescriptorPool)
	);

	renderer->uniformBufferDescriptorPools = (VkDescriptorPool*) SDL_malloc(
		sizeof(VkDescriptorPool)
	);

	uniformBufferPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	uniformBufferPoolSize.descriptorCount = UNIFORM_BUFFER_DESCRIPTOR_POOL_SIZE;

	uniformBufferPoolInfo.poolSizeCount = 1;
	uniformBufferPoolInfo.pPoolSizes = &uniformBufferPoolSize;
	uniformBufferPoolInfo.maxSets = UNIFORM_BUFFER_DESCRIPTOR_POOL_SIZE;

	vulkanResult = renderer->vkCreateDescriptorPool(
		renderer->logicalDevice,
		&uniformBufferPoolInfo,
		NULL,
		&renderer->uniformBufferDescriptorPools[0]
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateDescriptorPool", vulkanResult);
		return NULL;
	}

	renderer->uniformBufferDescriptorPoolCapacity = 1;
	renderer->activeUniformBufferDescriptorPoolIndex = 0;

	samplerPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerPoolSize.descriptorCount = SAMPLER_DESCRIPTOR_POOL_SIZE;

	samplerPoolInfo.poolSizeCount = 1;
	samplerPoolInfo.pPoolSizes = &samplerPoolSize;
	samplerPoolInfo.maxSets = SAMPLER_DESCRIPTOR_POOL_SIZE;

	vulkanResult = renderer->vkCreateDescriptorPool(
		renderer->logicalDevice,
		&samplerPoolInfo,
		NULL,
		&renderer->samplerDescriptorPools[0]
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateDescriptorPool", vulkanResult);
		return NULL;
	}

	renderer->samplerDescriptorPoolCapacity = 1;
	renderer->activeSamplerDescriptorPoolIndex = 0;

	/*
	 * Init various renderer properties
	 */

	renderer->currentDepthFormat = presentationParameters->depthStencilFormat;
	renderer->currentPipeline = NULL_PIPELINE;
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

	#define SUPPORTED_FORMAT(fmt) \
		((fmt.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) && \
		(fmt.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT))
	renderer->supportsDxt1 = SUPPORTED_FORMAT(formatPropsBC1);
	renderer->supportsS3tc = (
		SUPPORTED_FORMAT(formatPropsBC2) ||
		SUPPORTED_FORMAT(formatPropsBC3)
	);

	/*
	 * Initialize various render object caches
	 */

	hmdefault(renderer->pipelineHashMap, NULL_PIPELINE);
	hmdefault(renderer->pipelineLayoutHashMap, NULL_PIPELINE_LAYOUT);
	hmdefault(renderer->renderPassHashMap, NULL_RENDER_PASS);
	hmdefault(renderer->framebufferHashMap, NULL_FRAMEBUFFER);
	hmdefault(renderer->samplerStateHashMap, NULL_SAMPLER);

	/*
	 * Initialize renderer members not covered by SDL_memset('\0')
	 */

	SDL_memset(
		renderer->multiSampleMask,
		-1,
		sizeof(renderer->multiSampleMask) /* AKA 0xFFFFFFFF */
	);


	/*
	 * Create query pool
	 */

	queryPoolCreateInfo.flags = 0;
	queryPoolCreateInfo.queryType = VK_QUERY_TYPE_OCCLUSION;
	queryPoolCreateInfo.queryCount = MAX_QUERIES;
	vulkanResult = renderer->vkCreateQueryPool(
		renderer->logicalDevice,
		&queryPoolCreateInfo,
		NULL,
		&renderer->queryPool
	);
	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateQueryPool", vulkanResult);
		return NULL;
	}

	/* Set up the stack, the value at each index is the next available
	 * index, or -1 if no such index exists.
	 */
	for (i = 0; i < MAX_QUERIES - 1; i += 1)
	{
		renderer->freeQueryIndexStack[i] = i + 1;
	}
	renderer->freeQueryIndexStack[MAX_QUERIES - 1] = -1;

	/*
	 * Create dummy data
	 */

	renderer->dummyVertTexture = (VulkanTexture*) VULKAN_CreateTexture2D(
		(FNA3D_Renderer*) renderer,
		FNA3D_SURFACEFORMAT_COLOR,
		1,
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
		renderer->dummyFragTexture->image,
		&renderer->dummyFragTexture->resourceAccessType
	);
	renderer->dummyVertUniformBuffer = (VulkanBuffer*) VULKAN_INTERNAL_CreateBuffer(
		1,
		RESOURCE_ACCESS_VERTEX_SHADER_READ_UNIFORM_BUFFER
	);
	VULKAN_INTERNAL_SetBufferData(
		(FNA3D_Renderer*) renderer,
		(FNA3D_Buffer*) renderer->dummyVertUniformBuffer,
		0,
		&dummyData,
		1,
		FNA3D_SETDATAOPTIONS_NOOVERWRITE
	);

	renderer->dummyFragUniformBuffer = (VulkanBuffer*) VULKAN_INTERNAL_CreateBuffer(
		1,
		RESOURCE_ACCESS_FRAGMENT_SHADER_READ_UNIFORM_BUFFER
	);
	VULKAN_INTERNAL_SetBufferData(
		(FNA3D_Renderer*) renderer,
		(FNA3D_Buffer*) renderer->dummyFragUniformBuffer,
		0,
		&dummyData,
		1,
		FNA3D_SETDATAOPTIONS_NOOVERWRITE
	);

	SDL_zero(samplerCreateInfo);
	samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.mipLodBias = 0;
	samplerCreateInfo.minLod = 0;
	samplerCreateInfo.maxLod = 1;
	samplerCreateInfo.maxAnisotropy = 1;
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
		&renderer->dummyFragSamplerState
	);

	return result;
}

FNA3D_Driver VulkanDriver = {
	"Vulkan",
	VULKAN_PrepareWindowAttributes,
	VULKAN_GetDrawableSize,
	VULKAN_CreateDevice
};

#else

extern int this_tu_is_empty;

#endif /* FNA3D_DRIVER_VULKAN */
