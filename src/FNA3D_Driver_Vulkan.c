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

typedef struct FNAVulkanImageData
{
	VkImage image;
	VkImageView view;
	VkDeviceMemory memory;
	VkExtent2D dimensions;
	VulkanResourceAccessType resourceAccessType;
	VkDeviceSize memorySize;
} FNAVulkanImageData;

typedef struct FNAVulkanFramebuffer
{
	VkFramebuffer framebuffer;
	FNAVulkanImageData color;
	FNAVulkanImageData depth;
	int32_t width;
	int32_t height;
} FNAVulkanFramebuffer;

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
	uint32_t attachmentCount;
} RenderPassHash;

struct RenderPassHashMap
{
	RenderPassHash key;
	VkRenderPass value;
};

struct FramebufferHashMap
{
	RenderPassHash key;
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
	FNA3D_TextureAddressMode wrapS;
	FNA3D_TextureAddressMode wrapT;
	FNA3D_TextureAddressMode wrapR;
	FNA3D_TextureFilter filter;
	float anisotropy;
	int32_t maxMipmapLevel;
	float lodBias;
	VkImage *next; /* linked list */
};

static VulkanTexture NullTexture =
{
	NULL,
	NULL,
	0,
	0,
	0,
	0,
	FNA3D_SURFACEFORMAT_COLOR,
	FNA3D_TEXTUREADDRESSMODE_WRAP,
	FNA3D_TEXTUREADDRESSMODE_WRAP,
	FNA3D_TEXTUREADDRESSMODE_WRAP,
	FNA3D_TEXTUREFILTER_LINEAR,
	0.0f,
	0,
	0.0f,
	NULL
};

struct VulkanRenderbuffer /* Cast from FNA3D_Renderbuffer */
{
	VulkanColorBuffer *colorBuffer;
	VulkanDepthStencilBuffer *depthBuffer;
};

struct VulkanColorBuffer
{
	VkImageView handle;
	VkExtent2D dimensions;
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

	QueueFamilyIndices queueFamilyIndices;
	VkQueue graphicsQueue;
	VkQueue presentQueue;

	VkSurfaceKHR surface;
	VkSwapchainKHR swapChain;
	FNAVulkanImageData **swapChainImages; /* NOTE: these do not have memory; special case */
	uint32_t swapChainImageCount;
	VkExtent2D swapChainExtent;
	uint32_t currentSwapChainIndex;

	VkCommandPool commandPool;
	VkPipelineCache pipelineCache;

	VkRenderPass renderPass;
	VkFramebuffer framebuffer;
	VkPipeline currentPipeline;
	VkPipelineLayout currentPipelineLayout;
	uint64_t currentVertexBufferBindingHash;
	VkCommandBuffer *commandBuffers;

	FNA3D_Vec4 clearColor;
	float clearDepthValue;
	uint32_t clearStencilValue;

	/* Queries */
	VkQueryPool queryPool;
	int8_t freeQueryIndexStack[MAX_QUERIES];
	int8_t freeQueryIndexStackHead;

	SurfaceFormatMapping surfaceFormatMapping;
	FNA3D_SurfaceFormat fauxBackbufferSurfaceFormat;
	FNAVulkanImageData fauxBackbufferColorImageData;
	VulkanColorBuffer fauxBackbufferColor;
	VulkanDepthStencilBuffer fauxBackbufferDepthStencil;
	VkFramebuffer fauxBackbufferFramebuffer;
	VkRenderPass backbufferRenderPass;
	uint32_t fauxBackbufferWidth;
	uint32_t fauxBackbufferHeight;
	FNA3D_DepthFormat fauxBackbufferDepthFormat;
	VkSampleCountFlagBits fauxBackbufferMultisampleCount;

	VulkanColorBuffer *colorAttachments[MAX_RENDERTARGET_BINDINGS];
	uint32_t colorAttachmentCount;
	VulkanDepthStencilBuffer *depthStencilAttachment;
	uint8_t depthStencilAttachmentActive;

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
	VkBuffer **ldVertUniformBuffers; /* FIXME: init these !! */
	VkBuffer **ldFragUniformBuffers;
	VkDeviceSize *ldVertUniformOffsets;
	VkDeviceSize *ldFragUniformOffsets;

	/* needs to be dynamic because of swap chain count */
	VkBuffer *ldVertexBuffers;
	VkDeviceSize *ldVertexBufferOffsets;
	uint32_t ldVertexBufferCount;

	int32_t stencilRef;

	int32_t numSamplers;
	int32_t numTextureSlots;
	int32_t numVertexTextureSlots;

	/* count needs to be dynamic because of swap chain count */
	uint32_t textureCount;

	VulkanTexture **textures;
	VkSampler *samplers;
	uint8_t *textureNeedsUpdate;
	uint8_t *samplerNeedsUpdate;

	VkDescriptorSetLayout vertUniformBufferDescriptorSetLayouts[2];
	VkDescriptorSetLayout vertSamplerDescriptorSetLayouts[MAX_VERTEXTEXTURE_SAMPLERS];
	VkDescriptorSetLayout fragUniformBufferDescriptorSetLayouts[2];
	VkDescriptorSetLayout fragSamplerDescriptorSetLayouts[MAX_TEXTURE_SAMPLERS];

	/* TODO: reset these at end of frame */
	VkDescriptorPool *samplerDescriptorPools;
	uint32_t activeSamplerDescriptorPoolIndex;
	uint32_t activeSamplerPoolUsage;
	uint32_t samplerDescriptorPoolCapacity;

	VkDescriptorPool *uniformBufferDescriptorPools;
	uint32_t activeUniformBufferDescriptorPoolIndex;
	uint32_t activeUniformBufferPoolUsage;
	uint32_t uniformBufferDescriptorPoolCapacity;

	VkDescriptorImageInfo *vertSamplerImageInfos;
	uint32_t vertSamplerImageInfoCount;
	VkDescriptorImageInfo *fragSamplerImageInfos;
	uint32_t fragSamplerImageInfoCount;
	VkDescriptorBufferInfo *vertUniformBufferInfo; /* count is swap image count */
	VkDescriptorBufferInfo *fragUniformBufferInfo; /* count is swap image count */

	/* TODO: clean this at end of frame */
	VkDescriptorSet *activeDescriptorSets; 
	VkDescriptorSet currentVertSamplerDescriptorSet;
	VkDescriptorSet currentFragSamplerDescriptorSet;
	VkDescriptorSet currentVertUniformBufferDescriptorSet;
	VkDescriptorSet currentFragUniformBufferDescriptorSet;
	uint32_t activeDescriptorSetCount;
	uint32_t activeDescriptorSetCapacity;

	PipelineLayoutHash currentPipelineLayoutHash;

	VkImageMemoryBarrier *imageMemoryBarriers;
	uint32_t imageMemoryBarrierCount;
	uint32_t imageMemoryBarrierCapacity;

	VkBufferMemoryBarrier *bufferMemoryBarriers;
	uint32_t bufferMemoryBarrierCount;
	uint32_t bufferMemoryBarrierCapacity;

	VkPipelineStageFlags currentSrcStageMask;
	VkPipelineStageFlags currentDstStageMask;

	PipelineLayoutHashMap *pipelineLayoutHashMap;
	PipelineHashMap *pipelineHashMap;
	RenderPassHashMap *renderPassHashMap;
	FramebufferHashMap *framebufferHashMap;
	SamplerStateHashMap *samplerStateHashMap;

	VkFence renderQueueFence;
	VkSemaphore imageAvailableSemaphore;
	VkSemaphore renderFinishedSemaphore;

	/* MojoShader Interop */
	MOJOSHADER_vkContext *mojoshaderContext;
	MOJOSHADER_effect *currentEffect;
	const MOJOSHADER_effectTechnique *currentTechnique;
	uint32_t currentPass;

	/* 
	 * Storing references to objects that need to be destroyed
	 * so we don't have to stall or invalidate the command buffer
	 */
	VulkanRenderbuffer **renderbuffersToDestroy;
	uint32_t renderbuffersToDestroyCount;
	uint32_t renderbuffersToDestroyCapacity;
	
	VulkanBuffer **buffersToDestroy;
	uint32_t buffersToDestroyCount;
	uint32_t buffersToDestroyCapacity;

	BufferMemoryWrapper **bufferMemoryWrappersToDestroy;
	uint32_t bufferMemoryWrappersToDestroyCount;
	uint32_t bufferMemoryWrappersToDestroyCapacity;

	VulkanEffect **effectsToDestroy;
	uint32_t effectsToDestroyCount;
	uint32_t effectsToDestroyCapacity;

	FNAVulkanImageData **imageDatasToDestroy;
	uint32_t imageDatasToDestroyCount;
	uint32_t imageDatasToDestroyCapacity;

	uint8_t frameInProgress;
	uint8_t renderPassInProgress;
	uint8_t shouldClearColor;
	uint8_t shouldClearDepth;
	uint8_t shouldClearStencil;
	uint8_t needNewRenderPass;

	/* Capabilities */
	uint8_t supportsDxt1;
	uint8_t supportsS3tc;
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
	const VulkanResourceAccessType *pPrevAccesses;
	uint32_t prevAccessCount;
	const VulkanResourceAccessType *pNextAccesses;
	uint32_t nextAccessCount;
	uint8_t discardContents;
	uint32_t srcQueueFamilyIndex;
	uint32_t dstQueueFamilyIndex;
	VkImage image;
	VkImageSubresourceRange subresourceRange;
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

static void CheckPrimitiveTypeAndBindPipeline(
	FNAVulkanRenderer *renderer,
	FNA3D_PrimitiveType primitiveType
);

static void CheckVertexBufferBindingsAndBindPipeline(
	FNAVulkanRenderer *renderer,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings
);

static void CheckVertexDeclarationAndBindPipeline(
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
	BufferMemoryBarrierCreateInfo barrierCreateInfo
);

static void CreateImageMemoryBarrier(
	FNAVulkanRenderer *renderer,
	ImageMemoryBarrierCreateInfo barrierCreateInfo
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

static void InternalClear(
	FNAVulkanRenderer *renderer,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil,
	uint8_t clearColor,
	uint8_t clearDepth,
	uint8_t clearStencil
);

static void UpdateRenderPass(
	FNA3D_Renderer *driverData
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

static void SubmitPipelineBarrier(
	FNAVulkanRenderer *renderer
);

static void RemoveBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
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

		default:
			errorString = "Unknown";
			break;
	}

	return errorString;
}

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
		renderer->vkCmdBindPipeline(
			renderer->commandBuffers[renderer->currentSwapChainIndex],
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipeline
		);

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

	if (renderer->activeSamplerPoolUsage + additionalCount >= SAMPLER_DESCRIPTOR_POOL_SIZE)
	{
		renderer->activeSamplerDescriptorPoolIndex++;

		/* if we have used all the pools, allocate a new one */
		if (renderer->activeSamplerDescriptorPoolIndex >= renderer->samplerDescriptorPoolCapacity)
		{
			renderer->samplerDescriptorPoolCapacity++;

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

		renderer->activeSamplerPoolUsage = 0;
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
	if (renderer->activeUniformBufferPoolUsage + additionalCount > UNIFORM_BUFFER_DESCRIPTOR_POOL_SIZE
	) {
		renderer->activeUniformBufferDescriptorPoolIndex++;

		/* if we have used all the pools, allocate a new one */
		if (renderer->activeUniformBufferDescriptorPoolIndex >= renderer->uniformBufferDescriptorPoolCapacity)
		{
			renderer->uniformBufferDescriptorPoolCapacity++;

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

		renderer->activeUniformBufferPoolUsage = 0;
	}
}

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
	uint64_t vOff, fOff, vSize, fSize;
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
	uint32_t dynamicOffsets[2];
	uint32_t dynamicOffsetsCount = 0;
	VkDescriptorSet descriptorSetsToBind[4];
	VkResult vulkanResult;

	SubmitPipelineBarrier(renderer);

	vertexSamplerDescriptorSetNeedsUpdate = (renderer->currentVertSamplerDescriptorSet == NULL);
	fragSamplerDescriptorSetNeedsUpdate = (renderer->currentFragSamplerDescriptorSet == NULL);
	vertUniformBufferDescriptorSetNeedsUpdate = (renderer->currentVertUniformBufferDescriptorSet == NULL);
	fragUniformBufferDescriptorSetNeedsUpdate = (renderer->currentFragUniformBufferDescriptorSet == NULL);

	vertArrayOffset = (renderer->currentSwapChainIndex * MAX_TOTAL_SAMPLERS) + MAX_TEXTURE_SAMPLERS;
	fragArrayOffset = (renderer->currentSwapChainIndex * MAX_TOTAL_SAMPLERS);

	for (i = 0; i < renderer->currentPipelineLayoutHash.vertSamplerCount; i++)
	{
		if (	renderer->textureNeedsUpdate[vertArrayOffset + i] || 
				renderer->samplerNeedsUpdate[vertArrayOffset + i]		)
		{		
			renderer->vertSamplerImageInfos[vertArrayOffset + i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			renderer->vertSamplerImageInfos[vertArrayOffset + i].imageView = renderer->textures[vertArrayOffset + i]->imageData->view;
			renderer->vertSamplerImageInfos[vertArrayOffset + i].sampler = renderer->samplers[vertArrayOffset + i];

			vertexSamplerDescriptorSetNeedsUpdate = 1;
			renderer->textureNeedsUpdate[vertArrayOffset + i] = 0;
			renderer->samplerNeedsUpdate[vertArrayOffset + i] = 0;
		}
	}

	for (i = 0; i < renderer->currentPipelineLayoutHash.fragSamplerCount; i++)
	{
		if (	renderer->textureNeedsUpdate[fragArrayOffset + i] || 
				renderer->samplerNeedsUpdate[fragArrayOffset + i]		)
		{
			renderer->fragSamplerImageInfos[fragArrayOffset + i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			renderer->fragSamplerImageInfos[fragArrayOffset + i].imageView = renderer->textures[fragArrayOffset + i]->imageData->view;
			renderer->fragSamplerImageInfos[fragArrayOffset + i].sampler = renderer->samplers[fragArrayOffset + i];

			fragSamplerDescriptorSetNeedsUpdate = 1;
			renderer->textureNeedsUpdate[fragArrayOffset + i] = 0;
			renderer->samplerNeedsUpdate[fragArrayOffset + i] = 0;
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

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkAllocateDescriptorSets", vulkanResult);
			return;
		}
	}
	
	if (vertSamplerIndex != -1)
	{
		for (i = 0; i < renderer->currentPipelineLayoutHash.vertSamplerCount; i++)
		{
			vertSamplerWrite.dstSet = samplerDescriptorSets[vertSamplerIndex];
			vertSamplerWrite.dstBinding = i;
			vertSamplerWrite.dstArrayElement = 0;
			vertSamplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			vertSamplerWrite.descriptorCount = 1;
			vertSamplerWrite.pBufferInfo = NULL;
			vertSamplerWrite.pImageInfo = &renderer->vertSamplerImageInfos[vertArrayOffset];
			vertSamplerWrite.pTexelBufferView = NULL;

			writeDescriptorSets[writeDescriptorSetCount] = vertSamplerWrite;
			writeDescriptorSetCount++;
		}
	}

	if (fragSamplerIndex != -1)
	{
		for (i = 0; i < renderer->currentPipelineLayoutHash.fragSamplerCount; i++)
		{
			fragSamplerWrite.dstSet = samplerDescriptorSets[fragSamplerIndex];
			fragSamplerWrite.dstBinding = i;
			fragSamplerWrite.dstArrayElement = 0;
			fragSamplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			fragSamplerWrite.descriptorCount = 1;
			fragSamplerWrite.pBufferInfo = NULL;
			fragSamplerWrite.pImageInfo = &renderer->fragSamplerImageInfos[fragArrayOffset];
			fragSamplerWrite.pTexelBufferView = NULL;

			writeDescriptorSets[writeDescriptorSetCount] = fragSamplerWrite;
			writeDescriptorSetCount++;
		}
	}

	renderer->activeSamplerPoolUsage += samplerLayoutsToUpdateCount;

	MOJOSHADER_vkGetUniformBuffers(
		(void**) &vUniform,
		&vOff,
		&vSize,
		(void**) &fUniform,
		&fOff,
		&fSize
	);

	/* TODO: we treat buffer pointer updates and offset updates differently for performance */
	if (renderer->currentPipelineLayoutHash.vertUniformBufferCount > 0)
	{
		if (vUniform != renderer->ldVertUniformBuffers[renderer->currentSwapChainIndex])
		{
			renderer->vertUniformBufferInfo[renderer->currentSwapChainIndex].buffer = *vUniform;
			renderer->vertUniformBufferInfo[renderer->currentSwapChainIndex].offset = 0; /* because of dynamic offset */
			renderer->vertUniformBufferInfo[renderer->currentSwapChainIndex].range = VK_WHOLE_SIZE;

			vertUniformBufferDescriptorSetNeedsUpdate = 1;
			renderer->ldVertUniformBuffers[renderer->currentSwapChainIndex] = vUniform;
			renderer->ldVertUniformOffsets[renderer->currentSwapChainIndex] = vOff;
		}
		else if (vOff != renderer->ldVertUniformOffsets[renderer->currentSwapChainIndex])
		{
			renderer->ldVertUniformOffsets[renderer->currentSwapChainIndex] = vOff;
		}
	}

	if (renderer->currentPipelineLayoutHash.fragUniformBufferCount > 0)
	{
		if (fUniform != renderer->ldFragUniformBuffers[renderer->currentSwapChainIndex])
		{
			renderer->fragUniformBufferInfo[renderer->currentSwapChainIndex].buffer = *fUniform;
			renderer->fragUniformBufferInfo[renderer->currentSwapChainIndex].offset = 0; /* because of dynamic offset */
			renderer->fragUniformBufferInfo[renderer->currentSwapChainIndex].range = VK_WHOLE_SIZE;

			fragUniformBufferDescriptorSetNeedsUpdate = 1;
			renderer->ldFragUniformBuffers[renderer->currentSwapChainIndex] = fUniform;
			renderer->ldFragUniformOffsets[renderer->currentSwapChainIndex] = fOff;
		}
		else if (fOff != renderer->ldFragUniformOffsets[renderer->currentSwapChainIndex])
		{
			renderer->ldFragUniformOffsets[renderer->currentSwapChainIndex] = fOff;
		}
	}

	if (vertUniformBufferDescriptorSetNeedsUpdate)
	{
		uniformBufferLayouts[uniformBufferLayoutCount] = renderer->vertUniformBufferDescriptorSetLayouts[
			renderer->currentPipelineLayoutHash.vertUniformBufferCount
		];
		vertUniformBufferIndex = uniformBufferLayoutCount;
		uniformBufferLayoutCount++;
	}

	if (fragUniformBufferDescriptorSetNeedsUpdate)
	{
		uniformBufferLayouts[uniformBufferLayoutCount] = renderer->fragUniformBufferDescriptorSetLayouts[
			renderer->currentPipelineLayoutHash.fragUniformBufferCount
		];
		fragUniformBufferIndex = uniformBufferLayoutCount;
		uniformBufferLayoutCount++;
	}

	if (uniformBufferLayoutCount > 0)
	{
		/* allocate the UBO descriptor sets */
		CheckUniformBufferDescriptorPool(renderer, uniformBufferLayoutCount);

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

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkAllocateDescriptorSets", vulkanResult);
			return;
		}
	}
	
	if (	vertUniformBufferDescriptorSetNeedsUpdate && 
			renderer->currentPipelineLayoutHash.vertUniformBufferCount > 0	)
	{
		vertUniformBufferWrite.dstSet = uniformBufferDescriptorSets[vertUniformBufferIndex];
		vertUniformBufferWrite.dstBinding = 0;
		vertUniformBufferWrite.dstArrayElement = 0;
		vertUniformBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		vertUniformBufferWrite.descriptorCount = 1;
		vertUniformBufferWrite.pBufferInfo = &renderer->vertUniformBufferInfo[renderer->currentSwapChainIndex];
		vertUniformBufferWrite.pImageInfo = NULL;
		vertUniformBufferWrite.pTexelBufferView = NULL;

		writeDescriptorSets[writeDescriptorSetCount] = vertUniformBufferWrite;
		writeDescriptorSetCount++;
	}

	if (	fragUniformBufferDescriptorSetNeedsUpdate &&
			renderer->currentPipelineLayoutHash.fragUniformBufferCount > 0	)
	{
		fragUniformBufferWrite.dstSet = uniformBufferDescriptorSets[fragUniformBufferIndex];
		fragUniformBufferWrite.dstBinding = 0;
		fragUniformBufferWrite.dstArrayElement = 0;
		fragUniformBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		fragUniformBufferWrite.descriptorCount = 1;
		fragUniformBufferWrite.pBufferInfo = &renderer->fragUniformBufferInfo[renderer->currentSwapChainIndex];
		fragUniformBufferWrite.pImageInfo = NULL;
		fragUniformBufferWrite.pTexelBufferView = NULL;

		writeDescriptorSets[writeDescriptorSetCount] = fragUniformBufferWrite;
		writeDescriptorSetCount++;
	}

	renderer->activeUniformBufferPoolUsage += uniformBufferLayoutCount;

	if (renderer->activeDescriptorSetCount + writeDescriptorSetCount > renderer->activeDescriptorSetCapacity)
	{
		renderer->activeDescriptorSetCapacity *= 2;

		renderer->activeDescriptorSets = SDL_realloc(
			renderer->activeDescriptorSets,
			sizeof(VkDescriptorSet) * renderer->activeDescriptorSetCapacity
		);
	}

	/* vert samplers */
	if (vertSamplerIndex != -1)
	{
		renderer->activeDescriptorSets[
			renderer->activeDescriptorSetCount
		] = samplerDescriptorSets[vertSamplerIndex];

		renderer->activeDescriptorSetCount++;

		renderer->currentVertSamplerDescriptorSet = samplerDescriptorSets[vertSamplerIndex];
	}

	/* frag samplers */
	if (fragSamplerIndex != -1)
	{
		renderer->activeDescriptorSets[
			renderer->activeDescriptorSetCount
		] = samplerDescriptorSets[fragSamplerIndex];

		renderer->activeDescriptorSetCount++;

		renderer->currentFragSamplerDescriptorSet = samplerDescriptorSets[fragSamplerIndex];
	}

	/* vert ubo */
	if (vertUniformBufferIndex != -1)
	{
		renderer->activeDescriptorSets[
			renderer->activeDescriptorSetCount
		] = uniformBufferDescriptorSets[vertUniformBufferIndex];

		renderer->activeDescriptorSetCount++;

		renderer->currentVertUniformBufferDescriptorSet = uniformBufferDescriptorSets[vertUniformBufferIndex];
	}

	if (renderer->currentPipelineLayoutHash.vertUniformBufferCount > 0)
	{
		dynamicOffsets[dynamicOffsetsCount] = renderer->ldVertUniformOffsets[
			renderer->currentSwapChainIndex
		];
		dynamicOffsetsCount++;
	}

	/* frag ubo */
	if (fragUniformBufferIndex != -1)
	{
		renderer->activeDescriptorSets[
			renderer->activeDescriptorSetCount
		] = uniformBufferDescriptorSets[fragUniformBufferIndex];

		renderer->activeDescriptorSetCount++;

		renderer->currentFragUniformBufferDescriptorSet = uniformBufferDescriptorSets[fragUniformBufferIndex];
	}

	if (renderer->currentPipelineLayoutHash.fragUniformBufferCount > 0)
	{
		dynamicOffsets[dynamicOffsetsCount] = renderer->ldFragUniformOffsets[
			renderer->currentSwapChainIndex
		];
		dynamicOffsetsCount++;
	}

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

	renderer->vkCmdBindDescriptorSets(
		renderer->commandBuffers[renderer->currentSwapChainIndex],
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		renderer->currentPipelineLayout,
		0,
		4,
		descriptorSetsToBind,
		dynamicOffsetsCount,
		dynamicOffsets
	);
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

	swapChainOffset = MAX_TOTAL_SAMPLERS * renderer->currentSwapChainIndex;

	if (	renderer->ldVertexBuffers[swapChainOffset] != handle ||
			renderer->ldVertexBufferOffsets[swapChainOffset] != offset	)
	{
		renderer->vkCmdBindVertexBuffers(
			renderer->commandBuffers[renderer->currentSwapChainIndex],
			0,
			1,
			&handle,
			&offset
		);
		renderer->ldVertexBuffers[swapChainOffset] = handle;
		renderer->ldVertexBufferOffsets[swapChainOffset] = offset;
	}
}

static void CheckPrimitiveTypeAndBindPipeline(
	FNAVulkanRenderer *renderer,
	FNA3D_PrimitiveType primitiveType
)
{
	if (primitiveType != renderer->currentPrimitiveType)
	{
		renderer->currentPrimitiveType = primitiveType;

		if (renderer->renderPassInProgress)
		{
			BindPipeline(renderer);
		}
	}
}

/* vertex buffer bindings are fixed in the pipeline
 * so we need to bind a new pipeline if it changes
 */
static void CheckVertexBufferBindingsAndBindPipeline(
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

		if (renderer->renderPassInProgress)
		{
			BindPipeline(renderer);	
		}
	}
}

/* vertex buffer bindings are fixed in the pipeline
 * so we need to bind a new pipeline if it changes
 */

static void CheckVertexDeclarationAndBindPipeline(
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

		if (renderer->renderPassInProgress)
		{
			BindPipeline(renderer);	
		}
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
	uint8_t *contentsPtr = (uint8_t*) data;

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
		contentsPtr + buffer->internalOffset,
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
	uint32_t i;

	VkResult waitResult = renderer->vkDeviceWaitIdle(renderer->logicalDevice);

	if (waitResult != VK_SUCCESS)
	{
		LogVulkanResult("vkDeviceWaitIdle", waitResult);
	}

	if (renderer->userVertexBuffer != NULL)
	{
		DestroyBuffer(renderer, renderer->userVertexBuffer);
	}

	if (renderer->userIndexBuffer != NULL)
	{
		DestroyBuffer(renderer, renderer->userIndexBuffer);
	}

	currentBuffer = renderer->buffers;
	while (currentBuffer != NULL)
	{
		nextBuffer = currentBuffer->next;
		DestroyBuffer(renderer, currentBuffer);
		currentBuffer = nextBuffer;
	}

	PerformDeferredDestroys(renderer);

	DestroyFauxBackbuffer(renderer);

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
		renderer->renderQueueFence,
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

	for (i = 0; i < 2; i++)
	{
		renderer->vkDestroyDescriptorSetLayout(
			renderer->logicalDevice,
			renderer->vertUniformBufferDescriptorSetLayouts[i],
			NULL
		);

		renderer->vkDestroyDescriptorSetLayout(
			renderer->logicalDevice,
			renderer->fragUniformBufferDescriptorSetLayouts[i],
			NULL
		);
	}

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

	for (i = 0; i < renderer->uniformBufferDescriptorPoolCapacity; i++)
	{
		renderer->vkDestroyDescriptorPool(
			renderer->logicalDevice,
			renderer->uniformBufferDescriptorPools[i],
			NULL
		);
	}

	for (i = 0; i < renderer->samplerDescriptorPoolCapacity; i++)
	{
		renderer->vkDestroyDescriptorPool(
			renderer->logicalDevice,
			renderer->samplerDescriptorPools[i],
			NULL
		);
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

	for (i = 0; i < renderer->swapChainImageCount; i++)
	{
		renderer->vkDestroyImageView(
			renderer->logicalDevice,
			renderer->swapChainImages[i]->view,
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

	SDL_free(renderer->ldVertexBuffers);
	SDL_free(renderer->ldFragUniformBuffers);
	SDL_free(renderer->ldVertexBufferOffsets);
	SDL_free(renderer->ldFragUniformOffsets);
	SDL_free(renderer->textures);
	SDL_free(renderer->samplers);
	SDL_free(renderer->textureNeedsUpdate);
	SDL_free(renderer->samplerNeedsUpdate);
	SDL_free(renderer->activeDescriptorSets);
	SDL_free(renderer->imageMemoryBarriers);
	SDL_free(renderer->bufferMemoryBarriers);
	SDL_free(renderer->commandBuffers);
	SDL_free(renderer->swapChainImages);
	SDL_free(renderer->renderbuffersToDestroy);
	SDL_free(renderer->buffersToDestroy);
	SDL_free(renderer->bufferMemoryWrappersToDestroy);
	SDL_free(renderer->effectsToDestroy);
	SDL_free(renderer->imageDatasToDestroy);
	SDL_free(renderer);
	SDL_free(device);
}

static void CreateBufferMemoryBarrier(
	FNAVulkanRenderer *renderer,
	BufferMemoryBarrierCreateInfo barrierCreateInfo
) {
	VkPipelineStageFlags srcStages = 0;
	VkPipelineStageFlags dstStages = 0;
	VkBufferMemoryBarrier memoryBarrier = {
		VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER
	};
	uint32_t i;
	VulkanResourceAccessType prevAccess, nextAccess;
	const VulkanResourceAccessInfo *prevAccessInfo, *nextAccessInfo;

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

	if (renderer->imageMemoryBarrierCount + renderer->bufferMemoryBarrierCount > 0)
	{
		if (	srcStages != renderer->currentSrcStageMask ||
				dstStages != renderer->currentDstStageMask	)
		{
			SubmitPipelineBarrier(renderer);
		}
	}

	renderer->currentSrcStageMask = srcStages;
	renderer->currentDstStageMask = dstStages;

	if (renderer->bufferMemoryBarrierCount >= renderer->bufferMemoryBarrierCapacity)
	{
		renderer->bufferMemoryBarrierCapacity *= 2;

		renderer->bufferMemoryBarriers = SDL_realloc(
			renderer->bufferMemoryBarriers,
			sizeof(VkBufferMemoryBarrier) *
			renderer->bufferMemoryBarrierCapacity
		);
	}

	renderer->bufferMemoryBarriers[renderer->bufferMemoryBarrierCount] = memoryBarrier;
	renderer->bufferMemoryBarrierCount++;
}

static void CreateImageMemoryBarrier(
	FNAVulkanRenderer *renderer,
	ImageMemoryBarrierCreateInfo barrierCreateInfo
) {
	VkPipelineStageFlags srcStages = 0;
	VkPipelineStageFlags dstStages = 0;
	VkImageMemoryBarrier memoryBarrier = {
		VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER
	};
	uint32_t i;
	VulkanResourceAccessType prevAccess, nextAccess;
	const VulkanResourceAccessInfo *pPrevAccessInfo, *pNextAccessInfo;

	memoryBarrier.srcAccessMask = 0;
	memoryBarrier.dstAccessMask = 0;
	memoryBarrier.srcQueueFamilyIndex = barrierCreateInfo.srcQueueFamilyIndex;
	memoryBarrier.dstQueueFamilyIndex = barrierCreateInfo.dstQueueFamilyIndex;
	memoryBarrier.image = barrierCreateInfo.image;
	memoryBarrier.subresourceRange = barrierCreateInfo.subresourceRange;
	memoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	memoryBarrier.newLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	for (i = 0; i < barrierCreateInfo.prevAccessCount; i++)
	{
		prevAccess = barrierCreateInfo.pPrevAccesses[i];
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
	}

	for (i = 0; i < barrierCreateInfo.nextAccessCount; i++)
	{
		nextAccess = barrierCreateInfo.pNextAccesses[i];
		pNextAccessInfo = &AccessMap[nextAccess];

		dstStages |= pNextAccessInfo->stageMask;

		memoryBarrier.dstAccessMask |= pNextAccessInfo->accessMask;
		memoryBarrier.newLayout = pNextAccessInfo->imageLayout;
	}

	if (srcStages == 0)
	{
		srcStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	}
	if (dstStages == 0)
	{
		dstStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	}

	if (renderer->imageMemoryBarrierCount + renderer->bufferMemoryBarrierCount > 0)
	{
		if (	srcStages != renderer->currentSrcStageMask ||
				dstStages != renderer->currentDstStageMask	)
		{
			SubmitPipelineBarrier(renderer);
		}
	}

	renderer->currentSrcStageMask = srcStages;
	renderer->currentDstStageMask = dstStages;

	if (renderer->imageMemoryBarrierCount >= renderer->imageMemoryBarrierCapacity)
	{
		renderer->imageMemoryBarrierCapacity *= 2;

		renderer->imageMemoryBarriers = SDL_realloc(
			renderer->imageMemoryBarriers,
			sizeof(VkImageMemoryBarrier) *
			renderer->imageMemoryBarrierCapacity
		);
	}

	renderer->imageMemoryBarriers[renderer->imageMemoryBarrierCount] = memoryBarrier;
	renderer->imageMemoryBarrierCount++;
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

	imageData->resourceAccessType = RESOURCE_ACCESS_NONE;

	result = renderer->vkCreateImage(
		renderer->logicalDevice,
		&imageCreateInfo,
		NULL,
		&imageData->image
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateImage", result);
		FNA3D_LogError("Failed to create image");
		return 0;
	}

	renderer->vkGetImageMemoryRequirements(
		renderer->logicalDevice,
		imageData->image,
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
		imageData->image,
		imageData->memory,
		0
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkBindImageMemory", result);
		return 0;
	}

	imageViewCreateInfo.flags = 0;
	imageViewCreateInfo.image = imageData->image;
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
	result->wrapS = FNA3D_TEXTUREADDRESSMODE_WRAP;
	result->wrapT = FNA3D_TEXTUREADDRESSMODE_WRAP;
	result->wrapR = FNA3D_TEXTUREADDRESSMODE_WRAP;
	result->filter = FNA3D_TEXTUREFILTER_LINEAR;
	result->anisotropy = 1.0f;
	result->maxMipmapLevel = 0; /* FIXME: ???? */
	result->lodBias = 0.0f;
	result->next = NULL;

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
	FNAVulkanImageData *srcImage,
	FNA3D_Rect srcRect,
	FNAVulkanImageData *dstImage,
	FNA3D_Rect dstRect
) {
	VkImageBlit blit;
	VulkanResourceAccessType nextAccessType;
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

	if (srcImage->resourceAccessType != RESOURCE_ACCESS_TRANSFER_READ)
	{
		nextAccessType = RESOURCE_ACCESS_TRANSFER_READ;

		memoryBarrierCreateInfo.pPrevAccesses = &srcImage->resourceAccessType;
		memoryBarrierCreateInfo.prevAccessCount = 1;
		memoryBarrierCreateInfo.pNextAccesses = &nextAccessType;
		memoryBarrierCreateInfo.nextAccessCount = 1;
		memoryBarrierCreateInfo.image = srcImage->image;
		memoryBarrierCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		memoryBarrierCreateInfo.subresourceRange.baseArrayLayer = 0;
		memoryBarrierCreateInfo.subresourceRange.baseMipLevel = 0;
		memoryBarrierCreateInfo.subresourceRange.layerCount = 1;
		memoryBarrierCreateInfo.subresourceRange.levelCount = 1;
		memoryBarrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		memoryBarrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		memoryBarrierCreateInfo.discardContents = 0;

		CreateImageMemoryBarrier(
			renderer,
			memoryBarrierCreateInfo
		);

		srcImage->resourceAccessType = RESOURCE_ACCESS_TRANSFER_READ;
	}

	if (dstImage->resourceAccessType != RESOURCE_ACCESS_TRANSFER_WRITE)
	{
		nextAccessType = RESOURCE_ACCESS_TRANSFER_WRITE;

		memoryBarrierCreateInfo.pPrevAccesses = &dstImage->resourceAccessType;
		memoryBarrierCreateInfo.prevAccessCount = 1;
		memoryBarrierCreateInfo.pNextAccesses = &nextAccessType;
		memoryBarrierCreateInfo.nextAccessCount = 1;
		memoryBarrierCreateInfo.image = dstImage->image;
		memoryBarrierCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		memoryBarrierCreateInfo.subresourceRange.baseArrayLayer = 0;
		memoryBarrierCreateInfo.subresourceRange.baseMipLevel = 0;
		memoryBarrierCreateInfo.subresourceRange.layerCount = 1;
		memoryBarrierCreateInfo.subresourceRange.levelCount = 1;
		memoryBarrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		memoryBarrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		memoryBarrierCreateInfo.discardContents = 0;

		CreateImageMemoryBarrier(
			renderer,
			memoryBarrierCreateInfo
		);

		dstImage->resourceAccessType = RESOURCE_ACCESS_TRANSFER_WRITE;
	}

	SubmitPipelineBarrier(renderer);

	/* TODO: use vkCmdResolveImage for multisampled images */
	/* TODO: blit depth/stencil buffer as well */
	renderer->vkCmdBlitImage(
		renderer->commandBuffers[renderer->currentSwapChainIndex],
		srcImage->image,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		dstImage->image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&blit,
		VK_FILTER_LINEAR /* FIXME: where is the final blit filter defined? -cosmonaut */
	);

	if (dstImage->resourceAccessType != RESOURCE_ACCESS_PRESENT)
	{
		nextAccessType = RESOURCE_ACCESS_PRESENT;

		memoryBarrierCreateInfo.pPrevAccesses = &dstImage->resourceAccessType;
		memoryBarrierCreateInfo.prevAccessCount = 1;
		memoryBarrierCreateInfo.pNextAccesses = &nextAccessType;
		memoryBarrierCreateInfo.nextAccessCount = 1;
		memoryBarrierCreateInfo.image = dstImage->image;
		memoryBarrierCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		memoryBarrierCreateInfo.subresourceRange.baseArrayLayer = 0;
		memoryBarrierCreateInfo.subresourceRange.baseMipLevel = 0;
		memoryBarrierCreateInfo.subresourceRange.layerCount = 1;
		memoryBarrierCreateInfo.subresourceRange.levelCount = 1;
		memoryBarrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		memoryBarrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		memoryBarrierCreateInfo.discardContents = 0;

		CreateImageMemoryBarrier(
			renderer,
			memoryBarrierCreateInfo
		);

		dstImage->resourceAccessType = RESOURCE_ACCESS_PRESENT;
	}

	if (srcImage->resourceAccessType != RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE)
	{
		nextAccessType = RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE;

		memoryBarrierCreateInfo.pPrevAccesses = &srcImage->resourceAccessType;
		memoryBarrierCreateInfo.prevAccessCount = 1;
		memoryBarrierCreateInfo.pNextAccesses = &nextAccessType;
		memoryBarrierCreateInfo.nextAccessCount = 1;
		memoryBarrierCreateInfo.image = srcImage->image;
		memoryBarrierCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		memoryBarrierCreateInfo.subresourceRange.baseArrayLayer = 0;
		memoryBarrierCreateInfo.subresourceRange.baseMipLevel = 0;
		memoryBarrierCreateInfo.subresourceRange.layerCount = 1;
		memoryBarrierCreateInfo.subresourceRange.levelCount = 1;
		memoryBarrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		memoryBarrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		memoryBarrierCreateInfo.discardContents = 0;

		CreateImageMemoryBarrier(
			renderer,
			memoryBarrierCreateInfo
		);

		srcImage->resourceAccessType = RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE;
	}

	SubmitPipelineBarrier(renderer);

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
	setLayouts[2] = renderer->vertUniformBufferDescriptorSetLayouts[hash.vertUniformBufferCount];
	setLayouts[3] = renderer->fragUniformBufferDescriptorSetLayouts[hash.fragUniformBufferCount];

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
	VkPipelineLayout pipelineLayout;
	VkGraphicsPipelineCreateInfo pipelineCreateInfo = {
		VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
	};

	PipelineHash hash = GetPipelineHash(renderer);
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

	dynamicStateInfo.dynamicStateCount = sizeof(dynamicStates)/sizeof(dynamicStates[0]);
	dynamicStateInfo.pDynamicStates = dynamicStates;

	MOJOSHADER_vkGetBoundShaders(&vertShader, &fragShader);

	vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertShaderStageInfo.module = (VkShaderModule) MOJOSHADER_vkGetShaderModule(vertShader);
	vertShaderStageInfo.pName = MOJOSHADER_vkGetShaderParseData(vertShader)->mainfn;

	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = (VkShaderModule) MOJOSHADER_vkGetShaderModule(fragShader);
	fragShaderStageInfo.pName = MOJOSHADER_vkGetShaderParseData(fragShader)->mainfn;

	stageInfos[0] = vertShaderStageInfo;
	stageInfos[1] = fragShaderStageInfo;

	pipelineLayout = FetchPipelineLayout(renderer, vertShader, fragShader);

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
	pipelineCreateInfo.layout = pipelineLayout;
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

	/* putting this here is kind of a kludge -cosmonaut */
	renderer->currentPipelineLayout = pipelineLayout;

	SDL_free(bindingDescriptions);
	SDL_free(attributeDescriptions);

	hmput(renderer->pipelineHashMap, hash, pipeline);
	return pipeline;
}

static VkRenderPass FetchRenderPass(
	FNAVulkanRenderer *renderer
) {
	VkResult vulkanResult;
	VkRenderPass renderPass;
	VkAttachmentDescription attachmentDescriptions[MAX_RENDERTARGET_BINDINGS + 1];
	uint32_t i;
	VkAttachmentReference *colorAttachmentReferences;
	VkAttachmentReference depthStencilAttachmentReference;
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
		attachmentDescriptions[i].format = renderer->surfaceFormatMapping.formatColor;
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

	if (renderer->currentDepthFormat != FNA3D_DEPTHFORMAT_NONE)
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
		renderer->depthStencilAttachmentActive = 1;
	}
	else
	{
		renderer->depthStencilAttachmentActive = 0;
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

	if (renderer->currentDepthFormat == FNA3D_DEPTHFORMAT_NONE)
	{
		subpass.pDepthStencilAttachment = NULL;
	}
	else
	{
		subpass.pDepthStencilAttachment = &depthStencilAttachmentReference;
	}

	subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	subpassDependency.dstSubpass = 0;
	subpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.srcAccessMask = 0;
	subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	subpassDependency.dependencyFlags = 0;

	renderPassCreateInfo.attachmentCount = renderer->colorAttachmentCount + renderer->depthStencilAttachmentActive;
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

static VkFramebuffer FetchFramebuffer(
	FNAVulkanRenderer *renderer,
	VkRenderPass renderPass
) {
	VkFramebuffer framebuffer;
	VkImageView imageViewAttachments[MAX_RENDERTARGET_BINDINGS + 1];
	uint32_t i;
	VkFramebufferCreateInfo framebufferInfo = {
		VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
	};
	VkResult vulkanResult;

	/* framebuffer is cached, can return it */

	RenderPassHash hash = GetRenderPassHash(renderer);
	if (hmgeti(renderer->framebufferHashMap, hash) != -1)
	{
		return hmget(renderer->framebufferHashMap, hash);
	}

	/* otherwise make a new one */

	for (i = 0; i < renderer->colorAttachmentCount; i++)
	{
		imageViewAttachments[i] = renderer->colorAttachments[i]->handle;
	}
	if (renderer->depthStencilAttachmentActive)
	{
		imageViewAttachments[renderer->colorAttachmentCount] = renderer->depthStencilAttachment->handle.view;
	}

	framebufferInfo.flags = 0;
	framebufferInfo.renderPass = renderPass;
	framebufferInfo.attachmentCount = renderer->colorAttachmentCount + renderer->depthStencilAttachmentActive;
	framebufferInfo.pAttachments = imageViewAttachments;
	framebufferInfo.width = renderer->swapChainExtent.width;
	framebufferInfo.height = renderer->swapChainExtent.height;
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
	hash.attachmentCount = renderer->colorAttachmentCount + renderer->depthStencilAttachmentActive;
	return hash;
}

static void BeginRenderPass(
	FNAVulkanRenderer *renderer
) {
	VkRenderPassBeginInfo renderPassBeginInfo = {
		VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
	};
	VkOffset2D offset = { 0, 0 }; /* FIXME: these values are not correct */
	const float blendConstants[] =
	{
		ColorConvert(renderer->blendState.blendFactor.r),
		ColorConvert(renderer->blendState.blendFactor.g),
		ColorConvert(renderer->blendState.blendFactor.b),
		ColorConvert(renderer->blendState.blendFactor.a)
	};
	uint32_t swapChainOffset, i;

	renderer->renderPass = FetchRenderPass(renderer);
	renderer->framebuffer = FetchFramebuffer(renderer, renderer->renderPass);

	renderPassBeginInfo.renderArea.offset = offset;
	renderPassBeginInfo.renderArea.extent = renderer->swapChainExtent;

	renderPassBeginInfo.renderPass = renderer->renderPass;
	renderPassBeginInfo.framebuffer = renderer->framebuffer;

	renderer->vkCmdBeginRenderPass(
		renderer->commandBuffers[renderer->currentSwapChainIndex],
		&renderPassBeginInfo,
		VK_SUBPASS_CONTENTS_INLINE
	);

	renderer->renderPassInProgress = 1;

	SetViewportCommand(renderer);
	SetScissorRectCommand(renderer);
	SetStencilReferenceValueCommand(renderer);

	renderer->vkCmdSetBlendConstants(
		renderer->commandBuffers[renderer->currentSwapChainIndex],
		blendConstants
	);

	renderer->vkCmdSetDepthBias(
		renderer->commandBuffers[renderer->currentSwapChainIndex],
		renderer->rasterizerState.depthBias,
		0, /* unused */
		renderer->rasterizerState.slopeScaleDepthBias
	);

	/* TODO: visibility buffer */

	/* Reset bindings for the current frame in flight */
	
	swapChainOffset = MAX_TOTAL_SAMPLERS * renderer->currentSwapChainIndex;

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

	renderer->ldFragUniformBuffers[renderer->currentSwapChainIndex] = NULL;
	renderer->ldFragUniformOffsets[renderer->currentSwapChainIndex] = 0;
	renderer->ldVertUniformBuffers[renderer->currentSwapChainIndex] = NULL;
	renderer->ldVertUniformOffsets[renderer->currentSwapChainIndex] = 0;
	renderer->currentPipeline = NULL;

	swapChainOffset = MAX_BOUND_VERTEX_BUFFERS * renderer->currentSwapChainIndex;

	for (i = swapChainOffset; i < swapChainOffset + MAX_BOUND_VERTEX_BUFFERS; i++)
	{
		renderer->ldVertexBuffers[i] = NULL;
		renderer->ldVertexBufferOffsets[i] = 0;
	}

	renderer->needNewRenderPass = 0;

	BindPipeline(renderer);
}

void VULKAN_BeginFrame(FNA3D_Renderer *driverData)
{
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VkCommandBufferBeginInfo beginInfo = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
	};
	VulkanResourceAccessType nextAccessType;
	ImageMemoryBarrierCreateInfo barrierCreateInfo;
	VkResult result;
	uint32_t i;

	if (renderer->frameInProgress) return;

	result = renderer->vkWaitForFences(
		renderer->logicalDevice,
		1,
		&renderer->renderQueueFence,
		VK_TRUE,
		UINT64_MAX
	);

	LogVulkanResult("vkWaitForFences", result);

	renderer->vkResetFences(
		renderer->logicalDevice,
		1,
		&renderer->renderQueueFence
	);

	PerformDeferredDestroys(renderer);

	if (renderer->activeDescriptorSetCount != 0)
	{
		for (i = 0; i < renderer->samplerDescriptorPoolCapacity; i++)
		{
			renderer->vkResetDescriptorPool(
				renderer->logicalDevice,
				renderer->samplerDescriptorPools[i],
				0
			);
		}

		renderer->activeUniformBufferDescriptorPoolIndex = 0;
		renderer->activeSamplerPoolUsage = 0;

		for (i = 0; i < renderer->uniformBufferDescriptorPoolCapacity; i++)
		{
			renderer->vkResetDescriptorPool(
				renderer->logicalDevice,
				renderer->uniformBufferDescriptorPools[i],
				0
			);
		}

		renderer->activeUniformBufferDescriptorPoolIndex = 0;
		renderer->activeUniformBufferPoolUsage = 0;

		renderer->activeDescriptorSetCount = 0;
		renderer->currentVertSamplerDescriptorSet = NULL;
		renderer->currentFragSamplerDescriptorSet = NULL;
		renderer->currentVertUniformBufferDescriptorSet = NULL;
		renderer->currentFragUniformBufferDescriptorSet = NULL;
	}

	result = renderer->vkAcquireNextImageKHR(
		renderer->logicalDevice,
		renderer->swapChain,
		UINT64_MAX,
		renderer->imageAvailableSemaphore,
		VK_NULL_HANDLE,
		&renderer->currentSwapChainIndex
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkAcquireNextImageKHR", result);
		return;
	}

	renderer->vkResetCommandBuffer(
		renderer->commandBuffers[renderer->currentSwapChainIndex],
		VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT
	);

	renderer->vkBeginCommandBuffer(
		renderer->commandBuffers[renderer->currentSwapChainIndex],
		&beginInfo
	);

	renderer->frameInProgress = 1;
	renderer->needNewRenderPass = 1;

		/* layout transition faux backbuffer attachments if necessary */

	nextAccessType = RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE;

	if (renderer->fauxBackbufferColorImageData.resourceAccessType != nextAccessType)
	{
		barrierCreateInfo.pPrevAccesses = &renderer->fauxBackbufferColorImageData.resourceAccessType;
		barrierCreateInfo.prevAccessCount = 1;
		barrierCreateInfo.pNextAccesses = &nextAccessType;
		barrierCreateInfo.nextAccessCount = 1;
		barrierCreateInfo.image = renderer->fauxBackbufferColorImageData.image;
		barrierCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrierCreateInfo.subresourceRange.baseArrayLayer = 0;
		barrierCreateInfo.subresourceRange.baseMipLevel = 0;
		barrierCreateInfo.subresourceRange.layerCount = 1;
		barrierCreateInfo.subresourceRange.levelCount = 1;
		barrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrierCreateInfo.discardContents = 0;

		CreateImageMemoryBarrier(renderer, barrierCreateInfo);
		renderer->fauxBackbufferColorImageData.resourceAccessType = nextAccessType;
	}

	nextAccessType = RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_WRITE;

	if (renderer->fauxBackbufferDepthStencil.handle.resourceAccessType != nextAccessType)
	{
		barrierCreateInfo.pPrevAccesses = &renderer->fauxBackbufferDepthStencil.handle.resourceAccessType;
		barrierCreateInfo.prevAccessCount = 1;
		barrierCreateInfo.pNextAccesses = &nextAccessType;
		barrierCreateInfo.nextAccessCount = 1;
		barrierCreateInfo.image = renderer->fauxBackbufferDepthStencil.handle.image;
		barrierCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		barrierCreateInfo.subresourceRange.baseArrayLayer = 0;
		barrierCreateInfo.subresourceRange.baseMipLevel = 0;
		barrierCreateInfo.subresourceRange.layerCount = 1;
		barrierCreateInfo.subresourceRange.levelCount = 1;
		barrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrierCreateInfo.discardContents = 0;

		CreateImageMemoryBarrier(renderer, barrierCreateInfo);
		renderer->fauxBackbufferDepthStencil.handle.resourceAccessType = RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_WRITE;
	}

	SubmitPipelineBarrier(renderer);
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
	int32_t w, h;
	VkResult vulkanResult;
	VkPipelineStageFlags waitStages[] = {
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
	};
	VkSubmitInfo submitInfo = {
		VK_STRUCTURE_TYPE_SUBMIT_INFO
	};
	VkSwapchainKHR swapChains[] = { renderer->swapChain };
	uint32_t imageIndices[] = { renderer->currentSwapChainIndex };
	VkPresentInfoKHR presentInfo = {
		VK_STRUCTURE_TYPE_PRESENT_INFO_KHR
	};
	VulkanBuffer *buf;

	VULKAN_BeginFrame(driverData);
	VULKAN_SetRenderTargets(driverData, NULL, 0, NULL, FNA3D_DEPTHFORMAT_NONE);
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
		VULKAN_GetDrawableSize(overrideWindowHandle, &w, &h);
		dstRect.x = 0;
		dstRect.y = 0;
		dstRect.w = w;
		dstRect.h = h;
	}

	/* special case because of the attachment description */
	renderer->fauxBackbufferColorImageData.resourceAccessType = RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE;

	BlitFramebuffer(
		renderer,
		&renderer->fauxBackbufferColorImageData,
		srcRect,
		renderer->swapChainImages[renderer->currentSwapChainIndex],
		dstRect
	);

	vulkanResult = renderer->vkEndCommandBuffer(
		renderer->commandBuffers[renderer->currentSwapChainIndex]
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkEndCommandBuffer", vulkanResult);
		return;
	}

	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &renderer->imageAvailableSemaphore;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &renderer->renderFinishedSemaphore;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &renderer->commandBuffers[renderer->currentSwapChainIndex];

	result = renderer->vkQueueSubmit(
		renderer->graphicsQueue,
		1,
		&submitInfo,
		renderer->renderQueueFence
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkQueueSubmit", result);
		return;
	}

	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &renderer->renderFinishedSemaphore;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = imageIndices;
	presentInfo.pResults = NULL;

	result = renderer->vkQueuePresentKHR(
		renderer->presentQueue,
		&presentInfo
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkQueuePresentKHR", result);
	}

	buf = renderer->buffers;
	while (buf != NULL)
	{
		buf->internalOffset = 0;
		buf->boundThisFrame = 0;
		buf->prevDataLength = 0;
		buf = buf->next;
	}
	
	MOJOSHADER_vkEndFrame();

	renderer->frameInProgress = 0;
}

/* Drawing */

static void InternalClear(
	FNAVulkanRenderer *renderer,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil,
	uint8_t clearColor,
	uint8_t clearDepth,
	uint8_t clearStencil
) {
	if (!clearColor && !clearDepth && !clearStencil) { return; }

	VkClearAttachment *clearAttachments = SDL_stack_alloc(
		VkClearAttachment,
		renderer->colorAttachmentCount + renderer->depthStencilAttachmentActive
	);
	VkClearRect clearRect;
	VkClearValue clearValue = {{{
		renderer->clearColor.x,
		renderer->clearColor.y,
		renderer->clearColor.z,
		renderer->clearColor.w
	}}};
	uint32_t i;

	clearRect.baseArrayLayer = 0;
	clearRect.layerCount = 1;
	clearRect.rect.offset.x = 0;
	clearRect.rect.offset.y = 0;
	clearRect.rect.extent = renderer->colorAttachments[0]->dimensions;

	if (clearColor)
	{
		renderer->clearColor = *color;

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
		if (renderer->depthStencilAttachmentActive)
		{
			clearAttachments[renderer->colorAttachmentCount].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

			clearRect.rect.extent.width = SDL_max(
				clearRect.rect.extent.width,
				renderer->depthStencilAttachment->handle.dimensions.width
			);
			clearRect.rect.extent.height = SDL_max(
				clearRect.rect.extent.height,
				renderer->depthStencilAttachment->handle.dimensions.height
			);

			if (clearDepth)
			{
				renderer->clearDepthValue = depth;
				clearAttachments[renderer->colorAttachmentCount].clearValue.depthStencil.depth = depth;
			}
			if (clearStencil)
			{
				renderer->clearStencilValue = stencil;
				clearAttachments[renderer->colorAttachmentCount].clearValue.depthStencil.stencil = stencil;
			}
		}
	}

	renderer->vkCmdClearAttachments(
		renderer->commandBuffers[renderer->currentSwapChainIndex],
		renderer->colorAttachmentCount + renderer->depthStencilAttachmentActive,
		clearAttachments,
		1,
		&clearRect
	);

	SDL_stack_free(clearAttachments);
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

	if (renderer->renderPassInProgress)
	{
		InternalClear(
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
		renderer->needNewRenderPass = 1;
		renderer->shouldClearColor = clearColor;
		renderer->clearColor = *color;
		renderer->shouldClearDepth = clearDepth;
		renderer->clearDepthValue = depth;
		renderer->shouldClearStencil = clearStencil;
		renderer->clearStencilValue = stencil;
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

	CheckPrimitiveTypeAndBindPipeline(
		renderer, primitiveType
	);

	BindResources(renderer);

	renderer->vkCmdBindIndexBuffer(
		renderer->commandBuffers[renderer->currentSwapChainIndex],
		indexBuffer->handle,
		totalIndexOffset,
		XNAToVK_IndexType[indexElementSize]
	);

	renderer->vkCmdDrawIndexed(
		renderer->commandBuffers[renderer->currentSwapChainIndex],
		PrimitiveVerts(primitiveType, primitiveCount),
		instanceCount,
		minVertexIndex,
		totalIndexOffset,
		0
	);
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

	CheckPrimitiveTypeAndBindPipeline(
		renderer, primitiveType
	);

	BindResources(renderer);

	renderer->vkCmdDraw(
		renderer->commandBuffers[renderer->currentSwapChainIndex],
		PrimitiveVerts(primitiveType, primitiveCount),
		1,
		vertexStart,
		0
	);
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

	CheckPrimitiveTypeAndBindPipeline(renderer, primitiveType);

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

	renderer->vkCmdBindIndexBuffer(
		renderer->commandBuffers[renderer->currentSwapChainIndex],
		renderer->userIndexBuffer->handle,
		renderer->userIndexBuffer->internalOffset,
		XNAToVK_IndexType[indexElementSize]
	);

	firstIndex = indexOffset / indexSize;
	
	renderer->vkCmdDrawIndexed(
		renderer->commandBuffers[renderer->currentSwapChainIndex],
		numIndices,
		1,
		firstIndex,
		vertexOffset,
		0
	);
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

	CheckPrimitiveTypeAndBindPipeline(renderer, primitiveType);
	BindResources(renderer);

	BindUserVertexBuffer(
		renderer,
		vertexData,
		numVerts,
		vertexOffset
	);

	renderer->vkCmdDraw(
		renderer->commandBuffers[renderer->currentSwapChainIndex],
		numVerts,
		1,
		vertexOffset,
		0
	);
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

		if (renderer->frameInProgress)
		{
			renderer->vkCmdSetBlendConstants(
				renderer->commandBuffers[renderer->currentSwapChainIndex],
				blendConstants
			);
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

		if (renderer->renderPassInProgress)
		{
			BindPipeline(renderer);
		}
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
	if (renderer->frameInProgress) {
		renderer->vkCmdSetBlendConstants(
			renderer->commandBuffers[renderer->currentSwapChainIndex],
			blendConstants
		);

		if (renderer->renderPassInProgress)
		{
			BindPipeline(renderer);
		}
	}
}

void VULKAN_SetDepthStencilState(
	FNA3D_Renderer *driverData,
	FNA3D_DepthStencilState *depthStencilState
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	SDL_memcpy(&renderer->depthStencilState, depthStencilState, sizeof(FNA3D_DepthStencilState));

	/* Dynamic state */
	if (renderer->renderPassInProgress)
	{
		BindPipeline(renderer);
	}
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
		
		if (renderer->renderPassInProgress)
		{
			BindPipeline(renderer);
		}
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
	uint32_t texArrayOffset = (renderer->currentSwapChainIndex * MAX_TOTAL_SAMPLERS);
	uint32_t textureIndex = texArrayOffset + index;
	VulkanResourceAccessType nextAccess;
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

	if (	vulkanTexture == renderer->textures[textureIndex] &&
			sampler->addressU == vulkanTexture->wrapS &&
			sampler->addressV == vulkanTexture->wrapT &&
			sampler->addressW == vulkanTexture->wrapR &&
			sampler->filter == vulkanTexture->filter &&
			sampler->maxAnisotropy == vulkanTexture->anisotropy &&
			sampler->maxMipLevel == vulkanTexture->maxMipmapLevel &&
			sampler->mipMapLevelOfDetailBias == vulkanTexture->lodBias	)
	{
		return;
	}

	if (vulkanTexture != renderer->textures[textureIndex])
	{
		renderer->textures[textureIndex] = vulkanTexture;
		renderer->textureNeedsUpdate[textureIndex] = 1;
	}

	vulkanTexture->wrapS = sampler->addressU;
	vulkanTexture->wrapT = sampler->addressV;
	vulkanTexture->wrapR = sampler->addressW;
	vulkanTexture->filter = sampler->filter;
	vulkanTexture->anisotropy = sampler->maxAnisotropy;
	vulkanTexture->maxMipmapLevel = sampler->maxMipLevel;
	vulkanTexture->lodBias = sampler->mipMapLevelOfDetailBias;

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

	nextAccess = RESOURCE_ACCESS_FRAGMENT_SHADER_READ_SAMPLED_IMAGE;

	if (vulkanTexture->imageData->resourceAccessType != nextAccess)
	{
		memoryBarrierCreateInfo.pPrevAccesses = &vulkanTexture->imageData->resourceAccessType;
		memoryBarrierCreateInfo.prevAccessCount = 1;
		memoryBarrierCreateInfo.pNextAccesses = &nextAccess;
		memoryBarrierCreateInfo.nextAccessCount = 1;
		memoryBarrierCreateInfo.image = vulkanTexture->imageData->image;
		memoryBarrierCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		memoryBarrierCreateInfo.subresourceRange.baseArrayLayer = 0;
		memoryBarrierCreateInfo.subresourceRange.baseMipLevel = 0;
		memoryBarrierCreateInfo.subresourceRange.layerCount = 1;
		memoryBarrierCreateInfo.subresourceRange.levelCount = 1;
		memoryBarrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		memoryBarrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		memoryBarrierCreateInfo.discardContents = 0;

		CreateImageMemoryBarrier(
			renderer,
			memoryBarrierCreateInfo
		);

		vulkanTexture->imageData->resourceAccessType = nextAccess;
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

	CheckVertexBufferBindingsAndBindPipeline(
		renderer,
		bindings,
		numBindings
	);

	UpdateRenderPass(driverData);

	firstVertexBufferIndex = MAX_BOUND_VERTEX_BUFFERS * renderer->currentSwapChainIndex;
	bufferCount = 0;
	
	for (i = 0; i < renderer->numVertexBindings; i++)
	{
		vertexBufferIndex = firstVertexBufferIndex + i;

		vertexBuffer = (VulkanBuffer*) renderer->vertexBindings[i].vertexBuffer;
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
			(renderer->vertexBindings[i].vertexOffset + baseVertex) *
			renderer->vertexBindings[i].vertexDeclaration.vertexStride
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
		renderer->vkCmdBindVertexBuffers(
			renderer->commandBuffers[renderer->currentSwapChainIndex],
			0,
			bufferCount,
			buffers,
			offsets
		);
	}
}

void VULKAN_ApplyVertexDeclaration(
	FNA3D_Renderer *driverData,
	FNA3D_VertexDeclaration *vertexDeclaration,
	void* ptr,
	int32_t vertexOffset
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;

	CheckVertexDeclarationAndBindPipeline(
		renderer,
		vertexDeclaration
	);
	renderer->userVertexStride = vertexDeclaration->vertexStride;

	UpdateRenderPass(driverData);
}

/* Render Targets */

static void UpdateRenderPass(
	FNA3D_Renderer *driverData
) {
	/* TODO: incomplete */
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;

	if (!renderer->needNewRenderPass) { return; }

	VULKAN_BeginFrame(driverData);

	if (renderer->renderPassInProgress)
	{
		EndPass(renderer);
	}

	/* TODO: optimize this to pick a render pass with a LOAD_OP_CLEAR */

	BeginRenderPass(renderer);

	InternalClear(
		renderer,
		&renderer->clearColor,
		renderer->clearDepthValue,
		renderer->clearStencilValue,
		renderer->shouldClearColor,
		renderer->shouldClearDepth,
		renderer->shouldClearStencil
	);

	renderer->needNewRenderPass = 0;
	renderer->shouldClearColor = 0;
	renderer->shouldClearDepth = 0;
	renderer->shouldClearStencil = 0;
}

static void PerformDeferredDestroys(
	FNAVulkanRenderer *renderer
) {
	uint32_t i;

	for (i = 0; i < renderer->renderbuffersToDestroyCount; i++)
	{
		DestroyRenderbuffer(renderer, renderer->renderbuffersToDestroy[i]);
	}
	renderer->renderbuffersToDestroyCount = 0;

	for (i = 0; i < renderer->buffersToDestroyCount; i++)
	{
		DestroyBuffer(renderer, renderer->buffersToDestroy[i]);
	}
	renderer->buffersToDestroyCount = 0;

	for (i = 0; i < renderer->bufferMemoryWrappersToDestroyCount; i++)
	{
		DestroyBufferAndMemory(renderer, renderer->bufferMemoryWrappersToDestroy[i]);
	}
	renderer->bufferMemoryWrappersToDestroyCount = 0;

	for (i = 0; i < renderer->effectsToDestroyCount; i++)
	{
		DestroyEffect(renderer, renderer->effectsToDestroy[i]);
	}
	renderer->effectsToDestroyCount = 0;

	for (i = 0; i < renderer->imageDatasToDestroyCount; i++)
	{
		DestroyImageData(renderer, renderer->imageDatasToDestroy[i]);
	}
	renderer->imageDatasToDestroyCount = 0;

	MOJOSHADER_vkFreeBuffers();
}

static void DestroyBuffer(
	FNAVulkanRenderer *renderer,
	VulkanBuffer *buffer
) {
	VulkanBuffer *vulkanBuffer = (VulkanBuffer*) buffer;

	renderer->vkDestroyBuffer(
		renderer->logicalDevice,
		vulkanBuffer->handle,
		NULL
	);

	renderer->vkFreeMemory(
		renderer->logicalDevice,
		vulkanBuffer->deviceMemory,
		NULL
	);

	SDL_free(vulkanBuffer);
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
		renderer->vkCmdEndRenderPass(
			renderer->commandBuffers[renderer->currentSwapChainIndex]
		);

		renderer->renderPassInProgress = 0;
	}
}

void VULKAN_SetRenderTargets(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *renderbuffer,
	FNA3D_DepthFormat depthFormat
) {
	/* TODO: incomplete */
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	uint32_t i;

	/* Perform any pending clears before switching render targets */

	if (	renderer->shouldClearColor ||
			renderer->shouldClearDepth ||
			renderer->shouldClearStencil	)
	{
		UpdateRenderPass(driverData);
	}

	renderer->needNewRenderPass = 1;

	for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i++)
	{
		renderer->colorAttachments[i] = NULL;
	}
	renderer->depthStencilAttachment = NULL;
	renderer->depthStencilAttachmentActive = 0;

	if (renderTargets == NULL)
	{
		renderer->colorAttachments[0] = &renderer->fauxBackbufferColor;
		if (renderer->fauxBackbufferDepthFormat != FNA3D_DEPTHFORMAT_NONE)
		{
			renderer->depthStencilAttachment = &renderer->fauxBackbufferDepthStencil;
			renderer->depthStencilAttachmentActive = 1;
		}
		return;
	}

	/* TODO: update attachments */
}

/* Dynamic State Functions */

static void SetDepthBiasCommand(FNAVulkanRenderer *renderer)
{
	if (renderer->renderPassInProgress)
	{
		renderer->vkCmdSetDepthBias(
			renderer->commandBuffers[renderer->currentSwapChainIndex],
			renderer->rasterizerState.depthBias,
			0.0, /* no clamp */
			renderer->rasterizerState.slopeScaleDepthBias
		);
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

		renderer->vkCmdSetScissor(
			renderer->commandBuffers[renderer->currentSwapChainIndex],
			0,
			1,
			&vulkanScissorRect
		);
	}
}

static void SetStencilReferenceValueCommand(
	FNAVulkanRenderer *renderer
) {
	if (renderer->renderPassInProgress)
	{
		renderer->vkCmdSetStencilReference(
			renderer->commandBuffers[renderer->currentSwapChainIndex],
			VK_STENCIL_FACE_FRONT_AND_BACK,
			renderer->stencilRef
		);
	}
}

static void SetViewportCommand(
	FNAVulkanRenderer *renderer
) {
	VkViewport vulkanViewport;

	/* v-flipping the viewport for compatibility with other APIs -cosmonaut */
	vulkanViewport.x = renderer->viewport.x;
	vulkanViewport.y = renderer->viewport.h - renderer->viewport.y;
	vulkanViewport.width = renderer->viewport.w;
	vulkanViewport.height = -(renderer->viewport.h - renderer->viewport.y);
	vulkanViewport.minDepth = renderer->viewport.minDepth;
	vulkanViewport.maxDepth = renderer->viewport.maxDepth;

	if (renderer->frameInProgress)
	{
		renderer->vkCmdSetViewport(
			renderer->commandBuffers[renderer->currentSwapChainIndex],
			0,
			1,
			&vulkanViewport
		);
	}
}

static void Stall(FNAVulkanRenderer *renderer)
{
	VkSubmitInfo submitInfo = {
		VK_STRUCTURE_TYPE_SUBMIT_INFO
	};
	VkResult result;
	VulkanBuffer *buf;

	EndPass(renderer);

	submitInfo.waitSemaphoreCount = 0;
	submitInfo.pWaitSemaphores = NULL;
	submitInfo.pWaitDstStageMask = NULL;
	submitInfo.signalSemaphoreCount = 0;
	submitInfo.pSignalSemaphores = NULL;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &renderer->commandBuffers[renderer->currentSwapChainIndex];

	result = renderer->vkQueueSubmit(
		renderer->graphicsQueue,
		1,
		&submitInfo,
		renderer->renderQueueFence
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkQueueSubmit", result);
		return;
	}

	result = renderer->vkQueueWaitIdle(renderer->graphicsQueue);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkQueueWaitIdle", result);
		return;
	}

	renderer->needNewRenderPass = 1;

	buf = renderer->buffers;
	while (buf != NULL)
	{
		buf->internalOffset = 0;
		buf->boundThisFrame = 0;
		buf->prevDataLength = 0;
		buf = buf->next;
	}
}

static void SubmitPipelineBarrier(
	FNAVulkanRenderer *renderer
) {
	uint8_t renderPassWasInProgress;

	InternalBeginFrame(renderer);

	if (renderer->bufferMemoryBarrierCount + renderer->imageMemoryBarrierCount > 0)
	{
		renderPassWasInProgress = renderer->renderPassInProgress;

		if (renderPassWasInProgress)
		{
			EndPass(renderer);
		}

		renderer->vkCmdPipelineBarrier(
			renderer->commandBuffers[renderer->currentSwapChainIndex],
			renderer->currentSrcStageMask,
			renderer->currentDstStageMask,
			0,
			0,
			NULL,
			renderer->bufferMemoryBarrierCount,
			renderer->bufferMemoryBarriers,
			renderer->imageMemoryBarrierCount,
			renderer->imageMemoryBarriers
		);

		renderer->imageMemoryBarrierCount = 0;
		renderer->bufferMemoryBarrierCount = 0;

		if (renderPassWasInProgress)
		{
			BeginRenderPass(renderer);
		}
	}
}

void VULKAN_ResolveTarget(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *target
) {
	/* TODO */
}

/* Backbuffer Functions */

static void DestroyFauxBackbuffer(FNAVulkanRenderer *renderer)
{
	renderer->vkDestroyFramebuffer(
		renderer->logicalDevice,
		renderer->fauxBackbufferFramebuffer,
		NULL
	);

	renderer->vkDestroyRenderPass(
		renderer->logicalDevice,
		renderer->backbufferRenderPass,
		NULL
	);

	renderer->vkDestroyImageView(
		renderer->logicalDevice,
		renderer->fauxBackbufferColorImageData.view,
		NULL
	);

	renderer->vkDestroyImage(
		renderer->logicalDevice,
		renderer->fauxBackbufferColorImageData.image,
		NULL
	);

	renderer->vkFreeMemory(
		renderer->logicalDevice,
		renderer->fauxBackbufferColorImageData.memory,
		NULL
	);

	renderer->vkDestroyImageView(
		renderer->logicalDevice,
		renderer->fauxBackbufferDepthStencil.handle.view,
		NULL
	);

	renderer->vkDestroyImage(
		renderer->logicalDevice,
		renderer->fauxBackbufferDepthStencil.handle.image,
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
	uint32_t texArrayOffset = (renderer->currentSwapChainIndex * MAX_TOTAL_SAMPLERS);
	uint32_t i, textureIndex;

	for (i = 0; i < renderer->colorAttachmentCount; i++)
	{
		if (vulkanTexture->imageData->view == renderer->colorAttachments[i]->handle)
		{
			renderer->colorAttachments[i] = NULL;
		}
	}

	for (i = 0; i < renderer->textureCount; i++)
	{
		textureIndex = texArrayOffset + i;

		if (vulkanTexture == renderer->textures[textureIndex])
		{
			renderer->textures[textureIndex] = &NullTexture;
			renderer->textureNeedsUpdate[textureIndex] = 1;
		}
	}

	QueueImageDestroy(renderer, vulkanTexture->imageData);
	vulkanTexture->imageData = NULL;
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
	VulkanResourceAccessType nextResourceAccessType;
	BufferMemoryBarrierCreateInfo bufferBarrierCreateInfo;
	VkBufferImageCopy imageCopy;

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
	
	nextResourceAccessType = RESOURCE_ACCESS_TRANSFER_WRITE;

	if (vulkanTexture->imageData->resourceAccessType != nextResourceAccessType) 
	{
		imageBarrierCreateInfo.pPrevAccesses = &vulkanTexture->imageData->resourceAccessType;
		imageBarrierCreateInfo.prevAccessCount = 1;
		imageBarrierCreateInfo.pNextAccesses = &nextResourceAccessType;
		imageBarrierCreateInfo.nextAccessCount = 1;
		imageBarrierCreateInfo.image = vulkanTexture->imageData->image;
		imageBarrierCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBarrierCreateInfo.subresourceRange.baseArrayLayer = 0;
		imageBarrierCreateInfo.subresourceRange.baseMipLevel = 0;
		imageBarrierCreateInfo.subresourceRange.layerCount = 1;
		imageBarrierCreateInfo.subresourceRange.levelCount = 1;
		imageBarrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrierCreateInfo.discardContents = 0;

		CreateImageMemoryBarrier(
			renderer,
			imageBarrierCreateInfo
		);

		vulkanTexture->imageData->resourceAccessType = nextResourceAccessType;
	}

	nextResourceAccessType = RESOURCE_ACCESS_TRANSFER_READ;

	if (stagingBuffer->resourceAccessType != nextResourceAccessType)
	{
		bufferBarrierCreateInfo.pPrevAccesses = &stagingBuffer->resourceAccessType;
		bufferBarrierCreateInfo.prevAccessCount = 1;
		bufferBarrierCreateInfo.pNextAccesses = &nextResourceAccessType;
		bufferBarrierCreateInfo.nextAccessCount = 1;
		bufferBarrierCreateInfo.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufferBarrierCreateInfo.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufferBarrierCreateInfo.buffer = stagingBuffer->handle;
		bufferBarrierCreateInfo.offset = stagingBuffer->internalOffset;
		bufferBarrierCreateInfo.size = stagingBuffer->internalBufferSize;

		CreateBufferMemoryBarrier(
			renderer,
			bufferBarrierCreateInfo
		);

		stagingBuffer->resourceAccessType = nextResourceAccessType;
	}

	SubmitPipelineBarrier(renderer);

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

	renderer->vkCmdCopyBufferToImage(
		renderer->commandBuffers[renderer->currentSwapChainIndex],
		stagingBuffer->handle,
		vulkanTexture->imageData->image,
		AccessMap[vulkanTexture->imageData->resourceAccessType].imageLayout,
		1,
		&imageCopy
	);
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
	/* TODO */
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

	imageViewInfo.image = vlkTexture->imageData->image;
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
	renderbuffer->colorBuffer->dimensions = dimensions;

	result = renderer->vkCreateImageView(
		renderer->logicalDevice,
		&imageViewInfo,
		NULL,
		&renderbuffer->colorBuffer->handle
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
			renderbuffer->depthBuffer->handle.image,
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
			renderbuffer->colorBuffer->handle,
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
	if (renderer->imageDatasToDestroyCount + 1 >= renderer->imageDatasToDestroyCapacity)
	{
		renderer->imageDatasToDestroyCapacity *= 2;

		renderer->imageDatasToDestroy = SDL_realloc(
			renderer->imageDatasToDestroy,
			sizeof(FNAVulkanImageData*) * renderer->imageDatasToDestroyCapacity
		);
	}

	renderer->imageDatasToDestroy[renderer->imageDatasToDestroyCount] = imageData;
	renderer->imageDatasToDestroyCount++;
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
		imageData->image,
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
		if (renderer->depthStencilAttachment == vlkRenderBuffer->depthBuffer)
		{
			renderer->depthStencilAttachment = NULL;
			renderer->depthStencilAttachmentActive = 0;
		}
	} 
	else
	{
		// Iterate through color attachments
		for (i = 0; i < MAX_RENDERTARGET_BINDINGS; ++i)
		{
			if (renderer->colorAttachments[i] == vlkRenderBuffer->colorBuffer)
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
	renderer->renderbuffersToDestroyCount++;
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

	if (renderer->bufferMemoryWrappersToDestroyCount + 1 >= renderer->bufferMemoryWrappersToDestroyCapacity)
	{
		renderer->bufferMemoryWrappersToDestroyCapacity *= 2;
		
		renderer->bufferMemoryWrappersToDestroy = SDL_realloc(
			renderer->bufferMemoryWrappersToDestroy,
			sizeof(BufferMemoryWrapper*) * renderer->bufferMemoryWrappersToDestroyCapacity
		);
	}

	renderer->bufferMemoryWrappersToDestroy[renderer->bufferMemoryWrappersToDestroyCount] = bufferMemoryWrapper;
	renderer->bufferMemoryWrappersToDestroyCount++;
}

static void QueueBufferDestroy(
	FNAVulkanRenderer *renderer,
	VulkanBuffer *vulkanBuffer
) {
	if (renderer->buffersToDestroyCount + 1 >= renderer->buffersToDestroyCapacity)
	{
		renderer->buffersToDestroyCapacity *= 2;

		renderer->buffersToDestroy = SDL_realloc(
			renderer->buffersToDestroy,
			sizeof(VulkanBuffer*) * renderer->buffersToDestroyCapacity
		);
	}

	renderer->buffersToDestroy[renderer->buffersToDestroyCount] = vulkanBuffer;
	renderer->buffersToDestroyCount++;
}

static void RemoveBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanBuffer *vulkanBuffer, *curr, *prev;

	vulkanBuffer = (VulkanBuffer*) buffer;

	LinkedList_Remove(
		renderer->buffers,
		vulkanBuffer,
		curr,
		prev
	);

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

	if (renderer->effectsToDestroyCount + 1 >= renderer->effectsToDestroyCapacity)
	{
		renderer->effectsToDestroyCapacity *= 2;

		renderer->effectsToDestroy = SDL_realloc(
			renderer->effectsToDestroy,
			sizeof(VulkanEffect*) * renderer->effectsToDestroyCapacity
		);
	}

	renderer->effectsToDestroy[renderer->effectsToDestroyCount] = vulkanEffect;
	renderer->effectsToDestroyCount++;
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

	renderer->vkCmdResetQueryPool(
		renderer->commandBuffers[renderer->currentSwapChainIndex],
		renderer->queryPool,
		vulkanQuery->index,
		1
	);

	/* Push the now-freed index to the stack */
	renderer->freeQueryIndexStack[vulkanQuery->index] = renderer->freeQueryIndexStackHead;
	renderer->freeQueryIndexStackHead = vulkanQuery->index;

	SDL_free(vulkanQuery);
}

void VULKAN_QueryBegin(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanQuery *vulkanQuery = (VulkanQuery*) query;

	renderer->vkCmdBeginQuery(
		renderer->commandBuffers[renderer->currentSwapChainIndex],
		renderer->queryPool,
		vulkanQuery->index,
		VK_QUERY_CONTROL_PRECISE_BIT
	);
}

void VULKAN_QueryEnd(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanQuery *vulkanQuery = (VulkanQuery*) query;

	/* Assume that the user is calling this in the same pass as they started it */

	renderer->vkCmdEndQuery(
		renderer->commandBuffers[renderer->currentSwapChainIndex],
		renderer->queryPool,
		vulkanQuery->index
	);
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
	/* TODO */
}

/* Function Loading */

static uint8_t LoadGlobalFunctions(void)
{
	vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) SDL_Vulkan_GetVkGetInstanceProcAddr();
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

static uint8_t CheckInstanceExtensionSupport(
	const char** requiredExtensions,
	uint32_t requiredExtensionsLength
) {
	uint32_t extensionCount, i, j;
	VkExtensionProperties *availableExtensions;
	uint8_t extensionFound;

	vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);
	availableExtensions = (VkExtensionProperties*) SDL_malloc(
		extensionCount * sizeof(VkExtensionProperties)
	);
	vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, availableExtensions);

	for (i = 0; i < requiredExtensionsLength; i++)
	{
		extensionFound = 0;

		for (j = 0; j < extensionCount; j++)
		{
			if (SDL_strcmp(requiredExtensions[i], availableExtensions[j].extensionName) == 0)
			{
				extensionFound = 1;
				break;
			}
		}

		if (!extensionFound)
		{
			SDL_free(availableExtensions);
			return 0;
		}
	}

	SDL_free(availableExtensions);
	return 1;
}

static uint8_t CheckDeviceExtensionSupport(
	FNAVulkanRenderer *renderer,
	VkPhysicalDevice physicalDevice,
	const char** requiredExtensions,
	uint32_t requiredExtensionsLength
) {
	uint32_t extensionCount, i, j;
	VkExtensionProperties *availableExtensions;
	uint8_t extensionFound;

	renderer->vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &extensionCount, NULL);
	availableExtensions = (VkExtensionProperties*) SDL_malloc(
		extensionCount * sizeof(VkExtensionProperties)
	);
	renderer->vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &extensionCount, availableExtensions);

	for (i = 0; i < requiredExtensionsLength; i++)
	{
		extensionFound = 0;

		for (j = 0; j < extensionCount; j++)
		{
			if (SDL_strcmp(requiredExtensions[i], availableExtensions[j].extensionName) == 0)
			{
				extensionFound = 1;
				break;
			}
		}

		if (!extensionFound)
		{
			SDL_free(availableExtensions);
			return 0;
		}
	}

	SDL_free(availableExtensions);
	return 1;
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

/* we want a physical device that is dedicated and supports our features */
static uint8_t IsDeviceIdeal(
	FNAVulkanRenderer *renderer,
	VkPhysicalDevice physicalDevice,
	const char** requiredExtensionNames,
	uint32_t requiredExtensionNamesLength,
	VkSurfaceKHR surface,
	QueueFamilyIndices* queueFamilyIndices
) {
	VkPhysicalDeviceProperties deviceProperties;
	uint32_t queueFamilyCount, i;
	SwapChainSupportDetails swapChainSupportDetails;
	VkQueueFamilyProperties *queueProps;
	VkBool32 supportsPresent;

	queueFamilyIndices->graphicsFamily = UINT32_MAX;
	queueFamilyIndices->presentFamily = UINT32_MAX;

	renderer->vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);

	if (deviceProperties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
	{
		return 0;
	}

	if (!CheckDeviceExtensionSupport(renderer, physicalDevice, requiredExtensionNames, requiredExtensionNamesLength))
	{
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

	queueProps = (VkQueueFamilyProperties*) SDL_malloc(
		queueFamilyCount * sizeof(VkQueueFamilyProperties)
	);
	renderer->vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueProps);

	for (i = 0; i < queueFamilyCount; i++) {
		renderer->vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &supportsPresent);
		if (supportsPresent && (queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
			queueFamilyIndices->graphicsFamily = i;
			queueFamilyIndices->presentFamily = i;
			SDL_free(queueProps);
			return 1;
		}
	}

	SDL_free(queueProps);
	return 0;
}

/* FIXME: remove code duplication here */
/* if no dedicated device exists, one that supports our features would be fine */
static uint8_t IsDeviceSuitable(
	FNAVulkanRenderer *renderer,
	VkPhysicalDevice physicalDevice,
	const char** requiredExtensionNames,
	uint32_t requiredExtensionNamesLength,
	VkSurfaceKHR surface,
	QueueFamilyIndices* queueFamilyIndices
) {
	VkPhysicalDeviceProperties deviceProperties;
	uint32_t queueFamilyCount, i;
	SwapChainSupportDetails swapChainSupportDetails;
	VkQueueFamilyProperties *queueProps;
	VkBool32 supportsPresent;

	queueFamilyIndices->graphicsFamily = UINT32_MAX;
	queueFamilyIndices->presentFamily = UINT32_MAX;

	if (!CheckDeviceExtensionSupport(renderer, physicalDevice, requiredExtensionNames, requiredExtensionNamesLength))
	{
		return 0;
	}

	renderer->vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);

	renderer->vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, NULL);

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

	queueProps = (VkQueueFamilyProperties*) SDL_malloc(
		queueFamilyCount * sizeof(VkQueueFamilyProperties)
	);
	renderer->vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueProps);

	for (i = 0; i < queueFamilyCount; i++) {
		renderer->vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &supportsPresent);
		if (supportsPresent && (queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
			queueFamilyIndices->graphicsFamily = i;
			queueFamilyIndices->presentFamily = i;
			SDL_free(queueProps);
			return 1;
		}
	}

	SDL_free(queueProps);
	return 0;
}

static uint8_t CheckValidationLayerSupport(
	const char** validationLayers,
	uint32_t length
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

	for (i = 0; i < length; i++)
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
			SDL_free(availableLayers);
			return 0;
		}
	}

	SDL_free(availableLayers);
	return 1;
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
		for (i = 0; i < availablePresentModesLength; i++)
		{
			if (availablePresentModes[i] == VK_PRESENT_MODE_FIFO_RELAXED_KHR)
			{
				*outputPresentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
				return 1;
			}
		}
	}
	else if (desiredPresentInterval == FNA3D_PRESENTINTERVAL_TWO)
	{
		FNA3D_LogError("FNA3D_PRESENTINTERVAL_TWO not supported in Vulkan");
	}
	else /* FNA3D_PRESENTINTERVAL_IMMEDIATE */
	{
		for (i = 0; i < availablePresentModesLength; i++)
		{
			if (availablePresentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
			{
				*outputPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
				return 1;
			}
		}
	}

	FNA3D_LogInfo("Could not find desired presentation interval, falling back to VK_PRESENT_MODE_FIFO_KHR");

	*outputPresentMode = VK_PRESENT_MODE_FIFO_KHR;
	return 1;
}

static VkExtent2D ChooseSwapExtent(
	const VkSurfaceCapabilitiesKHR capabilities,
	uint32_t width,
	uint32_t height
) {
	VkExtent2D actualExtent = { width, height };

	if (capabilities.currentExtent.width != UINT32_MAX)
	{
		return capabilities.currentExtent;
	}
	else
	{
		actualExtent.width = SDL_max(
			capabilities.minImageExtent.width,
			SDL_min(
				capabilities.maxImageExtent.width,
				actualExtent.width
			)
		);

		actualExtent.height = SDL_max(
			capabilities.minImageExtent.height,
			SDL_min(
				capabilities.maxImageExtent.height,
				actualExtent.height
			)
		);

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

		*attributeDescriptionCount = attributeDescriptionCounter;
	}
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
	appInfo.pApplicationName = "FNA";
	appInfo.apiVersion = VK_MAKE_VERSION(1, 2, 137);

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

	instanceExtensionNames = SDL_stack_alloc(const char*, instanceExtensionCount);

	if (
		!SDL_Vulkan_GetInstanceExtensions(
			(SDL_Window*) presentationParameters->deviceWindowHandle,
			&instanceExtensionCount,
			instanceExtensionNames
		)
	) {
		FNA3D_LogError(
			"SDL_Vulkan_GetInstanceExtensions(): getExtensions %s",
			SDL_GetError()
		);
		goto create_instance_fail;
	}

	if (!CheckInstanceExtensionSupport(instanceExtensionNames, instanceExtensionCount))
	{
		FNA3D_LogError("Required Vulkan instance extensions not supported");
		goto create_instance_fail;
	}

	/* create info structure */

	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledExtensionCount = instanceExtensionCount;
	createInfo.ppEnabledExtensionNames = instanceExtensionNames;
	createInfo.ppEnabledLayerNames = layerNames;

	if (renderer->debugMode)
	{
		createInfo.enabledLayerCount = sizeof(layerNames)/sizeof(layerNames[0]);
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
	uint32_t physicalDeviceCount;
	VkPhysicalDevice physicalDevice;

	QueueFamilyIndices queueFamilyIndices;
	uint8_t physicalDeviceAssigned = 0;

	uint32_t i;

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

	for (i = 0; i < physicalDeviceCount; i++)
	{
		if (IsDeviceIdeal(renderer, physicalDevices[i], deviceExtensionNames, deviceExtensionCount, renderer->surface, &queueFamilyIndices))
		{
			physicalDevice = physicalDevices[i];
			physicalDeviceAssigned = 1;
			break;
		}
	}

	if (!physicalDeviceAssigned)
	{
		for (i = 0; i < physicalDeviceCount; i++)
		{
			if (IsDeviceSuitable(renderer, physicalDevices[i], deviceExtensionNames, deviceExtensionCount, renderer->surface, &queueFamilyIndices))
			{
				physicalDevice = physicalDevices[i];
				physicalDeviceAssigned = 1;
				break;
			}
		}
	}

	if (!physicalDeviceAssigned)
	{
		FNA3D_LogError("No suitable physical devices found.");
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
		"Vulkan Driver: %s %s", 
		renderer->physicalDeviceDriverProperties.driverName,
		renderer->physicalDeviceDriverProperties.driverInfo
	);
	FNA3D_LogInfo(
		"Conformance Version: %u.%u.%u",
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

static uint8_t CreateSwapChain(
	FNAVulkanRenderer *renderer,
	FNA3D_PresentationParameters *presentationParameters
) {
	VkResult vulkanResult;
	SwapChainSupportDetails swapChainSupportDetails;
	VkSurfaceFormatKHR surfaceFormat;
	VkPresentModeKHR presentMode;
	VkExtent2D extent;
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
		return 0;
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
		return 0;
	}

	renderer->surfaceFormatMapping = surfaceFormatMapping;

	if (
		!ChooseSwapPresentMode(
			presentationParameters->presentationInterval,
			swapChainSupportDetails.presentModes,
			swapChainSupportDetails.presentModesLength,
			&presentMode
		)
	) {
		SDL_free(swapChainSupportDetails.formats);
		SDL_free(swapChainSupportDetails.presentModes);
		return 0;
	}

	extent = ChooseSwapExtent(
		swapChainSupportDetails.capabilities,
		presentationParameters->backBufferWidth,
		presentationParameters->backBufferHeight
	);

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

		return 0;
	}

	renderer->vkGetSwapchainImagesKHR(renderer->logicalDevice, renderer->swapChain, &swapChainImageCount, NULL);

	renderer->swapChainImages = (FNAVulkanImageData**) SDL_malloc(sizeof(FNAVulkanImageData*) * swapChainImageCount);
	if (!renderer->swapChainImages)
	{
		SDL_OutOfMemory();
		return 0;
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
			return 0;
		}

		renderer->swapChainImages[i] = (FNAVulkanImageData*) SDL_malloc(sizeof(FNAVulkanImageData));
		SDL_zerop(renderer->swapChainImages[i]);

		renderer->swapChainImages[i]->image = swapChainImages[i];
		renderer->swapChainImages[i]->view = swapChainImageView;
	}

	SDL_stack_free(swapChainImages);
	return 1;
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
	for (i = 0; i < 2; i++)
	{
		layoutBindings = SDL_stack_alloc(VkDescriptorSetLayoutBinding, i);

		for (j = 0; j < i; j++)
		{
			SDL_zero(layoutBinding);
			layoutBinding.binding = 0;
			layoutBinding.descriptorCount = i;
			layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
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
			&renderer->vertUniformBufferDescriptorSetLayouts[i]
		);

		SDL_stack_free(layoutBindings);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateDescriptorSetLayout", vulkanResult);
			return 0;
		}
	}

	/* define all possible vert sampler layouts */
	for (i = 0; i < MAX_VERTEXTEXTURE_SAMPLERS; i++)
	{
		layoutBindings = SDL_stack_alloc(VkDescriptorSetLayoutBinding, i);

		for (j = 0; j < i; j++)
		{
			SDL_zero(layoutBinding);
			layoutBinding.binding = j;
			layoutBinding.descriptorCount = i;
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
	for (i = 0; i < 2; i++)
	{
		layoutBindings = SDL_stack_alloc(VkDescriptorSetLayoutBinding, i);

		for (j = 0; j < i; j++)
		{
			SDL_zero(layoutBinding);
			layoutBinding.binding = 0;
			layoutBinding.descriptorCount = 1;
			layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
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
			&renderer->fragUniformBufferDescriptorSetLayouts[i]
		);

		SDL_stack_free(layoutBindings);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateDescriptorSetLayout", vulkanResult);
			return 0;
		}
	}

	/* define all possible frag sampler layouts */
	for (i = 0; i < MAX_TEXTURE_SAMPLERS; i++)
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
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&renderer->fauxBackbufferColorImageData
		)
	) {
		FNA3D_LogError("Failed to create color attachment image");
		return 0;
	}

	renderer->fauxBackbufferWidth = presentationParameters->backBufferWidth;
	renderer->fauxBackbufferHeight = presentationParameters->backBufferHeight;

	renderer->fauxBackbufferColor.handle = renderer->fauxBackbufferColorImageData.view;
	renderer->fauxBackbufferColor.dimensions = renderer->fauxBackbufferColorImageData.dimensions;
	
	renderer->colorAttachments[0] = &renderer->fauxBackbufferColor;
	renderer->colorAttachmentCount = 1;

	renderer->fauxBackbufferSurfaceFormat = presentationParameters->backBufferFormat;
	renderer->fauxBackbufferMultisampleCount = XNAToVK_SampleCount(presentationParameters->multiSampleCount);

	/* create faux backbuffer depth stencil image */

	renderer->fauxBackbufferDepthFormat = presentationParameters->depthStencilFormat;

	if (renderer->fauxBackbufferDepthFormat != FNA3D_DEPTHFORMAT_NONE)
	{
		vulkanDepthStencilFormat = XNAToVK_DepthFormat(presentationParameters->depthStencilFormat);

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
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				&renderer->fauxBackbufferDepthStencil.handle
			)
		) {
			FNA3D_LogError("Failed to create depth stencil image");
			return 0;
		}

		renderer->depthStencilAttachment = &renderer->fauxBackbufferDepthStencil;
		renderer->depthStencilAttachmentActive = 1;
	}
	else
	{
		renderer->depthStencilAttachmentActive = 0;
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
		1, /* TODO: multiple frames in flight */
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
	VkResult vulkanResult;

	VkDescriptorPoolSize uniformBufferPoolSize;
	VkDescriptorPoolSize samplerPoolSize;

	VkDescriptorPoolCreateInfo uniformBufferPoolInfo = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
	};
	VkDescriptorPoolCreateInfo samplerPoolInfo = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
	};

	renderer->samplerDescriptorPools = (VkDescriptorPool*) SDL_malloc(
		sizeof(VkDescriptorPool)
	);

	renderer->uniformBufferDescriptorPools = (VkDescriptorPool*) SDL_malloc(
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
		&renderer->uniformBufferDescriptorPools[0]
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateDescriptorPool", vulkanResult);
		return 0;
	}

	renderer->uniformBufferDescriptorPoolCapacity = 1;
	renderer->activeUniformBufferDescriptorPoolIndex = 0;
	renderer->activeUniformBufferPoolUsage = 0;

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
		return 0;
	}

	renderer->samplerDescriptorPoolCapacity = 1;
	renderer->activeSamplerDescriptorPoolIndex = 0;
	renderer->activeSamplerPoolUsage = 0;

	renderer->activeDescriptorSetCapacity = renderer->swapChainImageCount * (MAX_TOTAL_SAMPLERS + 2);
	renderer->activeDescriptorSetCount = 0;

	renderer->activeDescriptorSets = (VkDescriptorSet*) SDL_malloc(
		sizeof(VkDescriptorSet) *
		renderer->activeDescriptorSetCapacity
	);

	if (!renderer->activeDescriptorSets)
	{
		SDL_OutOfMemory();
		return 0;
	}

	return 1;
}

static uint8_t AllocateBuffersAndOffsets(
	FNAVulkanRenderer *renderer
) {
	renderer->ldVertUniformBuffers = (VkBuffer**) SDL_malloc(
		sizeof(VkBuffer*) *
		renderer->swapChainImageCount
	);

	if (!renderer->ldVertUniformBuffers)
	{
		SDL_OutOfMemory();
		return 0;
	}

	renderer->ldFragUniformBuffers = (VkBuffer**) SDL_malloc(
		sizeof(VkBuffer*) *
		renderer->swapChainImageCount
	);

	if (!renderer->ldFragUniformBuffers)
	{
		SDL_OutOfMemory();
		return 0;
	}

	renderer->ldVertUniformOffsets = (VkDeviceSize*) SDL_malloc(
		sizeof(VkDeviceSize) *
		renderer->swapChainImageCount
	);

	if (!renderer->ldVertUniformOffsets)
	{
		SDL_OutOfMemory();
		return 0;
	}

	renderer->ldFragUniformOffsets = (VkDeviceSize*) SDL_malloc(
		sizeof(VkDeviceSize) *
		renderer->swapChainImageCount
	);

	if (!renderer->ldFragUniformOffsets)
	{
		SDL_OutOfMemory();
		return 0;
	}

	renderer->ldVertexBufferCount = MAX_BOUND_VERTEX_BUFFERS * renderer->swapChainImageCount;

	renderer->ldVertexBuffers = (VkBuffer*) SDL_malloc(
		sizeof(VkBuffer) * 
		renderer->ldVertexBufferCount
	);
	
	if (!renderer->ldVertexBuffers)
	{
		SDL_OutOfMemory();
		return 0;
	}

	renderer->ldVertexBufferOffsets = (VkDeviceSize*) SDL_malloc(
		sizeof(VkDeviceSize) * 
		renderer->ldVertexBufferCount
	);

	if (!renderer->ldVertexBufferOffsets)
	{
		SDL_OutOfMemory();
		return 0;
	}

	return 1;
}

static uint8_t AllocateTextureAndSamplerStorage(
	FNAVulkanRenderer *renderer
) {
	uint32_t i;

	renderer->textureCount = MAX_TOTAL_SAMPLERS * renderer->swapChainImageCount;

	renderer->textures = (VulkanTexture**) SDL_malloc(
		sizeof(VulkanTexture*) *
		renderer->textureCount
	);

	if (!renderer->textures)
	{
		SDL_OutOfMemory();
		return 0;
	}

	renderer->samplers = (VkSampler*) SDL_malloc(
		sizeof(VkSampler) *
		renderer->textureCount
	);

	if (!renderer->samplers)
	{
		SDL_OutOfMemory();
		return 0;
	}

	renderer->textureNeedsUpdate = (uint8_t*) SDL_malloc(
		sizeof(uint8_t) *
		renderer->textureCount
	);

	if (!renderer->textureNeedsUpdate)
	{
		SDL_OutOfMemory();
		return 0;
	}

	renderer->samplerNeedsUpdate = (uint8_t*) SDL_malloc(
		sizeof(uint8_t) *
		renderer->textureCount
	);

	if (!renderer->samplerNeedsUpdate)
	{
		SDL_OutOfMemory();
		return 0;
	};

	for (i = 0; i < renderer->textureCount; i++)
	{
		renderer->textures[i] = &NullTexture;
		renderer->samplers[i] = NULL;
		renderer->textureNeedsUpdate[i] = 0;
		renderer->samplerNeedsUpdate[i] = 0;
	}

	return 1;
}

static uint8_t CreateCommandPool(
	FNAVulkanRenderer *renderer
) {
	VkResult vulkanResult;
	VkCommandPoolCreateInfo commandPoolCreateInfo;

	SDL_zero(commandPoolCreateInfo);
	commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolCreateInfo.queueFamilyIndex = renderer->queueFamilyIndices.graphicsFamily;
	commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	vulkanResult = renderer->vkCreateCommandPool(renderer->logicalDevice, &commandPoolCreateInfo, NULL, &renderer->commandPool);
	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateCommandPool", vulkanResult);
		return 0;
	}

	return 1;
}

static uint8_t CreateFenceAndSemaphores(
	FNAVulkanRenderer *renderer
) {
	VkResult vulkanResult;

	VkFenceCreateInfo fenceInfo = { 
		VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
	};
	VkSemaphoreCreateInfo semaphoreInfo = {
		VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
	};

	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	vulkanResult = renderer->vkCreateFence(
		renderer->logicalDevice,
		&fenceInfo,
		NULL,
		&renderer->renderQueueFence
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
		&renderer->imageAvailableSemaphore
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
		&renderer->renderFinishedSemaphore
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateSemaphore", vulkanResult);
		return 0;
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

static uint8_t CreateBindingInfos(
	FNAVulkanRenderer *renderer
) {
	uint32_t i;

	renderer->vertSamplerImageInfoCount = renderer->swapChainImageCount * MAX_VERTEXTEXTURE_SAMPLERS;

	renderer->vertSamplerImageInfos = (VkDescriptorImageInfo*) SDL_malloc(
		sizeof(VkDescriptorImageInfo) *
		renderer->vertSamplerImageInfoCount
	);

	if (!renderer->vertSamplerImageInfos)
	{
		SDL_OutOfMemory();
		return 0;
	}

	renderer->fragSamplerImageInfoCount = renderer->swapChainImageCount * MAX_TEXTURE_SAMPLERS;

	renderer->fragSamplerImageInfos = (VkDescriptorImageInfo*) SDL_malloc(
		sizeof(VkDescriptorImageInfo) *
		renderer->fragSamplerImageInfoCount
	);

	if (!renderer->fragSamplerImageInfos)
	{
		SDL_OutOfMemory();
		return 0;
	}

	renderer->vertUniformBufferInfo = (VkDescriptorBufferInfo*) SDL_malloc(
		sizeof(VkDescriptorBufferInfo) *
		renderer->swapChainImageCount
	);

	if (!renderer->vertUniformBufferInfo)
	{
		SDL_OutOfMemory();
		return 0;
	}

	renderer->fragUniformBufferInfo = (VkDescriptorBufferInfo*) SDL_malloc(
		sizeof(VkDescriptorBufferInfo) *
		renderer->swapChainImageCount
	);

	if (!renderer->fragUniformBufferInfo)
	{
		SDL_OutOfMemory();
		return 0;
	}

	for (i = 0; i < renderer->swapChainImageCount * MAX_VERTEXTEXTURE_SAMPLERS; i++)
	{
		renderer->vertSamplerImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		renderer->vertSamplerImageInfos[i].imageView = NULL;
		renderer->vertSamplerImageInfos[i].sampler = NULL;
	}

	for (i = 0; i < renderer->swapChainImageCount * MAX_TEXTURE_SAMPLERS; i++)
	{
		renderer->fragSamplerImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		renderer->fragSamplerImageInfos[i].imageView = NULL;
		renderer->fragSamplerImageInfos[i].sampler = NULL;
	}

	for (i = 0; i < renderer->swapChainImageCount; i++)
	{
		renderer->vertUniformBufferInfo[i].buffer = NULL;
		renderer->vertUniformBufferInfo[i].offset = 0;
		renderer->vertUniformBufferInfo[i].range = 0;

		renderer->fragUniformBufferInfo[i].buffer = NULL;
		renderer->fragUniformBufferInfo[i].offset = 0;
		renderer->fragUniformBufferInfo[i].range = 0;
	}

	return 1;
}

static uint8_t CreateBarrierStorage(
	FNAVulkanRenderer *renderer
) {
	renderer->imageMemoryBarrierCapacity = 256;
	renderer->imageMemoryBarrierCount = 0;

	renderer->imageMemoryBarriers = (VkImageMemoryBarrier*) SDL_malloc(
		sizeof(VkImageMemoryBarrier) *
		renderer->imageMemoryBarrierCapacity
	);

	if (!renderer->imageMemoryBarriers)
	{
		SDL_OutOfMemory();
		return 0;
	}

	renderer->bufferMemoryBarrierCapacity = 256;
	renderer->bufferMemoryBarrierCount = 0;

	renderer->bufferMemoryBarriers = (VkBufferMemoryBarrier*) SDL_malloc(
		sizeof(VkBufferMemoryBarrier) *
		renderer->bufferMemoryBarrierCapacity
	);

	if (!renderer->bufferMemoryBarriers)
	{
		SDL_OutOfMemory();
		return 0;
	}

	return 1;
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

	const char* deviceExtensionNames[] = { "VK_KHR_swapchain" };
	uint32_t deviceExtensionCount = SDL_arraysize(deviceExtensionNames);
	VkFormatProperties formatPropsBC1, formatPropsBC2, formatPropsBC3;

	VkCommandBufferAllocateInfo commandBufferAllocateInfo = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
	};

	/* Create the FNA3D_Device */
	result = (FNA3D_Device*) SDL_malloc(sizeof(FNA3D_Device));
	ASSIGN_DRIVER(VULKAN)

	/* Init the FNAVulkanRenderer */
	renderer = (FNAVulkanRenderer*) SDL_malloc(sizeof(FNAVulkanRenderer));
	SDL_zero(*renderer);

	renderer->debugMode = debugMode;
	renderer->parentDevice = result;
	result->driverData = (FNA3D_Renderer*) renderer;

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

	if (!CreateSwapChain(
		renderer,
		presentationParameters
	)) {
		FNA3D_LogError("Failed to create swap chain");
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

	if (!CreateMojoshaderContext(renderer))
	{
		FNA3D_LogError("Failed to create MojoShader context");
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

	if (!AllocateBuffersAndOffsets(renderer))
	{
		FNA3D_LogError("Failed to allocate buffer pointer memory");
		return NULL;
	}

	if (!AllocateTextureAndSamplerStorage(renderer))
	{
		FNA3D_LogError("Failed to allocate texture and sampler storage memory");
		return NULL;
	}

	if (!CreateCommandPool(renderer))
	{
		FNA3D_LogError("Failed to create command pool");
		return NULL;
	}

	/* init various renderer properties */
	renderer->currentDepthFormat = presentationParameters->depthStencilFormat;
	renderer->commandBuffers = (VkCommandBuffer*) SDL_malloc(sizeof(VkCommandBuffer) * renderer->swapChainImageCount);

	commandBufferAllocateInfo.commandPool = renderer->commandPool;
	commandBufferAllocateInfo.commandBufferCount = renderer->swapChainImageCount;

	renderer->vkAllocateCommandBuffers(
		renderer->logicalDevice,
		&commandBufferAllocateInfo,
		renderer->commandBuffers
	);

	renderer->currentPipeline = NULL;
	renderer->needNewRenderPass = 1;
	renderer->frameInProgress = 0;

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

	/* initialize various render object caches */
	hmdefault(renderer->pipelineHashMap, NULL);
	hmdefault(renderer->pipelineLayoutHashMap, NULL);
	hmdefault(renderer->renderPassHashMap, NULL);
	hmdefault(renderer->framebufferHashMap, NULL);
	hmdefault(renderer->samplerStateHashMap, NULL);

	/* Initialize renderer members not covered by SDL_memset('\0') */
	SDL_memset(renderer->multiSampleMask, -1, sizeof(renderer->multiSampleMask)); /* AKA 0xFFFFFFFF */

	if (!CreateFenceAndSemaphores(renderer))
	{
		FNA3D_LogError("Failed to create fence and semaphores");
		return NULL;
	}

	if (!CreateQueryPool(renderer))
	{
		FNA3D_LogError("Failed to create query pool");
		return NULL;
	}

	if (!CreateBindingInfos(renderer))
	{
		FNA3D_LogError("Failed to create binding info structs");
		return NULL;
	}

	if (!CreateBarrierStorage(renderer))
	{
		FNA3D_LogError("Failed to create barrier storage");
		return NULL;
	}

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

	renderer->bufferMemoryWrappersToDestroyCapacity = 16;
	renderer->bufferMemoryWrappersToDestroyCount = 0;

	renderer->bufferMemoryWrappersToDestroy = (BufferMemoryWrapper**) SDL_malloc(
		sizeof(BufferMemoryWrapper*) *
		renderer->bufferMemoryWrappersToDestroyCapacity
	);

	renderer->effectsToDestroyCapacity = 16;
	renderer->effectsToDestroyCount = 0;

	renderer->effectsToDestroy = (VulkanEffect**) SDL_malloc(
		sizeof(VulkanEffect*) *
		renderer->effectsToDestroyCapacity
	);

	renderer->imageDatasToDestroyCapacity = 16;
	renderer->imageDatasToDestroyCount = 0;

	renderer->imageDatasToDestroy = (FNAVulkanImageData**) SDL_malloc(
		sizeof(FNAVulkanImageData*) *
		renderer->imageDatasToDestroyCapacity
	);

	return result;
}

FNA3D_Driver VulkanDriver = {
	"Vulkan",
	VULKAN_PrepareWindowAttributes,
	VULKAN_GetDrawableSize,
	VULKAN_CreateDevice
};

#endif /* FNA3D_DRIVER_VULKAN */
