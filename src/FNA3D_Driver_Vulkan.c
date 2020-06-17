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

#include "FNA3D_Driver.h"
#include "FNA3D_Driver_Vulkan.h"
#include "FNA3D_PipelineCache.h"
#include "stb_ds.h"

#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_vulkan.h>

/* constants */

/* TODO: this _should_ work fine with a value of 3, but keeping it at 1 until things are farther along */
#define MAX_FRAMES_IN_FLIGHT 1
#define TEXTURE_COUNT MAX_TOTAL_SAMPLERS * MAX_FRAMES_IN_FLIGHT

/* should be equivalent to the number of values in FNA3D_PrimitiveType */
#define PRIMITIVE_TYPES_COUNT 5

#define SAMPLER_DESCRIPTOR_POOL_SIZE 256
#define UNIFORM_BUFFER_DESCRIPTOR_POOL_SIZE 32

const VkComponentMapping IDENTITY_SWIZZLE =
{
	VK_COMPONENT_SWIZZLE_R,
	VK_COMPONENT_SWIZZLE_G,
	VK_COMPONENT_SWIZZLE_B,
	VK_COMPONENT_SWIZZLE_A
};

typedef enum VulkanResourceAccessType {
	/* reads */
	RESOURCE_ACCESS_NONE, /* for initialization */
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

	/* writes */
	RESOURCE_ACCESS_VERTEX_SHADER_WRITE,
	RESOURCE_ACCESS_FRAGMENT_SHADER_WRITE,
	RESOURCE_ACCESS_COLOR_ATTACHMENT_WRITE,
	RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE,
	RESOURCE_ACCESS_TRANSFER_WRITE,
	RESOURCE_ACCESS_HOST_WRITE,
	RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE,
	RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_WRITE,
	RESOURCE_ACCESS_GENERAL,

	/* count */
	RESOURCE_ACCESS_TYPES_COUNT
} VulkanResourceAccessType;

typedef enum CreateSwapchainResult {
	CREATE_SWAPCHAIN_FAIL,
	CREATE_SWAPCHAIN_SUCCESS,
	CREATE_SWAPCHAIN_SURFACE_ZERO,
} CreateSwapchainResult;

/* Internal Structures */

typedef struct VulkanTexture VulkanTexture;
typedef struct VulkanRenderbuffer VulkanRenderbuffer;
typedef struct VulkanColorBuffer VulkanColorBuffer;
typedef struct VulkanDepthStencilBuffer VulkanDepthStencilBuffer;
typedef struct VulkanBuffer VulkanBuffer;
typedef struct VulkanEffect VulkanEffect;
typedef struct VulkanQuery VulkanQuery;
typedef struct PipelineHashMap PipelineHashMap;
typedef struct RenderPassHashMap RenderPassHashMap;
typedef struct FramebufferHashMap FramebufferHashMap;
typedef struct SamplerStateHashMap SamplerStateHashMap;
typedef struct PipelineLayoutHashMap PipelineLayoutHashMap;

typedef struct SurfaceFormatMapping {
	VkFormat formatColor;
	VkComponentMapping swizzle;
} SurfaceFormatMapping;

typedef struct QueueFamilyIndices {
	uint32_t graphicsFamily;
	uint32_t presentFamily;
} QueueFamilyIndices;

typedef struct SwapChainSupportDetails {
	VkSurfaceCapabilitiesKHR capabilities;
	VkSurfaceFormatKHR *formats;
	uint32_t formatsLength;
	VkPresentModeKHR *presentModes;
	uint32_t presentModesLength;
} SwapChainSupportDetails;

typedef struct VulkanImageResource
{
	VkImage image;
	VulkanResourceAccessType resourceAccessType;
} VulkanImageResource;

typedef struct FNAVulkanImageData
{
	VulkanImageResource imageResource;
	VkImageView view;
	VkDeviceMemory memory;
	VkExtent2D dimensions;
	VkDeviceSize memorySize;
	VkFormat surfaceFormat;
} FNAVulkanImageData;

/* FIXME: this could be packed better */
typedef struct PipelineHash
{
	StateHash blendState;
	StateHash rasterizerState;
	StateHash depthStencilState;
	uint64_t vertexBufferBindingsHash;
	uint64_t vertexDeclarationHash;
	FNA3D_PrimitiveType primitiveType;
	VkSampleMask sampleMask;
	uint64_t vertShader;
	uint64_t fragShader;
	/* pipelines have to be compatible with a render pass */
	VkRenderPass renderPass;
} PipelineHash;

struct PipelineHashMap
{
	PipelineHash key;
	VkPipeline value;
};

typedef struct RenderPassHash
{
	VkFormat colorAttachmentFormatOne;
	VkFormat colorAttachmentFormatTwo;
	VkFormat colorAttachmentFormatThree;
	VkFormat colorAttachmentFormatFour;
	VkFormat depthStencilAttachmentFormat;
	uint32_t width;
	uint32_t height;
} RenderPassHash;

struct RenderPassHashMap
{
	RenderPassHash key;
	VkRenderPass value;
};

typedef struct FramebufferHash
{
	VkImageView colorAttachmentViewOne;
	VkImageView colorAttachmentViewTwo;
	VkImageView colorAttachmentViewThree;
	VkImageView colorAttachmentViewFour;
	VkImageView depthStencilAttachmentViewFive;
	uint32_t width;
	uint32_t height;
} FramebufferHash;

struct FramebufferHashMap
{
	FramebufferHash key;
	VkFramebuffer value;
};

struct SamplerStateHashMap
{
	StateHash key;
	VkSampler value;
};

/* FIXME: this can be packed better */
typedef struct PipelineLayoutHash
{
	uint8_t vertUniformBufferCount;
	uint8_t vertSamplerCount;
	uint8_t fragUniformBufferCount;
	uint8_t fragSamplerCount;
} PipelineLayoutHash;

struct PipelineLayoutHashMap
{
	PipelineLayoutHash key;
	VkPipelineLayout value;
};

struct VulkanEffect {
	MOJOSHADER_effect *effect;
};

struct VulkanQuery {
	uint32_t index;
};

struct VulkanTexture {
	FNAVulkanImageData *imageData;
	VulkanBuffer *stagingBuffer;
	uint8_t hasMipmaps;
	int32_t width;
	int32_t height;
	uint8_t isPrivate;
	FNA3D_SurfaceFormat format;
};

static VulkanTexture NullTexture =
{
	NULL,
	NULL,
	0,
	0,
	0,
	0,
	FNA3D_SURFACEFORMAT_COLOR
};

struct VulkanRenderbuffer /* Cast from FNA3D_Renderbuffer */
{
	VulkanColorBuffer *colorBuffer;
	VulkanDepthStencilBuffer *depthBuffer;
};

struct VulkanColorBuffer
{
	FNAVulkanImageData handle;
};

struct VulkanDepthStencilBuffer
{
	FNAVulkanImageData handle;
};

struct VulkanBuffer
{
	VkBuffer handle;
	VkDeviceMemory deviceMemory;
	VkDeviceSize size;
	VkDeviceSize internalOffset;
	VkDeviceSize internalBufferSize;
	VkDeviceSize prevDataLength;
	VkDeviceSize prevInternalOffset;
	FNA3D_BufferUsage usage;
	VkBufferUsageFlags usageFlags;
	VulkanResourceAccessType resourceAccessType;
	uint8_t boundThisFrame;
	VulkanBuffer *next; /* linked list */
};

/* used to delay destruction until command buffer completes */
typedef struct BufferMemoryWrapper
{
	VkBuffer buffer;
	VkDeviceMemory deviceMemory;
} BufferMemoryWrapper;

typedef struct FNAVulkanRenderer
{
	FNA3D_Device *parentDevice;
	VkInstance instance;
	VkPhysicalDevice physicalDevice;
	VkPhysicalDeviceProperties2 physicalDeviceProperties;
	VkPhysicalDeviceDriverProperties physicalDeviceDriverProperties;
	VkDevice logicalDevice;
	SDL_mutex *cmdLock;

	FNA3D_PresentInterval presentInterval;
	void* deviceWindowHandle;

	QueueFamilyIndices queueFamilyIndices;
	VkQueue graphicsQueue;
	VkQueue presentQueue;

	VkSurfaceKHR surface;
	VkSwapchainKHR swapChain;
	VulkanImageResource **swapChainImages;
	VkImageView *swapChainImageViews;
	uint32_t swapChainImageCount;
	VkExtent2D swapChainExtent;
	uint32_t currentFrame;

	VkCommandPool dataCommandPool;
	VkCommandPool drawCommandPool;
	VkPipelineCache pipelineCache;

	VkRenderPass renderPass;
	VkPipeline currentPipeline;
	VkPipelineLayout currentPipelineLayout;
	uint64_t currentVertexBufferBindingHash;
	VkCommandBuffer dataCommandBuffers[MAX_FRAMES_IN_FLIGHT];
	VkCommandBuffer drawCommandBuffers[MAX_FRAMES_IN_FLIGHT];

	/* Queries */
	VkQueryPool queryPool;
	int8_t freeQueryIndexStack[MAX_QUERIES];
	int8_t freeQueryIndexStackHead;

	SurfaceFormatMapping surfaceFormatMapping;
	FNA3D_SurfaceFormat fauxBackbufferSurfaceFormat;
	VulkanColorBuffer fauxBackbufferColor;
	VulkanDepthStencilBuffer fauxBackbufferDepthStencil;
	VkFramebuffer fauxBackbufferFramebuffer;
	uint32_t fauxBackbufferWidth;
	uint32_t fauxBackbufferHeight;
	FNA3D_DepthFormat fauxBackbufferDepthFormat;
	VkSampleCountFlagBits fauxBackbufferMultisampleCount;

	FNAVulkanImageData *colorAttachments[MAX_RENDERTARGET_BINDINGS];
	uint32_t colorAttachmentCount;

	FNAVulkanImageData *depthStencilAttachment;
	FNA3D_DepthFormat currentDepthFormat;

	FNA3D_Viewport viewport;
	FNA3D_Rect scissorRect;

	VkSampleMask multiSampleMask[MAX_MULTISAMPLE_MASK_SIZE];
	FNA3D_BlendState blendState;

	FNA3D_DepthStencilState depthStencilState;
	FNA3D_RasterizerState rasterizerState;
	FNA3D_PrimitiveType currentPrimitiveType;

	VulkanBuffer *buffers;
	VulkanBuffer *userVertexBuffer;
	VulkanBuffer *userIndexBuffer;
	int32_t userVertexStride;
	uint8_t userVertexBufferInUse;
	FNA3D_VertexDeclaration userVertexDeclaration;
	uint64_t currentUserVertexDeclarationHash;

	/* Some vertex declarations may have overlapping attributes :/ */
	uint8_t attrUse[MOJOSHADER_USAGE_TOTAL][16];

	uint32_t numVertexBindings;
	FNA3D_VertexBufferBinding *vertexBindings;

	/* counts equal to swap chain count */
	VkBuffer *ldVertUniformBuffers[MAX_FRAMES_IN_FLIGHT];
	VkBuffer *ldFragUniformBuffers[MAX_FRAMES_IN_FLIGHT];
	VkDeviceSize ldVertUniformOffsets[MAX_FRAMES_IN_FLIGHT];
	VkDeviceSize ldFragUniformOffsets[MAX_FRAMES_IN_FLIGHT];

	/* needs to be dynamic because of swap chain count */
	VkBuffer ldVertexBuffers[MAX_BOUND_VERTEX_BUFFERS * MAX_FRAMES_IN_FLIGHT];
	VkDeviceSize ldVertexBufferOffsets[MAX_BOUND_VERTEX_BUFFERS * MAX_FRAMES_IN_FLIGHT];

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

	VkDescriptorPool *samplerDescriptorPools[MAX_FRAMES_IN_FLIGHT];
	uint32_t activeSamplerDescriptorPoolIndex[MAX_FRAMES_IN_FLIGHT];
	uint32_t activeSamplerPoolUsage[MAX_FRAMES_IN_FLIGHT];
	uint32_t samplerDescriptorPoolCapacity[MAX_FRAMES_IN_FLIGHT];

	VkDescriptorPool *uniformBufferDescriptorPools[MAX_FRAMES_IN_FLIGHT];
	uint32_t activeUniformBufferDescriptorPoolIndex[MAX_FRAMES_IN_FLIGHT];
	uint32_t activeUniformBufferPoolUsage[MAX_FRAMES_IN_FLIGHT];
	uint32_t uniformBufferDescriptorPoolCapacity[MAX_FRAMES_IN_FLIGHT];

	VkDescriptorSet *activeDescriptorSets[MAX_FRAMES_IN_FLIGHT]; 
	uint32_t activeDescriptorSetCount[MAX_FRAMES_IN_FLIGHT];
	uint32_t activeDescriptorSetCapacity[MAX_FRAMES_IN_FLIGHT];
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

	VkFence inFlightFences[MAX_FRAMES_IN_FLIGHT];
	VkSemaphore imageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT];
	VkSemaphore dataFinishedSemaphores[MAX_FRAMES_IN_FLIGHT];
	VkSemaphore renderFinishedSemaphores[MAX_FRAMES_IN_FLIGHT];
	VkFence *imagesInFlight;

	/* MojoShader Interop */
	MOJOSHADER_vkContext *mojoshaderContext;
	MOJOSHADER_effect *currentEffect;
	const MOJOSHADER_effectTechnique *currentTechnique;
	uint32_t currentPass;

	/* 
	 * Storing references to objects that need to be destroyed
	 * so we don't have to stall or invalidate the command buffer
	 */
	VulkanRenderbuffer **queuedRenderbuffersToDestroy;
	uint32_t queuedRenderbuffersToDestroyCount;
	uint32_t queuedRenderbuffersToDestroyCapacity;

	VulkanRenderbuffer **renderbuffersToDestroy[MAX_FRAMES_IN_FLIGHT];
	uint32_t renderbuffersToDestroyCount[MAX_FRAMES_IN_FLIGHT];
	uint32_t renderbuffersToDestroyCapacity[MAX_FRAMES_IN_FLIGHT];
	
	VulkanBuffer **queuedBuffersToDestroy;
	uint32_t queuedBuffersToDestroyCount;
	uint32_t queuedBuffersToDestroyCapacity;

	VulkanBuffer **buffersToDestroy[MAX_FRAMES_IN_FLIGHT];
	uint32_t buffersToDestroyCount[MAX_FRAMES_IN_FLIGHT];
	uint32_t buffersToDestroyCapacity[MAX_FRAMES_IN_FLIGHT];

	BufferMemoryWrapper **queuedBufferMemoryWrappersToDestroy;
	uint32_t queuedBufferMemoryWrappersToDestroyCount;
	uint32_t queuedBufferMemoryWrappersToDestroyCapacity;

	BufferMemoryWrapper **bufferMemoryWrappersToDestroy[MAX_FRAMES_IN_FLIGHT];
	uint32_t bufferMemoryWrappersToDestroyCount[MAX_FRAMES_IN_FLIGHT];
	uint32_t bufferMemoryWrappersToDestroyCapacity[MAX_FRAMES_IN_FLIGHT];

	VulkanEffect **queuedEffectsToDestroy;
	uint32_t queuedEffectsToDestroyCount;
	uint32_t queuedEffectsToDestroyCapacity;

	VulkanEffect **effectsToDestroy[MAX_FRAMES_IN_FLIGHT];
	uint32_t effectsToDestroyCount[MAX_FRAMES_IN_FLIGHT];
	uint32_t effectsToDestroyCapacity[MAX_FRAMES_IN_FLIGHT];

	FNAVulkanImageData **queuedImageDatasToDestroy;
	uint32_t queuedImageDatasToDestroyCount;
	uint32_t queuedImageDatasToDestroyCapacity;

	FNAVulkanImageData **imageDatasToDestroy[MAX_FRAMES_IN_FLIGHT];
	uint32_t imageDatasToDestroyCount[MAX_FRAMES_IN_FLIGHT];
	uint32_t imageDatasToDestroyCapacity[MAX_FRAMES_IN_FLIGHT];

	uint8_t commandBufferActive[MAX_FRAMES_IN_FLIGHT];
	uint8_t frameInProgress[MAX_FRAMES_IN_FLIGHT];
	uint8_t renderPassInProgress;
	uint8_t needNewRenderPass;
	uint8_t renderTargetBound;

	/* Capabilities */
	uint8_t supportsDxt1;
	uint8_t supportsS3tc;
	uint8_t supportsDebugUtils;
	uint8_t debugMode;

	#define VULKAN_INSTANCE_FUNCTION(ext, ret, func, params) \
		vkfntype_##func func;
	#include "FNA3D_Driver_Vulkan_instance_funcs.h"
	#undef VULKAN_INSTANCE_FUNCTION

	#define VULKAN_DEVICE_FUNCTION(ext, ret, func, params) \
		vkfntype_##func func;
	#include "FNA3D_Driver_Vulkan_device_funcs.h"
	#undef VULKAN_DEVICE_FUNCTION
} FNAVulkanRenderer;

/* image barrier stuff */

typedef struct BufferMemoryBarrierCreateInfo {
	const VulkanResourceAccessType *pPrevAccesses;
	uint32_t prevAccessCount;
	const VulkanResourceAccessType *pNextAccesses;
	uint32_t nextAccessCount;
	uint32_t srcQueueFamilyIndex;
	uint32_t dstQueueFamilyIndex;
	VkBuffer buffer;
	VkDeviceSize offset;
	VkDeviceSize size;
} BufferMemoryBarrierCreateInfo;

typedef struct ImageMemoryBarrierCreateInfo {
	VkImageSubresourceRange subresourceRange;
	uint8_t discardContents;
	uint32_t srcQueueFamilyIndex;
	uint32_t dstQueueFamilyIndex;
	VulkanResourceAccessType nextAccess;
} ImageMemoryBarrierCreateInfo;

typedef struct VulkanResourceAccessInfo {
	VkPipelineStageFlags stageMask;
	VkAccessFlags accessMask;
	VkImageLayout imageLayout;
} VulkanResourceAccessInfo;

static const VulkanResourceAccessInfo AccessMap[RESOURCE_ACCESS_TYPES_COUNT] = {
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

	/* RESOURCE_ACECSS_COLOR_ATTACHMENT_WRITE */
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

	/* RESOURCE_ACCESS_GENERAL */
	{
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
		VK_IMAGE_LAYOUT_GENERAL
	}
};

/* forward declarations */

static void BeginRenderPass(
	FNAVulkanRenderer *renderer
);

static void BindPipeline(
	FNAVulkanRenderer *renderer
);

static void BindResources(
	FNAVulkanRenderer *renderer
);

static void BindUserVertexBuffer(
	FNAVulkanRenderer *renderer,
	void *vertexData,
	int32_t vertexCount,
	int32_t vertexOffset
);

static void CheckPrimitiveType(
	FNAVulkanRenderer *renderer,
	FNA3D_PrimitiveType primitiveType
);

static void CheckVertexBufferBindings(
	FNAVulkanRenderer *renderer,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings
);

static void CheckVertexDeclaration(
	FNAVulkanRenderer *renderer,
	FNA3D_VertexDeclaration *vertexDeclaration
);

static void CreateBackingBuffer(
	FNAVulkanRenderer *renderer,
	VulkanBuffer *buffer,
	VkDeviceSize previousSize,
	VkBufferUsageFlags usageFlags
);

/* FIXME: do we even have a write only buffer concept in vulkan? */
static VulkanBuffer* CreateBuffer(
	FNAVulkanRenderer *renderer,
	FNA3D_BufferUsage usage, 
	VkDeviceSize size,
	VkBufferUsageFlags usageFlags
); 

static void CreateBufferMemoryBarrier(
	FNAVulkanRenderer *renderer,
	VkCommandBuffer commandBuffer,
	VulkanResourceAccessType nextResourceAccessType,
	VulkanBuffer *stagingBuffer
);

static void CreateImageMemoryBarrier(
	FNAVulkanRenderer *renderer,
	VkCommandBuffer commandBuffer,
	ImageMemoryBarrierCreateInfo barrierCreateInfo,
	VulkanImageResource *imageResource
);

static uint8_t CreateImage(
	FNAVulkanRenderer *renderer,
	uint32_t width,
	uint32_t height,
	VkSampleCountFlagBits samples,
	VkFormat format,
	VkComponentMapping swizzle,
	VkImageAspectFlags aspectMask,
	VkImageTiling tiling,
	VkImageType imageType,
	VkImageUsageFlags usage,
	VkMemoryPropertyFlags memoryProperties,
	FNAVulkanImageData *imageData
);

static VulkanTexture* CreateTexture(
	FNAVulkanRenderer *renderer,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget,
	VkImageType imageType
);

static CreateSwapchainResult CreateSwapchain(
	FNAVulkanRenderer *renderer
);

static void RecreateSwapchain(
	FNAVulkanRenderer *renderer
);

static void PerformDeferredDestroys(
	FNAVulkanRenderer *renderer
);

static void DestroyBuffer(
	FNAVulkanRenderer *renderer,
	VulkanBuffer *buffer
);

static void DestroyBufferAndMemory(
	FNAVulkanRenderer *renderer,
	BufferMemoryWrapper *bufferMemoryWrapper
);

static void DestroyEffect(
	FNAVulkanRenderer *renderer,
	VulkanEffect *effect
);

static void DestroyFauxBackbuffer(
	FNAVulkanRenderer *renderer
);

static void DestroyRenderbuffer(
	FNAVulkanRenderer *renderer,
	VulkanRenderbuffer *framebuffer
);

static void DestroyImageData(
	FNAVulkanRenderer *renderer,
	FNAVulkanImageData *imageData
);

static void DestroySwapchain(
	FNAVulkanRenderer *renderer
);

static void EndPass(
	FNAVulkanRenderer *renderer
);

static VkPipeline FetchPipeline(
	FNAVulkanRenderer *renderer
);

static VkPipelineLayout FetchPipelineLayout(
	FNAVulkanRenderer *renderer,
	MOJOSHADER_vkShader *vertShader,
	MOJOSHADER_vkShader *fragShader
);

static VkRenderPass FetchRenderPass(
	FNAVulkanRenderer *renderer
);

static VkFramebuffer FetchFramebuffer(
	FNAVulkanRenderer *renderer,
	VkRenderPass renderPass
);

static VkSampler FetchSamplerState(
	FNAVulkanRenderer *renderer,
	FNA3D_SamplerState *samplerState,
	uint8_t hasMipmaps
);

static PipelineHash GetPipelineHash(
	FNAVulkanRenderer *renderer
);

static PipelineLayoutHash GetPipelineLayoutHash(
	FNAVulkanRenderer *renderer,
	MOJOSHADER_vkShader *vertShader,
	MOJOSHADER_vkShader *fragShader
);

static RenderPassHash GetRenderPassHash(
	FNAVulkanRenderer *renderer
);

static uint8_t FindMemoryType(
	FNAVulkanRenderer *renderer,
	uint32_t typeFilter,
	VkMemoryPropertyFlags properties,
	uint32_t *result
);

static void GenerateUserVertexInputInfo(
	FNAVulkanRenderer *renderer,
	VkVertexInputBindingDescription *bindingDescriptions,
	VkVertexInputAttributeDescription* attributeDescriptions,
	uint32_t *attributeDescriptionCount
);

static void GenerateVertexInputInfo(
	FNAVulkanRenderer *renderer,
	VkVertexInputBindingDescription *bindingDescriptions,
	VkVertexInputAttributeDescription* attributeDescriptions,
	uint32_t *attributeDescriptionCount
);

static void InternalBeginFrame(FNAVulkanRenderer *renderer);

static void RenderPassClear(
	FNAVulkanRenderer *renderer,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil,
	uint8_t clearColor,
	uint8_t clearDepth,
	uint8_t clearStencil
);

static void UpdateRenderPass(
	FNAVulkanRenderer *renderer
);

static void SetBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
);

static void SetUserBufferData(
	FNAVulkanRenderer *renderer,
	VulkanBuffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength
);

void VULKAN_SetRenderTargets(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *renderbuffer,
	FNA3D_DepthFormat depthFormat
); 

static void SetDepthBiasCommand(FNAVulkanRenderer *renderer);
static void SetScissorRectCommand(FNAVulkanRenderer *renderer);
static void SetStencilReferenceValueCommand(FNAVulkanRenderer *renderer);
static void SetViewportCommand(FNAVulkanRenderer *renderer);

static void Stall(FNAVulkanRenderer *renderer);

static void RemoveBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
);

static VkExtent2D ChooseSwapExtent(
	void* windowHandle,
	const VkSurfaceCapabilitiesKHR capabilities
);

static uint8_t QuerySwapChainSupport(
	FNAVulkanRenderer* renderer,
	VkPhysicalDevice physicalDevice,
	VkSurfaceKHR surface,
	SwapChainSupportDetails* outputDetails
);

static void QueueBufferAndMemoryDestroy(
	FNAVulkanRenderer *renderer,
	VkBuffer vkBuffer,
	VkDeviceMemory vkDeviceMemory
);

static void QueueBufferDestroy(
	FNAVulkanRenderer *renderer,
	VulkanBuffer *vulkanBuffer
);

static void QueueImageDestroy(
	FNAVulkanRenderer *renderer,
	FNAVulkanImageData *imageData
);

static uint8_t CreateFauxBackbuffer(
	FNAVulkanRenderer *renderer,
	FNA3D_PresentationParameters *presentationParameters
);

void VULKAN_GetBackbufferSize(
	FNA3D_Renderer *driverData,
	int32_t *w,
	int32_t *h
);

/* static vars */

static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = NULL;

#define VULKAN_GLOBAL_FUNCTION(name) static PFN_##name name = NULL;
#include "FNA3D_Driver_Vulkan_global_funcs.h"
#undef VULKAN_GLOBAL_FUNCTION

/* translations arrays go here */

static VkIndexType XNAToVK_IndexType[] = 
{
	VK_INDEX_TYPE_UINT16, /* FNA3D_INDEXELEMENTSIZE_16BIT */
	VK_INDEX_TYPE_UINT32 /* FNA3D_INDEXELEMENTSIZE_32BIT */
};

static VkSampleCountFlagBits XNAToVK_SampleCount(uint8_t sampleCount)
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
		/* FIXME: emit warning here? */
		return VK_SAMPLE_COUNT_1_BIT;
	}
}

static SurfaceFormatMapping XNAToVK_SurfaceFormat[] =
{
	/* SurfaceFormat.Color */
	{
		VK_FORMAT_R8G8B8A8_UNORM
	},
	/* SurfaceFormat.Bgr565 */
	{
		VK_FORMAT_R5G6B5_UNORM_PACK16
	},
	/* SurfaceFormat.Bgra5551 */
	{
		VK_FORMAT_A1R5G5B5_UNORM_PACK16
	},
	/* SurfaceFormat.Bgra4444 */
	{
		VK_FORMAT_B4G4R4A4_UNORM_PACK16,
		{
			VK_COMPONENT_SWIZZLE_G,
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_A,
			VK_COMPONENT_SWIZZLE_B
		}
	},
	/* SurfaceFormat.Dxt1 */
	{
		VK_FORMAT_BC1_RGBA_UNORM_BLOCK
	},
	/* SurfaceFormat.Dxt3 */
	{
		VK_FORMAT_BC2_UNORM_BLOCK
	},
	/* SurfaceFormat.Dxt5 */
	{
		VK_FORMAT_BC3_UNORM_BLOCK
	},
	/* SurfaceFormat.NormalizedByte2 */
	{
		VK_FORMAT_R8G8_SNORM,
		{
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_G,
			VK_COMPONENT_SWIZZLE_ONE,
			VK_COMPONENT_SWIZZLE_ONE
		}
	},
	/* SurfaceFormat.NormalizedByte4 */
	{
		VK_FORMAT_R8G8B8A8_SNORM
	},
	/* SurfaceFormat.Rgba1010102 */
	{
		VK_FORMAT_A2R10G10B10_UNORM_PACK32
	},
	/* SurfaceFormat.Rg32 */
	{
		VK_FORMAT_R16G16_UNORM,
		{
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_G,
			VK_COMPONENT_SWIZZLE_ONE,
			VK_COMPONENT_SWIZZLE_ONE
		}
	},
	/* SurfaceFormat.Rgba64 */
	{
		VK_FORMAT_R16G16B16A16_UNORM
	},
	/* SurfaceFormat.Alpha8 */
	{
		VK_FORMAT_R8_UNORM,
		{
			VK_COMPONENT_SWIZZLE_ZERO,
			VK_COMPONENT_SWIZZLE_ZERO,
			VK_COMPONENT_SWIZZLE_ZERO,
			VK_COMPONENT_SWIZZLE_R
		}
	},
	/* SurfaceFormat.Single */
	{
		VK_FORMAT_R32_SFLOAT,
		{
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_ONE,
			VK_COMPONENT_SWIZZLE_ONE,
			VK_COMPONENT_SWIZZLE_ONE
		}
	},
	/* SurfaceFormat.Vector2 */
	{
		VK_FORMAT_R32G32_SFLOAT,
		{
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_G,
			VK_COMPONENT_SWIZZLE_ONE,
			VK_COMPONENT_SWIZZLE_ONE
		}
	},
	/* SurfaceFormat.Vector4 */
	{
		VK_FORMAT_R32G32B32A32_SFLOAT
	},
	/* SurfaceFormat.HalfSingle */
	{
		VK_FORMAT_R16_SFLOAT,
		{
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_ONE,
			VK_COMPONENT_SWIZZLE_ONE,
			VK_COMPONENT_SWIZZLE_ONE
		}
	},
	/* SurfaceFormat.HalfVector2 */
	{
		VK_FORMAT_R16G16_SFLOAT,
		{
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_G,
			VK_COMPONENT_SWIZZLE_ONE,
			VK_COMPONENT_SWIZZLE_ONE
		}
	},
	/* SurfaceFormat.HalfVector4 */
	{
		VK_FORMAT_R16G16B16A16_SFLOAT
	},
	/* SurfaceFormat.HdrBlendable */
	{
		VK_FORMAT_R16G16B16A16_SFLOAT
	},
	/* SurfaceFormat.ColorBgraEXT */
	{
		VK_FORMAT_R8G8B8A8_UNORM
	}
};

static VkFormat XNAToVK_DepthFormat(
	FNA3D_DepthFormat format
) {
	/* FIXME: check device compatibility with renderer */
	switch(format)
	{
		case FNA3D_DEPTHFORMAT_D16: return VK_FORMAT_D16_UNORM;
		case FNA3D_DEPTHFORMAT_D24: return VK_FORMAT_D24_UNORM_S8_UINT;
		case FNA3D_DEPTHFORMAT_D24S8: return VK_FORMAT_D24_UNORM_S8_UINT;
		default:
			FNA3D_LogError(
				"Tried to convert FNA3D_DEPTHFORMAT_NONE to VkFormat; something has gone very wrong"
			);
			return VK_FORMAT_UNDEFINED;
	}
}

static float XNAToVK_DepthBiasScale[] =
{
	0.0f,				/* FNA3D_DEPTHFORMAT_NONE */
	(float) ((1 << 16) - 1),	/* FNA3D_DEPTHFORMAT_D16 */
	(float) ((1 << 24) - 1),	/* FNA3D_DEPTHFORMAT_D24 */
	(float) ((1 << 24) - 1) 	/* FNA3D_DEPTHFORMAT_D24S8 */
};

static VkBlendFactor XNAToVK_BlendFactor[] =
{
	VK_BLEND_FACTOR_ONE, 						/* FNA3D_BLEND_ONE */
	VK_BLEND_FACTOR_ZERO, 						/* FNA3D_BLEND_ZERO */
	VK_BLEND_FACTOR_SRC_COLOR, 					/* FNA3D_BLEND_SOURCECOLOR */
	VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,		/* FNA3D_BLEND_INVERSESOURCECOLOR */
	VK_BLEND_FACTOR_SRC_ALPHA,					/* FNA3D_BLEND_SOURCEALPHA */
	VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,		/* FNA3D_BLEND_INVERSESOURCEALPHA */
	VK_BLEND_FACTOR_DST_COLOR,					/* FNA3D_BLEND_DESTINATIONCOLOR */
	VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,		/* FNA3D_BLEND_INVERSEDESTINATIONCOLOR */
	VK_BLEND_FACTOR_DST_ALPHA,					/* FNA3D_BLEND_DESTINATIONALPHA */
	VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,		/* FNA3D_BLEND_INVERSEDESTINATIONALPHA */
	VK_BLEND_FACTOR_CONSTANT_COLOR,				/* FNA3D_BLEND_BLENDFACTOR */
	VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,	/* FNA3D_BLEND_INVERSEBLENDFACTOR */
	VK_BLEND_FACTOR_SRC_ALPHA_SATURATE 			/* FNA3D_BLEND_SOURCEALPHASATURATION */
};

static VkBlendOp XNAToVK_BlendOp[] =
{
	VK_BLEND_OP_ADD, 				/* FNA3D_BLENDFUNCTION_ADD */
	VK_BLEND_OP_SUBTRACT, 			/* FNA3D_BLENDFUNCTION_SUBTRACT */
	VK_BLEND_OP_REVERSE_SUBTRACT, 	/* FNA3D_BLENDFUNCTION_REVERSESUBTRACT */
	VK_BLEND_OP_MAX, 				/* FNA3D_BLENDFUNCTION_MAX */
	VK_BLEND_OP_MIN					/* FNA3D_BLENDFUNCTION_MIN */
};

static VkPolygonMode XNAToVK_PolygonMode[] =
{
	VK_POLYGON_MODE_FILL, 	/* FNA3D_FILLMODE_SOLID */
	VK_POLYGON_MODE_LINE	/* FNA3D_FILLMODE_WIREFRAME */
};

static VkCullModeFlags XNAToVK_CullMode[] =
{
	VK_CULL_MODE_NONE, 		/* FNA3D_CULLMODE_NONE */
	VK_CULL_MODE_FRONT_BIT, /* FNA3D_CULLMODE_CULLCLOCKWISEFACE */
	VK_CULL_MODE_BACK_BIT	/* FNA3D_CULLMODE_CULLCOUNTERCLOCKWISEFACE */
};

static VkPrimitiveTopology XNAToVK_Topology[] =
{
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,	/* FNA3D_PRIMITIVETYPE_TRIANGLELIST */
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,	/* FNA3D_PRIMITIVETYPE_TRIANGLESTRIP */
	VK_PRIMITIVE_TOPOLOGY_LINE_LIST,		/* FNA3D_PRIMITIVETYPE_LINELIST */
	VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,		/* FNA3D_PRIMITIVETYPE_LINESTRIP */
	VK_PRIMITIVE_TOPOLOGY_POINT_LIST		/* FNA3D_PRIMITIVETYPE_POINTLIST_EXT */
};

static VkSamplerAddressMode XNAToVK_SamplerAddressMode[] =
{
	VK_SAMPLER_ADDRESS_MODE_REPEAT, 			/* FNA3D_TEXTUREADDRESSMODE_WRAP */
	VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 		/* FNA3D_TEXTUREADDRESSMODE_CLAMP */
	VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT 	/* FNA3D_TEXTUREADDRESSMODE_MIRROR */
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
	VK_COMPARE_OP_ALWAYS, 			/* FNA3D_COMPAREFUNCTION_ALWAYS */
	VK_COMPARE_OP_NEVER, 			/* FNA3D_COMPAREFUNCTION_NEVER */
	VK_COMPARE_OP_LESS,				/* FNA3D_COMPAREFUNCTION_LESS */
	VK_COMPARE_OP_LESS_OR_EQUAL, 	/* FNA3D_COMPAREFUNCTION_LESSEQUAL */
	VK_COMPARE_OP_EQUAL,			/* FNA3D_COMPAREFUNCTION_EQUAL */
	VK_COMPARE_OP_GREATER_OR_EQUAL,	/* FNA3D_COMPAREFUNCTION_GREATEREQUAL */
	VK_COMPARE_OP_GREATER,			/* FNA3D_COMPAREFUNCTION_GREATER */
	VK_COMPARE_OP_NOT_EQUAL			/* FNA3D_COMPAREFUNCTION_NOTEQUAL */
};

static VkStencilOp XNAToVK_StencilOp[] =
{
	VK_STENCIL_OP_KEEP,					/* FNA3D_STENCILOPERATION_KEEP */
	VK_STENCIL_OP_ZERO,					/* FNA3D_STENCILOPERATION_ZERO */
	VK_STENCIL_OP_REPLACE,				/* FNA3D_STENCILOPERATION_REPLACE */
	VK_STENCIL_OP_INCREMENT_AND_WRAP, 	/* FNA3D_STENCILOPERATION_INCREMENT */
	VK_STENCIL_OP_DECREMENT_AND_WRAP,	/* FNA3D_STENCILOPERATION_DECREMENT */
	VK_STENCIL_OP_INCREMENT_AND_CLAMP, 	/* FNA3D_STENCILOPERATION_INCREMENTSATURATION */
	VK_STENCIL_OP_DECREMENT_AND_CLAMP,	/* FNA3D_STENCILOPERATION_DECREMENTSATURATION */
	VK_STENCIL_OP_INVERT				/* FNA3D_STENCILOPERATION_INVERT */
};

static VkFormat XNAToVK_VertexAttribType[] =
{
	VK_FORMAT_R32_SFLOAT,				/* FNA3D_VERTEXELEMENTFORMAT_SINGLE */
	VK_FORMAT_R32G32_SFLOAT,			/* FNA3D_VERTEXELEMENTFORMAT_VECTOR2 */
	VK_FORMAT_R32G32B32_SFLOAT,			/* FNA3D_VERTEXELEMENTFORMAT_VECTOR3 */
	VK_FORMAT_R32G32B32A32_SFLOAT, 		/* FNA3D_VERTEXELEMENTFORMAT_VECTOR4 */
	VK_FORMAT_R8G8B8A8_UNORM,			/* FNA3D_VERTEXELEMENTFORMAT_COLOR */
	VK_FORMAT_R8G8B8A8_UINT,			/* FNA3D_VERTEXELEMENTFORMAT_BYTE4 */
	VK_FORMAT_R16G16_SINT, 				/* FNA3D_VERTEXELEMENTFORMAT_SHORT2 */
	VK_FORMAT_R16G16B16A16_SINT,		/* FNA3D_VERTEXELEMENTFORMAT_SHORT4 */
	VK_FORMAT_R16G16_SNORM,				/* FNA3D_VERTEXELEMENTFORMAT_NORMALIZEDSHORT2 */
	VK_FORMAT_R16G16B16A16_SNORM,		/* FNA3D_VERTEXELEMENTFORMAT_NORMALIZEDSHORT4 */
	VK_FORMAT_R16G16_SFLOAT,			/* FNA3D_VERTEXELEMENTFORMAT_HALFVECTOR2 */
	VK_FORMAT_R16G16B16A16_SFLOAT		/* FNA3D_VERTEXELEMENTFORMAT_HALFVECTOR4 */
};

static float ColorConvert(uint8_t colorValue)
{
	return colorValue / 255.0f;
}

/* error handling */

static const char* VkErrorMessages(VkResult code)
{
	const char *errorString;

	switch(code)
	{
		case VK_ERROR_OUT_OF_HOST_MEMORY:
			errorString = "Out of host memory";
			break;

		case VK_ERROR_OUT_OF_DEVICE_MEMORY:
			errorString = "Out of device memory";
			break;

		case VK_ERROR_FRAGMENTED_POOL:
			errorString = "Pool fragmentation";
			break;

		case VK_ERROR_OUT_OF_POOL_MEMORY:
			errorString = "Out of pool memory";
			break;

		case VK_ERROR_INITIALIZATION_FAILED:
			errorString = "Initialization failed";
			break;

		case VK_ERROR_LAYER_NOT_PRESENT:
			errorString = "Layer not present";
			break;

		case VK_ERROR_EXTENSION_NOT_PRESENT:
			errorString = "Extension not present";
			break;

		case VK_ERROR_FEATURE_NOT_PRESENT:
			errorString = "Feature not present";
			break;

		case VK_ERROR_TOO_MANY_OBJECTS:
			errorString = "Too many objects";
			break;

		case VK_ERROR_DEVICE_LOST:
			errorString = "Device lost";
			break;

		case VK_ERROR_INCOMPATIBLE_DRIVER:
			errorString = "Incompatible driver";
			break;

		case VK_ERROR_OUT_OF_DATE_KHR:
			errorString = "Out of date KHR";
			break;

		case VK_ERROR_SURFACE_LOST_KHR:
			errorString = "Surface lost KHR";
			break;

		case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
			errorString = "Full screen exclusive mode lost";
			break;

		case VK_SUBOPTIMAL_KHR:
			errorString = "Suboptimal KHR";
			break;

		default:
			errorString = "Unknown";
			break;
	}

	return errorString;
}

#if 0 /* FIXME: Do we need this? */
static const char* VkImageLayoutString(VkImageLayout layout)
{
	const char *layoutString;

	switch(layout)
	{
		case VK_IMAGE_LAYOUT_UNDEFINED:
			layoutString = "VK_IMAGE_LAYOUT_UNDEFINED";
			break;

		case VK_IMAGE_LAYOUT_GENERAL:
			layoutString = "VK_IMAGE_LAYOUT_GENERAL";
			break;

		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			layoutString = "VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL";
			break;

		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			layoutString = "VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL";
			break;

		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
			layoutString = "VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL";
			break;

		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			layoutString = "VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL";
			break;

		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			layoutString = "VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL";
			break;

		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			layoutString = "VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL";
			break;

		case VK_IMAGE_LAYOUT_PREINITIALIZED:
			layoutString = "VK_IMAGE_LAYOUT_PREINITIALIZED";
			break;

		case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
			layoutString = "VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL";
			break;

		case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
			layoutString = "VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL";
			break;

		case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
			layoutString = "VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL";
			break;

		case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
			layoutString = "VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL";
			break;

		case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
			layoutString = "VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL";
			break;

		case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
			layoutString = "VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL";
			break;

		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			layoutString = "VK_IMAGE_LAYOUT_PRESENT_SRC_KHR";
			break;

		case VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR:
			layoutString = "VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR";
			break;

		case VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV:
			layoutString = "VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV";
			break;

		case VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT:
			layoutString = "VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT";
			break;

		default:
			layoutString = "UNKNOWN";
	}

	return layoutString;
}
#endif

static void LogVulkanResult(
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

/* Command Functions */

static void BindPipeline(FNAVulkanRenderer *renderer)
{	
	VkPipeline pipeline = FetchPipeline(renderer);

	if (pipeline != renderer->currentPipeline)
	{
		SDL_LockMutex(renderer->cmdLock);
		renderer->vkCmdBindPipeline(
			renderer->drawCommandBuffers[renderer->currentFrame],
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipeline
		);
		SDL_UnlockMutex(renderer->cmdLock);

		renderer->currentPipeline = pipeline;
	}
}

static void CheckSamplerDescriptorPool(
	FNAVulkanRenderer *renderer,
	uint32_t additionalCount
) {
	VkDescriptorPoolSize samplerPoolSize;
	VkDescriptorPoolCreateInfo samplerPoolInfo = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
	};
	VkResult vulkanResult;

	if (renderer->activeSamplerPoolUsage[renderer->currentFrame] + additionalCount >= SAMPLER_DESCRIPTOR_POOL_SIZE)
	{
		renderer->activeSamplerDescriptorPoolIndex[renderer->currentFrame]++;

		/* if we have used all the pools, allocate a new one */
		if (renderer->activeSamplerDescriptorPoolIndex[renderer->currentFrame] >= renderer->samplerDescriptorPoolCapacity[renderer->currentFrame])
		{
			renderer->samplerDescriptorPoolCapacity[renderer->currentFrame]++;

			renderer->samplerDescriptorPools[renderer->currentFrame] = SDL_realloc(
				renderer->samplerDescriptorPools[renderer->currentFrame],
				sizeof(VkDescriptorPool) * renderer->samplerDescriptorPoolCapacity[renderer->currentFrame]
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
				&renderer->samplerDescriptorPools[renderer->currentFrame][
					renderer->activeSamplerDescriptorPoolIndex[renderer->currentFrame]
				]
			);

			if (vulkanResult != VK_SUCCESS)
			{
				LogVulkanResult("vkCreateDescriptorPool", vulkanResult);
				return;
			}
		}

		renderer->activeSamplerPoolUsage[renderer->currentFrame] = 0;
	}
}

static void CheckUniformBufferDescriptorPool(
	FNAVulkanRenderer *renderer,
	uint32_t additionalCount
) {
	VkDescriptorPoolSize bufferPoolSize;
	VkDescriptorPoolCreateInfo bufferPoolInfo = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
	};
	VkResult vulkanResult;

	/* if the UBO descriptor pool is maxed out, create another one */
	if (renderer->activeUniformBufferPoolUsage[renderer->currentFrame] + additionalCount > UNIFORM_BUFFER_DESCRIPTOR_POOL_SIZE
	) {
		renderer->activeUniformBufferDescriptorPoolIndex[renderer->currentFrame]++;

		/* if we have used all the pools, allocate a new one */
		if (renderer->activeUniformBufferDescriptorPoolIndex[renderer->currentFrame] >= renderer->uniformBufferDescriptorPoolCapacity[renderer->currentFrame])
		{
			renderer->uniformBufferDescriptorPoolCapacity[renderer->currentFrame]++;

			renderer->uniformBufferDescriptorPools[renderer->currentFrame] = SDL_realloc(
				renderer->uniformBufferDescriptorPools[renderer->currentFrame],
				sizeof(VkDescriptorPool) * renderer->uniformBufferDescriptorPoolCapacity[renderer->currentFrame]
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
				&renderer->uniformBufferDescriptorPools[renderer->currentFrame][
					renderer->activeUniformBufferDescriptorPoolIndex[renderer->currentFrame]
				]				
			);

			if (vulkanResult != VK_SUCCESS)
			{
				LogVulkanResult("vkCreateDescriptorPool", vulkanResult);
				return;
			}
		}

		renderer->activeUniformBufferPoolUsage[renderer->currentFrame] = 0;
	}
}

/* FIXME: broken descriptor bindings when a descriptor set is empty */
/* e.g. right now pipelines with no frag UBO will throw validator errors */
static void BindResources(FNAVulkanRenderer *renderer)
{
	uint8_t vertexSamplerDescriptorSetNeedsUpdate, fragSamplerDescriptorSetNeedsUpdate;
	uint8_t vertUniformBufferDescriptorSetNeedsUpdate, fragUniformBufferDescriptorSetNeedsUpdate;
	uint32_t vertArrayOffset, fragArrayOffset, i;
	VkWriteDescriptorSet writeDescriptorSets[MAX_TOTAL_SAMPLERS + 2];
	uint32_t writeDescriptorSetCount = 0;
	VkDescriptorSet samplerDescriptorSets[2];
	VkDescriptorSetLayout samplerLayoutsToUpdate[2];
	uint32_t samplerLayoutsToUpdateCount = 0;
	uint32_t vertSamplerIndex = -1;
	uint32_t fragSamplerIndex = -1;
	VkDescriptorSetAllocateInfo samplerAllocateInfo = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
	};
	VkWriteDescriptorSet vertSamplerWrite = {
		VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
	};
	VkWriteDescriptorSet fragSamplerWrite = {
		VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
	};
	VkWriteDescriptorSet vertUniformBufferWrite = {
		VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
	};
	VkBuffer *vUniform, *fUniform;
	unsigned long long vOff, fOff, vSize, fSize; /* MojoShader type... */
	VkDescriptorSetLayout uniformBufferLayouts[2];
	VkDescriptorSet uniformBufferDescriptorSets[2];
	uint32_t uniformBufferLayoutCount = 0;
	int32_t vertUniformBufferIndex = -1;
	int32_t fragUniformBufferIndex = -1;
	VkDescriptorSetAllocateInfo uniformBufferAllocateInfo = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
	};
	VkWriteDescriptorSet fragUniformBufferWrite = {
		VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
	};
	VkDescriptorImageInfo vertSamplerImageInfos[MAX_VERTEXTEXTURE_SAMPLERS];
	VkDescriptorImageInfo fragSamplerImageInfos[MAX_TEXTURE_SAMPLERS];
	VkDescriptorBufferInfo vertUniformBufferInfo;
	VkDescriptorBufferInfo fragUniformBufferInfo;
	uint32_t dynamicOffsets[2];
	uint32_t dynamicOffsetsCount = 0;
	VkDescriptorSet descriptorSetsToBind[4];
	VkResult vulkanResult;

	vertexSamplerDescriptorSetNeedsUpdate = (renderer->currentVertSamplerDescriptorSet == NULL);
	fragSamplerDescriptorSetNeedsUpdate = (renderer->currentFragSamplerDescriptorSet == NULL);
	vertUniformBufferDescriptorSetNeedsUpdate = (renderer->currentVertUniformBufferDescriptorSet == NULL);
	fragUniformBufferDescriptorSetNeedsUpdate = (renderer->currentFragUniformBufferDescriptorSet == NULL);

	vertArrayOffset = (renderer->currentFrame * MAX_TOTAL_SAMPLERS) + MAX_TEXTURE_SAMPLERS;
	fragArrayOffset = (renderer->currentFrame * MAX_TOTAL_SAMPLERS);

	for (i = 0; i < renderer->currentPipelineLayoutHash.vertSamplerCount; i++)
	{
#if 0 /* FIXME: vertSamplerImageInfos in Renderer! */
		if (	renderer->textureNeedsUpdate[vertArrayOffset + i] || 
				renderer->samplerNeedsUpdate[vertArrayOffset + i]		)
#endif
		{		
			vertSamplerImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			vertSamplerImageInfos[i].imageView = renderer->textures[vertArrayOffset + i]->imageData->view;
			vertSamplerImageInfos[i].sampler = renderer->samplers[vertArrayOffset + i];

			vertexSamplerDescriptorSetNeedsUpdate = 1;
			renderer->textureNeedsUpdate[vertArrayOffset + i] = 0;
			renderer->samplerNeedsUpdate[vertArrayOffset + i] = 0;
		}
	}
	
	/* use dummy data if sampler count is 0 */
	if (renderer->currentPipelineLayoutHash.vertSamplerCount == 0)
	{
#if 0 /* FIXME: vertSamplerImageInfos in Renderer! */
		if (renderer->currentVertSamplerDescriptorSet == NULL ||
			renderer->textureNeedsUpdate[vertArrayOffset] ||
			renderer->samplerNeedsUpdate[vertArrayOffset]	)
#endif
		{
			vertSamplerImageInfos[0].imageView = renderer->dummyVertTexture->imageData->view;
			vertSamplerImageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			vertSamplerImageInfos[0].sampler = renderer->dummyVertSamplerState;

			vertexSamplerDescriptorSetNeedsUpdate = 1;
			renderer->textureNeedsUpdate[vertArrayOffset] = 0;
			renderer->samplerNeedsUpdate[vertArrayOffset] = 0;
		}
	}

	for (i = 0; i < renderer->currentPipelineLayoutHash.fragSamplerCount; i++)
	{
#if 0 /* FIXME: fragSamplerImageInfos in Renderer! */
		if (	renderer->textureNeedsUpdate[fragArrayOffset + i] || 
				renderer->samplerNeedsUpdate[fragArrayOffset + i]		)
#endif
		{
			fragSamplerImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			fragSamplerImageInfos[i].imageView = renderer->textures[fragArrayOffset + i]->imageData->view;
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
			fragSamplerImageInfos[0].imageView = renderer->dummyFragTexture->imageData->view;
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
		samplerLayoutsToUpdateCount++;
	}

	if (fragSamplerDescriptorSetNeedsUpdate)
	{
		samplerLayoutsToUpdate[samplerLayoutsToUpdateCount] = renderer->fragSamplerDescriptorSetLayouts[
			renderer->currentPipelineLayoutHash.fragSamplerCount
		];

		fragSamplerIndex = samplerLayoutsToUpdateCount;
		samplerLayoutsToUpdateCount++;
	}

	if (samplerLayoutsToUpdateCount > 0)
	{
		/* allocate the sampler descriptor sets */
		CheckSamplerDescriptorPool(renderer, samplerLayoutsToUpdateCount);

		samplerAllocateInfo.descriptorPool = renderer->samplerDescriptorPools[renderer->currentFrame][
			renderer->activeSamplerDescriptorPoolIndex[renderer->currentFrame]
		];
		samplerAllocateInfo.descriptorSetCount = samplerLayoutsToUpdateCount;
		samplerAllocateInfo.pSetLayouts = samplerLayoutsToUpdate;

		vulkanResult = renderer->vkAllocateDescriptorSets(
			renderer->logicalDevice,
			&samplerAllocateInfo,
			samplerDescriptorSets
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkAllocateDescriptorSets", vulkanResult);
		}
	}
	
	if (vertSamplerIndex != -1)
	{
		if (renderer->currentPipelineLayoutHash.vertSamplerCount > 0)
		{
			for (i = 0; i < renderer->currentPipelineLayoutHash.vertSamplerCount; i++)
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
				writeDescriptorSetCount++;
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
			writeDescriptorSetCount++;
		}
	}

	if (fragSamplerIndex != -1)
	{
		if (renderer->currentPipelineLayoutHash.fragSamplerCount > 0)
		{
			for (i = 0; i < renderer->currentPipelineLayoutHash.fragSamplerCount; i++)
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
				writeDescriptorSetCount++;
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
			writeDescriptorSetCount++;
		}
	}

	renderer->activeSamplerPoolUsage[renderer->currentFrame] += samplerLayoutsToUpdateCount;

	MOJOSHADER_vkGetUniformBuffers(
		(void**) &vUniform,
		&vOff,
		&vSize,
		(void**) &fUniform,
		&fOff,
		&fSize
	);

	/* we can't bind a NULL UBO so we have dummy data instead */
	if (	renderer->currentVertUniformBufferDescriptorSet == NULL ||
			vUniform != renderer->ldVertUniformBuffers[renderer->currentFrame]
	) {
		if (vUniform != NULL)
		{
			vertUniformBufferInfo.buffer = *vUniform;
		}
		else
		{
			vertUniformBufferInfo.buffer = renderer->dummyVertUniformBuffer->handle;
		}
		vertUniformBufferInfo.offset = 0; /* because of dynamic offset */
		vertUniformBufferInfo.range = VK_WHOLE_SIZE;

		vertUniformBufferDescriptorSetNeedsUpdate = 1;
		renderer->ldVertUniformBuffers[renderer->currentFrame] = vUniform;
		renderer->ldVertUniformOffsets[renderer->currentFrame] = vOff;
	}
	else if (vOff != renderer->ldVertUniformOffsets[renderer->currentFrame])
	{
		renderer->ldVertUniformOffsets[renderer->currentFrame] = vOff;
	}
	
	if (	renderer->currentFragUniformBufferDescriptorSet == NULL ||
			fUniform != renderer->ldFragUniformBuffers[renderer->currentFrame]
	) {
		if (fUniform != NULL)
		{
			fragUniformBufferInfo.buffer = *fUniform;
		}
		else
		{
			fragUniformBufferInfo.buffer = renderer->dummyFragUniformBuffer->handle;
		}
		
		fragUniformBufferInfo.offset = 0; /* because of dynamic offset */
		fragUniformBufferInfo.range = VK_WHOLE_SIZE;

		fragUniformBufferDescriptorSetNeedsUpdate = 1;
		renderer->ldFragUniformBuffers[renderer->currentFrame] = fUniform;
		renderer->ldFragUniformOffsets[renderer->currentFrame] = fOff;
	}
	else if (fOff != renderer->ldFragUniformOffsets[renderer->currentFrame])
	{
		renderer->ldFragUniformOffsets[renderer->currentFrame] = fOff;
	}

	if (vertUniformBufferDescriptorSetNeedsUpdate)
	{
		uniformBufferLayouts[uniformBufferLayoutCount] = renderer->vertUniformBufferDescriptorSetLayout;
		vertUniformBufferIndex = uniformBufferLayoutCount;
		uniformBufferLayoutCount++;
	}

	if (fragUniformBufferDescriptorSetNeedsUpdate)
	{
		uniformBufferLayouts[uniformBufferLayoutCount] = renderer->fragUniformBufferDescriptorSetLayout;
		fragUniformBufferIndex = uniformBufferLayoutCount;
		uniformBufferLayoutCount++;
	}

	if (uniformBufferLayoutCount > 0)
	{
		/* allocate the UBO descriptor sets */
		CheckUniformBufferDescriptorPool(renderer, uniformBufferLayoutCount);

		uniformBufferAllocateInfo.descriptorPool = renderer->uniformBufferDescriptorPools[renderer->currentFrame][
			renderer->activeUniformBufferDescriptorPoolIndex[renderer->currentFrame]
		];
		uniformBufferAllocateInfo.descriptorSetCount = uniformBufferLayoutCount;
		uniformBufferAllocateInfo.pSetLayouts = uniformBufferLayouts;

		vulkanResult = renderer->vkAllocateDescriptorSets(
			renderer->logicalDevice,
			&uniformBufferAllocateInfo,
			uniformBufferDescriptorSets
		);

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
		writeDescriptorSetCount++;
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
		writeDescriptorSetCount++;
	}

	renderer->activeUniformBufferPoolUsage[renderer->currentFrame] += uniformBufferLayoutCount;

	if (renderer->activeDescriptorSetCount[renderer->currentFrame] + writeDescriptorSetCount > renderer->activeDescriptorSetCapacity[renderer->currentFrame])
	{
		renderer->activeDescriptorSetCapacity[renderer->currentFrame] *= 2;

		renderer->activeDescriptorSets[renderer->currentFrame] = SDL_realloc(
			renderer->activeDescriptorSets[renderer->currentFrame],
			sizeof(VkDescriptorSet) * renderer->activeDescriptorSetCapacity[renderer->currentFrame]
		);
	}

	/* vert samplers */
	if (vertSamplerIndex != -1)
	{
		renderer->activeDescriptorSets[renderer->currentFrame][
			renderer->activeDescriptorSetCount[renderer->currentFrame]
		] = samplerDescriptorSets[vertSamplerIndex];

		renderer->activeDescriptorSetCount[renderer->currentFrame]++;

		renderer->currentVertSamplerDescriptorSet = samplerDescriptorSets[vertSamplerIndex];
	}

	/* frag samplers */
	if (fragSamplerIndex != -1)
	{
		renderer->activeDescriptorSets[renderer->currentFrame][
			renderer->activeDescriptorSetCount[renderer->currentFrame]
		] = samplerDescriptorSets[fragSamplerIndex];

		renderer->activeDescriptorSetCount[renderer->currentFrame]++;

		renderer->currentFragSamplerDescriptorSet = samplerDescriptorSets[fragSamplerIndex];
	}

	/* vert ubo */
	if (vertUniformBufferIndex != -1)
	{
		renderer->activeDescriptorSets[renderer->currentFrame][
			renderer->activeDescriptorSetCount[renderer->currentFrame]
		] = uniformBufferDescriptorSets[vertUniformBufferIndex];

		renderer->activeDescriptorSetCount[renderer->currentFrame]++;

		renderer->currentVertUniformBufferDescriptorSet = uniformBufferDescriptorSets[vertUniformBufferIndex];
	}

	dynamicOffsets[dynamicOffsetsCount] = renderer->ldVertUniformOffsets[
		renderer->currentFrame
	];
	dynamicOffsetsCount++;

	/* frag ubo */
	if (fragUniformBufferIndex != -1)
	{
		renderer->activeDescriptorSets[renderer->currentFrame][
			renderer->activeDescriptorSetCount[renderer->currentFrame]
		] = uniformBufferDescriptorSets[fragUniformBufferIndex];
		
		renderer->activeDescriptorSetCount[renderer->currentFrame]++;
		renderer->currentFragUniformBufferDescriptorSet = uniformBufferDescriptorSets[fragUniformBufferIndex];
	}

	dynamicOffsets[dynamicOffsetsCount] = renderer->ldFragUniformOffsets[
		renderer->currentFrame
	];
	dynamicOffsetsCount++;

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

	SDL_LockMutex(renderer->cmdLock);
	renderer->vkCmdBindDescriptorSets(
		renderer->drawCommandBuffers[renderer->currentFrame],
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		renderer->currentPipelineLayout,
		0,
		4,
		descriptorSetsToBind,
		dynamicOffsetsCount,
		dynamicOffsets
	);
	SDL_UnlockMutex(renderer->cmdLock);
}

static void BindUserVertexBuffer(
	FNAVulkanRenderer *renderer,
	void *vertexData,
	int32_t vertexCount,
	int32_t vertexOffset
) {
	VkDeviceSize len, offset;
	VkBuffer handle;
	uint32_t swapChainOffset;

	len = vertexCount * renderer->userVertexStride;
	if (renderer->userVertexBuffer == NULL)
	{
		renderer->userVertexBuffer = CreateBuffer(
			renderer,
			FNA3D_BUFFERUSAGE_WRITEONLY,
			len,
			RESOURCE_ACCESS_VERTEX_BUFFER
		);
	}

	SetUserBufferData(
		renderer,
		renderer->userVertexBuffer,
		vertexOffset * renderer->userVertexStride,
		vertexData,
		len
	);

	offset = renderer->userVertexBuffer->internalOffset;
	handle = renderer->userVertexBuffer->handle;

	swapChainOffset = MAX_BOUND_VERTEX_BUFFERS * renderer->currentFrame;

	if (	renderer->ldVertexBuffers[swapChainOffset] != handle ||
			renderer->ldVertexBufferOffsets[swapChainOffset] != offset	)
	{
		SDL_LockMutex(renderer->cmdLock);
		renderer->vkCmdBindVertexBuffers(
			renderer->drawCommandBuffers[renderer->currentFrame],
			0,
			1,
			&handle,
			&offset
		);
		SDL_UnlockMutex(renderer->cmdLock);
		renderer->ldVertexBuffers[swapChainOffset] = handle;
		renderer->ldVertexBufferOffsets[swapChainOffset] = offset;
	}
}

static void CheckPrimitiveType(
	FNAVulkanRenderer *renderer,
	FNA3D_PrimitiveType primitiveType
) {
	if (primitiveType != renderer->currentPrimitiveType)
	{
		renderer->currentPrimitiveType = primitiveType;
	}
}

/* vertex buffer bindings are fixed in the pipeline
 * so we need to bind a new pipeline if it changes
 */
static void CheckVertexBufferBindings(
	FNAVulkanRenderer *renderer,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings
) {
	MOJOSHADER_vkShader *vertexShader, *blah;
	uint64_t hash;

	MOJOSHADER_vkGetBoundShaders(&vertexShader, &blah);

	hash = GetVertexBufferBindingsHash(
		bindings,
		numBindings,
		vertexShader
	);

	if (	renderer->userVertexBufferInUse ||
			renderer->vertexBindings != bindings ||
			renderer->numVertexBindings != numBindings ||
			hash != renderer->currentVertexBufferBindingHash	)
	{
		renderer->vertexBindings = bindings;
		renderer->numVertexBindings = numBindings;
		renderer->userVertexBufferInUse = 0;
		renderer->currentVertexBufferBindingHash = hash;
		renderer->currentUserVertexDeclarationHash = 0;
	}
}

/* vertex buffer bindings are fixed in the pipeline
 * so we need to bind a new pipeline if it changes
 */

static void CheckVertexDeclaration(
	FNAVulkanRenderer *renderer,
	FNA3D_VertexDeclaration *vertexDeclaration
) {
	MOJOSHADER_vkShader *vertexShader, *blah;
	uint64_t hash;

	MOJOSHADER_vkGetBoundShaders(&vertexShader, &blah);

	hash = GetVertexDeclarationHash(
		*vertexDeclaration,
		vertexShader
	);

	if (	renderer->userVertexBufferInUse ||
			hash != renderer->currentUserVertexDeclarationHash	)
	{
		renderer->userVertexBufferInUse = 0;
		renderer->currentUserVertexDeclarationHash = hash;
		renderer->currentVertexBufferBindingHash = 0;
		renderer->userVertexDeclaration = *vertexDeclaration;
	}
}

static void CreateBackingBuffer(
	FNAVulkanRenderer *renderer,
	VulkanBuffer *buffer,
	VkDeviceSize previousSize,
	VkBufferUsageFlags usageFlags
) {
	VkResult vulkanResult;
	VkBuffer oldBuffer = buffer->handle;
	VkDeviceMemory oldBufferMemory = buffer->deviceMemory;
	VkDeviceSize oldBufferSize = buffer->size;
	VkBufferCreateInfo buffer_create_info = {
		VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO
	};
	VkMemoryRequirements memoryRequirements;
	VkMemoryAllocateInfo allocInfo = {
		VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
	};
	void *oldContents, *contents;

	buffer_create_info.size = buffer->internalBufferSize;
	buffer_create_info.usage = usageFlags;
	buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	buffer_create_info.queueFamilyIndexCount = 1;
	buffer_create_info.pQueueFamilyIndices = &renderer->queueFamilyIndices.graphicsFamily;

	renderer->vkCreateBuffer(
		renderer->logicalDevice,
		&buffer_create_info,
		NULL,
		&buffer->handle
	);

	renderer->vkGetBufferMemoryRequirements(
		renderer->logicalDevice,
		buffer->handle,
		&memoryRequirements
	);

	allocInfo.allocationSize = memoryRequirements.size;

	if (
		!FindMemoryType(
			renderer,
			memoryRequirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&allocInfo.memoryTypeIndex
		)
	) {
		FNA3D_LogError("Failed to allocate vertex buffer memory!");
		return;
	}

	vulkanResult = renderer->vkAllocateMemory(
		renderer->logicalDevice,
		&allocInfo,
		NULL,
		&buffer->deviceMemory
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkAllocateMemory", vulkanResult);
	}

	vulkanResult = renderer->vkBindBufferMemory(
		renderer->logicalDevice,
		buffer->handle,
		buffer->deviceMemory,
		0
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkBindBufferMemory", vulkanResult);
	}

	if (previousSize != -1)
	{
		renderer->vkMapMemory(
			renderer->logicalDevice,
			oldBufferMemory,
			0,
			oldBufferSize,
			0,
			&oldContents
		);

		renderer->vkMapMemory(
			renderer->logicalDevice,
			buffer->deviceMemory,
			0,
			buffer_create_info.size,
			0,
			&contents
		);

		SDL_memcpy(
			contents,
			oldContents,
			sizeof(previousSize)
		);

		renderer->vkUnmapMemory(
			renderer->logicalDevice,
			oldBufferMemory
		);

		renderer->vkUnmapMemory(
			renderer->logicalDevice,
			buffer->deviceMemory
		);

		QueueBufferAndMemoryDestroy(renderer, oldBuffer, oldBufferMemory);
	}
}

static VulkanBuffer* CreateBuffer(
	FNAVulkanRenderer *renderer,
	FNA3D_BufferUsage usage,
	VkDeviceSize size,
	VulkanResourceAccessType resourceAccessType
) {
	VkBufferUsageFlags usageFlags = 0;
	VulkanBuffer *result, *curr;

	result = (VulkanBuffer*) SDL_malloc(sizeof(VulkanBuffer));
	SDL_memset(result, 0, sizeof(VulkanBuffer));

	result->usage = usage;
	result->size = size;
	result->internalBufferSize = size;
	result->resourceAccessType = resourceAccessType;

	if (resourceAccessType == RESOURCE_ACCESS_INDEX_BUFFER)
	{
		usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	}
	else if (resourceAccessType == RESOURCE_ACCESS_VERTEX_BUFFER)
	{
		usageFlags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	}
	else if (	resourceAccessType == RESOURCE_ACCESS_VERTEX_SHADER_READ_UNIFORM_BUFFER ||
				resourceAccessType == RESOURCE_ACCESS_FRAGMENT_SHADER_READ_UNIFORM_BUFFER	)
	{
		usageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	}
	else if (resourceAccessType == RESOURCE_ACCESS_TRANSFER_READ)
	{
		usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	}
	else if (resourceAccessType == RESOURCE_ACCESS_TRANSFER_WRITE)
	{
		usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	}

	result->usageFlags = usageFlags;

	CreateBackingBuffer(renderer, result, -1, usageFlags);

	LinkedList_Add(renderer->buffers, result, curr);
	return result;
}

static void SetBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanBuffer *vulkanBuffer = (VulkanBuffer*) buffer;
	int32_t sizeRequired, previousSize;
	void* contents;

	if (vulkanBuffer->boundThisFrame)
	{
		if (options == FNA3D_SETDATAOPTIONS_NONE)
		{
			if (renderer->debugMode)
			{
				FNA3D_LogWarn(
					"Pipeline stall triggered by binding buffer with FNA3D_SETDATAOPTIONS_NONE multiple times in a frame. "
					"This is discouraged and will cause performance degradation."
				);
			}

			Stall(renderer);
			vulkanBuffer->boundThisFrame = 1;
		}
		else if (options == FNA3D_SETDATAOPTIONS_DISCARD)
		{
			vulkanBuffer->internalOffset += vulkanBuffer->size;
			sizeRequired = vulkanBuffer->internalOffset + dataLength;
			if (sizeRequired > vulkanBuffer->internalBufferSize)
			{
				previousSize = vulkanBuffer->internalBufferSize;
				vulkanBuffer->internalBufferSize *= 2;
				CreateBackingBuffer(
					renderer,
					vulkanBuffer,
					previousSize,
					vulkanBuffer->usageFlags
				);
			}
		}
	}

	/* Copy previous contents if necessary */
	if (	dataLength < vulkanBuffer->size &&
			vulkanBuffer->prevInternalOffset != vulkanBuffer->internalOffset)
	{
		renderer->vkMapMemory(
			renderer->logicalDevice,
			vulkanBuffer->deviceMemory,
			0,
			vulkanBuffer->size,
			0,
			&contents
		);

		SDL_memcpy(
			(uint8_t*) contents + vulkanBuffer->internalOffset,
			(uint8_t*) contents + vulkanBuffer->prevInternalOffset,
			vulkanBuffer->size
		);

		renderer->vkUnmapMemory(
			renderer->logicalDevice,
			vulkanBuffer->deviceMemory
		);
	}

	renderer->vkMapMemory(
		renderer->logicalDevice,
		vulkanBuffer->deviceMemory,
		0,
		dataLength,
		0,
		&contents
	);

	/* Copy data into buffer */
	SDL_memcpy(
		(uint8_t*) contents + vulkanBuffer->internalOffset + offsetInBytes,
		data,
		dataLength
	);

	renderer->vkUnmapMemory(
		renderer->logicalDevice,
		vulkanBuffer->deviceMemory
	);

	vulkanBuffer->prevInternalOffset = vulkanBuffer->internalOffset;
}

static void SetUserBufferData(
	FNAVulkanRenderer *renderer,
	VulkanBuffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength
) {
	int32_t sizeRequired, previousSize;
	void* contents;

	buffer->internalOffset += buffer->prevDataLength;
	sizeRequired = buffer->internalOffset + dataLength;
	if (sizeRequired > buffer->internalBufferSize)
	{
		previousSize = buffer->internalBufferSize;
		buffer->internalBufferSize = SDL_max(
			buffer->internalBufferSize * 2,
			buffer->internalBufferSize + dataLength
		);
		CreateBackingBuffer(renderer, buffer, previousSize, buffer->usageFlags);
	}

	renderer->vkMapMemory(
		renderer->logicalDevice,
		buffer->deviceMemory,
		0,
		buffer->internalBufferSize,
		0,
		&contents
	);

	SDL_memcpy(
		(uint8_t*) contents + buffer->internalOffset,
		(uint8_t*) data + offsetInBytes,
		dataLength
	);

	renderer->vkUnmapMemory(
		renderer->logicalDevice,
		buffer->deviceMemory
	);

	buffer->prevDataLength = dataLength;
}

/* Init/Quit */

uint8_t VULKAN_PrepareWindowAttributes(uint32_t *flags)
{
	*flags = SDL_WINDOW_VULKAN;
	return 1;
}

void VULKAN_GetDrawableSize(void* window, int32_t *x, int32_t *y)
{
	SDL_Vulkan_GetDrawableSize((SDL_Window*) window, x, y);
}

void VULKAN_DestroyDevice(FNA3D_Device *device)
{
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) device->driverData;
	VulkanBuffer *currentBuffer, *nextBuffer;
	uint32_t i, j;

	VkResult waitResult = renderer->vkDeviceWaitIdle(renderer->logicalDevice);

	if (waitResult != VK_SUCCESS)
	{
		LogVulkanResult("vkDeviceWaitIdle", waitResult);
	}

	DestroyBuffer(renderer, renderer->dummyVertUniformBuffer);
	DestroyBuffer(renderer, renderer->dummyFragUniformBuffer);

	DestroyImageData(renderer, renderer->dummyVertTexture->imageData);
	DestroyBuffer(renderer, renderer->dummyVertTexture->stagingBuffer);
	SDL_free(renderer->dummyVertTexture);

	DestroyImageData(renderer, renderer->dummyFragTexture->imageData);
	DestroyBuffer(renderer, renderer->dummyFragTexture->stagingBuffer);
	SDL_free(renderer->dummyFragTexture);

	if (renderer->userVertexBuffer != NULL)
	{
		DestroyBuffer(renderer, renderer->userVertexBuffer);
	}

	if (renderer->userIndexBuffer != NULL)
	{
		DestroyBuffer(renderer, renderer->userIndexBuffer);
	}

	for (i = 0; i < MAX_FRAMES_IN_FLIGHT + 1; i++)
	{
		PerformDeferredDestroys(renderer);
		renderer->currentFrame = (renderer->currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	currentBuffer = renderer->buffers;
	while (currentBuffer != NULL)
	{
		nextBuffer = currentBuffer->next;
		DestroyBuffer(renderer, currentBuffer);
		currentBuffer = nextBuffer;
	}

	DestroyFauxBackbuffer(renderer);

	for (i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		renderer->vkDestroySemaphore(
			renderer->logicalDevice,
			renderer->imageAvailableSemaphores[i],
			NULL
		);

		renderer->vkDestroySemaphore(
			renderer->logicalDevice,
			renderer->dataFinishedSemaphores[i],
			NULL
		);

		renderer->vkDestroySemaphore(
			renderer->logicalDevice,
			renderer->renderFinishedSemaphores[i],
			NULL
		);

		renderer->vkDestroyFence(
			renderer->logicalDevice,
			renderer->inFlightFences[i],
			NULL
		);
	}

	renderer->vkDestroyQueryPool(
		renderer->logicalDevice,
		renderer->queryPool,
		NULL
	);

	renderer->vkDestroyCommandPool(
		renderer->logicalDevice,
		renderer->dataCommandPool,
		NULL
	);

	renderer->vkDestroyCommandPool(
		renderer->logicalDevice,
		renderer->drawCommandPool,
		NULL
	);

	for (i = 0; i < hmlenu(renderer->framebufferHashMap); i++)
	{
		renderer->vkDestroyFramebuffer(
			renderer->logicalDevice,
			renderer->framebufferHashMap[i].value,
			NULL
		);
	}

	for (i = 0; i < hmlenu(renderer->pipelineHashMap); i++)
	{
		renderer->vkDestroyPipeline(
			renderer->logicalDevice,
			renderer->pipelineHashMap[i].value,
			NULL
		);
	}

	for (i = 0; i < MAX_VERTEXTEXTURE_SAMPLERS; i++)
	{
		renderer->vkDestroyDescriptorSetLayout(
			renderer->logicalDevice,
			renderer->vertSamplerDescriptorSetLayouts[i],
			NULL
		);
	}

	for (i = 0; i < MAX_TEXTURE_SAMPLERS; i++)
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
	
	for (i = 0; i < hmlenu(renderer->pipelineLayoutHashMap); i++)
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

	for (i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		for (j = 0; j < renderer->uniformBufferDescriptorPoolCapacity[i]; j++)
		{
			renderer->vkDestroyDescriptorPool(
				renderer->logicalDevice,
				renderer->uniformBufferDescriptorPools[i][j],
				NULL
			);
		}

		for (j = 0; j < renderer->samplerDescriptorPoolCapacity[i]; j++)
		{
			renderer->vkDestroyDescriptorPool(
				renderer->logicalDevice,
				renderer->samplerDescriptorPools[i][j],
				NULL
			);
		}

		SDL_free(renderer->activeDescriptorSets[i]);
	}

	for (i = 0; i < hmlenu(renderer->renderPassHashMap); i++)
	{
		renderer->vkDestroyRenderPass(
			renderer->logicalDevice,
			renderer->renderPassHashMap[i].value,
			NULL
		);
	}

	for (i = 0; i < hmlenu(renderer->samplerStateHashMap); i++)
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

	for (i = 0; i < renderer->swapChainImageCount; i++)
	{
		renderer->vkDestroyImageView(
			renderer->logicalDevice,
			renderer->swapChainImageViews[i],
			NULL
		);

		SDL_free(renderer->swapChainImages[i]);
	}

	renderer->vkDestroySwapchainKHR(
		renderer->logicalDevice,
		renderer->swapChain,
		NULL
	);

	renderer->vkDestroySurfaceKHR(
		renderer->instance,
		renderer->surface,
		NULL
	);

	renderer->vkDestroyDevice(renderer->logicalDevice, NULL);
	renderer->vkDestroyInstance(renderer->instance, NULL);

	hmfree(renderer->pipelineLayoutHashMap);
	hmfree(renderer->pipelineHashMap);
	hmfree(renderer->renderPassHashMap);
	hmfree(renderer->framebufferHashMap);
	hmfree(renderer->samplerStateHashMap);

	SDL_free(renderer->swapChainImageViews);
	SDL_free(renderer->swapChainImages);
	SDL_free(renderer->imagesInFlight);

	SDL_free(renderer->queuedRenderbuffersToDestroy);
	SDL_free(renderer->queuedBuffersToDestroy);
	SDL_free(renderer->queuedBufferMemoryWrappersToDestroy);
	SDL_free(renderer->queuedEffectsToDestroy);
	SDL_free(renderer->queuedImageDatasToDestroy);

	for (i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		SDL_free(renderer->renderbuffersToDestroy[i]);
		SDL_free(renderer->buffersToDestroy[i]);
		SDL_free(renderer->bufferMemoryWrappersToDestroy[i]);
		SDL_free(renderer->effectsToDestroy[i]);
		SDL_free(renderer->imageDatasToDestroy[i]);
	}

	SDL_DestroyMutex(renderer->cmdLock);
	SDL_free(renderer);
	SDL_free(device);
}

static void CreateBufferMemoryBarrier(
	FNAVulkanRenderer *renderer,
	VkCommandBuffer commandBuffer,
	VulkanResourceAccessType nextResourceAccessType,
	VulkanBuffer *stagingBuffer
) {
	BufferMemoryBarrierCreateInfo barrierCreateInfo;
	VkPipelineStageFlags srcStages = 0;
	VkPipelineStageFlags dstStages = 0;
	VkBufferMemoryBarrier memoryBarrier = {
		VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER
	};
	uint32_t i;
	VulkanResourceAccessType prevAccess, nextAccess;
	const VulkanResourceAccessInfo *prevAccessInfo, *nextAccessInfo;

	if (stagingBuffer->resourceAccessType == nextResourceAccessType)
	{
		return;
	}

	barrierCreateInfo.pPrevAccesses = &stagingBuffer->resourceAccessType;
	barrierCreateInfo.prevAccessCount = 1;
	barrierCreateInfo.pNextAccesses = &nextResourceAccessType;
	barrierCreateInfo.nextAccessCount = 1;
	barrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrierCreateInfo.buffer = stagingBuffer->handle;
	barrierCreateInfo.offset = stagingBuffer->internalOffset;
	barrierCreateInfo.size = stagingBuffer->internalBufferSize;

	memoryBarrier.srcAccessMask = 0;
	memoryBarrier.dstAccessMask = 0;
	memoryBarrier.srcQueueFamilyIndex = barrierCreateInfo.srcQueueFamilyIndex;
	memoryBarrier.dstQueueFamilyIndex = barrierCreateInfo.dstQueueFamilyIndex;
	memoryBarrier.buffer = barrierCreateInfo.buffer;
	memoryBarrier.offset = barrierCreateInfo.offset;
	memoryBarrier.size = barrierCreateInfo.size;

	for (i = 0; i < barrierCreateInfo.prevAccessCount; i++)
	{
		prevAccess = barrierCreateInfo.pPrevAccesses[i];
		prevAccessInfo = &AccessMap[prevAccess];

		srcStages |= prevAccessInfo->stageMask;

		if (prevAccess > RESOURCE_ACCESS_END_OF_READ)
		{
			memoryBarrier.srcAccessMask |= prevAccessInfo->accessMask;
		}
	}

	for (i = 0; i < barrierCreateInfo.nextAccessCount; i++)
	{
		nextAccess = barrierCreateInfo.pNextAccesses[i];
		nextAccessInfo = &AccessMap[nextAccess];

		dstStages |= nextAccessInfo->stageMask;

		if (memoryBarrier.srcAccessMask != 0)
		{
			memoryBarrier.dstAccessMask |= nextAccessInfo->accessMask;
		}
	}

	if (srcStages == 0)
	{
		srcStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	}
	
	if (dstStages == 0)
	{
		dstStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	}

	if (renderer->renderPassInProgress)
	{
		EndPass(renderer);

		SDL_LockMutex(renderer->cmdLock);
		renderer->vkCmdPipelineBarrier(
			commandBuffer,
			srcStages,
			dstStages,
			0,
			0,
			NULL,
			1,
			&memoryBarrier,
			0,
			NULL
		);
		SDL_UnlockMutex(renderer->cmdLock);

		renderer->needNewRenderPass = 1;
	}
	else
	{
		SDL_LockMutex(renderer->cmdLock);
		renderer->vkCmdPipelineBarrier(
			commandBuffer,
			srcStages,
			dstStages,
			0,
			0,
			NULL,
			1,
			&memoryBarrier,
			0,
			NULL
		);
		SDL_UnlockMutex(renderer->cmdLock);
	}

	stagingBuffer->resourceAccessType = nextResourceAccessType;
}

static void CreateImageMemoryBarrier(
	FNAVulkanRenderer *renderer,
	VkCommandBuffer commandBuffer,
	ImageMemoryBarrierCreateInfo barrierCreateInfo,
	VulkanImageResource *imageResource
) {
	VkPipelineStageFlags srcStages = 0;
	VkPipelineStageFlags dstStages = 0;
	VkImageMemoryBarrier memoryBarrier = {
		VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER
	};
	VulkanResourceAccessType prevAccess;
	const VulkanResourceAccessInfo *pPrevAccessInfo, *pNextAccessInfo;

	if (imageResource->resourceAccessType == barrierCreateInfo.nextAccess)
	{ 
		return; 
	}

	memoryBarrier.srcAccessMask = 0;
	memoryBarrier.dstAccessMask = 0;
	memoryBarrier.srcQueueFamilyIndex = barrierCreateInfo.srcQueueFamilyIndex;
	memoryBarrier.dstQueueFamilyIndex = barrierCreateInfo.dstQueueFamilyIndex;
	memoryBarrier.image = imageResource->image;
	memoryBarrier.subresourceRange = barrierCreateInfo.subresourceRange;
	memoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	memoryBarrier.newLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	prevAccess = imageResource->resourceAccessType;
	pPrevAccessInfo = &AccessMap[prevAccess];

	srcStages |= pPrevAccessInfo->stageMask;

	if (prevAccess > RESOURCE_ACCESS_END_OF_READ)
	{
		memoryBarrier.srcAccessMask |= pPrevAccessInfo->accessMask;
	}

	if (barrierCreateInfo.discardContents)
	{
		memoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}
	else
	{
		memoryBarrier.oldLayout = pPrevAccessInfo->imageLayout;
	}

	pNextAccessInfo = &AccessMap[barrierCreateInfo.nextAccess];

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

	if (renderer->renderPassInProgress)
	{
		EndPass(renderer);

		SDL_LockMutex(renderer->cmdLock);
		renderer->vkCmdPipelineBarrier(
			commandBuffer,
			srcStages,
			dstStages,
			0,
			0,
			NULL,
			0,
			NULL,
			1,
			&memoryBarrier
		);
		SDL_UnlockMutex(renderer->cmdLock);

		renderer->needNewRenderPass = 1;
	}
	else
	{
		SDL_LockMutex(renderer->cmdLock);
		renderer->vkCmdPipelineBarrier(
			commandBuffer,
			srcStages,
			dstStages,
			0,
			0,
			NULL,
			0,
			NULL,
			1,
			&memoryBarrier
		);
		SDL_UnlockMutex(renderer->cmdLock);
	}

	imageResource->resourceAccessType = barrierCreateInfo.nextAccess;
}

static uint8_t CreateImage(
	FNAVulkanRenderer *renderer,
	uint32_t width,
	uint32_t height,
	VkSampleCountFlagBits samples,
	VkFormat format,
	VkComponentMapping swizzle,
	VkImageAspectFlags aspectMask,
	VkImageTiling tiling,
	VkImageType imageType,
	VkImageUsageFlags usage,
	VkMemoryPropertyFlags memoryProperties,
	FNAVulkanImageData *imageData
) {
	VkResult result;
	VkImageCreateInfo imageCreateInfo = {
		VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO
	};
	VkMemoryRequirements memoryRequirements;
	VkMemoryAllocateInfo allocInfo = {
		VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
	};
	VkImageViewCreateInfo imageViewCreateInfo = {
		VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
	};

	imageCreateInfo.flags = 0;
	imageCreateInfo.imageType = imageType;
	imageCreateInfo.format = format;
	imageCreateInfo.extent.width = width;
	imageCreateInfo.extent.height = height;
	imageCreateInfo.extent.depth = 1;
	imageCreateInfo.mipLevels = 1;
	imageCreateInfo.arrayLayers = 1;
	imageCreateInfo.samples = samples;
	imageCreateInfo.tiling = tiling;
	imageCreateInfo.usage = usage;
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCreateInfo.queueFamilyIndexCount = 0;
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	imageData->imageResource.resourceAccessType = RESOURCE_ACCESS_NONE;
	imageData->surfaceFormat = format;

	result = renderer->vkCreateImage(
		renderer->logicalDevice,
		&imageCreateInfo,
		NULL,
		&imageData->imageResource.image
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateImage", result);
		FNA3D_LogError("Failed to create image");
		return 0;
	}

	renderer->vkGetImageMemoryRequirements(
		renderer->logicalDevice,
		imageData->imageResource.image,
		&memoryRequirements
	);

	imageData->memorySize = memoryRequirements.size;
	allocInfo.allocationSize = memoryRequirements.size;

	if (
		!FindMemoryType(
			renderer,
			memoryRequirements.memoryTypeBits,
			memoryProperties,
			&allocInfo.memoryTypeIndex
		)
	) {
		FNA3D_LogError("Could not find valid memory type for image creation");
		return 0;
	}

	result = renderer->vkAllocateMemory(
		renderer->logicalDevice,
		&allocInfo,
		NULL,
		&imageData->memory
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkAllocateMemory", result);
		return 0;
	}

	result = renderer->vkBindImageMemory(
		renderer->logicalDevice,
		imageData->imageResource.image,
		imageData->memory,
		0
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkBindImageMemory", result);
		return 0;
	}

	imageViewCreateInfo.flags = 0;
	imageViewCreateInfo.image = imageData->imageResource.image;
	imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imageViewCreateInfo.format = format;
	imageViewCreateInfo.components = swizzle;
	imageViewCreateInfo.subresourceRange.aspectMask = aspectMask;
	imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
	imageViewCreateInfo.subresourceRange.levelCount = 1;
	imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
	imageViewCreateInfo.subresourceRange.layerCount = 1;

	result = renderer->vkCreateImageView(
		renderer->logicalDevice,
		&imageViewCreateInfo,
		NULL,
		&imageData->view
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateImageView", result);
		FNA3D_LogError("Failed to create color attachment image view");
		return 0;
	}

	imageData->dimensions.width = width;
	imageData->dimensions.height = height;

	return 1;
}

static VulkanTexture* CreateTexture(
	FNAVulkanRenderer *renderer,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget,
	VkImageType imageType
) {
	VulkanTexture *result;
	FNAVulkanImageData *imageData;
	VulkanBuffer *stagingBuffer;
	SurfaceFormatMapping surfaceFormatMapping = XNAToVK_SurfaceFormat[format];
	uint32_t usageFlags = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	result = (VulkanTexture*) SDL_malloc(sizeof(VulkanTexture));
	SDL_memset(result, '\0', sizeof(VulkanTexture));

	imageData = (FNAVulkanImageData*) SDL_malloc(sizeof(FNAVulkanImageData));
	SDL_memset(imageData, '\0', sizeof(FNAVulkanImageData));

	stagingBuffer = (VulkanBuffer*) SDL_malloc(sizeof(VulkanBuffer));
	SDL_memset(stagingBuffer, '\0', sizeof(VulkanBuffer));

	result->imageData = imageData;

	if (isRenderTarget)
	{
		usageFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	CreateImage(
		renderer,
		width,
		height,
		VK_SAMPLE_COUNT_1_BIT,
		surfaceFormatMapping.formatColor,
		surfaceFormatMapping.swizzle,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_TILING_OPTIMAL,
		imageType,
		usageFlags,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		result->imageData
	);

	result->width = width;
	result->height = height;
	result->format = format;
	result->hasMipmaps = levelCount > 1;
	result->isPrivate = isRenderTarget;

	result->stagingBuffer = CreateBuffer(
		renderer,
		FNA3D_BUFFERUSAGE_NONE, /* arbitrary */
		result->imageData->memorySize,
		RESOURCE_ACCESS_TRANSFER_READ
	);

	return result;
}

static uint8_t BlitFramebuffer(
	FNAVulkanRenderer *renderer,
	VulkanImageResource *srcImage,
	FNA3D_Rect srcRect,
	VulkanImageResource *dstImage,
	FNA3D_Rect dstRect
) {
	VkImageBlit blit;
	ImageMemoryBarrierCreateInfo memoryBarrierCreateInfo;

	blit.srcOffsets[0].x = srcRect.x;
	blit.srcOffsets[0].y = srcRect.y;
	blit.srcOffsets[0].z = 0;
	blit.srcOffsets[1].x = srcRect.x + srcRect.w;
	blit.srcOffsets[1].y = srcRect.y + srcRect.h;
	blit.srcOffsets[1].z = 1;

	blit.srcSubresource.mipLevel = 0;
	blit.srcSubresource.baseArrayLayer = 0;
	blit.srcSubresource.layerCount = 1;
	blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; /* TODO: support depth/stencil */

	blit.dstOffsets[0].x = dstRect.x;
	blit.dstOffsets[0].y = dstRect.y;
	blit.dstOffsets[0].z = 0;
	blit.dstOffsets[1].x = dstRect.x + dstRect.w;
	blit.dstOffsets[1].y = dstRect.y + dstRect.h;
	blit.dstOffsets[1].z = 1;

	blit.dstSubresource.mipLevel = 0;
	blit.dstSubresource.baseArrayLayer = 0;
	blit.dstSubresource.layerCount = 1;
	blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; /* TODO: support depth/stencil */

	memoryBarrierCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	memoryBarrierCreateInfo.subresourceRange.baseArrayLayer = 0;
	memoryBarrierCreateInfo.subresourceRange.baseMipLevel = 0;
	memoryBarrierCreateInfo.subresourceRange.layerCount = 1;
	memoryBarrierCreateInfo.subresourceRange.levelCount = 1;
	memoryBarrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrierCreateInfo.discardContents = 0;
	memoryBarrierCreateInfo.nextAccess = RESOURCE_ACCESS_TRANSFER_READ;

	CreateImageMemoryBarrier(
		renderer,
		renderer->drawCommandBuffers[renderer->currentFrame],
		memoryBarrierCreateInfo,
		srcImage
	);

	memoryBarrierCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	memoryBarrierCreateInfo.subresourceRange.baseArrayLayer = 0;
	memoryBarrierCreateInfo.subresourceRange.baseMipLevel = 0;
	memoryBarrierCreateInfo.subresourceRange.layerCount = 1;
	memoryBarrierCreateInfo.subresourceRange.levelCount = 1;
	memoryBarrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrierCreateInfo.discardContents = 0;
	memoryBarrierCreateInfo.nextAccess = RESOURCE_ACCESS_TRANSFER_WRITE;
	CreateImageMemoryBarrier(
		renderer,
		renderer->drawCommandBuffers[renderer->currentFrame],
		memoryBarrierCreateInfo,
		dstImage
	);
	
	/* TODO: use vkCmdResolveImage for multisampled images */
	/* TODO: blit depth/stencil buffer as well */
	SDL_LockMutex(renderer->cmdLock);
	renderer->vkCmdBlitImage(
		renderer->drawCommandBuffers[renderer->currentFrame],
		srcImage->image,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		dstImage->image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&blit,
		VK_FILTER_LINEAR /* FIXME: where is the final blit filter defined? -cosmonaut */
	);
	SDL_UnlockMutex(renderer->cmdLock);

	memoryBarrierCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	memoryBarrierCreateInfo.subresourceRange.baseArrayLayer = 0;
	memoryBarrierCreateInfo.subresourceRange.baseMipLevel = 0;
	memoryBarrierCreateInfo.subresourceRange.layerCount = 1;
	memoryBarrierCreateInfo.subresourceRange.levelCount = 1;
	memoryBarrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrierCreateInfo.discardContents = 0;
	memoryBarrierCreateInfo.nextAccess = RESOURCE_ACCESS_PRESENT;

	CreateImageMemoryBarrier(
		renderer,
		renderer->drawCommandBuffers[renderer->currentFrame],
		memoryBarrierCreateInfo,
		dstImage
	);

	memoryBarrierCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	memoryBarrierCreateInfo.subresourceRange.baseArrayLayer = 0;
	memoryBarrierCreateInfo.subresourceRange.baseMipLevel = 0;
	memoryBarrierCreateInfo.subresourceRange.layerCount = 1;
	memoryBarrierCreateInfo.subresourceRange.levelCount = 1;
	memoryBarrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrierCreateInfo.discardContents = 0;
	memoryBarrierCreateInfo.nextAccess = RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE;

	CreateImageMemoryBarrier(
		renderer,
		renderer->drawCommandBuffers[renderer->currentFrame],
		memoryBarrierCreateInfo,
		srcImage
	);

	return 1;
}

static PipelineLayoutHash GetPipelineLayoutHash(
	FNAVulkanRenderer *renderer,
	MOJOSHADER_vkShader *vertShader,
	MOJOSHADER_vkShader *fragShader
) {
	PipelineLayoutHash hash;
	hash.vertUniformBufferCount = MOJOSHADER_vkGetShaderParseData(vertShader)->uniform_count > 0;
	hash.vertSamplerCount = MOJOSHADER_vkGetShaderParseData(vertShader)->sampler_count;
	hash.fragUniformBufferCount = MOJOSHADER_vkGetShaderParseData(fragShader)->uniform_count > 0;
	hash.fragSamplerCount = MOJOSHADER_vkGetShaderParseData(fragShader)->sampler_count;
	return hash;
}

static VkPipelineLayout FetchPipelineLayout(
	FNAVulkanRenderer *renderer,
	MOJOSHADER_vkShader *vertShader,
	MOJOSHADER_vkShader *fragShader
) {
	PipelineLayoutHash hash = GetPipelineLayoutHash(renderer, vertShader, fragShader);
	VkDescriptorSetLayout setLayouts[4];
	VkPipelineLayoutCreateInfo layoutCreateInfo = {
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
	};
	VkPipelineLayout layout;
	VkResult vulkanResult;

	renderer->currentPipelineLayoutHash = hash;

	if (hmgeti(renderer->pipelineLayoutHashMap, hash) != -1)
	{
		return hmget(renderer->pipelineLayoutHashMap, hash);
	}

	setLayouts[0] = renderer->vertSamplerDescriptorSetLayouts[hash.vertSamplerCount];
	setLayouts[1] = renderer->fragSamplerDescriptorSetLayouts[hash.fragSamplerCount];
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
		return NULL;
	}

	hmput(renderer->pipelineLayoutHashMap, hash, layout);
	return layout;
}

static VkPipeline FetchPipeline(
	FNAVulkanRenderer *renderer
) {
	VkResult vulkanResult;
	VkPipeline pipeline;
	VkPipelineViewportStateCreateInfo viewportStateInfo = {
		VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
	};
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {
		VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
	};
	VkVertexInputBindingDescription *bindingDescriptions;
	VkVertexInputAttributeDescription *attributeDescriptions;
	uint32_t attributeDescriptionCount;
	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
		VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
	};
	VkPipelineRasterizationStateCreateInfo rasterizerInfo = {
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
	};
	VkPipelineMultisampleStateCreateInfo multisamplingInfo = {
		VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
	};
	VkPipelineColorBlendAttachmentState colorBlendAttachment;
	VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = {
		VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
	};
	VkStencilOpState frontStencilState, backStencilState;
	VkPipelineDepthStencilStateCreateInfo depthStencilStateInfo = {
		VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
	};
	VkDynamicState dynamicStates[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_BLEND_CONSTANTS,
		VK_DYNAMIC_STATE_STENCIL_REFERENCE,
		VK_DYNAMIC_STATE_DEPTH_BIAS
	};
	VkPipelineDynamicStateCreateInfo dynamicStateInfo = {
		VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
	};
	MOJOSHADER_vkShader *vertShader, *fragShader;
	VkPipelineShaderStageCreateInfo vertShaderStageInfo = {
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
	};
	VkPipelineShaderStageCreateInfo fragShaderStageInfo = {
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
	};
	VkPipelineShaderStageCreateInfo stageInfos[2];
	VkGraphicsPipelineCreateInfo pipelineCreateInfo = {
		VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
	};

	PipelineHash hash = GetPipelineHash(renderer);

	MOJOSHADER_vkGetBoundShaders(&vertShader, &fragShader);
	renderer->currentPipelineLayout = FetchPipelineLayout(renderer, vertShader, fragShader);

	if (hmgeti(renderer->pipelineHashMap, hash) != -1)
	{
		return hmget(renderer->pipelineHashMap, hash);
	}

	/* NOTE: because viewport and scissor are dynamic,
	 * values must be set using the command buffer
	 */
	viewportStateInfo.viewportCount = 1;
	viewportStateInfo.scissorCount = 1;

	inputAssemblyInfo.topology = XNAToVK_Topology[renderer->currentPrimitiveType];
	inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

	bindingDescriptions = (VkVertexInputBindingDescription*) SDL_malloc(
		renderer->numVertexBindings *
		sizeof(VkVertexInputBindingDescription)
	);
	attributeDescriptions = (VkVertexInputAttributeDescription*) SDL_malloc(
		renderer->numVertexBindings *
		MAX_VERTEX_ATTRIBUTES *
		sizeof(VkVertexInputAttributeDescription)
	);

	if (renderer->userVertexBufferInUse)
	{
		GenerateUserVertexInputInfo(
			renderer,
			bindingDescriptions,
			attributeDescriptions,
			&attributeDescriptionCount
		);

		vertexInputInfo.vertexBindingDescriptionCount = 1;
	}
	else
	{
		GenerateVertexInputInfo(
			renderer,
			bindingDescriptions,
			attributeDescriptions,
			&attributeDescriptionCount
		);

		vertexInputInfo.vertexBindingDescriptionCount = renderer->numVertexBindings;
	}

	vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions;
	vertexInputInfo.vertexAttributeDescriptionCount = attributeDescriptionCount;
	vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

	rasterizerInfo.depthClampEnable = VK_FALSE;
	rasterizerInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterizerInfo.polygonMode = XNAToVK_PolygonMode[renderer->rasterizerState.fillMode];
	rasterizerInfo.lineWidth = 1.0f;
	rasterizerInfo.cullMode = XNAToVK_CullMode[renderer->rasterizerState.cullMode];
	/* this is reversed because we are flipping the viewport -cosmonaut */
	rasterizerInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizerInfo.depthBiasEnable = VK_TRUE;

	multisamplingInfo.sampleShadingEnable = VK_FALSE;
	multisamplingInfo.minSampleShading = 1.0f;
	multisamplingInfo.pSampleMask = renderer->multiSampleMask;
	multisamplingInfo.rasterizationSamples = XNAToVK_SampleCount(renderer->rasterizerState.multiSampleAntiAlias);
	multisamplingInfo.alphaToCoverageEnable = VK_FALSE;
	multisamplingInfo.alphaToOneEnable = VK_FALSE;

	/* FIXME: i think we need one colorblendattachment per colorattachment? */

	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_TRUE;

	colorBlendAttachment.srcColorBlendFactor = XNAToVK_BlendFactor[
		renderer->blendState.colorSourceBlend
	];
	colorBlendAttachment.srcAlphaBlendFactor = XNAToVK_BlendFactor[
		renderer->blendState.alphaSourceBlend
	];
	colorBlendAttachment.dstColorBlendFactor = XNAToVK_BlendFactor[
		renderer->blendState.colorDestinationBlend
	];
	colorBlendAttachment.dstAlphaBlendFactor = XNAToVK_BlendFactor[
		renderer->blendState.alphaDestinationBlend
	];

	colorBlendAttachment.colorBlendOp = XNAToVK_BlendOp[
		renderer->blendState.colorBlendFunction
	];
	colorBlendAttachment.alphaBlendOp = XNAToVK_BlendOp[
		renderer->blendState.alphaBlendFunction
	];

	colorBlendStateInfo.logicOpEnable = VK_FALSE;
	colorBlendStateInfo.logicOp = VK_LOGIC_OP_COPY;
	colorBlendStateInfo.attachmentCount = 1;
	colorBlendStateInfo.pAttachments = &colorBlendAttachment;

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

	depthStencilStateInfo.flags = 0; /* unused */
	depthStencilStateInfo.depthTestEnable = renderer->depthStencilState.depthBufferEnable;
	depthStencilStateInfo.depthWriteEnable = renderer->depthStencilState.depthBufferWriteEnable;
	depthStencilStateInfo.depthCompareOp = XNAToVK_CompareOp[
		renderer->depthStencilState.depthBufferFunction
	];
	depthStencilStateInfo.depthBoundsTestEnable = 0; /* unused */
	depthStencilStateInfo.stencilTestEnable = renderer->depthStencilState.stencilEnable;
	depthStencilStateInfo.front = frontStencilState;
	depthStencilStateInfo.back = backStencilState;
	depthStencilStateInfo.minDepthBounds = 0; /* unused */
	depthStencilStateInfo.maxDepthBounds = 0; /* unused */

	dynamicStateInfo.dynamicStateCount = SDL_arraysize(dynamicStates);
	dynamicStateInfo.pDynamicStates = dynamicStates;

	vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertShaderStageInfo.module = (VkShaderModule) MOJOSHADER_vkGetShaderModule(vertShader);
	vertShaderStageInfo.pName = MOJOSHADER_vkGetShaderParseData(vertShader)->mainfn;

	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = (VkShaderModule) MOJOSHADER_vkGetShaderModule(fragShader);
	fragShaderStageInfo.pName = MOJOSHADER_vkGetShaderParseData(fragShader)->mainfn;

	stageInfos[0] = vertShaderStageInfo;
	stageInfos[1] = fragShaderStageInfo;

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
		FNA3D_LogError("Something has gone very wrong!");
		return NULL;
	}

	SDL_free(bindingDescriptions);
	SDL_free(attributeDescriptions);

	hmput(renderer->pipelineHashMap, hash, pipeline);
	return pipeline;
}

static VkRenderPass FetchRenderPass(
	FNAVulkanRenderer *renderer
) {
	VkResult vulkanResult;
	VkRenderPass renderPass = NULL;
	VkAttachmentDescription attachmentDescriptions[MAX_RENDERTARGET_BINDINGS + 1];
	uint32_t i;
	VkAttachmentReference *colorAttachmentReferences;
	VkAttachmentReference depthStencilAttachmentReference;
	uint32_t attachmentCount;
	VkSubpassDescription subpass;
	VkSubpassDependency subpassDependency;
	VkRenderPassCreateInfo renderPassCreateInfo = {
		VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO
	};

	/* the render pass is already cached, can return it */

	RenderPassHash hash = GetRenderPassHash(renderer);
	if (hmgeti(renderer->renderPassHashMap, hash) != -1)
	{
		return hmget(renderer->renderPassHashMap, hash);
	}

	/* otherwise lets make a new one */

	for (i = 0; i < renderer->colorAttachmentCount; i++)
	{
		/* TODO: handle multisample */

		attachmentDescriptions[i].flags = 0;
		attachmentDescriptions[i].format = renderer->colorAttachments[i]->surfaceFormat;
		attachmentDescriptions[i].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescriptions[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachmentDescriptions[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescriptions[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachmentDescriptions[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescriptions[i].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachmentDescriptions[i].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

	colorAttachmentReferences = (VkAttachmentReference*) SDL_stack_alloc(VkAttachmentReference, renderer->colorAttachmentCount);

	for (i = 0; i < renderer->colorAttachmentCount; i++)
	{
		colorAttachmentReferences[i].attachment = i;
		colorAttachmentReferences[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

	if (renderer->depthStencilAttachment != NULL)
	{
		depthStencilAttachmentReference.attachment = renderer->colorAttachmentCount;
		depthStencilAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attachmentDescriptions[renderer->colorAttachmentCount].flags = 0;
		attachmentDescriptions[renderer->colorAttachmentCount].format = XNAToVK_DepthFormat(renderer->currentDepthFormat);
		attachmentDescriptions[renderer->colorAttachmentCount].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescriptions[renderer->colorAttachmentCount].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachmentDescriptions[renderer->colorAttachmentCount].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescriptions[renderer->colorAttachmentCount].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachmentDescriptions[renderer->colorAttachmentCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescriptions[renderer->colorAttachmentCount].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attachmentDescriptions[renderer->colorAttachmentCount].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}

	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.flags = 0;
	subpass.inputAttachmentCount = 0;
	subpass.pInputAttachments = NULL;
	subpass.colorAttachmentCount = renderer->colorAttachmentCount;
	subpass.pColorAttachments = colorAttachmentReferences;
	subpass.pResolveAttachments = NULL;
	subpass.preserveAttachmentCount = 0;
	subpass.pPreserveAttachments = NULL;

	attachmentCount = renderer->colorAttachmentCount;

	if (renderer->depthStencilAttachment == NULL)
	{
		subpass.pDepthStencilAttachment = NULL;
	}
	else
	{
		subpass.pDepthStencilAttachment = &depthStencilAttachmentReference;
		attachmentCount++;
	}

	subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	subpassDependency.dstSubpass = 0;
	subpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.srcAccessMask = 0;
	subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	subpassDependency.dependencyFlags = 0;

	renderPassCreateInfo.attachmentCount = attachmentCount;
	renderPassCreateInfo.pAttachments = attachmentDescriptions;
	renderPassCreateInfo.subpassCount = 1;
	renderPassCreateInfo.pSubpasses = &subpass;
	renderPassCreateInfo.dependencyCount = 1;
	renderPassCreateInfo.pDependencies = &subpassDependency;
	renderPassCreateInfo.flags = 0;

	vulkanResult = renderer->vkCreateRenderPass(
		renderer->logicalDevice,
		&renderPassCreateInfo,
		NULL,
		&renderPass
	);

	SDL_stack_free(colorAttachmentReferences);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateRenderPass", vulkanResult);
		FNA3D_LogError("Error during render pass creation. Something has gone very wrong!");
		return NULL;
	}

	hmput(renderer->renderPassHashMap, hash, renderPass);
	return renderPass;
}

static FramebufferHash GetFramebufferHash(
	FNAVulkanRenderer *renderer
) {
	FramebufferHash hash;
	hash.colorAttachmentViewOne = renderer->colorAttachments[0]->view;
	hash.colorAttachmentViewTwo = renderer->colorAttachments[1] != NULL ?
		renderer->colorAttachments[1]->view :
		NULL;
	hash.colorAttachmentViewThree = renderer->colorAttachments[2] != NULL ?
		renderer->colorAttachments[2]->view :
		NULL;
	hash.colorAttachmentViewFour = renderer->colorAttachments[3] != NULL ?
		renderer->colorAttachments[3]->view :
		NULL;
	hash.depthStencilAttachmentViewFive = renderer->depthStencilAttachment != NULL ?
		renderer->depthStencilAttachment->view :
		NULL;
	hash.width = renderer->colorAttachments[0]->dimensions.width;
	hash.height = renderer->colorAttachments[0]->dimensions.height;
	return hash;
}

static VkFramebuffer FetchFramebuffer(
	FNAVulkanRenderer *renderer,
	VkRenderPass renderPass
) {
	VkFramebuffer framebuffer;
	VkImageView imageViewAttachments[MAX_RENDERTARGET_BINDINGS + 1];
	uint32_t i, attachmentCount;
	VkFramebufferCreateInfo framebufferInfo = {
		VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
	};
	VkResult vulkanResult;

	/* framebuffer is cached, can return it */

	FramebufferHash hash = GetFramebufferHash(renderer);
	if (hmgeti(renderer->framebufferHashMap, hash) != -1)
	{
		return hmget(renderer->framebufferHashMap, hash);
	}

	/* otherwise make a new one */

	attachmentCount = renderer->colorAttachmentCount;

	for (i = 0; i < renderer->colorAttachmentCount; i++)
	{
		imageViewAttachments[i] = renderer->colorAttachments[i]->view;
	}
	if (renderer->depthStencilAttachment != NULL)
	{
		imageViewAttachments[renderer->colorAttachmentCount] = renderer->depthStencilAttachment->view;
		attachmentCount++;
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

static VkSampler FetchSamplerState(
	FNAVulkanRenderer *renderer,
	FNA3D_SamplerState *samplerState,
	uint8_t hasMipmaps
) {
	VkSamplerCreateInfo createInfo = {
		VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO
	};
	VkSampler state;
	VkResult result;

	StateHash hash = GetSamplerStateHash(*samplerState);
	if (hmgeti(renderer->samplerStateHashMap, hash) != -1)
	{
		return hmget(renderer->samplerStateHashMap, hash);
	}

	createInfo.addressModeU = XNAToVK_SamplerAddressMode[samplerState->addressU];
	createInfo.addressModeV = XNAToVK_SamplerAddressMode[samplerState->addressV];
	createInfo.addressModeW = XNAToVK_SamplerAddressMode[samplerState->addressW];
	createInfo.magFilter = XNAToVK_MagFilter[samplerState->filter];
	createInfo.minFilter = XNAToVK_MinFilter[samplerState->filter];
	if (hasMipmaps)
	{
		createInfo.mipmapMode = XNAToVK_MipFilter[samplerState->filter];
	}
	createInfo.mipLodBias = samplerState->mipMapLevelOfDetailBias;
	/* FIXME: double check that the lod range is correct */
	createInfo.minLod = 0;
	createInfo.maxLod = samplerState->maxMipLevel;
	createInfo.maxAnisotropy = samplerState->filter == FNA3D_TEXTUREFILTER_ANISOTROPIC ?
		SDL_max(1, samplerState->maxAnisotropy) :
		1;

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

static PipelineHash GetPipelineHash(
	FNAVulkanRenderer *renderer
) {
	PipelineHash hash;
	MOJOSHADER_vkShader *vertShader, *fragShader;
	hash.blendState = GetBlendStateHash(renderer->blendState);
	hash.rasterizerState = GetRasterizerStateHash(
		renderer->rasterizerState,
		renderer->rasterizerState.depthBias * XNAToVK_DepthBiasScale[renderer->currentDepthFormat]
	);
	hash.depthStencilState = GetDepthStencilStateHash(renderer->depthStencilState);
	hash.vertexDeclarationHash = renderer->currentUserVertexDeclarationHash;
	hash.vertexBufferBindingsHash = renderer->currentVertexBufferBindingHash;
	hash.primitiveType = renderer->currentPrimitiveType;
	hash.sampleMask = renderer->multiSampleMask[0];
	MOJOSHADER_vkGetBoundShaders(&vertShader, &fragShader);
	hash.vertShader = (uint64_t) vertShader;
	hash.fragShader = (uint64_t) fragShader;
	hash.renderPass = renderer->renderPass;
	return hash;
}

static RenderPassHash GetRenderPassHash(
	FNAVulkanRenderer *renderer
) {
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
	return hash;
}

static void BeginRenderPass(
	FNAVulkanRenderer *renderer
) {
	VkRenderPassBeginInfo renderPassBeginInfo = {
		VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
	};
	VkFramebuffer framebuffer;
	VkOffset2D offset = { 0, 0 };
	const float blendConstants[] =
	{
		ColorConvert(renderer->blendState.blendFactor.r),
		ColorConvert(renderer->blendState.blendFactor.g),
		ColorConvert(renderer->blendState.blendFactor.b),
		ColorConvert(renderer->blendState.blendFactor.a)
	};
	uint32_t swapChainOffset, i;
	
	renderer->renderPass = FetchRenderPass(renderer);
	framebuffer = FetchFramebuffer(renderer, renderer->renderPass);

	renderPassBeginInfo.renderArea.offset = offset;
	renderPassBeginInfo.renderArea.extent = renderer->colorAttachments[0]->dimensions;

	renderPassBeginInfo.renderPass = renderer->renderPass;
	renderPassBeginInfo.framebuffer = framebuffer;

	SDL_LockMutex(renderer->cmdLock);

	renderer->vkCmdBeginRenderPass(
		renderer->drawCommandBuffers[renderer->currentFrame],
		&renderPassBeginInfo,
		VK_SUBPASS_CONTENTS_INLINE
	);

	renderer->renderPassInProgress = 1;

	SetViewportCommand(renderer);
	SetScissorRectCommand(renderer);
	SetStencilReferenceValueCommand(renderer);

	renderer->vkCmdSetBlendConstants(
		renderer->drawCommandBuffers[renderer->currentFrame],
		blendConstants
	);

	renderer->vkCmdSetDepthBias(
		renderer->drawCommandBuffers[renderer->currentFrame],
		renderer->rasterizerState.depthBias,
		0, /* unused */
		renderer->rasterizerState.slopeScaleDepthBias
	);

	/* TODO: visibility buffer */

	SDL_UnlockMutex(renderer->cmdLock);

	/* Reset bindings for the current frame in flight */
	
	swapChainOffset = MAX_TOTAL_SAMPLERS * renderer->currentFrame;

	for (i = swapChainOffset; i < swapChainOffset + MAX_TOTAL_SAMPLERS; i++)
	{
		if (renderer->textures[i] != &NullTexture)
		{
			renderer->textureNeedsUpdate[i] = 1;
		}
		if (renderer->samplers[i] != NULL)
		{
			renderer->samplerNeedsUpdate[i] = 1;
		}
	}

	renderer->ldFragUniformBuffers[renderer->currentFrame] = NULL;
	renderer->ldFragUniformOffsets[renderer->currentFrame] = 0;
	renderer->ldVertUniformBuffers[renderer->currentFrame] = NULL;
	renderer->ldVertUniformOffsets[renderer->currentFrame] = 0;
	renderer->currentPipeline = NULL;

	swapChainOffset = MAX_BOUND_VERTEX_BUFFERS * renderer->currentFrame;

	for (i = swapChainOffset; i < swapChainOffset + MAX_BOUND_VERTEX_BUFFERS; i++)
	{
		renderer->ldVertexBuffers[i] = NULL;
		renderer->ldVertexBufferOffsets[i] = 0;
	}

	renderer->needNewRenderPass = 0;
}

void VULKAN_BeginFrame(FNA3D_Renderer *driverData)
{
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VkCommandBufferBeginInfo beginInfo = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
	};
	VulkanBuffer *buf;
	VkResult result;
	uint32_t i;

	if (renderer->frameInProgress[renderer->currentFrame]) return;

	result = renderer->vkWaitForFences(
		renderer->logicalDevice,
		1,
		&renderer->inFlightFences[renderer->currentFrame],
		VK_TRUE,
		UINT64_MAX
	);

	LogVulkanResult("vkWaitForFences", result);

	/* perform cleanup */

	SDL_LockMutex(renderer->cmdLock);

	renderer->vkResetCommandBuffer(
		renderer->dataCommandBuffers[renderer->currentFrame],
		VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT
	);

	renderer->vkResetCommandBuffer(
		renderer->drawCommandBuffers[renderer->currentFrame],
		VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT
	);

	PerformDeferredDestroys(renderer);

	if (renderer->activeDescriptorSetCount[renderer->currentFrame] != 0)
	{
		for (i = 0; i < renderer->samplerDescriptorPoolCapacity[renderer->currentFrame]; i++)
		{
			renderer->vkResetDescriptorPool(
				renderer->logicalDevice,
				renderer->samplerDescriptorPools[renderer->currentFrame][i],
				0
			);
		}

		renderer->activeUniformBufferDescriptorPoolIndex[renderer->currentFrame] = 0;
		renderer->activeSamplerPoolUsage[renderer->currentFrame] = 0;

		for (i = 0; i < renderer->uniformBufferDescriptorPoolCapacity[renderer->currentFrame]; i++)
		{
			renderer->vkResetDescriptorPool(
				renderer->logicalDevice,
				renderer->uniformBufferDescriptorPools[renderer->currentFrame][i],
				0
			);
		}

		renderer->activeUniformBufferDescriptorPoolIndex[renderer->currentFrame] = 0;
		renderer->activeUniformBufferPoolUsage[renderer->currentFrame] = 0;

		renderer->activeDescriptorSetCount[renderer->currentFrame] = 0;

		renderer->currentVertSamplerDescriptorSet = NULL;
		renderer->currentFragSamplerDescriptorSet = NULL;
		renderer->currentVertUniformBufferDescriptorSet = NULL;
		renderer->currentFragUniformBufferDescriptorSet = NULL;
	}

	MOJOSHADER_vkEndFrame();

	buf = renderer->buffers;
	while (buf != NULL)
	{
		buf->internalOffset = 0;
		buf->boundThisFrame = 0;
		buf->prevDataLength = 0;
		buf = buf->next;
	}

	renderer->vkBeginCommandBuffer(
		renderer->dataCommandBuffers[renderer->currentFrame],
		&beginInfo
	);

	renderer->vkBeginCommandBuffer(
		renderer->drawCommandBuffers[renderer->currentFrame],
		&beginInfo
	);

	renderer->commandBufferActive[renderer->currentFrame] = 1;
	renderer->frameInProgress[renderer->currentFrame] = 1;
	renderer->needNewRenderPass = 1;

	SDL_UnlockMutex(renderer->cmdLock);
}

static void InternalBeginFrame(FNAVulkanRenderer *renderer)
{
	VULKAN_BeginFrame((FNA3D_Renderer*) renderer);
}

void VULKAN_SwapBuffers(
	FNA3D_Renderer *driverData,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VkResult result;
	FNA3D_Rect srcRect;
	FNA3D_Rect dstRect;
	VkResult vulkanResult;
	VkSemaphore drawWaitSemaphores[] = {
		renderer->imageAvailableSemaphores[renderer->currentFrame],
		renderer->dataFinishedSemaphores[renderer->currentFrame]
	};
	VkPipelineStageFlags waitStages[] = {
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
	};
	VkSubmitInfo submitInfo = {
		VK_STRUCTURE_TYPE_SUBMIT_INFO
	};
	VkPresentInfoKHR presentInfo = {
		VK_STRUCTURE_TYPE_PRESENT_INFO_KHR
	};
	uint32_t swapChainImageIndex;

	/* begin next frame */
	result = renderer->vkAcquireNextImageKHR(
		renderer->logicalDevice,
		renderer->swapChain,
		UINT64_MAX,
		renderer->imageAvailableSemaphores[renderer->currentFrame],
		VK_NULL_HANDLE,
		&swapChainImageIndex
	);

	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		RecreateSwapchain(renderer);
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
	
	renderer->imagesInFlight[swapChainImageIndex] = renderer->inFlightFences[renderer->currentFrame];

	EndPass(renderer); /* must end render pass before blitting */

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

	/* special case because of the attachment description */
	renderer->fauxBackbufferColor.handle.imageResource.resourceAccessType = RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE;

	BlitFramebuffer(
		renderer,
		&renderer->fauxBackbufferColor.handle.imageResource,
		srcRect,
		renderer->swapChainImages[swapChainImageIndex],
		dstRect
	);

	SDL_LockMutex(renderer->cmdLock);

	vulkanResult = renderer->vkEndCommandBuffer(
		renderer->dataCommandBuffers[renderer->currentFrame]
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkEndCommandBuffer", vulkanResult);
		return;
	}

	vulkanResult = renderer->vkEndCommandBuffer(
		renderer->drawCommandBuffers[renderer->currentFrame]
	);

	renderer->commandBufferActive[renderer->currentFrame] = 0;

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkEndCommandBuffer", vulkanResult);
		return;
	}

	submitInfo.waitSemaphoreCount = 0;
	submitInfo.pWaitSemaphores = NULL;
	submitInfo.pWaitDstStageMask = NULL;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &renderer->dataFinishedSemaphores[renderer->currentFrame];
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &renderer->dataCommandBuffers[renderer->currentFrame];

	result = renderer->vkQueueSubmit(
		renderer->graphicsQueue,
		1,
		&submitInfo,
		NULL
	);

	if (result != VK_SUCCESS)
	{
		FNA3D_LogError("failed to submit data command buffer");
		LogVulkanResult("vkQueueSubmit", result);
		return;
	}

	submitInfo.waitSemaphoreCount = SDL_arraysize(drawWaitSemaphores);
	submitInfo.pWaitSemaphores = drawWaitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &renderer->renderFinishedSemaphores[renderer->currentFrame];
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &renderer->drawCommandBuffers[renderer->currentFrame];

	renderer->vkResetFences(
		renderer->logicalDevice,
		1,
		&renderer->inFlightFences[renderer->currentFrame]
	);

	result = renderer->vkQueueSubmit(
		renderer->graphicsQueue,
		1,
		&submitInfo,
		renderer->inFlightFences[renderer->currentFrame]
	);

	if (result != VK_SUCCESS)
	{
		FNA3D_LogError("failed to submit draw command buffer");
		LogVulkanResult("vkQueueSubmit", result);
		return;
	}

	SDL_UnlockMutex(renderer->cmdLock);

	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &renderer->renderFinishedSemaphores[renderer->currentFrame];
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &renderer->swapChain;
	presentInfo.pImageIndices = &swapChainImageIndex;
	presentInfo.pResults = NULL;

	result = renderer->vkQueuePresentKHR(
		renderer->presentQueue,
		&presentInfo
	);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
	{
		RecreateSwapchain(renderer);
		return;
	}

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkQueuePresentKHR", result);
		FNA3D_LogError("failed to present image");
	}

	renderer->currentFrame = (renderer->currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

	renderer->vkWaitForFences(
		renderer->logicalDevice,
		1,
		&renderer->inFlightFences[renderer->currentFrame],
		VK_TRUE,
		UINT64_MAX
	);

	renderer->frameInProgress[renderer->currentFrame] = 0;
}

/* Drawing */

static void RenderPassClear(
	FNAVulkanRenderer *renderer,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil,
	uint8_t clearColor,
	uint8_t clearDepth,
	uint8_t clearStencil
) {
	VkClearAttachment* clearAttachments;
	VkClearRect clearRect;
	VkClearValue clearValue = {{{
		color->x,
		color->y,
		color->z,
		color->w
	}}};
	uint32_t i, attachmentCount;

	if (!clearColor && !clearDepth && !clearStencil) { 
		return; 
	}

	attachmentCount = renderer->colorAttachmentCount;
	if (renderer->depthStencilAttachment != NULL) { 
		attachmentCount++; 
	}

	clearAttachments = SDL_stack_alloc(
		VkClearAttachment,
		attachmentCount
	);

	clearRect.baseArrayLayer = 0;
	clearRect.layerCount = 1;
	clearRect.rect.offset.x = 0;
	clearRect.rect.offset.y = 0;
	clearRect.rect.extent = renderer->colorAttachments[0]->dimensions;

	if (clearColor)
	{
		for (i = 0; i < renderer->colorAttachmentCount; i++)
		{
			clearRect.rect.extent.width = SDL_max(
				clearRect.rect.extent.width, 
				renderer->colorAttachments[i]->dimensions.width
			);
			clearRect.rect.extent.height = SDL_max(
				clearRect.rect.extent.height,
				renderer->colorAttachments[i]->dimensions.height
			);
			clearAttachments[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			clearAttachments[i].colorAttachment = i;
			clearAttachments[i].clearValue = clearValue;
		}
	}

	if (clearDepth || clearStencil)
	{
		if (renderer->depthStencilAttachment != NULL)
		{
			clearAttachments[renderer->colorAttachmentCount].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

			clearRect.rect.extent.width = SDL_max(
				clearRect.rect.extent.width,
				renderer->depthStencilAttachment->dimensions.width
			);
			clearRect.rect.extent.height = SDL_max(
				clearRect.rect.extent.height,
				renderer->depthStencilAttachment->dimensions.height
			);

			if (clearDepth)
			{
				clearAttachments[renderer->colorAttachmentCount].clearValue.depthStencil.depth = depth;
			}
			if (clearStencil)
			{
				clearAttachments[renderer->colorAttachmentCount].clearValue.depthStencil.stencil = stencil;
			}
		}
	}

	SDL_LockMutex(renderer->cmdLock);
	renderer->vkCmdClearAttachments(
		renderer->drawCommandBuffers[renderer->currentFrame],
		attachmentCount,
		clearAttachments,
		1,
		&clearRect
	);
	SDL_UnlockMutex(renderer->cmdLock);

	SDL_stack_free(clearAttachments);
}

static void OutsideRenderPassClear(
	FNAVulkanRenderer *renderer,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil,
	uint8_t clearColor,
	uint8_t clearDepth,
	uint8_t clearStencil
) {
	uint32_t i;
	ImageMemoryBarrierCreateInfo barrierCreateInfo;
	VkImageSubresourceRange subresourceRange;
	VkClearColorValue clearValue = {{
		color->x,
		color->y,
		color->z,
		color->w
	}};
	VkClearDepthStencilValue clearDepthStencilValue;
	clearDepthStencilValue.depth = depth;
	clearDepthStencilValue.stencil = stencil;

	SDL_LockMutex(renderer->cmdLock);

	if (clearColor)
	{
		for (i = 0; i < renderer->colorAttachmentCount; i++)
		{
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseArrayLayer = 0;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.layerCount = 1;
			subresourceRange.levelCount = 1;

			barrierCreateInfo.subresourceRange = subresourceRange;
			barrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrierCreateInfo.discardContents = 0;
			barrierCreateInfo.nextAccess = RESOURCE_ACCESS_TRANSFER_WRITE;

			CreateImageMemoryBarrier(
				renderer,
				renderer->drawCommandBuffers[renderer->currentFrame],
				barrierCreateInfo,
				&renderer->colorAttachments[i]->imageResource
			);

			renderer->vkCmdClearColorImage(
				renderer->drawCommandBuffers[renderer->currentFrame],
				renderer->colorAttachments[i]->imageResource.image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				&clearValue,
				1,
				&subresourceRange
			);

			barrierCreateInfo.nextAccess = RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE;

			CreateImageMemoryBarrier(
				renderer,
				renderer->drawCommandBuffers[renderer->currentFrame],
				barrierCreateInfo,
				&renderer->colorAttachments[i]->imageResource
			);
		}
	}

	if (clearDepth || clearStencil)
	{
		if (renderer->depthStencilAttachment != NULL)
		{
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			subresourceRange.baseArrayLayer = 0;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.layerCount = 1;
			subresourceRange.levelCount = 1;

			barrierCreateInfo.subresourceRange = subresourceRange;
			barrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrierCreateInfo.discardContents = 0;
			barrierCreateInfo.nextAccess = RESOURCE_ACCESS_TRANSFER_WRITE;

			CreateImageMemoryBarrier(
				renderer,
				renderer->drawCommandBuffers[renderer->currentFrame],
				barrierCreateInfo,
				&renderer->depthStencilAttachment->imageResource
			);

			renderer->vkCmdClearDepthStencilImage(
				renderer->drawCommandBuffers[renderer->currentFrame],
				renderer->depthStencilAttachment->imageResource.image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				&clearDepthStencilValue,
				1,
				&subresourceRange
			);

			barrierCreateInfo.nextAccess = RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_WRITE;

			CreateImageMemoryBarrier(
				renderer,
				renderer->drawCommandBuffers[renderer->currentFrame],
				barrierCreateInfo,
				&renderer->depthStencilAttachment->imageResource
			);
		}
	}

	SDL_UnlockMutex(renderer->cmdLock);
}

void VULKAN_Clear(
	FNA3D_Renderer *driverData,
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
) {
	/* TODO: support depth stencil clear */
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	uint8_t clearColor = (options & FNA3D_CLEAROPTIONS_TARGET) == FNA3D_CLEAROPTIONS_TARGET;
	uint8_t clearDepth = (options & FNA3D_CLEAROPTIONS_DEPTHBUFFER) == FNA3D_CLEAROPTIONS_DEPTHBUFFER;
	uint8_t clearStencil = (options & FNA3D_CLEAROPTIONS_STENCIL) == FNA3D_CLEAROPTIONS_STENCIL;

	if (renderer->needNewRenderPass)
	{
		UpdateRenderPass(renderer);
	}

	if (renderer->renderPassInProgress)
	{
		RenderPassClear(
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
		OutsideRenderPassClear(
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

void VULKAN_DrawInstancedPrimitives(
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
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanBuffer *indexBuffer = (VulkanBuffer*) indices;
	int32_t totalIndexOffset;

	indexBuffer->boundThisFrame = 1;
	totalIndexOffset = (
		(startIndex * IndexSize(indexElementSize)) +
		indexBuffer->internalOffset
	);

	CheckPrimitiveType(renderer, primitiveType);
	UpdateRenderPass(renderer);
	BindPipeline(renderer);
	BindResources(renderer);

	SDL_LockMutex(renderer->cmdLock);

	renderer->vkCmdBindIndexBuffer(
		renderer->drawCommandBuffers[renderer->currentFrame],
		indexBuffer->handle,
		totalIndexOffset,
		XNAToVK_IndexType[indexElementSize]
	);

	renderer->vkCmdDrawIndexed(
		renderer->drawCommandBuffers[renderer->currentFrame],
		PrimitiveVerts(primitiveType, primitiveCount),
		instanceCount,
		minVertexIndex,
		totalIndexOffset,
		0
	);

	SDL_UnlockMutex(renderer->cmdLock);
}

void VULKAN_DrawIndexedPrimitives(
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

void VULKAN_DrawPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;

	CheckPrimitiveType(renderer, primitiveType);
	UpdateRenderPass(renderer);
	BindPipeline(renderer);
	BindResources(renderer);

	SDL_LockMutex(renderer->cmdLock);
	renderer->vkCmdDraw(
		renderer->drawCommandBuffers[renderer->currentFrame],
		PrimitiveVerts(primitiveType, primitiveCount),
		1,
		vertexStart,
		0
	);
	SDL_UnlockMutex(renderer->cmdLock);
}

void VULKAN_DrawUserIndexedPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	void* vertexData,
	int32_t vertexOffset,
	int32_t numVertices,
	void* indexData,
	int32_t indexOffset,
	FNA3D_IndexElementSize indexElementSize,
	int32_t primitiveCount
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	int32_t numIndices, indexSize;
	uint32_t firstIndex;
	VkDeviceSize len;

	CheckPrimitiveType(renderer, primitiveType);
	UpdateRenderPass(renderer);
	BindPipeline(renderer);
	BindResources(renderer);

	BindUserVertexBuffer(
		renderer,
		vertexData,
		numVertices,
		vertexOffset
	);

	numIndices = PrimitiveVerts(primitiveType, primitiveCount);
	indexSize = IndexSize(indexElementSize);
	len = numIndices * indexSize;

	if (renderer->userIndexBuffer == NULL)
	{
		renderer->userIndexBuffer = CreateBuffer(
			renderer,
			FNA3D_BUFFERUSAGE_WRITEONLY,
			len,
			RESOURCE_ACCESS_INDEX_BUFFER
		);
	}

	SetUserBufferData(
		renderer,
		renderer->userIndexBuffer,
		indexOffset * indexSize,
		indexData,
		len
	);

	SDL_LockMutex(renderer->cmdLock);

	renderer->vkCmdBindIndexBuffer(
		renderer->drawCommandBuffers[renderer->currentFrame],
		renderer->userIndexBuffer->handle,
		renderer->userIndexBuffer->internalOffset,
		XNAToVK_IndexType[indexElementSize]
	);

	firstIndex = indexOffset / indexSize;
	
	renderer->vkCmdDrawIndexed(
		renderer->drawCommandBuffers[renderer->currentFrame],
		numIndices,
		1,
		firstIndex,
		vertexOffset,
		0
	);

	SDL_UnlockMutex(renderer->cmdLock);
}

void VULKAN_DrawUserPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	void* vertexData,
	int32_t vertexOffset,
	int32_t primitiveCount
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;

	int32_t numVerts = PrimitiveVerts(
		primitiveType,
		primitiveCount
	);

	CheckPrimitiveType(renderer, primitiveType);
	UpdateRenderPass(renderer);
	BindPipeline(renderer);
	BindResources(renderer);

	BindUserVertexBuffer(
		renderer,
		vertexData,
		numVerts,
		vertexOffset
	);

	SDL_LockMutex(renderer->cmdLock);
	renderer->vkCmdDraw(
		renderer->drawCommandBuffers[renderer->currentFrame],
		numVerts,
		1,
		vertexOffset,
		0
	);
	SDL_UnlockMutex(renderer->cmdLock);
}

/* Mutable Render States */

void VULKAN_SetViewport(
	FNA3D_Renderer *driverData,
	FNA3D_Viewport *viewport
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;

	if (	viewport->x != renderer->viewport.x ||
			viewport->y != renderer->viewport.y ||
			viewport->w != renderer->viewport.w ||
			viewport->h != renderer->viewport.h ||
			viewport->minDepth != renderer->viewport.minDepth ||
			viewport->maxDepth != renderer->viewport.maxDepth	)
	{
		renderer->viewport = *viewport;
		SetViewportCommand(renderer);
	}
}

void VULKAN_SetScissorRect(
	FNA3D_Renderer *driverData,
	FNA3D_Rect *scissor
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;

	if (	scissor->x != renderer->scissorRect.x ||
			scissor->y != renderer->scissorRect.y ||
			scissor->w != renderer->scissorRect.w ||
			scissor->h != renderer->scissorRect.h	)
	{
		renderer->scissorRect = *scissor;
		SetScissorRectCommand(renderer);
	}
}

void VULKAN_GetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	SDL_memcpy(blendFactor, &renderer->blendState.blendFactor, sizeof(FNA3D_Color));
}

void VULKAN_SetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
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

		if (renderer->frameInProgress[renderer->currentFrame])
		{
			SDL_LockMutex(renderer->cmdLock);
			renderer->vkCmdSetBlendConstants(
				renderer->drawCommandBuffers[renderer->currentFrame],
				blendConstants
			);
			SDL_UnlockMutex(renderer->cmdLock);
		}
	}
}

int32_t VULKAN_GetMultiSampleMask(FNA3D_Renderer *driverData)
{
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	return renderer->multiSampleMask[0];
}

void VULKAN_SetMultiSampleMask(FNA3D_Renderer *driverData, int32_t mask)
{
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	if (renderer->debugMode && renderer->rasterizerState.multiSampleAntiAlias > 32) {
		FNA3D_LogWarn(
			"Using a 32-bit multisample mask for a 64-sample rasterizer. "
			"Last 32 bits of the mask will all be 1."
		);
	}
	if (renderer->multiSampleMask[0] != mask)
	{
		renderer->multiSampleMask[0] = mask;
	}
}

int32_t VULKAN_GetReferenceStencil(FNA3D_Renderer *driverData)
{
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	return renderer->stencilRef;
}

void VULKAN_SetReferenceStencil(FNA3D_Renderer *driverData, int32_t ref)
{
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	if (renderer->stencilRef != ref)
	{
		renderer->stencilRef = ref;
		SetStencilReferenceValueCommand(renderer);
	}
}

/* Immutable Render States */

void VULKAN_SetBlendState(
	FNA3D_Renderer *driverData,
	FNA3D_BlendState *blendState
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	const float blendConstants[] =
	{
		blendState->blendFactor.r,
		blendState->blendFactor.g,
		blendState->blendFactor.b,
		blendState->blendFactor.a
	};

	SDL_memcpy(&renderer->blendState, blendState, sizeof(FNA3D_BlendState));

	/* Dynamic state */
	if (renderer->frameInProgress[renderer->currentFrame])
	{
		SDL_LockMutex(renderer->cmdLock);
		renderer->vkCmdSetBlendConstants(
			renderer->drawCommandBuffers[renderer->currentFrame],
			blendConstants
		);
		SDL_UnlockMutex(renderer->cmdLock);
	}
}

void VULKAN_SetDepthStencilState(
	FNA3D_Renderer *driverData,
	FNA3D_DepthStencilState *depthStencilState
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	SDL_memcpy(&renderer->depthStencilState, depthStencilState, sizeof(FNA3D_DepthStencilState));
}

void VULKAN_ApplyRasterizerState(
	FNA3D_Renderer *driverData,
	FNA3D_RasterizerState *rasterizerState
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	float realDepthBias;

	if (rasterizerState->scissorTestEnable != renderer->rasterizerState.scissorTestEnable)
	{
		renderer->rasterizerState.scissorTestEnable = rasterizerState->scissorTestEnable;
		SetScissorRectCommand(renderer);
	}

	realDepthBias = rasterizerState->depthBias * XNAToVK_DepthBiasScale[
		renderer->currentDepthFormat
	];
	
	if (	realDepthBias != renderer->rasterizerState.depthBias ||
			rasterizerState->slopeScaleDepthBias != renderer->rasterizerState.slopeScaleDepthBias	)
	{
		renderer->rasterizerState.depthBias = realDepthBias;
		renderer->rasterizerState.slopeScaleDepthBias = rasterizerState->slopeScaleDepthBias;
		SetDepthBiasCommand(renderer);
	}

	if (	rasterizerState->cullMode != renderer->rasterizerState.cullMode ||
			rasterizerState->fillMode != renderer->rasterizerState.fillMode ||
			rasterizerState->multiSampleAntiAlias != renderer->rasterizerState.multiSampleAntiAlias	)
	{
		renderer->rasterizerState.cullMode = rasterizerState->cullMode;
		renderer->rasterizerState.fillMode = rasterizerState->fillMode;
		renderer->rasterizerState.multiSampleAntiAlias = rasterizerState->multiSampleAntiAlias;
	}
}

void VULKAN_VerifySampler(
	FNA3D_Renderer *driverData,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanTexture *vulkanTexture = (VulkanTexture*) texture;
	VkSampler vkSamplerState;
	uint32_t texArrayOffset = (renderer->currentFrame * MAX_TOTAL_SAMPLERS);
	uint32_t textureIndex = texArrayOffset + index;
	ImageMemoryBarrierCreateInfo memoryBarrierCreateInfo;

	if (texture == NULL)
	{
		if (renderer->textures[textureIndex] != &NullTexture)
		{
			renderer->textures[textureIndex] = &NullTexture;
			renderer->textureNeedsUpdate[textureIndex] = 1;
		}

		if (renderer->samplers[textureIndex] == NULL)
		{
			VkSampler samplerState = FetchSamplerState(
				renderer,
				sampler,
				0
			);

			renderer->samplers[textureIndex] = samplerState;
			renderer->samplerNeedsUpdate[textureIndex] = 1;
		}

		return;
	}

	memoryBarrierCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	memoryBarrierCreateInfo.subresourceRange.baseArrayLayer = 0;
	memoryBarrierCreateInfo.subresourceRange.baseMipLevel = 0;
	memoryBarrierCreateInfo.subresourceRange.layerCount = 1;
	memoryBarrierCreateInfo.subresourceRange.levelCount = 1;
	memoryBarrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrierCreateInfo.discardContents = 0;
	memoryBarrierCreateInfo.nextAccess = RESOURCE_ACCESS_FRAGMENT_SHADER_READ_SAMPLED_IMAGE;

	CreateImageMemoryBarrier(
		renderer,
		renderer->drawCommandBuffers[renderer->currentFrame],
		memoryBarrierCreateInfo,
		&vulkanTexture->imageData->imageResource
	);

	if (vulkanTexture != renderer->textures[textureIndex])
	{
		renderer->textures[textureIndex] = vulkanTexture;
		renderer->textureNeedsUpdate[textureIndex] = 1;
	}

	vkSamplerState = FetchSamplerState(
		renderer,
		sampler,
		vulkanTexture->hasMipmaps
	);

	if (vkSamplerState != renderer->samplers[textureIndex])
	{
		renderer->samplers[textureIndex] = vkSamplerState;
		renderer->samplerNeedsUpdate[textureIndex] = 1;
	}
}

void VULKAN_VerifyVertexSampler(
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

/* Vertex State */

void VULKAN_ApplyVertexBufferBindings(
	FNA3D_Renderer *driverData,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings,
	uint8_t bindingsUpdated,
	int32_t baseVertex
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanBuffer* vertexBuffer;
	VkDeviceSize offset;
	VkBuffer buffers[MAX_BOUND_VERTEX_BUFFERS];
	VkDeviceSize offsets[MAX_BOUND_VERTEX_BUFFERS];
	uint32_t i, firstVertexBufferIndex, vertexBufferIndex, bufferCount;
	uint8_t needsRebind = 0;

	CheckVertexBufferBindings(
		renderer,
		bindings,
		numBindings
	);

	firstVertexBufferIndex = MAX_BOUND_VERTEX_BUFFERS * renderer->currentFrame;
	bufferCount = 0;
	
	for (i = 0; i < numBindings; i++)
	{
		vertexBufferIndex = firstVertexBufferIndex + i;

		vertexBuffer = (VulkanBuffer*) bindings[i].vertexBuffer;
		if (vertexBuffer == NULL)
		{
			buffers[bufferCount] = NULL;
			offsets[bufferCount] = 0;
			bufferCount++;

			if (renderer->ldVertexBuffers[vertexBufferIndex] != NULL)
			{
				renderer->ldVertexBuffers[vertexBufferIndex] = NULL;
				needsRebind = 1;
			}
			continue;
		}

		offset = vertexBuffer->internalOffset + (
			(bindings[i].vertexOffset + baseVertex) *
			bindings[i].vertexDeclaration.vertexStride
		);

		vertexBuffer->boundThisFrame = 1;
		if (	renderer->ldVertexBuffers[vertexBufferIndex] != vertexBuffer->handle ||
				renderer->ldVertexBufferOffsets[vertexBufferIndex] != offset	)
		{
			renderer->ldVertexBuffers[vertexBufferIndex] = vertexBuffer->handle;
			renderer->ldVertexBufferOffsets[vertexBufferIndex] = offset;
			needsRebind = 1;
		}

		buffers[bufferCount] = vertexBuffer->handle;
		offsets[bufferCount] = offset;
		bufferCount++;
	}

	if (needsRebind)
	{
		SDL_LockMutex(renderer->cmdLock);
		renderer->vkCmdBindVertexBuffers(
			renderer->drawCommandBuffers[renderer->currentFrame],
			0,
			bufferCount,
			buffers,
			offsets
		);
		SDL_UnlockMutex(renderer->cmdLock);
	}
}

void VULKAN_ApplyVertexDeclaration(
	FNA3D_Renderer *driverData,
	FNA3D_VertexDeclaration *vertexDeclaration,
	void* ptr,
	int32_t vertexOffset
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;

	CheckVertexDeclaration(renderer,vertexDeclaration);
	renderer->userVertexStride = vertexDeclaration->vertexStride;
}

/* Render Targets */

static void UpdateRenderPass(
	FNAVulkanRenderer *renderer
) {
	if (!renderer->frameInProgress[renderer->currentFrame] || !renderer->needNewRenderPass) { return; }

	VULKAN_BeginFrame((FNA3D_Renderer*) renderer);

	if (renderer->renderPassInProgress)
	{
		EndPass(renderer);
	}

	/* TODO: optimize this to pick a render pass with a LOAD_OP_CLEAR */

	BeginRenderPass(renderer);
	renderer->needNewRenderPass = 0;
}

static void PerformDeferredDestroys(
	FNAVulkanRenderer *renderer
) {
	uint32_t i;

	for (i = 0; i < renderer->renderbuffersToDestroyCount[renderer->currentFrame]; i++)
	{
		DestroyRenderbuffer(renderer, renderer->renderbuffersToDestroy[renderer->currentFrame][i]);
	}
	renderer->renderbuffersToDestroyCount[renderer->currentFrame] = 0;

	for (i = 0; i < renderer->buffersToDestroyCount[renderer->currentFrame]; i++)
	{
		DestroyBuffer(renderer, renderer->buffersToDestroy[renderer->currentFrame][i]);
	}
	renderer->buffersToDestroyCount[renderer->currentFrame] = 0;

	for (i = 0; i < renderer->bufferMemoryWrappersToDestroyCount[renderer->currentFrame]; i++)
	{
		DestroyBufferAndMemory(renderer, renderer->bufferMemoryWrappersToDestroy[renderer->currentFrame][i]);
	}
	renderer->bufferMemoryWrappersToDestroyCount[renderer->currentFrame] = 0;

	for (i = 0; i < renderer->effectsToDestroyCount[renderer->currentFrame]; i++)
	{
		DestroyEffect(renderer, renderer->effectsToDestroy[renderer->currentFrame][i]);
	}
	renderer->effectsToDestroyCount[renderer->currentFrame] = 0;

	for (i = 0; i < renderer->imageDatasToDestroyCount[renderer->currentFrame]; i++)
	{
		DestroyImageData(renderer, renderer->imageDatasToDestroy[renderer->currentFrame][i]);
	}
	renderer->imageDatasToDestroyCount[renderer->currentFrame] = 0;

	MOJOSHADER_vkFreeBuffers();

	/* rotate */

	if (renderer->queuedRenderbuffersToDestroyCount > renderer->renderbuffersToDestroyCapacity[renderer->currentFrame])
	{
		renderer->renderbuffersToDestroyCapacity[renderer->currentFrame] = renderer->queuedRenderbuffersToDestroyCount;

		renderer->renderbuffersToDestroy[renderer->currentFrame] = SDL_realloc(
			renderer->renderbuffersToDestroy[renderer->currentFrame],
			sizeof(VulkanRenderbuffer*) * renderer->renderbuffersToDestroyCapacity[renderer->currentFrame]
		);
	}

	for (i = 0; i < renderer->queuedRenderbuffersToDestroyCount; i++)
	{
		renderer->renderbuffersToDestroy[renderer->currentFrame][i] = renderer->queuedRenderbuffersToDestroy[i];
	}
	renderer->renderbuffersToDestroyCount[renderer->currentFrame] = renderer->queuedRenderbuffersToDestroyCount;
	renderer->queuedRenderbuffersToDestroyCount = 0;

	if (renderer->queuedBuffersToDestroyCount > renderer->buffersToDestroyCapacity[renderer->currentFrame])
	{
		renderer->buffersToDestroyCapacity[renderer->currentFrame] = renderer->queuedBuffersToDestroyCount;

		renderer->buffersToDestroy[renderer->currentFrame] = SDL_realloc(
			renderer->buffersToDestroy[renderer->currentFrame],
			sizeof(VulkanBuffer*) * renderer->buffersToDestroyCapacity[renderer->currentFrame]
		);
	}

	for (i = 0; i < renderer->queuedBuffersToDestroyCount; i++)
	{
		renderer->buffersToDestroy[renderer->currentFrame][i] = renderer->queuedBuffersToDestroy[i];
	}
	renderer->buffersToDestroyCount[renderer->currentFrame] = renderer->queuedBuffersToDestroyCount;
	renderer->queuedBuffersToDestroyCount = 0;
	
	if (renderer->queuedBufferMemoryWrappersToDestroyCount > renderer->bufferMemoryWrappersToDestroyCapacity[renderer->currentFrame])
	{
		renderer->bufferMemoryWrappersToDestroyCapacity[renderer->currentFrame] = renderer->queuedBufferMemoryWrappersToDestroyCount;

		renderer->bufferMemoryWrappersToDestroy[renderer->currentFrame] = SDL_realloc(
			renderer->bufferMemoryWrappersToDestroy[renderer->currentFrame],
			sizeof(BufferMemoryWrapper*) * renderer->bufferMemoryWrappersToDestroyCapacity[renderer->currentFrame]
		);
	}

	for (i = 0; i < renderer->queuedBufferMemoryWrappersToDestroyCount; i++)
	{
		renderer->bufferMemoryWrappersToDestroy[renderer->currentFrame][i] = renderer->queuedBufferMemoryWrappersToDestroy[i];
	}
	renderer->bufferMemoryWrappersToDestroyCount[renderer->currentFrame] = renderer->queuedBufferMemoryWrappersToDestroyCount;
	renderer->queuedBufferMemoryWrappersToDestroyCount = 0;

	if (renderer->queuedEffectsToDestroyCount > renderer->effectsToDestroyCapacity[renderer->currentFrame])
	{
		renderer->effectsToDestroyCapacity[renderer->currentFrame] = renderer->queuedEffectsToDestroyCount;

		renderer->effectsToDestroy[renderer->currentFrame] = SDL_realloc(
			renderer->effectsToDestroy[renderer->currentFrame],
			sizeof(VulkanEffect*) * renderer->effectsToDestroyCapacity[renderer->currentFrame]
		);
	}

	for (i = 0; i < renderer->queuedEffectsToDestroyCount; i++)
	{
		renderer->effectsToDestroy[renderer->currentFrame][i] = renderer->queuedEffectsToDestroy[i];
	}
	renderer->effectsToDestroyCount[renderer->currentFrame] = renderer->queuedEffectsToDestroyCount;
	renderer->queuedEffectsToDestroyCount = 0;

	if (renderer->queuedImageDatasToDestroyCount > renderer->imageDatasToDestroyCapacity[renderer->currentFrame])
	{
		renderer->imageDatasToDestroyCapacity[renderer->currentFrame] = renderer->queuedImageDatasToDestroyCount;

		renderer->imageDatasToDestroy[renderer->currentFrame] = SDL_realloc(
			renderer->imageDatasToDestroy[renderer->currentFrame],
			sizeof(FNAVulkanImageData*) * renderer->imageDatasToDestroyCapacity[renderer->currentFrame]
		);
	}

	for (i = 0; i < renderer->queuedImageDatasToDestroyCount; i++)
	{
		renderer->imageDatasToDestroy[renderer->currentFrame][i] = renderer->queuedImageDatasToDestroy[i];
	}
	renderer->imageDatasToDestroyCount[renderer->currentFrame] = renderer->queuedImageDatasToDestroyCount;
	renderer->queuedImageDatasToDestroyCount = 0;
}

static void DestroyBuffer(
	FNAVulkanRenderer *renderer,
	VulkanBuffer *buffer
) {
	VulkanBuffer *curr, *prev;

	renderer->vkDestroyBuffer(
		renderer->logicalDevice,
		buffer->handle,
		NULL
	);

	renderer->vkFreeMemory(
		renderer->logicalDevice,
		buffer->deviceMemory,
		NULL
	);

	LinkedList_Remove(
		renderer->buffers,
		buffer,
		curr,
		prev
	);

	SDL_free(buffer);
}

static void DestroyBufferAndMemory(
	FNAVulkanRenderer *renderer,
	BufferMemoryWrapper *bufferMemoryWrapper
) {
	renderer->vkDestroyBuffer(
		renderer->logicalDevice,
		bufferMemoryWrapper->buffer,
		NULL
	);

	renderer->vkFreeMemory(
		renderer->logicalDevice,
		bufferMemoryWrapper->deviceMemory,
		NULL
	);

	SDL_free(bufferMemoryWrapper);
}

static void EndPass(
	FNAVulkanRenderer *renderer
) {
	if (renderer->renderPassInProgress)
	{
		SDL_LockMutex(renderer->cmdLock);
		renderer->vkCmdEndRenderPass(
			renderer->drawCommandBuffers[renderer->currentFrame]
		);
		SDL_UnlockMutex(renderer->cmdLock);

		renderer->renderPassInProgress = 0;
	}
}

void VULKAN_SetRenderTargets(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *depthStencilBuffer,
	FNA3D_DepthFormat depthFormat
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanColorBuffer *cb;
	VulkanTexture *tex;
	uint32_t i;
	ImageMemoryBarrierCreateInfo imageMemoryBarrierCreateInfo;

	for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i++)
	{
		renderer->colorAttachments[i] = NULL;
	}
	renderer->depthStencilAttachment = NULL;

	if (renderTargets == NULL)
	{
		renderer->colorAttachments[0] = &renderer->fauxBackbufferColor.handle;
		renderer->colorAttachmentCount = 1;
		if (renderer->fauxBackbufferDepthFormat != FNA3D_DEPTHFORMAT_NONE)
		{
			renderer->depthStencilAttachment = &renderer->fauxBackbufferDepthStencil.handle;
		}
		renderer->renderTargetBound = 0;
	}
	else
	{
		for (i = 0; i < numRenderTargets; i++)
		{
			if (renderTargets[i].colorBuffer != NULL)
			{
				cb = (VulkanColorBuffer*) renderTargets[i].colorBuffer;
				renderer->colorAttachments[i] = &cb->handle;
			}
			else
			{
				tex = (VulkanTexture*) renderTargets[i].texture;
				renderer->colorAttachments[i] = tex->imageData;
			}

			imageMemoryBarrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrierCreateInfo.discardContents = 0;
			imageMemoryBarrierCreateInfo.nextAccess = RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE;
			imageMemoryBarrierCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; /* FIXME: depth too? */
			imageMemoryBarrierCreateInfo.subresourceRange.baseArrayLayer = 0;
			imageMemoryBarrierCreateInfo.subresourceRange.baseMipLevel = 0;
			imageMemoryBarrierCreateInfo.subresourceRange.layerCount = 1;
			imageMemoryBarrierCreateInfo.subresourceRange.levelCount = 1;

			CreateImageMemoryBarrier(
				renderer,
				renderer->drawCommandBuffers[renderer->currentFrame],
				imageMemoryBarrierCreateInfo,
				&renderer->colorAttachments[i]->imageResource
			);
		}

		renderer->colorAttachmentCount = numRenderTargets;
		
		/* update depth stencil buffer */

		if (depthStencilBuffer != NULL)
		{
			renderer->depthStencilAttachment = &((VulkanDepthStencilBuffer*) depthStencilBuffer)->handle;
			renderer->currentDepthFormat = depthFormat;
		}
		renderer->renderTargetBound = 1;
	}

	renderer->needNewRenderPass = 1;
}

/* Dynamic State Functions */

static void SetDepthBiasCommand(FNAVulkanRenderer *renderer)
{
	if (renderer->renderPassInProgress)
	{
		SDL_LockMutex(renderer->cmdLock);
		renderer->vkCmdSetDepthBias(
			renderer->drawCommandBuffers[renderer->currentFrame],
			renderer->rasterizerState.depthBias,
			0.0, /* no clamp */
			renderer->rasterizerState.slopeScaleDepthBias
		);
		SDL_UnlockMutex(renderer->cmdLock);
	}
}

static void SetScissorRectCommand(FNAVulkanRenderer *renderer)
{
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

		SDL_LockMutex(renderer->cmdLock);
		renderer->vkCmdSetScissor(
			renderer->drawCommandBuffers[renderer->currentFrame],
			0,
			1,
			&vulkanScissorRect
		);
		SDL_UnlockMutex(renderer->cmdLock);
	}
}

static void SetStencilReferenceValueCommand(
	FNAVulkanRenderer *renderer
) {
	if (renderer->renderPassInProgress)
	{
		SDL_LockMutex(renderer->cmdLock);
		renderer->vkCmdSetStencilReference(
			renderer->drawCommandBuffers[renderer->currentFrame],
			VK_STENCIL_FACE_FRONT_AND_BACK,
			renderer->stencilRef
		);
		SDL_UnlockMutex(renderer->cmdLock);
	}
}

static void SetViewportCommand(
	FNAVulkanRenderer *renderer
) {
	int32_t targetHeight, meh;
	VkViewport vulkanViewport;

	/* v-flipping the viewport for compatibility with other APIs -cosmonaut */
	vulkanViewport.x = renderer->viewport.x;
	if (!renderer->renderTargetBound)
	{
		VULKAN_GetBackbufferSize((FNA3D_Renderer*)renderer, &meh, &targetHeight);
	}
	else
	{
		targetHeight = renderer->viewport.h;
	}

	vulkanViewport.y = targetHeight - renderer->viewport.y;
	vulkanViewport.width = renderer->viewport.w;
	vulkanViewport.height = -renderer->viewport.h;
	vulkanViewport.minDepth = renderer->viewport.minDepth;
	vulkanViewport.maxDepth = renderer->viewport.maxDepth;

	if (renderer->frameInProgress[renderer->currentFrame])
	{
		SDL_LockMutex(renderer->cmdLock);
		renderer->vkCmdSetViewport(
			renderer->drawCommandBuffers[renderer->currentFrame],
			0,
			1,
			&vulkanViewport
		);
		SDL_UnlockMutex(renderer->cmdLock);
	}
}

static void Stall(FNAVulkanRenderer *renderer)
{
	VkSubmitInfo submitInfo = {
		VK_STRUCTURE_TYPE_SUBMIT_INFO
	};
	VkCommandBufferBeginInfo beginInfo = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
	};
	VkPipelineStageFlags waitStages[] = {
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
	};
	VkResult result;
	VulkanBuffer *buf;

	EndPass(renderer);

	SDL_LockMutex(renderer->cmdLock);

	renderer->vkEndCommandBuffer(
		renderer->dataCommandBuffers[renderer->currentFrame]
	);

	renderer->vkEndCommandBuffer(
		renderer->drawCommandBuffers[renderer->currentFrame]
	);

	submitInfo.waitSemaphoreCount = 0;
	submitInfo.pWaitSemaphores = NULL;
	submitInfo.pWaitDstStageMask = NULL;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &renderer->dataFinishedSemaphores[renderer->currentFrame];
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &renderer->dataCommandBuffers[renderer->currentFrame];

	result = renderer->vkQueueSubmit(
		renderer->graphicsQueue,
		1,
		&submitInfo,
		NULL
	);

	if (result != VK_SUCCESS)
	{
		FNA3D_LogError("failed to submit data command buffer");
		LogVulkanResult("vkQueueSubmit", result);
		return;
	}

	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &renderer->dataFinishedSemaphores[renderer->currentFrame];
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.signalSemaphoreCount = 0;
	submitInfo.pSignalSemaphores = NULL;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &renderer->drawCommandBuffers[renderer->currentFrame];

	renderer->vkResetFences(
		renderer->logicalDevice,
		1,
		&renderer->inFlightFences[renderer->currentFrame]
	);

	result = renderer->vkQueueSubmit(
		renderer->graphicsQueue,
		1,
		&submitInfo,
		renderer->inFlightFences[renderer->currentFrame]
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkQueueSubmit", result);
		return;
	}

	result = renderer->vkWaitForFences(
		renderer->logicalDevice,
		1,
		&renderer->inFlightFences[renderer->currentFrame],
		VK_TRUE,
		UINT64_MAX
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkWaitForFences", result);
		return;
	}

	renderer->vkResetFences(
		renderer->logicalDevice,
		1,
		&renderer->inFlightFences[renderer->currentFrame]
	);

	renderer->needNewRenderPass = 1;

	buf = renderer->buffers;
	while (buf != NULL)
	{
		buf->internalOffset = 0;
		buf->boundThisFrame = 0;
		buf->prevDataLength = 0;
		buf = buf->next;
	}

	renderer->vkResetCommandBuffer(
		renderer->dataCommandBuffers[renderer->currentFrame],
		VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT
	);

	renderer->vkResetCommandBuffer(
		renderer->drawCommandBuffers[renderer->currentFrame],
		VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT
	);

	renderer->vkBeginCommandBuffer(
		renderer->dataCommandBuffers[renderer->currentFrame],
		&beginInfo
	);

	renderer->vkBeginCommandBuffer(
		renderer->drawCommandBuffers[renderer->currentFrame],
		&beginInfo
	);

	SDL_UnlockMutex(renderer->cmdLock);
}

void VULKAN_ResolveTarget(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *target
) {
	/* TODO */
}

/* Backbuffer Functions */

static void DestroySwapchain(FNAVulkanRenderer *renderer)
{
	uint32_t i;

	for (i = 0; i < hmlen(renderer->framebufferHashMap); i++)
	{
		renderer->vkDestroyFramebuffer(
			renderer->logicalDevice,
			renderer->framebufferHashMap[i].value,
			NULL
		);
	}
	hmfree(renderer->framebufferHashMap);

	for (i = 0; i < renderer->swapChainImageCount; i++)
	{
		renderer->vkDestroyImageView(
			renderer->logicalDevice,
			renderer->swapChainImageViews[i],
			NULL
		);

		SDL_free(renderer->swapChainImages[i]);
	}

	renderer->vkDestroySwapchainKHR(
		renderer->logicalDevice,
		renderer->swapChain,
		NULL
	);
}

static void DestroyFauxBackbuffer(FNAVulkanRenderer *renderer)
{
	renderer->vkDestroyFramebuffer(
		renderer->logicalDevice,
		renderer->fauxBackbufferFramebuffer,
		NULL
	);

	renderer->vkDestroyImageView(
		renderer->logicalDevice,
		renderer->fauxBackbufferColor.handle.view,
		NULL
	);

	renderer->vkDestroyImage(
		renderer->logicalDevice,
		renderer->fauxBackbufferColor.handle.imageResource.image,
		NULL
	);

	renderer->vkFreeMemory(
		renderer->logicalDevice,
		renderer->fauxBackbufferColor.handle.memory,
		NULL
	);

	renderer->vkDestroyImageView(
		renderer->logicalDevice,
		renderer->fauxBackbufferDepthStencil.handle.view,
		NULL
	);

	renderer->vkDestroyImage(
		renderer->logicalDevice,
		renderer->fauxBackbufferDepthStencil.handle.imageResource.image,
		NULL
	);

	renderer->vkFreeMemory(
		renderer->logicalDevice,
		renderer->fauxBackbufferDepthStencil.handle.memory,
		NULL
	);
}

void VULKAN_ResetBackbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_PresentationParameters *presentationParameters
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	renderer->presentInterval = presentationParameters->presentationInterval;
	renderer->deviceWindowHandle = presentationParameters->deviceWindowHandle;

	RecreateSwapchain(renderer);
	DestroyFauxBackbuffer(renderer);
	CreateFauxBackbuffer(
		renderer,
		presentationParameters
	);
}

void VULKAN_ReadBackbuffer(
	FNA3D_Renderer *driverData,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	void* data,
	int32_t dataLen
) {
	/* TODO */
}

void VULKAN_GetBackbufferSize(
	FNA3D_Renderer *driverData,
	int32_t *w,
	int32_t *h
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	*w = renderer->fauxBackbufferWidth;
	*h = renderer->fauxBackbufferHeight;
}

FNA3D_SurfaceFormat VULKAN_GetBackbufferSurfaceFormat(
	FNA3D_Renderer *driverData
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	return renderer->fauxBackbufferSurfaceFormat;
}

FNA3D_DepthFormat VULKAN_GetBackbufferDepthFormat(FNA3D_Renderer *driverData)
{
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	return renderer->fauxBackbufferDepthFormat;
}

int32_t VULKAN_GetBackbufferMultiSampleCount(FNA3D_Renderer *driverData)
{
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	return renderer->fauxBackbufferMultisampleCount;
}

/* Textures */

FNA3D_Texture* VULKAN_CreateTexture2D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;

	return (FNA3D_Texture*) CreateTexture(
		renderer,
		format,
		width,
		height,
		levelCount,
		isRenderTarget,
		VK_IMAGE_TYPE_2D
	);
}

FNA3D_Texture* VULKAN_CreateTexture3D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t depth,
	int32_t levelCount
) {
	/* TODO */
	return NULL;
}

FNA3D_Texture* VULKAN_CreateTextureCube(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t size,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	/* TODO */
	return NULL;
}

void VULKAN_AddDisposeTexture(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanTexture *vulkanTexture = (VulkanTexture*) texture;
	uint32_t texArrayOffset = (renderer->currentFrame * MAX_TOTAL_SAMPLERS);
	uint32_t i;

	for (i = 0; i < renderer->colorAttachmentCount; i++)
	{
		if (vulkanTexture->imageData->view == renderer->colorAttachments[i]->view)
		{
			renderer->colorAttachments[i] = NULL;
		}
	}

	for (i = texArrayOffset; i < texArrayOffset + TEXTURE_COUNT; i++)
	{
		if (vulkanTexture == renderer->textures[i])
		{
			renderer->textures[i] = &NullTexture;
			renderer->textureNeedsUpdate[i] = 1;
		}
	}

	QueueImageDestroy(renderer, vulkanTexture->imageData);
	QueueBufferDestroy(renderer, vulkanTexture->stagingBuffer);
	vulkanTexture->imageData = NULL;
	vulkanTexture->stagingBuffer = NULL;
	SDL_free(vulkanTexture);
}

void VULKAN_SetTextureData2D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanTexture *vulkanTexture = (VulkanTexture*) texture;
	VulkanBuffer *stagingBuffer = vulkanTexture->stagingBuffer;
	void *stagingData;
	ImageMemoryBarrierCreateInfo imageBarrierCreateInfo;
	VkBufferImageCopy imageCopy;

	VULKAN_BeginFrame(driverData);

	renderer->vkMapMemory(
		renderer->logicalDevice,
		stagingBuffer->deviceMemory,
		stagingBuffer->internalOffset,
		stagingBuffer->size,
		0,
		&stagingData
	);

	SDL_memcpy(stagingData, data, dataLength);

	renderer->vkUnmapMemory(
		renderer->logicalDevice,
		stagingBuffer->deviceMemory
	);
	
	imageBarrierCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageBarrierCreateInfo.subresourceRange.baseArrayLayer = 0;
	imageBarrierCreateInfo.subresourceRange.baseMipLevel = 0;
	imageBarrierCreateInfo.subresourceRange.layerCount = 1;
	imageBarrierCreateInfo.subresourceRange.levelCount = 1;
	imageBarrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarrierCreateInfo.discardContents = 0;
	imageBarrierCreateInfo.nextAccess = RESOURCE_ACCESS_TRANSFER_WRITE;

	CreateImageMemoryBarrier(
		renderer,
		renderer->dataCommandBuffers[renderer->currentFrame],
		imageBarrierCreateInfo,
		&vulkanTexture->imageData->imageResource
	);

	CreateBufferMemoryBarrier(
		renderer,
		renderer->dataCommandBuffers[renderer->currentFrame],
		RESOURCE_ACCESS_TRANSFER_READ,
		stagingBuffer
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
	imageCopy.imageSubresource.mipLevel = 0;
	imageCopy.bufferOffset = 0;
	imageCopy.bufferRowLength = w;
	imageCopy.bufferImageHeight = h;

	SDL_LockMutex(renderer->cmdLock);
	renderer->vkCmdCopyBufferToImage(
		renderer->dataCommandBuffers[renderer->currentFrame],
		stagingBuffer->handle,
		vulkanTexture->imageData->imageResource.image,
		AccessMap[vulkanTexture->imageData->imageResource.resourceAccessType].imageLayout,
		1,
		&imageCopy
	);
	SDL_UnlockMutex(renderer->cmdLock);
}

void VULKAN_SetTextureData3D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t level,
	int32_t left,
	int32_t top,
	int32_t right,
	int32_t bottom,
	int32_t front,
	int32_t back,
	void* data,
	int32_t dataLength
) {
	/* TODO */
}

void VULKAN_SetTextureDataCube(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	FNA3D_CubeMapFace cubeMapFace,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	/* TODO */
}

void VULKAN_SetTextureDataYUV(
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
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanTexture *tex;
	VulkanBuffer *stagingBuffer;
	void *stagingData;
	uint8_t *dataPtr = (uint8_t*) data;
	int32_t yDataLength = BytesPerImage(yWidth, yHeight, FNA3D_SURFACEFORMAT_ALPHA8);
	int32_t uvDataLength = BytesPerImage(uvWidth, uvHeight, FNA3D_SURFACEFORMAT_ALPHA8);
	ImageMemoryBarrierCreateInfo imageBarrierCreateInfo;
	VkBufferImageCopy imageCopy;

	VULKAN_BeginFrame(driverData);

	/* Initialize values that are the same for Y, U, and V */

	imageBarrierCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageBarrierCreateInfo.subresourceRange.baseArrayLayer = 0;
	imageBarrierCreateInfo.subresourceRange.baseMipLevel = 0;
	imageBarrierCreateInfo.subresourceRange.layerCount = 1;
	imageBarrierCreateInfo.subresourceRange.levelCount = 1;
	imageBarrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarrierCreateInfo.discardContents = 0;
	imageBarrierCreateInfo.nextAccess = RESOURCE_ACCESS_TRANSFER_WRITE;

	imageCopy.imageExtent.depth = 1;
	imageCopy.imageOffset.x = 0;
	imageCopy.imageOffset.y = 0;
	imageCopy.imageOffset.z = 0;
	imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageCopy.imageSubresource.baseArrayLayer = 0;
	imageCopy.imageSubresource.layerCount = 1;
	imageCopy.imageSubresource.mipLevel = 0;
	imageCopy.bufferOffset = 0;

	SDL_LockMutex(renderer->cmdLock);

	/* Y */

	tex = (VulkanTexture*) y;
	stagingBuffer = tex->stagingBuffer;

	renderer->vkMapMemory(
		renderer->logicalDevice,
		stagingBuffer->deviceMemory,
		stagingBuffer->internalOffset,
		stagingBuffer->size,
		0,
		&stagingData
	);

	SDL_memcpy(
		stagingData,
		dataPtr,
		yDataLength
	);

	renderer->vkUnmapMemory(
		renderer->logicalDevice,
		stagingBuffer->deviceMemory
	);

	CreateImageMemoryBarrier(
		renderer,
		renderer->dataCommandBuffers[renderer->currentFrame],
		imageBarrierCreateInfo,
		&tex->imageData->imageResource
	);

	CreateBufferMemoryBarrier(
		renderer,
		renderer->dataCommandBuffers[renderer->currentFrame],
		RESOURCE_ACCESS_TRANSFER_READ,
		stagingBuffer
	);

	imageCopy.imageExtent.width = yWidth;
	imageCopy.imageExtent.height = yHeight;
	imageCopy.bufferRowLength = BytesPerRow(yWidth, FNA3D_SURFACEFORMAT_ALPHA8);
	imageCopy.bufferImageHeight = yHeight;

	renderer->vkCmdCopyBufferToImage(
		renderer->dataCommandBuffers[renderer->currentFrame],
		stagingBuffer->handle,
		tex->imageData->imageResource.image,
		AccessMap[tex->imageData->imageResource.resourceAccessType].imageLayout,
		1,
		&imageCopy
	);

	/* These apply to both U and V */

	imageCopy.imageExtent.width = uvWidth;
	imageCopy.imageExtent.height = uvHeight;
	imageCopy.bufferRowLength = BytesPerRow(uvWidth, FNA3D_SURFACEFORMAT_ALPHA8);
	imageCopy.bufferImageHeight = uvHeight;

	/* U */

	tex = (VulkanTexture*) u;
	stagingBuffer = tex->stagingBuffer;

	renderer->vkMapMemory(
		renderer->logicalDevice,
		stagingBuffer->deviceMemory,
		stagingBuffer->internalOffset,
		stagingBuffer->size,
		0,
		&stagingData
	);

	SDL_memcpy(
		stagingData,
		dataPtr + yDataLength,
		uvDataLength
	);

	renderer->vkUnmapMemory(
		renderer->logicalDevice,
		stagingBuffer->deviceMemory
	);

	CreateImageMemoryBarrier(
		renderer,
		renderer->dataCommandBuffers[renderer->currentFrame],
		imageBarrierCreateInfo,
		&tex->imageData->imageResource
	);

	CreateBufferMemoryBarrier(
		renderer,
		renderer->dataCommandBuffers[renderer->currentFrame],
		RESOURCE_ACCESS_TRANSFER_READ,
		stagingBuffer
	);

	renderer->vkCmdCopyBufferToImage(
		renderer->dataCommandBuffers[renderer->currentFrame],
		stagingBuffer->handle,
		tex->imageData->imageResource.image,
		AccessMap[tex->imageData->imageResource.resourceAccessType].imageLayout,
		1,
		&imageCopy
	);

	/* V */

	tex = (VulkanTexture*) v;
	stagingBuffer = tex->stagingBuffer;

	renderer->vkMapMemory(
		renderer->logicalDevice,
		stagingBuffer->deviceMemory,
		stagingBuffer->internalOffset,
		stagingBuffer->size,
		0,
		&stagingData
	);

	SDL_memcpy(
		stagingData,
		dataPtr + yDataLength + uvDataLength,
		uvDataLength
	);

	renderer->vkUnmapMemory(
		renderer->logicalDevice,
		stagingBuffer->deviceMemory
	);

	CreateImageMemoryBarrier(
		renderer,
		renderer->dataCommandBuffers[renderer->currentFrame],
		imageBarrierCreateInfo,
		&tex->imageData->imageResource
	);

	CreateBufferMemoryBarrier(
		renderer,
		renderer->dataCommandBuffers[renderer->currentFrame],
		RESOURCE_ACCESS_TRANSFER_READ,
		stagingBuffer
	);

	renderer->vkCmdCopyBufferToImage(
		renderer->dataCommandBuffers[renderer->currentFrame],
		stagingBuffer->handle,
		tex->imageData->imageResource.image,
		AccessMap[tex->imageData->imageResource.resourceAccessType].imageLayout,
		1,
		&imageCopy
	);

	SDL_UnlockMutex(renderer->cmdLock);
}

void VULKAN_GetTextureData2D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	/* TODO */
}

void VULKAN_GetTextureData3D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
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
	/* TODO */
}

void VULKAN_GetTextureDataCube(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	FNA3D_CubeMapFace cubeMapFace,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	/* TODO */
}

/* Renderbuffers */

FNA3D_Renderbuffer* VULKAN_GenColorRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount,
	FNA3D_Texture *texture
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanTexture *vlkTexture = (VulkanTexture*) texture;
	SurfaceFormatMapping surfaceFormatMapping = XNAToVK_SurfaceFormat[format];
	VkImageViewCreateInfo imageViewInfo = {
		VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
	};
	VkExtent2D dimensions = {width, height};
	VulkanRenderbuffer *renderbuffer;
	VkResult result;

	imageViewInfo.image = vlkTexture->imageData->imageResource.image;
	imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imageViewInfo.format = surfaceFormatMapping.formatColor;
	imageViewInfo.components = surfaceFormatMapping.swizzle;
	imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageViewInfo.subresourceRange.baseMipLevel = 0;
	imageViewInfo.subresourceRange.levelCount = 1;
	imageViewInfo.subresourceRange.baseArrayLayer = 0;
	imageViewInfo.subresourceRange.layerCount = 1;

	/* Create and return the renderbuffer */
	renderbuffer = (VulkanRenderbuffer*) SDL_malloc(sizeof(VulkanRenderbuffer));
	renderbuffer->depthBuffer = NULL;
	renderbuffer->colorBuffer = (VulkanColorBuffer*) SDL_malloc(sizeof(VulkanColorBuffer));
	renderbuffer->colorBuffer->handle.dimensions = dimensions;
	renderbuffer->colorBuffer->handle.imageResource = vlkTexture->imageData->imageResource;

	result = renderer->vkCreateImageView(
		renderer->logicalDevice,
		&imageViewInfo,
		NULL,
		&renderbuffer->colorBuffer->handle.view
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateImageView", result);
		FNA3D_LogError("Failed to create color renderbuffer image view");
		return NULL;
	}

	return (FNA3D_Renderbuffer*) renderbuffer;
}

FNA3D_Renderbuffer* VULKAN_GenDepthStencilRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_DepthFormat format,
	int32_t multiSampleCount
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;

	VkFormat depthFormat = XNAToVK_DepthFormat(format);

	VulkanRenderbuffer *renderbuffer = (VulkanRenderbuffer*) SDL_malloc(sizeof(VulkanRenderbuffer));
	renderbuffer->colorBuffer = NULL;
	renderbuffer->depthBuffer = (VulkanDepthStencilBuffer*) SDL_malloc(sizeof(VulkanDepthStencilBuffer));

	if (
		!CreateImage(
			renderer,
			width,
			height,
			XNAToVK_SampleCount(multiSampleCount),
			depthFormat,
			IDENTITY_SWIZZLE,
			VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&renderbuffer->depthBuffer->handle
		)
	) {
		FNA3D_LogError("Failed to create depth stencil image");
		return NULL;
	}

	return (FNA3D_Renderbuffer*) renderbuffer;
}

static void DestroyRenderbuffer(
	FNAVulkanRenderer *renderer,
	VulkanRenderbuffer *renderbuffer
) {
	uint8_t isDepthStencil = (renderbuffer->colorBuffer == NULL);

	if (isDepthStencil)
	{
		renderer->vkDestroyImageView(
			renderer->logicalDevice,
			renderbuffer->depthBuffer->handle.view,
			NULL
		);

		renderer->vkDestroyImage(
			renderer->logicalDevice,
			renderbuffer->depthBuffer->handle.imageResource.image,
			NULL
		);

		renderer->vkFreeMemory(
			renderer->logicalDevice,
			renderbuffer->depthBuffer->handle.memory,
			NULL
		);

		SDL_free(renderbuffer->depthBuffer);
	}
	else
	{
		renderer->vkDestroyImageView(
			renderer->logicalDevice,
			renderbuffer->colorBuffer->handle.view,
			NULL
		);

		/* The image is owned by the texture it's from, so we don't free it here. */

		SDL_free(renderbuffer->colorBuffer);
	}

	SDL_free(renderbuffer);
}

static void QueueImageDestroy(
	FNAVulkanRenderer *renderer,
	FNAVulkanImageData *imageData
) {
	if (renderer->queuedImageDatasToDestroyCount + 1 >= renderer->queuedImageDatasToDestroyCapacity)
	{
		renderer->queuedImageDatasToDestroyCapacity *= 2;

		renderer->queuedImageDatasToDestroy = SDL_realloc(
			renderer->queuedImageDatasToDestroy,
			sizeof(FNAVulkanImageData*) * renderer->queuedImageDatasToDestroyCapacity
		);
	}

	renderer->queuedImageDatasToDestroy[renderer->queuedImageDatasToDestroyCount] = imageData;
	renderer->queuedImageDatasToDestroyCount++;
}

static void DestroyImageData(
	FNAVulkanRenderer *renderer,
	FNAVulkanImageData *imageData
) {

	renderer->vkFreeMemory(
		renderer->logicalDevice,
		imageData->memory,
		NULL
	);

	renderer->vkDestroyImageView(
		renderer->logicalDevice,
		imageData->view,
		NULL
	);

	renderer->vkDestroyImage(
		renderer->logicalDevice,
		imageData->imageResource.image,
		NULL
	);

	SDL_free(imageData);
}

void VULKAN_AddDisposeRenderbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Renderbuffer *renderbuffer
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanRenderbuffer *vlkRenderBuffer = (VulkanRenderbuffer*) renderbuffer;
	uint8_t isDepthStencil = (vlkRenderBuffer->colorBuffer == NULL);
	uint32_t i;

	if (isDepthStencil)
	{
		if (renderer->depthStencilAttachment == &vlkRenderBuffer->depthBuffer->handle)
		{
			renderer->depthStencilAttachment = NULL;
		}
	} 
	else
	{
		// Iterate through color attachments
		for (i = 0; i < MAX_RENDERTARGET_BINDINGS; ++i)
		{
			if (renderer->colorAttachments[i] == &vlkRenderBuffer->colorBuffer->handle)
			{
				renderer->colorAttachments[i] = NULL;
			}

		}
	}

	if (renderer->queuedRenderbuffersToDestroyCount + 1 >= renderer->queuedRenderbuffersToDestroyCapacity)
	{
		renderer->queuedRenderbuffersToDestroyCapacity *= 2;

		renderer->queuedRenderbuffersToDestroy = SDL_realloc(
			renderer->queuedRenderbuffersToDestroy,
			sizeof(VulkanRenderbuffer*) * renderer->queuedRenderbuffersToDestroyCapacity
		);
	}

	renderer->queuedRenderbuffersToDestroy[renderer->queuedRenderbuffersToDestroyCount] = vlkRenderBuffer;
	renderer->queuedRenderbuffersToDestroyCount++;
}

/* Vertex Buffers */

FNA3D_Buffer* VULKAN_GenVertexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t vertexCount,
	int32_t vertexStride
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;

	return (FNA3D_Buffer*) CreateBuffer(
		renderer,
		usage,
		vertexCount * vertexStride,
		RESOURCE_ACCESS_VERTEX_BUFFER
	);
}

static void QueueBufferAndMemoryDestroy(
	FNAVulkanRenderer *renderer,
	VkBuffer vkBuffer,
	VkDeviceMemory vkDeviceMemory
) {
	BufferMemoryWrapper *bufferMemoryWrapper = (BufferMemoryWrapper*) SDL_malloc(sizeof(BufferMemoryWrapper));
	bufferMemoryWrapper->buffer = vkBuffer;
	bufferMemoryWrapper->deviceMemory = vkDeviceMemory;

	if (renderer->queuedBufferMemoryWrappersToDestroyCount + 1 >= renderer->queuedBufferMemoryWrappersToDestroyCapacity)
	{
		renderer->queuedBufferMemoryWrappersToDestroyCapacity *= 2;
		
		renderer->queuedBufferMemoryWrappersToDestroy = SDL_realloc(
			renderer->queuedBufferMemoryWrappersToDestroy,
			sizeof(BufferMemoryWrapper*) * renderer->queuedBufferMemoryWrappersToDestroyCapacity
		);
	}

	renderer->queuedBufferMemoryWrappersToDestroy[renderer->queuedBufferMemoryWrappersToDestroyCount] = bufferMemoryWrapper;
	renderer->queuedBufferMemoryWrappersToDestroyCount++;
}

static void QueueBufferDestroy(
	FNAVulkanRenderer *renderer,
	VulkanBuffer *vulkanBuffer
) {
	if (renderer->queuedBuffersToDestroyCount + 1 >= renderer->queuedBuffersToDestroyCapacity)
	{
		renderer->queuedBuffersToDestroyCapacity *= 2;

		renderer->queuedBuffersToDestroy = SDL_realloc(
			renderer->queuedBuffersToDestroy,
			sizeof(VulkanBuffer*) * renderer->queuedBuffersToDestroyCapacity
		);
	}

	renderer->queuedBuffersToDestroy[renderer->queuedBuffersToDestroyCount] = vulkanBuffer;
	renderer->queuedBuffersToDestroyCount++;
}

static void RemoveBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanBuffer *vulkanBuffer;

	vulkanBuffer = (VulkanBuffer*) buffer;
	QueueBufferDestroy(renderer, vulkanBuffer);
}

void VULKAN_AddDisposeVertexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	RemoveBuffer(driverData, buffer);
}

void VULKAN_SetVertexBufferData(
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
	SetBufferData(
		driverData,
		buffer,
		offsetInBytes,
		data,
		elementCount * vertexStride,
		options
	);
}

void VULKAN_GetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanBuffer *vulkanBuffer = (VulkanBuffer*) buffer;
	uint8_t *dataBytes, *cpy, *src, *dst;
	uint8_t useStagingBuffer;
	int32_t i;
	void *contents;

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

	renderer->vkMapMemory(
		renderer->logicalDevice,
		vulkanBuffer->deviceMemory,
		0,
		vulkanBuffer->size,
		0,
		&contents
	);

	SDL_memcpy(
		cpy,
		(uint8_t*) contents + offsetInBytes,
		elementCount * vertexStride
	);

	if (useStagingBuffer)
	{
		src = cpy;
		dst = dataBytes;
		for (i = 0; i < elementCount; i++)
		{
			SDL_memcpy(dst, src, elementSizeInBytes);
			dst += elementSizeInBytes;
			src += vertexStride;
		}
		SDL_free(cpy);
	}

	renderer->vkUnmapMemory(
		renderer->logicalDevice,
		vulkanBuffer->deviceMemory
	);
}

/* Index Buffers */

FNA3D_Buffer* VULKAN_GenIndexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t indexCount,
	FNA3D_IndexElementSize indexElementSize
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;

	return (FNA3D_Buffer*) CreateBuffer(
		renderer,
		usage,
		indexCount * IndexSize(indexElementSize),
		RESOURCE_ACCESS_INDEX_BUFFER
	);
}

void VULKAN_AddDisposeIndexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	RemoveBuffer(driverData, buffer);
}

void VULKAN_SetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	SetBufferData(
		driverData,
		buffer,
		offsetInBytes,
		data,
		dataLength,
		options
	);
}

void VULKAN_GetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanBuffer *vulkanBuffer = (VulkanBuffer*) buffer;

	void *contents;
	renderer->vkMapMemory(
		renderer->logicalDevice,
		vulkanBuffer->deviceMemory,
		0,
		vulkanBuffer->size,
		0,
		&contents
	);

	SDL_memcpy(
		data,
		(uint8_t*) contents + offsetInBytes,
		dataLength
	);

	renderer->vkUnmapMemory(
		renderer->logicalDevice,
		vulkanBuffer->deviceMemory
	);
}

/* Effects */

void VULKAN_CreateEffect(
	FNA3D_Renderer *driverData,
	uint8_t *effectCode,
	uint32_t effectCodeLength,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **effectData
) {
	MOJOSHADER_effectShaderContext shaderBackend;
	VulkanEffect *result;
	uint32_t i;

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

	for (i = 0; i < (*effectData)->error_count; i++)
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

void VULKAN_CloneEffect(
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

static void DestroyEffect(
	FNAVulkanRenderer *renderer,
	VulkanEffect *vulkanEffect
) {
	MOJOSHADER_effect *effectData = vulkanEffect->effect;

	if (effectData == renderer->currentEffect) {
		MOJOSHADER_effectEndPass(renderer->currentEffect);
		MOJOSHADER_effectEnd(renderer->currentEffect);
		renderer->currentEffect = NULL;
		renderer->currentTechnique = NULL;
		renderer->currentPass = 0;
	}
	MOJOSHADER_deleteEffect(effectData);
	SDL_free(vulkanEffect);
}

void VULKAN_AddDisposeEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanEffect *vulkanEffect = (VulkanEffect*) effect;

	if (renderer->queuedEffectsToDestroyCount + 1 >= renderer->queuedEffectsToDestroyCapacity)
	{
		renderer->queuedEffectsToDestroyCapacity *= 2;

		renderer->queuedEffectsToDestroy = SDL_realloc(
			renderer->queuedEffectsToDestroy,
			sizeof(VulkanEffect*) * renderer->queuedEffectsToDestroyCapacity
		);
	}

	renderer->queuedEffectsToDestroy[renderer->queuedEffectsToDestroyCount] = vulkanEffect;
	renderer->queuedEffectsToDestroyCount++;
}

void VULKAN_SetEffectTechnique(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique
) {
	VulkanEffect *vkEffect = (VulkanEffect*) effect;
	MOJOSHADER_effectSetTechnique(vkEffect->effect, technique);
}

void VULKAN_ApplyEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	uint32_t pass,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanEffect *fnaEffect = (VulkanEffect*) effect;
	MOJOSHADER_effect *effectData = fnaEffect->effect;
	const MOJOSHADER_effectTechnique *technique = fnaEffect->effect->current_technique;
	uint32_t numPasses;

	VULKAN_BeginFrame(driverData);

	if (effectData == renderer->currentEffect)
	{
		if (	technique == renderer->currentTechnique &&
				pass == renderer->currentPass		)
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

void VULKAN_BeginPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	MOJOSHADER_effect *effectData = ((VulkanEffect *) effect)->effect;
	uint32_t whatever;

	VULKAN_BeginFrame(driverData);

	MOJOSHADER_effectBegin(
			effectData,
			&whatever,
			1,
			stateChanges
	);
	MOJOSHADER_effectBeginPass(effectData, 0);
}

void VULKAN_EndPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	MOJOSHADER_effect *effectData = ((VulkanEffect *) effect)->effect;
	MOJOSHADER_effectEndPass(effectData);
	MOJOSHADER_effectEnd(effectData);
}

/* Queries */

FNA3D_Query* VULKAN_CreateQuery(FNA3D_Renderer *driverData)
{
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer *) driverData;
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

void VULKAN_AddDisposeQuery(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanQuery *vulkanQuery = (VulkanQuery*) query;

	/* Need to do this between passes */
	EndPass(renderer);

	SDL_LockMutex(renderer->cmdLock);
	renderer->vkCmdResetQueryPool(
		renderer->drawCommandBuffers[renderer->currentFrame],
		renderer->queryPool,
		vulkanQuery->index,
		1
	);
	SDL_UnlockMutex(renderer->cmdLock);

	/* Push the now-freed index to the stack */
	renderer->freeQueryIndexStack[vulkanQuery->index] = renderer->freeQueryIndexStackHead;
	renderer->freeQueryIndexStackHead = vulkanQuery->index;

	SDL_free(vulkanQuery);
}

void VULKAN_QueryBegin(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanQuery *vulkanQuery = (VulkanQuery*) query;

	SDL_LockMutex(renderer->cmdLock);
	renderer->vkCmdBeginQuery(
		renderer->drawCommandBuffers[renderer->currentFrame],
		renderer->queryPool,
		vulkanQuery->index,
		VK_QUERY_CONTROL_PRECISE_BIT
	);
	SDL_UnlockMutex(renderer->cmdLock);
}

void VULKAN_QueryEnd(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanQuery *vulkanQuery = (VulkanQuery*) query;

	/* Assume that the user is calling this in the same pass as they started it */

	SDL_LockMutex(renderer->cmdLock);
	renderer->vkCmdEndQuery(
		renderer->drawCommandBuffers[renderer->currentFrame],
		renderer->queryPool,
		vulkanQuery->index
	);
	SDL_UnlockMutex(renderer->cmdLock);
}

uint8_t VULKAN_QueryComplete(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
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

int32_t VULKAN_QueryPixelCount(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
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

uint8_t VULKAN_SupportsDXT1(FNA3D_Renderer *driverData)
{
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	return renderer->supportsDxt1;
}

uint8_t VULKAN_SupportsS3TC(FNA3D_Renderer *driverData)
{
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	return renderer->supportsS3tc;
}

uint8_t VULKAN_SupportsHardwareInstancing(FNA3D_Renderer *driverData)
{
	return 1;
}

uint8_t VULKAN_SupportsNoOverwrite(FNA3D_Renderer *driverData)
{
	return 1;
}

void VULKAN_GetMaxTextureSlots(
	FNA3D_Renderer *driverData,
	int32_t *textures,
	int32_t *vertexTextures
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	*textures = renderer->numTextureSlots;
	*vertexTextures = renderer->numVertexTextureSlots;
}

int32_t VULKAN_GetMaxMultiSampleCount(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VkSampleCountFlags flags = renderer->physicalDeviceProperties.properties.limits.framebufferColorSampleCounts;
	uint32_t maxSupported = 1;

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

void VULKAN_SetStringMarker(FNA3D_Renderer *driverData, const char *text)
{
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VkDebugUtilsLabelEXT labelInfo = {
		VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT
	};
	labelInfo.pLabelName = text;

	if (renderer->supportsDebugUtils)
	{
		SDL_LockMutex(renderer->cmdLock);
		renderer->vkCmdInsertDebugUtilsLabelEXT(
			renderer->drawCommandBuffers[renderer->currentFrame],
			&labelInfo
		);
		SDL_UnlockMutex(renderer->cmdLock);
	}
}

/* Function Loading */

static uint8_t LoadGlobalFunctions(void)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
	vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) SDL_Vulkan_GetVkGetInstanceProcAddr();
#pragma GCC diagnostic pop
	if (!vkGetInstanceProcAddr)
	{
		FNA3D_LogError(
			"SDL_Vulkan_GetVkGetInstanceProcAddr(): %s",
			SDL_GetError()
		);
		return 0;
	}

	#define VULKAN_GLOBAL_FUNCTION(name)								\
		name = (PFN_##name) vkGetInstanceProcAddr(VK_NULL_HANDLE, #name);			\
		if (!name)										\
		{											\
			FNA3D_LogError("vkGetInstanceProcAddr(VK_NULL_HANDLE, \"" #name "\") failed");	\
			return 0;									\
		}

	#include "FNA3D_Driver_Vulkan_global_funcs.h"
	#undef VULKAN_GLOBAL_FUNCTION

	return 1;
}

static void LoadInstanceFunctions(
	FNAVulkanRenderer *renderer
) {
	#define VULKAN_INSTANCE_FUNCTION(ext, ret, func, params) \
		renderer->func = (vkfntype_##func) vkGetInstanceProcAddr(renderer->instance, #func);
	#include "FNA3D_Driver_Vulkan_instance_funcs.h"
	#undef VULKAN_INSTANCE_FUNCTION
}

static void LoadDeviceFunctions(
	FNAVulkanRenderer *renderer
) {
	#define VULKAN_DEVICE_FUNCTION(ext, ret, func, params) \
		renderer->func = (vkfntype_##func) renderer->vkGetDeviceProcAddr(renderer->logicalDevice, #func);
	#include "FNA3D_Driver_Vulkan_device_funcs.h"
	#undef VULKAN_DEVICE_FUNCTION
}

static inline uint8_t SupportsExtension(
	const char *ext,
	VkExtensionProperties *availableExtensions,
	uint32_t numAvailableExtensions
) {
	int32_t i;
	for (i = 0; i < numAvailableExtensions; i += 1)
	{
		if (SDL_strcmp(ext, availableExtensions[i].extensionName) == 0)
		{
			return 1;
		}
	}
	return 0;
}

static uint8_t CheckInstanceExtensionSupport(
	const char **requiredExtensions,
	uint32_t requiredExtensionsLength,
	uint8_t *supportsDebugUtils
) {
	uint32_t extensionCount, i;
	VkExtensionProperties *availableExtensions;
	uint8_t allExtensionsSupported = 1;

	vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);
	availableExtensions = SDL_stack_alloc(VkExtensionProperties, extensionCount);
	vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, availableExtensions);

	for (i = 0; i < requiredExtensionsLength; i++)
	{
		if (!SupportsExtension(requiredExtensions[i], availableExtensions, extensionCount))
		{
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

static uint8_t CheckDeviceExtensionSupport(
	FNAVulkanRenderer *renderer,
	VkPhysicalDevice physicalDevice,
	const char** requiredExtensions,
	uint32_t requiredExtensionsLength
) {
	uint32_t extensionCount, i;
	VkExtensionProperties *availableExtensions;
	uint8_t allExtensionsSupported = 1;

	renderer->vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &extensionCount, NULL);
	availableExtensions = SDL_stack_alloc(VkExtensionProperties, extensionCount);
	renderer->vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &extensionCount, availableExtensions);

	for (i = 0; i < requiredExtensionsLength; i++)
	{
		if (!SupportsExtension(requiredExtensions[i], availableExtensions, extensionCount))
		{
			allExtensionsSupported = 0;
			break;
		}
	}

	SDL_stack_free(availableExtensions);
	return allExtensionsSupported;
}

static uint8_t QuerySwapChainSupport(
	FNAVulkanRenderer *renderer,
	VkPhysicalDevice physicalDevice,
	VkSurfaceKHR surface,
	SwapChainSupportDetails *outputDetails
) {
	VkResult result;
	uint32_t formatCount;
	uint32_t presentModeCount;

	result = renderer->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &outputDetails->capabilities);
	if (result != VK_SUCCESS)
	{
		FNA3D_LogError(
			"vkGetPhysicalDeviceSurfaceCapabilitiesKHR: %s",
			VkErrorMessages(result)
		);

		return 0;
	}

	renderer->vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, NULL);

	if (formatCount != 0)
	{
		outputDetails->formats = (VkSurfaceFormatKHR*) SDL_malloc(sizeof(VkSurfaceFormatKHR) * formatCount);
		outputDetails->formatsLength = formatCount;

		if (!outputDetails->formats)
		{
			SDL_OutOfMemory();
			return 0;
		}

		result = renderer->vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, outputDetails->formats);
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

	renderer->vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, NULL);

	if (presentModeCount != 0)
	{
		outputDetails->presentModes = (VkPresentModeKHR*) SDL_malloc(sizeof(VkPresentModeKHR) * presentModeCount);
		outputDetails->presentModesLength = presentModeCount;

		if (!outputDetails->presentModes)
		{
			SDL_OutOfMemory();
			return 0;
		}

		result = renderer->vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, outputDetails->presentModes);
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

/* if no dedicated device exists, one that supports our features would be fine */
static uint8_t IsDeviceSuitable(
	FNAVulkanRenderer *renderer,
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
	uint8_t foundSuitableDevice = 0;
	VkPhysicalDeviceProperties deviceProperties;

	queueFamilyIndices->graphicsFamily = UINT32_MAX;
	queueFamilyIndices->presentFamily = UINT32_MAX;
	*isIdeal = 0;

	if (!CheckDeviceExtensionSupport(
		renderer,
		physicalDevice,
		requiredExtensionNames,
		requiredExtensionNamesLength
	)) {
		return 0;
	}

	renderer->vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, NULL);

	/* FIXME: need better structure for checking vs storing support details */
	if (!QuerySwapChainSupport(renderer, physicalDevice, surface, &swapChainSupportDetails))
	{
		SDL_free(swapChainSupportDetails.formats);
		SDL_free(swapChainSupportDetails.presentModes);
		return 0;
	}

	if (swapChainSupportDetails.formatsLength == 0 || swapChainSupportDetails.presentModesLength == 0)
	{
		SDL_free(swapChainSupportDetails.formats);
		SDL_free(swapChainSupportDetails.presentModes);
		return 0;
	}

	queueProps = (VkQueueFamilyProperties*) SDL_stack_alloc(VkQueueFamilyProperties, queueFamilyCount);
	renderer->vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueProps);

	for (i = 0; i < queueFamilyCount; i++)
	{
		renderer->vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &supportsPresent);
		if (supportsPresent && (queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
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
		renderer->vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
		if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			*isIdeal = 1;
		}
		return 1;
	}

	/* This device is useless for us, next! */
	return 0;
}

static uint8_t CheckValidationLayerSupport(
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

	for (i = 0; i < validationLayersLength; i++)
	{
		layerFound = 0;

		for (j = 0; j < layerCount; j++)
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

static uint8_t ChooseSwapSurfaceFormat(
	VkFormat desiredFormat,
	VkSurfaceFormatKHR *availableFormats,
	uint32_t availableFormatsLength,
	VkSurfaceFormatKHR *outputFormat
) {
	uint32_t i;
	for (i = 0; i < availableFormatsLength; i++)
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

static uint8_t ChooseSwapPresentMode(
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
		for (i = 0; i < availablePresentModesLength; i++)
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
		for (i = 0; i < availablePresentModesLength; i++)
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

static VkExtent2D ChooseSwapExtent(
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
		VULKAN_GetDrawableSize(
			windowHandle,
			&drawableWidth,
			&drawableHeight
		);

		actualExtent.width = drawableWidth;
		actualExtent.height = drawableHeight;

		return actualExtent;
	}
}

static uint8_t FindMemoryType(
	FNAVulkanRenderer *renderer,
	uint32_t typeFilter,
	VkMemoryPropertyFlags properties,
	uint32_t *result
) {
	VkPhysicalDeviceMemoryProperties memoryProperties;
	uint32_t i;

	renderer->vkGetPhysicalDeviceMemoryProperties(renderer->physicalDevice, &memoryProperties);

	for (i = 0; i < memoryProperties.memoryTypeCount; i++)
	{
		if ((typeFilter & (1 << i)) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
		{
			*result = i;
			return 1;
		}
	}

	FNA3D_LogError("Failed to find suitable memory type");

	return 0;
}

static void GenerateUserVertexInputInfo(
	FNAVulkanRenderer *renderer,
	VkVertexInputBindingDescription *bindingDescriptions,
	VkVertexInputAttributeDescription* attributeDescriptions,
	uint32_t *attributeDescriptionCount
) {
	MOJOSHADER_vkShader *vertexShader, *blah;
	uint32_t attributeDescriptionCounter = 0;
	uint32_t i, j;
	FNA3D_VertexElement element;
	FNA3D_VertexElementUsage usage;
	int32_t index, attribLoc;
	VkVertexInputAttributeDescription vInputAttribDescription;

	MOJOSHADER_vkGetBoundShaders(&vertexShader, &blah);

	bindingDescriptions[0].binding = 0;
	bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	bindingDescriptions[0].stride = renderer->userVertexDeclaration.vertexStride;

	SDL_memset(renderer->attrUse, '\0', sizeof(renderer->attrUse));
	for (i = 0; i < renderer->userVertexDeclaration.elementCount; i++)
	{
		element = renderer->userVertexDeclaration.elements[i];
		usage = element.vertexElementUsage;
		index = element.usageIndex;

		if (renderer->attrUse[usage][index])
		{
			index = -1;

			for (j = 0; j < MAX_VERTEX_ATTRIBUTES; j++)
			{
				if (!renderer->attrUse[usage][j])
				{
					index = j;
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

		renderer->attrUse[usage][index] = 1;

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
		vInputAttribDescription.binding = 0;

		attributeDescriptions[attributeDescriptionCounter] = vInputAttribDescription;
		attributeDescriptionCount++;
	}

	*attributeDescriptionCount = attributeDescriptionCounter;
}

static void GenerateVertexInputInfo(
	FNAVulkanRenderer *renderer,
	VkVertexInputBindingDescription *bindingDescriptions,
	VkVertexInputAttributeDescription* attributeDescriptions,
	uint32_t *attributeDescriptionCount
) {
	MOJOSHADER_vkShader *vertexShader, *blah;
	uint32_t attributeDescriptionCounter = 0;
	uint32_t i, j, k;
	FNA3D_VertexDeclaration vertexDeclaration;
	FNA3D_VertexElement element;
	FNA3D_VertexElementUsage usage;
	int32_t index, attribLoc;
	VkVertexInputAttributeDescription vInputAttribDescription;
	VkVertexInputBindingDescription vertexInputBindingDescription;

	MOJOSHADER_vkGetBoundShaders(&vertexShader, &blah);

	SDL_memset(renderer->attrUse, '\0', sizeof(renderer->attrUse));
	for (i = 0; i < renderer->numVertexBindings; i++)
	{
		vertexDeclaration = renderer->vertexBindings[i].vertexDeclaration;

		for (j = 0; j < vertexDeclaration.elementCount; j++)
		{
			element = vertexDeclaration.elements[j];
			usage = element.vertexElementUsage;
			index = element.usageIndex;

			if (renderer->attrUse[usage][index])
			{
				index = -1;

				for (k = 0; k < MAX_VERTEX_ATTRIBUTES; k++)
				{
					if (!renderer->attrUse[usage][k])
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

			renderer->attrUse[usage][index] = 1;

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

			attributeDescriptions[attributeDescriptionCounter] = vInputAttribDescription;
			attributeDescriptionCounter++;
		}

		vertexInputBindingDescription.binding = i;
		vertexInputBindingDescription.stride = vertexDeclaration.vertexStride;
		vertexInputBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		bindingDescriptions[i] = vertexInputBindingDescription;
	}

	*attributeDescriptionCount = attributeDescriptionCounter;
}

/* device initialization stuff */

static uint8_t CreateInstance(
	FNAVulkanRenderer *renderer,
	FNA3D_PresentationParameters *presentationParameters
) {
	VkResult vulkanResult;
	VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	const char **instanceExtensionNames;
	uint32_t instanceExtensionCount;
	VkInstanceCreateInfo createInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	const char *layerNames[] = { "VK_LAYER_KHRONOS_validation" };

	/* create instance */
	appInfo.pEngineName = "FNA3D";
	appInfo.engineVersion = FNA3D_COMPILED_VERSION;
	appInfo.apiVersion = VK_MAKE_VERSION(1, 0, 0);

	if (
		!SDL_Vulkan_GetInstanceExtensions(
			(SDL_Window*) presentationParameters->deviceWindowHandle,
			&instanceExtensionCount,
			NULL
		)
	) {
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

	instanceExtensionNames[instanceExtensionCount++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;

	if (!CheckInstanceExtensionSupport(
		instanceExtensionNames,
		instanceExtensionCount,
		&renderer->supportsDebugUtils
	)) {
		FNA3D_LogError("Required Vulkan instance extensions not supported");
		goto create_instance_fail;
	}

	if (renderer->supportsDebugUtils)
	{
		/* Append the debug extension to the end */
		instanceExtensionNames[instanceExtensionCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	}
	else
	{
		FNA3D_LogWarn("%s is not supported!", VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	/* create info structure */

	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledExtensionCount = instanceExtensionCount;
	createInfo.ppEnabledExtensionNames = instanceExtensionNames;
	createInfo.ppEnabledLayerNames = layerNames;

	if (renderer->debugMode)
	{
		createInfo.enabledLayerCount = SDL_arraysize(layerNames);
		if (!CheckValidationLayerSupport(layerNames, createInfo.enabledLayerCount))
		{
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

	SDL_stack_free(instanceExtensionNames);
	return 1;

create_instance_fail:
	SDL_stack_free(instanceExtensionNames);
	return 0;
}

static uint8_t DeterminePhysicalDevice(
	FNAVulkanRenderer *renderer,
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
	for (i = 0; i < physicalDeviceCount; i++)
	{
		if (IsDeviceSuitable(
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

	renderer->physicalDeviceDriverProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

	renderer->physicalDeviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	renderer->physicalDeviceProperties.pNext = &renderer->physicalDeviceDriverProperties;

	renderer->vkGetPhysicalDeviceProperties2(
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

static uint8_t CreateLogicalDevice(
	FNAVulkanRenderer *renderer,
	const char **deviceExtensionNames,
	uint32_t deviceExtensionCount
) {
	VkResult vulkanResult;
	VkDeviceCreateInfo deviceCreateInfo;
	VkPhysicalDeviceFeatures deviceFeatures;

	VkDeviceQueueCreateInfo *queueCreateInfos = SDL_stack_alloc(VkDeviceQueueCreateInfo, 2);
	VkDeviceQueueCreateInfo queueCreateInfoGraphics;
	VkDeviceQueueCreateInfo queueCreateInfoPresent;

	int queueInfoCount = 1;
	float queuePriority = 1.0f;

	SDL_zero(deviceCreateInfo);
	SDL_zero(deviceFeatures);
	SDL_zero(queueCreateInfoGraphics);
	SDL_zero(queueCreateInfoPresent);

	queueCreateInfoGraphics.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfoGraphics.queueFamilyIndex = renderer->queueFamilyIndices.graphicsFamily;
	queueCreateInfoGraphics.queueCount = 1;
	queueCreateInfoGraphics.pQueuePriorities = &queuePriority;

	queueCreateInfos[0] = queueCreateInfoGraphics;

	if (renderer->queueFamilyIndices.presentFamily != renderer->queueFamilyIndices.graphicsFamily)
	{
		queueCreateInfoPresent.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfoPresent.queueFamilyIndex = renderer->queueFamilyIndices.presentFamily;
		queueCreateInfoPresent.queueCount = 1;
		queueCreateInfoPresent.pQueuePriorities = &queuePriority;

		queueCreateInfos[1] = queueCreateInfoPresent;
		queueInfoCount++;
	}

	/* specifying used device features */

	deviceFeatures.occlusionQueryPrecise = VK_TRUE;

	/* creating the logical device */

	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.queueCreateInfoCount = queueInfoCount;
	deviceCreateInfo.pQueueCreateInfos = queueCreateInfos;
	deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
	deviceCreateInfo.ppEnabledExtensionNames = deviceExtensionNames;
	deviceCreateInfo.enabledExtensionCount = deviceExtensionCount;

	vulkanResult = renderer->vkCreateDevice(renderer->physicalDevice, &deviceCreateInfo, NULL, &renderer->logicalDevice);
	if (vulkanResult != VK_SUCCESS)
	{
		FNA3D_LogError(
			"vkCreateDevice failed: %s",
			VkErrorMessages(vulkanResult)
		);
		return 0;
	}

	/* assign logical device to the renderer and load entry points */

	LoadDeviceFunctions(renderer);

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

static void RecreateSwapchain(
	FNAVulkanRenderer *renderer
) {
	VkSubmitInfo submitInfo = {
		VK_STRUCTURE_TYPE_SUBMIT_INFO
	};
	VkPipelineStageFlags waitStages[] = {
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
	};
	CreateSwapchainResult createSwapchainResult;
	SwapChainSupportDetails swapChainSupportDetails;
	VkExtent2D extent;
	VkResult result;

	if (renderer->commandBufferActive[renderer->currentFrame])
	{
		EndPass(renderer);

		SDL_LockMutex(renderer->cmdLock);

		renderer->vkEndCommandBuffer(
			renderer->dataCommandBuffers[renderer->currentFrame]
		);

		renderer->vkEndCommandBuffer(
			renderer->drawCommandBuffers[renderer->currentFrame]
		);

		submitInfo.waitSemaphoreCount = 0;
		submitInfo.pWaitSemaphores = NULL;
		submitInfo.pWaitDstStageMask = NULL;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &renderer->dataFinishedSemaphores[renderer->currentFrame];
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &renderer->dataCommandBuffers[renderer->currentFrame];

		result = renderer->vkQueueSubmit(
			renderer->graphicsQueue,
			1,
			&submitInfo,
			NULL
		);

		if (result != VK_SUCCESS)
		{
			FNA3D_LogError("failed to submit data command buffer");
			LogVulkanResult("vkQueueSubmit", result);
			return;
		}

		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &renderer->dataFinishedSemaphores[renderer->currentFrame];
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.signalSemaphoreCount = 0;
		submitInfo.pSignalSemaphores = NULL;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &renderer->drawCommandBuffers[renderer->currentFrame];

		renderer->vkQueueSubmit(
			renderer->graphicsQueue,
			1,
			&submitInfo,
			NULL
		);

		if (result != VK_SUCCESS)
		{
			FNA3D_LogError("failed to submit draw command buffer");
			LogVulkanResult("vkQueueSubmit", result);
			return;
		}

		SDL_UnlockMutex(renderer->cmdLock);
	}

	renderer->commandBufferActive[renderer->currentFrame] = 0;
	renderer->frameInProgress[renderer->currentFrame] = 0;

	renderer->vkDeviceWaitIdle(renderer->logicalDevice);

	renderer->vkResetCommandPool(
		renderer->logicalDevice,
		renderer->dataCommandPool,
		VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT
	);

	renderer->vkResetCommandPool(
		renderer->logicalDevice,
		renderer->drawCommandPool,
		VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT
	);

	QuerySwapChainSupport(
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

	DestroySwapchain(renderer);
	createSwapchainResult = CreateSwapchain(renderer);

	if (createSwapchainResult == CREATE_SWAPCHAIN_FAIL)
	{
		FNA3D_LogError("Failed to recreate swapchain");
		return;
	}

	renderer->vkDeviceWaitIdle(renderer->logicalDevice);
}

static CreateSwapchainResult CreateSwapchain(
	FNAVulkanRenderer *renderer
) {
	VkResult vulkanResult;
	SwapChainSupportDetails swapChainSupportDetails;
	VkSurfaceFormatKHR surfaceFormat;
	VkPresentModeKHR presentMode;
	VkExtent2D extent = { 0, 0 };
	uint32_t imageCount, swapChainImageCount, i;
	VkSwapchainCreateInfoKHR swapChainCreateInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	VkImage *swapChainImages;
	VkImageViewCreateInfo createInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	VkImageView swapChainImageView;
	SurfaceFormatMapping surfaceFormatMapping = { VK_FORMAT_B8G8R8A8_UNORM };

	if (!QuerySwapChainSupport(
		renderer,
		renderer->physicalDevice,
		renderer->surface,
		&swapChainSupportDetails
	)) {
		FNA3D_LogError("Device does not support swap chain creation");
		return CREATE_SWAPCHAIN_FAIL;
	}

	if (
		!ChooseSwapSurfaceFormat(
			surfaceFormatMapping.formatColor,
			swapChainSupportDetails.formats,
			swapChainSupportDetails.formatsLength,
			&surfaceFormat
		)
	) {
		SDL_free(swapChainSupportDetails.formats);
		SDL_free(swapChainSupportDetails.presentModes);
		FNA3D_LogError("Device does not support swap chain format");
		return CREATE_SWAPCHAIN_FAIL;
	}

	renderer->surfaceFormatMapping = surfaceFormatMapping;

	if (
		!ChooseSwapPresentMode(
			renderer->presentInterval,
			swapChainSupportDetails.presentModes,
			swapChainSupportDetails.presentModesLength,
			&presentMode
		)
	) {
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

	vulkanResult = renderer->vkCreateSwapchainKHR(renderer->logicalDevice, &swapChainCreateInfo, NULL, &renderer->swapChain);
	
	SDL_free(swapChainSupportDetails.formats);
	SDL_free(swapChainSupportDetails.presentModes);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateSwapchainKHR", vulkanResult);

		return CREATE_SWAPCHAIN_FAIL;
	}

	renderer->vkGetSwapchainImagesKHR(renderer->logicalDevice, renderer->swapChain, &swapChainImageCount, NULL);

	if (renderer->swapChainImages)
	{
		SDL_free(renderer->swapChainImages);
	}

	renderer->swapChainImages = (VulkanImageResource**) SDL_malloc(sizeof(VulkanImageResource*) * swapChainImageCount);
	if (!renderer->swapChainImages)
	{
		SDL_OutOfMemory();
		return CREATE_SWAPCHAIN_FAIL;
	}

	if (renderer->swapChainImageViews)
	{
		SDL_free(renderer->swapChainImageViews);
	}

	renderer->swapChainImageViews = (VkImageView*) SDL_malloc(sizeof(VkImageView) * swapChainImageCount);
	if (!renderer->swapChainImageViews)
	{
		SDL_OutOfMemory();
		return CREATE_SWAPCHAIN_FAIL;
	}

	swapChainImages = SDL_stack_alloc(VkImage, swapChainImageCount);
	renderer->vkGetSwapchainImagesKHR(renderer->logicalDevice, renderer->swapChain, &swapChainImageCount, swapChainImages);
	renderer->swapChainImageCount = swapChainImageCount;
	renderer->swapChainExtent = extent;

	for (i = 0; i < swapChainImageCount; i++)
	{
		createInfo.image = swapChainImages[i];
		createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		createInfo.format = surfaceFormat.format;
		createInfo.components = surfaceFormatMapping.swizzle;
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

		renderer->swapChainImages[i] = (VulkanImageResource*) SDL_malloc(sizeof(FNAVulkanImageData));
		SDL_zerop(renderer->swapChainImages[i]);

		renderer->swapChainImages[i]->image = swapChainImages[i];
		renderer->swapChainImages[i]->resourceAccessType = RESOURCE_ACCESS_NONE;
		renderer->swapChainImageViews[i] = swapChainImageView;
	}

	SDL_stack_free(swapChainImages);
	return CREATE_SWAPCHAIN_SUCCESS;
}

static uint8_t CreateDescriptorSetLayouts(
	FNAVulkanRenderer *renderer
) {
	VkResult vulkanResult;
	VkDescriptorSetLayoutBinding *layoutBindings;
	VkDescriptorSetLayoutBinding layoutBinding;
	VkDescriptorSetLayoutCreateInfo layoutCreateInfo;
	uint32_t i, j;

	/* define vertex UBO set layout */
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
		return 0;
	}
	
	/* special 0 case for vert sampler layout */
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

	/* define all possible vert sampler layouts */
	for (i = 1; i < MAX_VERTEXTEXTURE_SAMPLERS; i++)
	{
		layoutBindings = SDL_stack_alloc(VkDescriptorSetLayoutBinding, i);

		for (j = 0; j < i; j++)
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
			return 0;
		}
	}

	/* define frag UBO set layout */
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
		return 0;
	}

	/* special 0 case for frag sampler layout */
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

	/* define all possible frag sampler layouts */
	for (i = 1; i < MAX_TEXTURE_SAMPLERS; i++)
	{
		layoutBindings = SDL_stack_alloc(VkDescriptorSetLayoutBinding, i);

		for (j = 0; j < i; j++)
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
			return 0;
		}
	}

	return 1;
}

static uint8_t CreateFauxBackbuffer(
	FNAVulkanRenderer *renderer,
	FNA3D_PresentationParameters *presentationParameters
) {
	VkFormat vulkanDepthStencilFormat;
	ImageMemoryBarrierCreateInfo barrierCreateInfo;

	if (
		!CreateImage(
			renderer,
			presentationParameters->backBufferWidth,
			presentationParameters->backBufferHeight,
			XNAToVK_SampleCount(presentationParameters->multiSampleCount),
			renderer->surfaceFormatMapping.formatColor,
			renderer->surfaceFormatMapping.swizzle,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_TYPE_2D,
			/* FIXME: transfer bit probably only needs to be set on 0? */
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&renderer->fauxBackbufferColor.handle
		)
	) {
		FNA3D_LogError("Failed to create color attachment image");
		return 0;
	}

	renderer->fauxBackbufferWidth = presentationParameters->backBufferWidth;
	renderer->fauxBackbufferHeight = presentationParameters->backBufferHeight;
	
	renderer->fauxBackbufferSurfaceFormat = presentationParameters->backBufferFormat;
	renderer->fauxBackbufferMultisampleCount = XNAToVK_SampleCount(presentationParameters->multiSampleCount);

	InternalBeginFrame(renderer);

	barrierCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrierCreateInfo.subresourceRange.baseArrayLayer = 0;
	barrierCreateInfo.subresourceRange.baseMipLevel = 0;
	barrierCreateInfo.subresourceRange.layerCount = 1;
	barrierCreateInfo.subresourceRange.levelCount = 1;
	barrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrierCreateInfo.discardContents = 0;
	barrierCreateInfo.nextAccess = RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE;

	CreateImageMemoryBarrier(
		renderer,
		renderer->dataCommandBuffers[renderer->currentFrame],
		barrierCreateInfo,
		&renderer->fauxBackbufferColor.handle.imageResource
	);

	/* create faux backbuffer depth stencil image */

	renderer->fauxBackbufferDepthFormat = presentationParameters->depthStencilFormat;

	if (renderer->fauxBackbufferDepthFormat != FNA3D_DEPTHFORMAT_NONE)
	{
		vulkanDepthStencilFormat = XNAToVK_DepthFormat(renderer->fauxBackbufferDepthFormat);

		if (
			!CreateImage(
				renderer,
				presentationParameters->backBufferWidth,
				presentationParameters->backBufferHeight,
				XNAToVK_SampleCount(presentationParameters->multiSampleCount),
				vulkanDepthStencilFormat,
				IDENTITY_SWIZZLE,
				VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_TYPE_2D,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				&renderer->fauxBackbufferDepthStencil.handle
			)
		) {
			FNA3D_LogError("Failed to create depth stencil image");
			return 0;
		}

		/* layout transition if required */

		barrierCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		barrierCreateInfo.subresourceRange.baseArrayLayer = 0;
		barrierCreateInfo.subresourceRange.baseMipLevel = 0;
		barrierCreateInfo.subresourceRange.layerCount = 1;
		barrierCreateInfo.subresourceRange.levelCount = 1;
		barrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrierCreateInfo.discardContents = 0;
		barrierCreateInfo.nextAccess = RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_WRITE;

		CreateImageMemoryBarrier(
			renderer,
			renderer->dataCommandBuffers[renderer->currentFrame],
			barrierCreateInfo,
			&renderer->fauxBackbufferDepthStencil.handle.imageResource
		);
	}

	return 1;
}

static uint8_t CreatePipelineCache(
	FNAVulkanRenderer *renderer
) {
	VkResult vulkanResult;
	VkPipelineCacheCreateInfo pipelineCacheCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };

	vulkanResult = renderer->vkCreatePipelineCache(
		renderer->logicalDevice,
		&pipelineCacheCreateInfo,
		NULL,
		&renderer->pipelineCache
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreatePipelineCache", vulkanResult);
		return 0;
	}

	return 1;
}

static uint8_t CreateMojoshaderContext(
	FNAVulkanRenderer *renderer
) {
	renderer->mojoshaderContext = MOJOSHADER_vkCreateContext(
		(MOJOSHADER_VkInstance*) &renderer->instance,
		(MOJOSHADER_VkPhysicalDevice*) &renderer->physicalDevice,
		(MOJOSHADER_VkDevice*) &renderer->logicalDevice,
		MAX_FRAMES_IN_FLIGHT,
		(PFN_MOJOSHADER_vkGetInstanceProcAddr) vkGetInstanceProcAddr,
		(PFN_MOJOSHADER_vkGetDeviceProcAddr) renderer->vkGetDeviceProcAddr,
		renderer->queueFamilyIndices.graphicsFamily,
		NULL,
		NULL,
		NULL
	);

	if (renderer->mojoshaderContext != NULL)
	{
		MOJOSHADER_vkMakeContextCurrent(renderer->mojoshaderContext);
		return 1;
	}
	else
	{
		return 0;
	}
}

static uint8_t CreateDescriptorPools(
	FNAVulkanRenderer *renderer
) {
	uint32_t i;
	VkResult vulkanResult;

	VkDescriptorPoolSize uniformBufferPoolSize;
	VkDescriptorPoolSize samplerPoolSize;

	VkDescriptorPoolCreateInfo uniformBufferPoolInfo = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
	};
	VkDescriptorPoolCreateInfo samplerPoolInfo = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
	};

	for (i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		renderer->samplerDescriptorPools[i] = (VkDescriptorPool*) SDL_malloc(
			sizeof(VkDescriptorPool)
		);

		renderer->uniformBufferDescriptorPools[i] = (VkDescriptorPool*) SDL_malloc(
			sizeof(VkDescriptorPool)
		);
	

		uniformBufferPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uniformBufferPoolSize.descriptorCount = UNIFORM_BUFFER_DESCRIPTOR_POOL_SIZE;

		uniformBufferPoolInfo.poolSizeCount = 1;
		uniformBufferPoolInfo.pPoolSizes = &uniformBufferPoolSize;
		uniformBufferPoolInfo.maxSets = UNIFORM_BUFFER_DESCRIPTOR_POOL_SIZE;

		vulkanResult = renderer->vkCreateDescriptorPool(
			renderer->logicalDevice,
			&uniformBufferPoolInfo,
			NULL,
			&renderer->uniformBufferDescriptorPools[i][0]
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateDescriptorPool", vulkanResult);
			return 0;
		}

		renderer->uniformBufferDescriptorPoolCapacity[i] = 1;
		renderer->activeUniformBufferDescriptorPoolIndex[i] = 0;
		renderer->activeUniformBufferPoolUsage[i] = 0;

		samplerPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerPoolSize.descriptorCount = SAMPLER_DESCRIPTOR_POOL_SIZE;

		samplerPoolInfo.poolSizeCount = 1;
		samplerPoolInfo.pPoolSizes = &samplerPoolSize;
		samplerPoolInfo.maxSets = SAMPLER_DESCRIPTOR_POOL_SIZE;

		vulkanResult = renderer->vkCreateDescriptorPool(
			renderer->logicalDevice,
			&samplerPoolInfo,
			NULL,
			&renderer->samplerDescriptorPools[i][0]
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateDescriptorPool", vulkanResult);
			return 0;
		}

		renderer->samplerDescriptorPoolCapacity[i] = 1;
		renderer->activeSamplerDescriptorPoolIndex[i] = 0;
		renderer->activeSamplerPoolUsage[i] = 0;

		renderer->activeDescriptorSetCapacity[i] = renderer->swapChainImageCount * (MAX_TOTAL_SAMPLERS + 2);
		renderer->activeDescriptorSetCount[i] = 0;

		renderer->activeDescriptorSets[i] = (VkDescriptorSet*) SDL_malloc(
			sizeof(VkDescriptorSet) *
			renderer->activeDescriptorSetCapacity[i]
		);

		if (!renderer->activeDescriptorSets[i])
		{
			SDL_OutOfMemory();
			return 0;
		}
	}

	return 1;
}

static uint8_t CreateCommandPoolAndBuffers(
	FNAVulkanRenderer *renderer
) {
	VkResult vulkanResult;
	VkCommandPoolCreateInfo commandPoolCreateInfo;
	VkCommandBufferAllocateInfo commandBufferAllocateInfo = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
	};

	SDL_zero(commandPoolCreateInfo);
	commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolCreateInfo.queueFamilyIndex = renderer->queueFamilyIndices.graphicsFamily;
	commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	vulkanResult = renderer->vkCreateCommandPool(
		renderer->logicalDevice,
		&commandPoolCreateInfo,
		NULL,
		&renderer->dataCommandPool
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateCommandPool", vulkanResult);
		return 0;
	}

	vulkanResult = renderer->vkCreateCommandPool(
		renderer->logicalDevice,
		&commandPoolCreateInfo,
		NULL,
		&renderer->drawCommandPool
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateCommandPool", vulkanResult);
		return 0;
	}

	commandBufferAllocateInfo.commandPool = renderer->dataCommandPool;
	commandBufferAllocateInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

	SDL_LockMutex(renderer->cmdLock);
	renderer->vkAllocateCommandBuffers(
		renderer->logicalDevice,
		&commandBufferAllocateInfo,
		renderer->dataCommandBuffers
	);
	renderer->vkAllocateCommandBuffers(
		renderer->logicalDevice,
		&commandBufferAllocateInfo,
		renderer->drawCommandBuffers
	);
	SDL_UnlockMutex(renderer->cmdLock);

	return 1;
}

static uint8_t CreateFenceAndSemaphores(
	FNAVulkanRenderer *renderer
) {
	uint32_t i;
	VkResult vulkanResult;

	VkFenceCreateInfo fenceInfo = { 
		VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
	};
	VkSemaphoreCreateInfo semaphoreInfo = {
		VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
	};

	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		vulkanResult = renderer->vkCreateSemaphore(
			renderer->logicalDevice,
			&semaphoreInfo,
			NULL,
			&renderer->imageAvailableSemaphores[i]
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateFence", vulkanResult);
			return 0;
		}

		vulkanResult = renderer->vkCreateSemaphore(
			renderer->logicalDevice,
			&semaphoreInfo,
			NULL,
			&renderer->dataFinishedSemaphores[i]
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateSemaphore", vulkanResult);
			return 0;
		}

		vulkanResult = renderer->vkCreateSemaphore(
			renderer->logicalDevice,
			&semaphoreInfo,
			NULL,
			&renderer->renderFinishedSemaphores[i]
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateSemaphore", vulkanResult);
			return 0;
		}

		vulkanResult = renderer->vkCreateFence(
			renderer->logicalDevice,
			&fenceInfo,
			NULL,
			&renderer->inFlightFences[i]
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateSemaphore", vulkanResult);
			return 0;
		}
	}

	renderer->imagesInFlight = (VkFence*) SDL_malloc(
		sizeof(VkFence) * renderer->swapChainImageCount
	);

	for (i = 0; i < renderer->swapChainImageCount; i++)
	{
		renderer->imagesInFlight[i] = VK_NULL_HANDLE;
	}

	return 1;
}

static uint8_t CreateQueryPool(
	FNAVulkanRenderer *renderer
) {
	VkResult vulkanResult;
	uint32_t i;

	VkQueryPoolCreateInfo queryPoolCreateInfo = { 
		VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO 
	};
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
		return 0;
	}

	/* Set up the stack, the value at each index is the next available index, or -1 if no such index exists. */
	for (i = 0; i < MAX_QUERIES - 1; ++i)
	{
		renderer->freeQueryIndexStack[i] = i + 1;
	}
	renderer->freeQueryIndexStack[MAX_QUERIES - 1] = -1;

	return 1;
}

static void CreateDeferredDestroyStorage(
	FNAVulkanRenderer *renderer
) {
	uint32_t i;

	for (i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		renderer->renderbuffersToDestroyCapacity[i] = 16;
		renderer->renderbuffersToDestroyCount[i] = 0;

		renderer->renderbuffersToDestroy[i] = (VulkanRenderbuffer**) SDL_malloc(
			sizeof(VulkanRenderbuffer*) *
			renderer->renderbuffersToDestroyCapacity[i]
		);

		renderer->buffersToDestroyCapacity[i] = 16;
		renderer->buffersToDestroyCount[i] = 0;

		renderer->buffersToDestroy[i] = (VulkanBuffer**) SDL_malloc(
			sizeof(VulkanBuffer*) *
			renderer->buffersToDestroyCapacity[i]
		);

		renderer->bufferMemoryWrappersToDestroyCapacity[i] = 16;
		renderer->bufferMemoryWrappersToDestroyCount[i] = 0;

		renderer->bufferMemoryWrappersToDestroy[i] = (BufferMemoryWrapper**) SDL_malloc(
			sizeof(BufferMemoryWrapper*) *
			renderer->bufferMemoryWrappersToDestroyCapacity[i]
		);

		renderer->effectsToDestroyCapacity[i] = 16;
		renderer->effectsToDestroyCount[i] = 0;

		renderer->effectsToDestroy[i] = (VulkanEffect**) SDL_malloc(
			sizeof(VulkanEffect*) *
			renderer->effectsToDestroyCapacity[i]
		);

		renderer->imageDatasToDestroyCapacity[i] = 16;
		renderer->imageDatasToDestroyCount[i] = 0;

		renderer->imageDatasToDestroy[i] = (FNAVulkanImageData**) SDL_malloc(
			sizeof(FNAVulkanImageData*) *
			renderer->imageDatasToDestroyCapacity[i]
		);
	}

	renderer->queuedRenderbuffersToDestroyCapacity = 16;
	renderer->queuedRenderbuffersToDestroyCount = 0;

	renderer->queuedRenderbuffersToDestroy = (VulkanRenderbuffer**) SDL_malloc(
		sizeof(VulkanRenderbuffer*) * renderer->queuedRenderbuffersToDestroyCapacity
	);

	renderer->queuedBuffersToDestroyCapacity = 16;
	renderer->queuedBuffersToDestroyCount = 0;

	renderer->queuedBuffersToDestroy = (VulkanBuffer**) SDL_malloc(
		sizeof(VulkanBuffer*) * renderer->queuedBuffersToDestroyCapacity
	);

	renderer->queuedBufferMemoryWrappersToDestroyCapacity = 16;
	renderer->queuedBufferMemoryWrappersToDestroyCount = 0;

	renderer->queuedBufferMemoryWrappersToDestroy = (BufferMemoryWrapper**) SDL_malloc(
		sizeof(BufferMemoryWrapper*) * renderer->queuedBufferMemoryWrappersToDestroyCapacity
	);

	renderer->queuedEffectsToDestroyCapacity = 16;
	renderer->queuedEffectsToDestroyCount = 0;

	renderer->queuedEffectsToDestroy = (VulkanEffect**) SDL_malloc(
		sizeof(VulkanEffect*) * renderer->queuedEffectsToDestroyCapacity
	);

	renderer->queuedImageDatasToDestroyCapacity = 16;
	renderer->queuedImageDatasToDestroyCount = 0;

	renderer->queuedImageDatasToDestroy = (FNAVulkanImageData**) SDL_malloc(
		sizeof(FNAVulkanImageData*) * renderer->queuedImageDatasToDestroyCapacity
	);
}

static void CreateDummyData(
	FNAVulkanRenderer *renderer
) {
	VkSamplerCreateInfo samplerCreateInfo;
	ImageMemoryBarrierCreateInfo memoryBarrierCreateInfo;

	renderer->dummyVertTexture = CreateTexture(
		renderer,
		FNA3D_SURFACEFORMAT_COLOR,
		1,
		1,
		1,
		0,
		VK_IMAGE_TYPE_2D
	);

	renderer->dummyFragTexture = CreateTexture(
		renderer,
		FNA3D_SURFACEFORMAT_COLOR,
		1,
		1,
		1,
		0,
		VK_IMAGE_TYPE_2D
	);

	memoryBarrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrierCreateInfo.discardContents = 0;
	memoryBarrierCreateInfo.nextAccess = RESOURCE_ACCESS_VERTEX_SHADER_READ_SAMPLED_IMAGE;
	memoryBarrierCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	memoryBarrierCreateInfo.subresourceRange.baseArrayLayer = 0;
	memoryBarrierCreateInfo.subresourceRange.baseMipLevel = 0;
	memoryBarrierCreateInfo.subresourceRange.layerCount = 1;
	memoryBarrierCreateInfo.subresourceRange.levelCount = 1;

	CreateImageMemoryBarrier(
		renderer,
		renderer->dataCommandBuffers[renderer->currentFrame],
		memoryBarrierCreateInfo,
		&renderer->dummyVertTexture->imageData->imageResource
	);

	CreateImageMemoryBarrier(
		renderer,
		renderer->dataCommandBuffers[renderer->currentFrame],
		memoryBarrierCreateInfo,
		&renderer->dummyFragTexture->imageData->imageResource
	);

	renderer->dummyVertUniformBuffer = CreateBuffer(
		renderer,
		FNA3D_BUFFERUSAGE_WRITEONLY,
		1,
		RESOURCE_ACCESS_VERTEX_SHADER_READ_UNIFORM_BUFFER
	);

	renderer->dummyFragUniformBuffer = CreateBuffer(
		renderer,
		FNA3D_BUFFERUSAGE_WRITEONLY,
		1,
		RESOURCE_ACCESS_FRAGMENT_SHADER_READ_UNIFORM_BUFFER
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
}

static inline uint8_t DXTFormatSupported(VkFormatProperties formatProps)
{
	return	(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) &&
		(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
}

FNA3D_Device* VULKAN_CreateDevice(
	FNA3D_PresentationParameters *presentationParameters,
	uint8_t debugMode
) {
	FNAVulkanRenderer *renderer;
	FNA3D_Device *result;
	uint32_t i = 0;

	const char* deviceExtensionNames[] =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_MAINTENANCE1_EXTENSION_NAME,
		VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME
	};
	uint32_t deviceExtensionCount = SDL_arraysize(deviceExtensionNames);
	VkFormatProperties formatPropsBC1, formatPropsBC2, formatPropsBC3;

	/* Create the FNA3D_Device */
	result = (FNA3D_Device*) SDL_malloc(sizeof(FNA3D_Device));
	ASSIGN_DRIVER(VULKAN)

	/* Init the FNAVulkanRenderer */
	renderer = (FNAVulkanRenderer*) SDL_malloc(sizeof(FNAVulkanRenderer));
	SDL_zero(*renderer);

	renderer->debugMode = debugMode;
	renderer->parentDevice = result;
	result->driverData = (FNA3D_Renderer*) renderer;

	renderer->presentInterval = presentationParameters->presentationInterval;
	renderer->deviceWindowHandle = presentationParameters->deviceWindowHandle;

	for (i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		renderer->commandBufferActive[i] = 0;
		renderer->frameInProgress[i] = 0;
	}

	if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
	{
		FNA3D_LogError("Video system not initialized");
		return NULL;
	}

	/* load library so we can load vk functions dynamically */
	if (SDL_Vulkan_LoadLibrary(NULL) == -1)
	{
		FNA3D_LogError(
			"Failed to load Vulkan library: %s",
			SDL_GetError()
		);
		return NULL;
	}

	if (!LoadGlobalFunctions())
	{
		FNA3D_LogError("Failed to load Vulkan global functions");
		return NULL;
	}

	if (!CreateInstance(renderer, presentationParameters))
	{
		FNA3D_LogError("Error creating vulkan instance");
		return NULL;
	}

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

	LoadInstanceFunctions(renderer);

	if (!DeterminePhysicalDevice(
		renderer,
		deviceExtensionNames,
		deviceExtensionCount
	)) {
		FNA3D_LogError("Failed to determine a suitable physical device");
		return NULL;
	}

	if (!CreateLogicalDevice(
		renderer,
		deviceExtensionNames,
		deviceExtensionCount
	)) {
		FNA3D_LogError("Failed to create logical device");
		return NULL;
	}

	CreateDeferredDestroyStorage(renderer);

	if (!CreateMojoshaderContext(renderer))
	{
		FNA3D_LogError("Failed to create MojoShader context");
		return NULL;
	}

	if (CreateSwapchain(renderer) != CREATE_SWAPCHAIN_SUCCESS)
	{
		FNA3D_LogError("Failed to create swap chain");
		return NULL;
	}

	if (!CreateFenceAndSemaphores(renderer))
	{
		FNA3D_LogError("Failed to create fence and semaphores");
		return NULL;
	}

	if (!CreateCommandPoolAndBuffers(renderer))
	{
		FNA3D_LogError("Failed to create command pool");
		return NULL;
	}

	if (!CreateFauxBackbuffer(renderer, presentationParameters))
	{
		FNA3D_LogError("Failed to create faux backbuffer");
		return NULL;
	}

	if (!CreatePipelineCache(renderer))
	{
		FNA3D_LogError("Failed to create pipeline cache");
		return NULL;
	}

	/* define sampler counts */

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

	if (!CreateDescriptorSetLayouts(renderer))
	{
		FNA3D_LogError("Failed to create descriptor set layouts");
		return NULL;
	}

	if (!CreateDescriptorPools(renderer))
	{
		FNA3D_LogError("Failed to create descriptor pools");
		return NULL;
	}

	/* init various renderer properties */
	renderer->currentDepthFormat = presentationParameters->depthStencilFormat;

	renderer->currentPipeline = NULL;
	renderer->needNewRenderPass = 1;

	/* check for DXT1/S3TC support */
	renderer->vkGetPhysicalDeviceFormatProperties(
		renderer->physicalDevice,
		XNAToVK_SurfaceFormat[FNA3D_SURFACEFORMAT_DXT1].formatColor,
		&formatPropsBC1
	);
	renderer->vkGetPhysicalDeviceFormatProperties(
		renderer->physicalDevice,
		XNAToVK_SurfaceFormat[FNA3D_SURFACEFORMAT_DXT3].formatColor,
		&formatPropsBC2
	);
	renderer->vkGetPhysicalDeviceFormatProperties(
		renderer->physicalDevice,
		XNAToVK_SurfaceFormat[FNA3D_SURFACEFORMAT_DXT5].formatColor,
		&formatPropsBC3
	);

	renderer->supportsDxt1 = DXTFormatSupported(formatPropsBC1);
	renderer->supportsS3tc = (
		DXTFormatSupported(formatPropsBC2) ||
		DXTFormatSupported(formatPropsBC3)
	);

	/* A mutex for accessing the command buffer */
	renderer->cmdLock = SDL_CreateMutex();

	/* initialize various render object caches */
	hmdefault(renderer->pipelineHashMap, NULL);
	hmdefault(renderer->pipelineLayoutHashMap, NULL);
	hmdefault(renderer->renderPassHashMap, NULL);
	hmdefault(renderer->framebufferHashMap, NULL);
	hmdefault(renderer->samplerStateHashMap, NULL);

	/* Initialize renderer members not covered by SDL_memset('\0') */
	SDL_memset(renderer->multiSampleMask, -1, sizeof(renderer->multiSampleMask)); /* AKA 0xFFFFFFFF */

	if (!CreateQueryPool(renderer))
	{
		FNA3D_LogError("Failed to create query pool");
		return NULL;
	}

	renderer->currentFrame = 0;

	renderer->colorAttachments[0] = &renderer->fauxBackbufferColor.handle;
	renderer->colorAttachmentCount = 1;

	if (renderer->fauxBackbufferDepthFormat != FNA3D_DEPTHFORMAT_NONE)
	{
		renderer->depthStencilAttachment = &renderer->fauxBackbufferDepthStencil.handle;
	}
	
	CreateDummyData(renderer);

	return result;
}

FNA3D_Driver VulkanDriver = {
	"Vulkan",
	VULKAN_PrepareWindowAttributes,
	VULKAN_GetDrawableSize,
	VULKAN_CreateDevice
};

#endif /* FNA3D_DRIVER_VULKAN */
