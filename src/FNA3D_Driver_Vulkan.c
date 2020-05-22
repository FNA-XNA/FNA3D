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
#define UNIFORM_BUFFER_DESCRIPTOR_POOL_SIZE 16

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

/* vulkan doesnt like null descriptors so we do this crap */
static FNAVulkanImageData NullImageData;

static VulkanTexture NullTexture =
{
	&NullImageData,
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

static VkSampler DummySampler;

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

static VulkanBuffer NullBuffer;

typedef struct FNAVulkanRenderer
{
	FNA3D_Device *parentDevice;
	VkInstance instance;
	VkPhysicalDevice physicalDevice;
	VkDevice logicalDevice;

	QueueFamilyIndices queueFamilyIndices;
	VkQueue graphicsQueue;
	VkQueue presentQueue;

	VkSurfaceKHR surface;
	VkSwapchainKHR swapChain;
	FNAVulkanImageData *swapChainImages;
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
	uint32_t commandBufferCapacity;
	uint32_t commandBufferCount;
	uint8_t commandBufferBegunThisFrame;

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
	VkBuffer **ldVertexBuffers;
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
	uint32_t samplerDescriptorPoolCount;

	VkDescriptorPool *uniformBufferDescriptorPools;
	uint32_t activeUniformBufferDescriptorPoolIndex;
	uint32_t activeUniformBufferPoolUsage;
	uint32_t uniformBufferDescriptorPoolCount;

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

	FNAVulkanFramebuffer *framebuffers;
	uint32_t framebufferCount;

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

	uint8_t frameInProgress;
	uint8_t renderPassInProgress;
	uint8_t shouldClearColor;
	uint8_t shouldClearDepth;
	uint8_t shouldClearStencil;
	uint8_t needNewRenderPass;

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
    VkPipelineStageFlags    stageMask;
    VkAccessFlags           accessMask;
    VkImageLayout           imageLayout;
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
    {   0,
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

	/* RESOURCE_ACCESS_GENERAL */
	{
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
		VK_IMAGE_LAYOUT_GENERAL
	}
};

/* forward declarations */

static uint8_t AllocateAndBeginCommandBuffer(
	FNAVulkanRenderer *renderer
);

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

static void DestroyBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
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

static void Stall(FNAVulkanRenderer *renderer);

static void SubmitPipelineBarrier(
	FNAVulkanRenderer *renderer
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
		VK_FORMAT_B8G8R8A8_UNORM
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
		case FNA3D_DEPTHFORMAT_NONE:
		{
			SDL_LogError(
				SDL_LOG_CATEGORY_APPLICATION,
				"%s\n",
				"Tried to convert FNA3D_DEPTHFORMAT_NONE to VkFormat; something has gone very wrong"
			);
			return VK_FORMAT_UNDEFINED;
		}
	}
}

static float XNAToVK_DepthBiasScale[] =
{
	0.0f,						/* FNA3D_DEPTHFORMAT_NONE */
	(float) ((1 << 16) - 1),	/* FNA3D_DEPTHFORMAT_D16*/
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
			"%s: %s\n",
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
			renderer->commandBuffers[renderer->commandBufferCount - 1],
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
	if (renderer->activeSamplerPoolUsage + additionalCount >= SAMPLER_DESCRIPTOR_POOL_SIZE)
	{
		renderer->activeSamplerDescriptorPoolIndex++;

		/* if we have used all the pools, allocate a new one */
		if (renderer->activeSamplerDescriptorPoolIndex >= renderer->samplerDescriptorPoolCount)
		{
			renderer->samplerDescriptorPoolCount++;

			renderer->samplerDescriptorPools = SDL_realloc(
				renderer->samplerDescriptorPools,
				sizeof(VkDescriptorPool) * renderer->samplerDescriptorPoolCount
			);

			VkDescriptorPoolSize samplerPoolSize;
			samplerPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			samplerPoolSize.descriptorCount = SAMPLER_DESCRIPTOR_POOL_SIZE;

			VkDescriptorPoolCreateInfo samplerPoolInfo = {
				VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
			};
			samplerPoolInfo.poolSizeCount = 1;
			samplerPoolInfo.pPoolSizes = &samplerPoolSize;
			samplerPoolInfo.maxSets = SAMPLER_DESCRIPTOR_POOL_SIZE;

			VkResult vulkanResult = renderer->vkCreateDescriptorPool(
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
	/* if the UBO descriptor pool is maxed out, create another one */
	if (renderer->activeUniformBufferPoolUsage + additionalCount > UNIFORM_BUFFER_DESCRIPTOR_POOL_SIZE
	) {
		renderer->activeUniformBufferDescriptorPoolIndex++;

		/* if we have used all the pools, allocate a new one */
		if (renderer->activeUniformBufferDescriptorPoolIndex >= renderer->uniformBufferDescriptorPoolCount)
		{
			renderer->uniformBufferDescriptorPoolCount++;

			renderer->uniformBufferDescriptorPools = SDL_realloc(
				renderer->uniformBufferDescriptorPools,
				sizeof(VkDescriptorPool) * renderer->uniformBufferDescriptorPoolCount
			);

			VkDescriptorPoolSize bufferPoolSize;
			bufferPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			bufferPoolSize.descriptorCount = UNIFORM_BUFFER_DESCRIPTOR_POOL_SIZE;

			VkDescriptorPoolCreateInfo bufferPoolInfo = {
				VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
			};
			bufferPoolInfo.poolSizeCount = 1;
			bufferPoolInfo.pPoolSizes = &bufferPoolSize;
			bufferPoolInfo.maxSets = UNIFORM_BUFFER_DESCRIPTOR_POOL_SIZE;

			VkResult vulkanResult = renderer->vkCreateDescriptorPool(
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
	VkResult vulkanResult;

	SubmitPipelineBarrier(renderer);

	uint8_t vertexSamplerDescriptorSetNeedsUpdate = (renderer->currentVertSamplerDescriptorSet == NULL);
	uint8_t fragSamplerDescriptorSetNeedsUpdate = (renderer->currentFragSamplerDescriptorSet == NULL);
	uint8_t vertUniformBufferDescriptorSetNeedsUpdate = (renderer->currentVertUniformBufferDescriptorSet == NULL);
	uint8_t fragUniformBufferDescriptorSetNeedsUpdate = (renderer->currentFragUniformBufferDescriptorSet == NULL);

	uint32_t vertArrayOffset = (renderer->currentSwapChainIndex * MAX_TOTAL_SAMPLERS);
	uint32_t fragArrayOffset = (renderer->currentSwapChainIndex * MAX_TOTAL_SAMPLERS) + MAX_VERTEXTEXTURE_SAMPLERS;

	for (uint32_t i = 0; i < renderer->currentPipelineLayoutHash.vertSamplerCount; i++)
	{
		if (	renderer->textureNeedsUpdate[vertArrayOffset + i] || 
				renderer->samplerNeedsUpdate[vertArrayOffset + i]		)
		{		
			renderer->vertSamplerImageInfos[vertArrayOffset + i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			renderer->vertSamplerImageInfos[vertArrayOffset + i].imageView = renderer->textures[vertArrayOffset + i]->imageData->view;
			renderer->vertSamplerImageInfos[vertArrayOffset + i].sampler = renderer->samplers[vertArrayOffset + i];

			vertexSamplerDescriptorSetNeedsUpdate = 1;
			renderer->textureNeedsUpdate[vertArrayOffset + 1] = 0;
			renderer->samplerNeedsUpdate[vertArrayOffset + 1] = 0;
		}
	}

	for (uint32_t i = 0; i < renderer->currentPipelineLayoutHash.fragSamplerCount; i++)
	{
		if (	renderer->textureNeedsUpdate[fragArrayOffset + i] || 
				renderer->samplerNeedsUpdate[fragArrayOffset + i]		)
		{
			renderer->fragSamplerImageInfos[fragArrayOffset + i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			renderer->fragSamplerImageInfos[fragArrayOffset + i].imageView = renderer->textures[fragArrayOffset + i]->imageData->view;
			renderer->fragSamplerImageInfos[fragArrayOffset + i].sampler = renderer->samplers[fragArrayOffset + i];

			fragSamplerDescriptorSetNeedsUpdate = 1;
			renderer->textureNeedsUpdate[fragArrayOffset + 1] = 0;
			renderer->samplerNeedsUpdate[fragArrayOffset + 1] = 0;
		}
	}

	VkWriteDescriptorSet writeDescriptorSets[MAX_TOTAL_SAMPLERS + 2];
	uint32_t writeDescriptorSetCount = 0;

	VkDescriptorSet samplerDescriptorSets[2];

	VkDescriptorSetLayout samplerLayoutsToUpdate[2];
	uint32_t samplerLayoutsToUpdateCount = 0;

	uint32_t vertSamplerIndex = -1;
	uint32_t fragSamplerIndex = -1;

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

		VkDescriptorSetAllocateInfo samplerAllocateInfo = {
			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
		};

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
		for (uint32_t i = 0; i < renderer->currentPipelineLayoutHash.vertSamplerCount; i++)
		{
			VkWriteDescriptorSet vertSamplerWrite = {
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
			};

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
		for (uint32_t i = 0; i < renderer->currentPipelineLayoutHash.fragSamplerCount; i++)
		{
			VkWriteDescriptorSet fragSamplerWrite = {
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
			};

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

	VkBuffer *vUniform, *fUniform;
	unsigned long long vOff, fOff, vSize, fSize;

	MOJOSHADER_vkGetUniformBuffers(
		(void**) &vUniform,
		&vOff,
		&vSize,
		(void**) &fUniform,
		&fOff,
		&fSize
	);

	if (renderer->currentPipelineLayoutHash.vertUniformBufferCount)
	{
		/* TODO: need to treat buffer pointer and offset changes differently */
		if (	vUniform != renderer->ldVertUniformBuffers[renderer->currentSwapChainIndex] ||
				vOff != renderer->ldVertUniformOffsets[renderer->currentSwapChainIndex]		)
		{
			renderer->vertUniformBufferInfo[renderer->currentSwapChainIndex].buffer = *vUniform;
			renderer->vertUniformBufferInfo[renderer->currentSwapChainIndex].offset = vOff;
			renderer->vertUniformBufferInfo[renderer->currentSwapChainIndex].range = vSize;

			vertUniformBufferDescriptorSetNeedsUpdate = 1;
			renderer->ldVertUniformBuffers[renderer->currentSwapChainIndex] = vUniform;
			renderer->ldVertUniformOffsets[renderer->currentSwapChainIndex] = vOff;
		}
	}

	if (renderer->currentPipelineLayoutHash.fragUniformBufferCount)
	{
		if (	fUniform != renderer->ldFragUniformBuffers[renderer->currentSwapChainIndex] ||
				fOff != renderer->ldFragUniformOffsets[renderer->currentSwapChainIndex]		)
		{
			renderer->fragUniformBufferInfo[renderer->currentSwapChainIndex].buffer = *fUniform;
			renderer->fragUniformBufferInfo[renderer->currentSwapChainIndex].offset = fOff;
			renderer->fragUniformBufferInfo[renderer->currentSwapChainIndex].range = fSize;

			fragUniformBufferDescriptorSetNeedsUpdate = 1;
			renderer->ldFragUniformBuffers[renderer->currentSwapChainIndex] = fUniform;
			renderer->ldFragUniformOffsets[renderer->currentSwapChainIndex] = fOff;
		}
	}

	VkDescriptorSetLayout uniformBufferLayouts[2];
	VkDescriptorSet uniformBufferDescriptorSets[2];
	uint32_t uniformBufferLayoutCount = 0;

	int32_t vertUniformBufferIndex = -1;
	int32_t fragUniformBufferIndex = -1;

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

		VkDescriptorSetAllocateInfo uniformBufferAllocateInfo = {
			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
		};

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
		VkWriteDescriptorSet vertUniformBufferWrite = {
			VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
		};

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
		VkWriteDescriptorSet fragUniformBufferWrite = {
			VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
		};

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

	uint32_t dynamicOffsets[2];
	uint32_t dynamicOffsetsCount = 0;

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

	VkDescriptorSet descriptorSetsToBind[4] = {
		renderer->currentVertSamplerDescriptorSet,
		renderer->currentFragSamplerDescriptorSet,
		renderer->currentVertUniformBufferDescriptorSet,
		renderer->currentFragUniformBufferDescriptorSet
	};

	/* TODO: update offsets for UBOs */	
	renderer->vkCmdBindDescriptorSets(
		renderer->commandBuffers[renderer->commandBufferCount - 1],
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
	VkBuffer *handle;

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
	handle = &renderer->userVertexBuffer->handle;

	if (	renderer->ldVertexBuffers[0] != handle ||
			renderer->ldVertexBufferOffsets[0] != offset	)
	{
		renderer->vkCmdBindVertexBuffers(
			renderer->commandBuffers[renderer->commandBufferCount - 1],
			0,
			1,
			handle,
			&offset
		);
		renderer->ldVertexBuffers[0] = handle;
		renderer->ldVertexBufferOffsets[0] = offset;
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

	VkMemoryRequirements memoryRequirements;
	renderer->vkGetBufferMemoryRequirements(
		renderer->logicalDevice,
		buffer->handle,
		&memoryRequirements
	);

	VkMemoryAllocateInfo allocInfo = {
		VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
	};

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
		void *oldContents;
		renderer->vkMapMemory(
			renderer->logicalDevice,
			oldBufferMemory,
			0,
			oldBufferSize,
			0,
			&oldContents
		);

		void *contents;
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

		renderer->vkDestroyBuffer(
			renderer->logicalDevice,
			oldBuffer,
			NULL
		);

		SDL_free(oldBuffer);
	}
}

static VulkanBuffer* CreateBuffer(
	FNAVulkanRenderer *renderer,
	FNA3D_BufferUsage usage,
	VkDeviceSize size,
	VulkanResourceAccessType resourceAccessType
) {
	VulkanBuffer *result, *curr;

	result = SDL_malloc(sizeof(VulkanBuffer));
	SDL_memset(result, '\0', sizeof(VulkanBuffer));

	result->usage = usage;
	result->size = size;
	result->internalBufferSize = size;
	result->resourceAccessType = resourceAccessType;

	VkBufferUsageFlags usageFlags = 0;
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

	if (vulkanBuffer->boundThisFrame)
	{
		if (options == FNA3D_SETDATAOPTIONS_NONE)
		{
			if (renderer->debugMode)
			{
				SDL_LogWarn(
					SDL_LOG_CATEGORY_APPLICATION,
					"%s\n%s\n",
					"Pipeline stall triggered by binding buffer with FNA3D_SETDATAOPTIONS_NONE multiple times in a frame",
					"This is discouraged and will cause performance degradation"
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
		void* contents;
		renderer->vkMapMemory(
			renderer->logicalDevice,
			vulkanBuffer->deviceMemory,
			vulkanBuffer->internalOffset,
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

	void* contents;
	renderer->vkMapMemory(
		renderer->logicalDevice,
		vulkanBuffer->deviceMemory,
		vulkanBuffer->internalOffset + offsetInBytes,
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

	void* contents;
	uint8_t *contentsPtr = (uint8_t*) data;

	renderer->vkMapMemory(
		renderer->logicalDevice,
		buffer->deviceMemory,
		buffer->internalOffset,
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

	VkResult waitResult = renderer->vkWaitForFences(
		renderer->logicalDevice,
		1,
		&renderer->renderQueueFence,
		VK_TRUE,
		UINT64_MAX
	);

	if (waitResult != VK_SUCCESS)
	{
		LogVulkanResult("vkWaitForFences", waitResult);
	}

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

	for (uint32_t i = 0; i < hmlenu(renderer->framebufferHashMap); i++)
	{
		renderer->vkDestroyFramebuffer(
			renderer->logicalDevice,
			renderer->framebufferHashMap[i].value,
			NULL
		);
	}

	renderer->vkDestroyFramebuffer(
		renderer->logicalDevice,
		renderer->fauxBackbufferFramebuffer,
		NULL
	);

	for (uint32_t i = 0; i < hmlenu(renderer->pipelineHashMap); i++)
	{
		renderer->vkDestroyPipeline(
			renderer->logicalDevice,
			renderer->pipelineHashMap[i].value,
			NULL
		);
	}

	for (uint32_t i = 0; i < MAX_VERTEXTEXTURE_SAMPLERS; i++)
	{
		renderer->vkDestroyDescriptorSetLayout(
			renderer->logicalDevice,
			renderer->vertSamplerDescriptorSetLayouts[i],
			NULL
		);
	}

	for (uint32_t i = 0; i < MAX_TEXTURE_SAMPLERS; i++)
	{
		renderer->vkDestroyDescriptorSetLayout(
			renderer->logicalDevice,
			renderer->fragSamplerDescriptorSetLayouts[i],
			NULL
		);
	}

	for (uint32_t i = 0; i < 2; i++)
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

	for (uint32_t i = 0; i < hmlenu(renderer->pipelineLayoutHashMap); i++)
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

	for (uint32_t i = 0; i < renderer->uniformBufferDescriptorPoolCount; i++)
	{
		renderer->vkDestroyDescriptorPool(
			renderer->logicalDevice,
			renderer->uniformBufferDescriptorPools[i],
			NULL
		);
	}

	for (uint32_t i = 0; i < renderer->samplerDescriptorPoolCount; i++)
	{
		renderer->vkDestroyDescriptorPool(
			renderer->logicalDevice,
			renderer->samplerDescriptorPools[i],
			NULL
		);
	}

	for (uint32_t i = 0; i < hmlenu(renderer->renderPassHashMap); i++)
	{
		renderer->vkDestroyRenderPass(
			renderer->logicalDevice,
			renderer->renderPassHashMap[i].value,
			NULL
		);
	}

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

	for (uint32_t i = 0; i < renderer->swapChainImageCount; i++)
	{
		renderer->vkDestroyImageView(
			renderer->logicalDevice,
			renderer->swapChainImages[i].view,
			NULL
		);
	}

	renderer->vkDestroySwapchainKHR(
		renderer->logicalDevice,
		renderer->swapChain,
		NULL
	);

	renderer->vkDestroyDevice(renderer->logicalDevice, NULL);

	renderer->vkDestroySurfaceKHR(
		renderer->instance,
		renderer->surface,
		NULL
	);

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
	SDL_free(renderer->commandBuffers);
	SDL_free(renderer->swapChainImages);
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

	memoryBarrier.srcAccessMask = 0;
	memoryBarrier.dstAccessMask = 0;
	memoryBarrier.srcQueueFamilyIndex = barrierCreateInfo.srcQueueFamilyIndex;
	memoryBarrier.dstQueueFamilyIndex = barrierCreateInfo.dstQueueFamilyIndex;
	memoryBarrier.buffer = barrierCreateInfo.buffer;
	memoryBarrier.offset = barrierCreateInfo.offset;
	memoryBarrier.size = barrierCreateInfo.size;

	for (uint32_t i = 0; i < barrierCreateInfo.prevAccessCount; i++)
	{
		VulkanResourceAccessType prevAccess = barrierCreateInfo.pPrevAccesses[i];
		const VulkanResourceAccessInfo *prevAccessInfo = &AccessMap[prevAccess];

		srcStages |= prevAccessInfo->stageMask;

		if (prevAccess > RESOURCE_ACCESS_END_OF_READ)
		{
			memoryBarrier.srcAccessMask |= prevAccessInfo->accessMask;
		}
	}

	for (uint32_t i = 0; i < barrierCreateInfo.nextAccessCount; i++)
	{
		VulkanResourceAccessType nextAccess = barrierCreateInfo.pNextAccesses[i];
		const VulkanResourceAccessInfo *nextAccessInfo = &AccessMap[nextAccess];

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
	memoryBarrier.srcAccessMask = 0;
	memoryBarrier.dstAccessMask = 0;
	memoryBarrier.srcQueueFamilyIndex = barrierCreateInfo.srcQueueFamilyIndex;
	memoryBarrier.dstQueueFamilyIndex = barrierCreateInfo.dstQueueFamilyIndex;
	memoryBarrier.image = barrierCreateInfo.image;
	memoryBarrier.subresourceRange = barrierCreateInfo.subresourceRange;
	memoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	memoryBarrier.newLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	for (uint32_t i = 0; i < barrierCreateInfo.prevAccessCount; i++)
	{
		VulkanResourceAccessType prevAccess = barrierCreateInfo.pPrevAccesses[i];
		const VulkanResourceAccessInfo *pPrevAccessInfo = &AccessMap[prevAccess];

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

	for (uint32_t i = 0; i < barrierCreateInfo.nextAccessCount; i++)
	{
		VulkanResourceAccessType nextAccess = barrierCreateInfo.pNextAccesses[i];
		const VulkanResourceAccessInfo *pNextAccessInfo = &AccessMap[nextAccess];

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

		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s\n",
			"Failed to create image"
		);

		return 0;
	}

	VkMemoryRequirements memoryRequirements;

	renderer->vkGetImageMemoryRequirements(
		renderer->logicalDevice,
		imageData->image,
		&memoryRequirements
	);

	VkMemoryAllocateInfo allocInfo = {
		VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
	};

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
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s\n",
			"Could not find valid memory type for image creation"
		);

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

	VkImageViewCreateInfo imageViewCreateInfo = {
		VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
	};

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

		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s\n",
			"Failed to create color attachment image view"
		);

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
	VulkanTexture *result = SDL_malloc(sizeof(VulkanTexture));
	SDL_memset(result, '\0', sizeof(VulkanTexture));

	FNAVulkanImageData *imageData = SDL_malloc(sizeof(FNAVulkanImageData));
	SDL_memset(imageData, '\0', sizeof(FNAVulkanImageData));

	VulkanBuffer *stagingBuffer = SDL_malloc(sizeof(VulkanBuffer));
	SDL_memset(stagingBuffer, '\0', sizeof(VulkanBuffer));

	result->imageData = imageData;

	SurfaceFormatMapping surfaceFormatMapping = XNAToVK_SurfaceFormat[format];

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
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
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
		VulkanResourceAccessType nextAccessType = RESOURCE_ACCESS_TRANSFER_READ;

		ImageMemoryBarrierCreateInfo memoryBarrierCreateInfo;
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

		SubmitPipelineBarrier(
			renderer
		);

		srcImage->resourceAccessType = RESOURCE_ACCESS_TRANSFER_READ;
	}

	if (dstImage->resourceAccessType != RESOURCE_ACCESS_TRANSFER_WRITE)
	{
		VulkanResourceAccessType nextAccessType = RESOURCE_ACCESS_TRANSFER_WRITE;

		ImageMemoryBarrierCreateInfo memoryBarrierCreateInfo;
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

		/* FIXME: this can be batched with the above call */
		SubmitPipelineBarrier(
			renderer
		);

		dstImage->resourceAccessType = RESOURCE_ACCESS_TRANSFER_WRITE;
	}

	/* TODO: use vkCmdResolveImage for multisampled images */
	/* TODO: blit depth/stencil buffer as well */
	renderer->vkCmdBlitImage(
		renderer->commandBuffers[renderer->commandBufferCount - 1],
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
		VulkanResourceAccessType nextAccessType = RESOURCE_ACCESS_PRESENT;

		ImageMemoryBarrierCreateInfo memoryBarrierCreateInfo;
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

		SubmitPipelineBarrier(
			renderer
		);

		dstImage->resourceAccessType = RESOURCE_ACCESS_PRESENT;
	}

	if (srcImage->resourceAccessType != RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE)
	{
		VulkanResourceAccessType nextAccessType = RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE;

		ImageMemoryBarrierCreateInfo memoryBarrierCreateInfo;
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

		SubmitPipelineBarrier(
			renderer
		);

		srcImage->resourceAccessType = RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE;
	}

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
	renderer->currentPipelineLayoutHash = hash;

	if (hmgeti(renderer->pipelineLayoutHashMap, hash) != -1)
	{
		return hmget(renderer->pipelineLayoutHashMap, hash);
	}

	VkDescriptorSetLayout setLayouts[4];

	setLayouts[0] = renderer->vertSamplerDescriptorSetLayouts[hash.vertSamplerCount];
	setLayouts[1] = renderer->fragSamplerDescriptorSetLayouts[hash.fragSamplerCount];
	setLayouts[2] = renderer->vertUniformBufferDescriptorSetLayouts[hash.vertUniformBufferCount];
	setLayouts[3] = renderer->fragUniformBufferDescriptorSetLayouts[hash.fragUniformBufferCount];

	VkPipelineLayoutCreateInfo layoutCreateInfo = {
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
	};

	layoutCreateInfo.setLayoutCount = 4;
	layoutCreateInfo.pSetLayouts = setLayouts;

	VkPipelineLayout layout;
	VkResult vulkanResult = renderer->vkCreatePipelineLayout(
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

	PipelineHash hash = GetPipelineHash(renderer);

	if (hmgeti(renderer->pipelineHashMap, hash) != -1)
	{
		return hmget(renderer->pipelineHashMap, hash);
	}

	VkPipeline pipeline;

	/* NOTE: because viewport and scissor are dynamic,
	 * values must be set using the command buffer
	 */
	VkPipelineViewportStateCreateInfo viewportStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	viewportStateInfo.viewportCount = 1;
	viewportStateInfo.scissorCount = 1;

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inputAssemblyInfo.topology = XNAToVK_Topology[renderer->currentPrimitiveType];
	inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

	VkVertexInputBindingDescription bindingDescriptions[renderer->numVertexBindings];
	VkVertexInputAttributeDescription attributeDescriptions[renderer->numVertexBindings * MAX_VERTEX_ATTRIBUTES];
	uint32_t attributeDescriptionCount;

	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
		VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
	};

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

	VkPipelineRasterizationStateCreateInfo rasterizerInfo = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rasterizerInfo.depthClampEnable = VK_FALSE;
	rasterizerInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterizerInfo.polygonMode = XNAToVK_PolygonMode[renderer->rasterizerState.fillMode];
	rasterizerInfo.lineWidth = 1.0f;
	rasterizerInfo.cullMode = XNAToVK_CullMode[renderer->rasterizerState.cullMode];
	rasterizerInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizerInfo.depthBiasEnable = VK_TRUE;

	VkPipelineMultisampleStateCreateInfo multisamplingInfo = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisamplingInfo.sampleShadingEnable = VK_FALSE;
	multisamplingInfo.minSampleShading = 1.0f;
	multisamplingInfo.pSampleMask = renderer->multiSampleMask;
	multisamplingInfo.rasterizationSamples = XNAToVK_SampleCount(renderer->rasterizerState.multiSampleAntiAlias);
	multisamplingInfo.alphaToCoverageEnable = VK_FALSE;
	multisamplingInfo.alphaToOneEnable = VK_FALSE;

	/* FIXME: i think we need one colorblendattachment per colorattachment? */

	VkPipelineColorBlendAttachmentState colorBlendAttachment;
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

	VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	colorBlendStateInfo.logicOpEnable = VK_FALSE;
	colorBlendStateInfo.logicOp = VK_LOGIC_OP_COPY;
	colorBlendStateInfo.attachmentCount = 1;
	colorBlendStateInfo.pAttachments = &colorBlendAttachment;

	VkStencilOpState frontStencilState;
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

	VkStencilOpState backStencilState;
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

	VkPipelineDepthStencilStateCreateInfo depthStencilStateInfo = {
		VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
	};
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
	dynamicStateInfo.dynamicStateCount = sizeof(dynamicStates)/sizeof(dynamicStates[0]);
	dynamicStateInfo.pDynamicStates = dynamicStates;

	MOJOSHADER_vkShader *vertShader, *fragShader;
	MOJOSHADER_vkGetBoundShaders(&vertShader, &fragShader);

	VkPipelineShaderStageCreateInfo vertShaderStageInfo = {
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
	};

	vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertShaderStageInfo.module = (VkShaderModule) MOJOSHADER_vkGetShaderModule(vertShader);
	vertShaderStageInfo.pName = MOJOSHADER_vkGetShaderParseData(vertShader)->mainfn;

	VkPipelineShaderStageCreateInfo fragShaderStageInfo = {
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
	};
	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = (VkShaderModule) MOJOSHADER_vkGetShaderModule(fragShader);
	fragShaderStageInfo.pName = MOJOSHADER_vkGetShaderParseData(fragShader)->mainfn;

	VkPipelineShaderStageCreateInfo stageInfos[2] = {
		vertShaderStageInfo,
		fragShaderStageInfo
	};

	VkPipelineLayout pipelineLayout = FetchPipelineLayout(renderer, vertShader, fragShader);

	VkGraphicsPipelineCreateInfo pipelineCreateInfo = { 
		VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO 
	};
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
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s\n",
			"Something has gone very wrong"
		);
		return NULL;
	}

	/* putting this here is kind of a kludge -cosmonaut */
	renderer->currentPipelineLayout = pipelineLayout;

	hmput(renderer->pipelineHashMap, hash, pipeline);
	return pipeline;
}

static VkRenderPass FetchRenderPass(
	FNAVulkanRenderer *renderer
) {
	VkResult vulkanResult;
	RenderPassHash hash = GetRenderPassHash(renderer);

	/* the render pass is already cached, can return it */

	if (hmgeti(renderer->renderPassHashMap, hash) != -1)
	{
		return hmget(renderer->renderPassHashMap, hash);
	}

	/* otherwise lets make a new one */
	VkRenderPass renderPass;

	VkAttachmentDescription attachmentDescriptions[MAX_RENDERTARGET_BINDINGS + 1];

	for (uint32_t i = 0; i < renderer->colorAttachmentCount; i++)
	{
		/* TODO: handle multisample */

		attachmentDescriptions[i].flags = 0;
		attachmentDescriptions[i].format = renderer->surfaceFormatMapping.formatColor;
		attachmentDescriptions[i].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescriptions[i].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescriptions[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescriptions[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescriptions[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescriptions[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescriptions[i].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

	VkAttachmentReference colorAttachmentReferences[renderer->colorAttachmentCount];
	for (uint32_t i = 0; i < renderer->colorAttachmentCount; i++)
	{
		colorAttachmentReferences[i].attachment = i;
		colorAttachmentReferences[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

	VkAttachmentReference depthStencilAttachmentReference;
	if (renderer->currentDepthFormat != FNA3D_DEPTHFORMAT_NONE)
	{
		depthStencilAttachmentReference.attachment = renderer->colorAttachmentCount;
		depthStencilAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attachmentDescriptions[renderer->colorAttachmentCount].flags = 0;
		attachmentDescriptions[renderer->colorAttachmentCount].format = XNAToVK_DepthFormat(renderer->currentDepthFormat);
		attachmentDescriptions[renderer->colorAttachmentCount].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescriptions[renderer->colorAttachmentCount].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescriptions[renderer->colorAttachmentCount].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescriptions[renderer->colorAttachmentCount].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescriptions[renderer->colorAttachmentCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescriptions[renderer->colorAttachmentCount].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescriptions[renderer->colorAttachmentCount].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		renderer->depthStencilAttachmentActive = 1;
	}
	else
	{
		renderer->depthStencilAttachmentActive = 0;
	}

	VkSubpassDescription subpass;
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

	/* FIXME: what happens here with depth stencil? */

	VkSubpassDependency subpassDependency;
	subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	subpassDependency.dstSubpass = 0;
	subpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.srcAccessMask = 0;
	subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	subpassDependency.dependencyFlags = 0;

	VkRenderPassCreateInfo renderPassCreateInfo = {
		VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO
	};
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

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateRenderPass", vulkanResult);
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s\n",
			"Error during render pass creation. Something has gone very wrong"
		);
		return NULL;
	}

	hmput(renderer->renderPassHashMap, hash, renderPass);
	return renderPass;
}

static VkFramebuffer FetchFramebuffer(
	FNAVulkanRenderer *renderer,
	VkRenderPass renderPass
) {
	RenderPassHash hash = GetRenderPassHash(renderer);

	/* framebuffer is cached, can return it */
	if (hmgeti(renderer->framebufferHashMap, hash) != -1)
	{
		return hmget(renderer->framebufferHashMap, hash);
	}

	/* otherwise make a new one */

	VkFramebuffer framebuffer;

	VkImageView imageViewAttachments[MAX_RENDERTARGET_BINDINGS + 1];

	for (uint32_t i = 0; i < renderer->colorAttachmentCount; i++)
	{
		imageViewAttachments[i] = renderer->colorAttachments[i]->handle;
	}
	if (renderer->depthStencilAttachmentActive)
	{
		imageViewAttachments[renderer->colorAttachmentCount] = renderer->depthStencilAttachment->handle.view;
	}

	VkFramebufferCreateInfo framebufferInfo = {
		VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
	};

	framebufferInfo.flags = 0;
	framebufferInfo.renderPass = renderPass;
	framebufferInfo.attachmentCount = renderer->colorAttachmentCount + renderer->depthStencilAttachmentActive;
	framebufferInfo.pAttachments = imageViewAttachments;
	framebufferInfo.width = renderer->swapChainExtent.width;
	framebufferInfo.height = renderer->swapChainExtent.height;
	framebufferInfo.layers = 1;

	VkResult vulkanResult;

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
	StateHash hash;

	hash = GetSamplerStateHash(*samplerState);

	if (hmgeti(renderer->samplerStateHashMap, hash) != -1)
	{
		return hmget(renderer->samplerStateHashMap, hash);
	}

	VkSamplerCreateInfo createInfo = {
		VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO
	};

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

	VkSampler state;
	VkResult result = renderer->vkCreateSampler(
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
	MOJOSHADER_vkShader *vertShader, *fragShader;
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

static uint8_t AllocateAndBeginCommandBuffer(
	FNAVulkanRenderer *renderer
) {
	VkResult vulkanResult;

	if (renderer->commandBufferCount > 0)
	{
		renderer->vkEndCommandBuffer(
			renderer->commandBuffers[renderer->commandBufferCount - 1]
		);
	}

	renderer->commandBufferCount++;

	if (renderer->commandBufferCount > renderer->commandBufferCapacity)
	{
		renderer->commandBufferCapacity *= 2;

		renderer->commandBuffers = SDL_realloc(
			renderer->commandBuffers,
			sizeof(VkCommandBuffer) * renderer->commandBufferCapacity
		);
	}

	VkCommandBufferAllocateInfo commandBufferAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	commandBufferAllocateInfo.commandPool = renderer->commandPool;
	commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandBufferAllocateInfo.commandBufferCount = 1; /* TODO: change for frames in flight */

	vulkanResult = renderer->vkAllocateCommandBuffers(
		renderer->logicalDevice,
		&commandBufferAllocateInfo,
		&renderer->commandBuffers[renderer->commandBufferCount - 1]
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkAllocateCommandBuffers", vulkanResult);
		return 0;
	}

	VkCommandBufferBeginInfo beginInfo = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
	};

	vulkanResult = renderer->vkBeginCommandBuffer(
		renderer->commandBuffers[renderer->commandBufferCount - 1],
		&beginInfo
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkBeginCommandBuffer", vulkanResult);
		return 0;
	}

	renderer->commandBufferBegunThisFrame = 1;

	return 1;
}

static void BeginRenderPass(
	FNAVulkanRenderer *renderer
)
{
	renderer->renderPass = FetchRenderPass(renderer);
	renderer->framebuffer = FetchFramebuffer(renderer, renderer->renderPass);

	VkRenderPassBeginInfo renderPassBeginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };

	/* FIXME: these values are not correct */
	VkOffset2D offset = { 0, 0 };
	renderPassBeginInfo.renderArea.offset = offset;
	renderPassBeginInfo.renderArea.extent = renderer->swapChainExtent;

	renderPassBeginInfo.renderPass = renderer->renderPass;
	renderPassBeginInfo.framebuffer = renderer->framebuffer;

	renderer->vkCmdBeginRenderPass(
		renderer->commandBuffers[renderer->commandBufferCount - 1],
		&renderPassBeginInfo,
		VK_SUBPASS_CONTENTS_INLINE
	);

	renderer->renderPassInProgress = 1;

	VkViewport viewport;
	viewport.x = renderer->viewport.x;
	viewport.y = renderer->viewport.y;
	viewport.width = (float) renderer->viewport.w;
	viewport.height = (float) renderer->viewport.h;
	viewport.minDepth = (float) renderer->viewport.minDepth;
	viewport.maxDepth = (float) renderer->viewport.maxDepth;

	renderer->vkCmdSetViewport(
		renderer->commandBuffers[renderer->commandBufferCount - 1],
		0,
		1,
		&viewport
	);

	SetScissorRectCommand(renderer);
	SetStencilReferenceValueCommand(renderer);

	const float blendConstants[] =
	{
		ColorConvert(renderer->blendState.blendFactor.r),
		ColorConvert(renderer->blendState.blendFactor.g),
		ColorConvert(renderer->blendState.blendFactor.b),
		ColorConvert(renderer->blendState.blendFactor.a)
	};

	renderer->vkCmdSetBlendConstants(
		renderer->commandBuffers[renderer->commandBufferCount - 1],
		blendConstants
	);

	renderer->vkCmdSetDepthBias(
		renderer->commandBuffers[renderer->commandBufferCount - 1],
		renderer->rasterizerState.depthBias,
		0, /* unused */
		renderer->rasterizerState.slopeScaleDepthBias
	);

	/* TODO: visibility buffer */

	/* Reset bindings for the current frame in flight */
	
	uint32_t swapChainOffset = MAX_TOTAL_SAMPLERS * renderer->currentSwapChainIndex;

	for (uint32_t i = swapChainOffset; i < swapChainOffset + MAX_TOTAL_SAMPLERS; i++)
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

	for (uint32_t i = swapChainOffset; i < swapChainOffset + MAX_BOUND_VERTEX_BUFFERS; i++)
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

	VkResult result;

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

	if (renderer->commandBufferCount == 0)
	{
		renderer->vkResetCommandPool(
			renderer->logicalDevice,
			renderer->commandPool,
			VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT
		);
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

	renderer->frameInProgress = 1;

	AllocateAndBeginCommandBuffer(renderer);
}

void VULKAN_SwapBuffers(
	FNA3D_Renderer *driverData,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
	VkResult result;
	FNA3D_Rect srcRect;
	FNA3D_Rect dstRect;

	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;

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
		int32_t w;
		int32_t h;
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
		&renderer->swapChainImages[renderer->currentSwapChainIndex],
		dstRect
	);

	VkResult vulkanResult = renderer->vkEndCommandBuffer(
		renderer->commandBuffers[renderer->commandBufferCount - 1]
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkEndCommandBuffer", vulkanResult);
		return;
	}

	VkSemaphore signalSemaphores[] = {
		renderer->renderFinishedSemaphore
	};

	VkPipelineStageFlags waitStages[] = {
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
	};

	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &renderer->imageAvailableSemaphore;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;
	submitInfo.commandBufferCount = renderer->commandBufferCount;
	submitInfo.pCommandBuffers = renderer->commandBuffers;

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

	renderer->commandBufferCount = 0;

	VkSwapchainKHR swapChains[] = { renderer->swapChain };
	uint32_t imageIndices[] = { renderer->currentSwapChainIndex };

	VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
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

	VulkanBuffer *buf = renderer->buffers;
	while (buf != NULL)
	{
		buf->internalOffset = 0;
		buf->boundThisFrame = 0;
		buf->prevDataLength = 0;
		buf = buf->next;
	}
	
	MOJOSHADER_vkEndFrame();

	renderer->commandBufferBegunThisFrame = 0;
	renderer->frameInProgress = 0;
}

void VULKAN_SetPresentationInterval(
	FNA3D_Renderer *driverData,
	FNA3D_PresentInterval presentInterval
) {
	/* TODO */
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
	VkClearAttachment clearAttachments[
		renderer->colorAttachmentCount +
		renderer->depthStencilAttachmentActive
	];

	VkClearRect clearRect;
	clearRect.baseArrayLayer = 0;
	clearRect.layerCount = 1;
	clearRect.rect.offset.x = 0;
	clearRect.rect.offset.y = 0;
	clearRect.rect.extent = renderer->colorAttachments[0]->dimensions;

	if (clearColor)
	{
		renderer->clearColor = *color;

		VkClearValue clearValue = {{{
			renderer->clearColor.x,
			renderer->clearColor.y,
			renderer->clearColor.z,
			renderer->clearColor.w
		}}};

		for (uint32_t i = 0; i < renderer->colorAttachmentCount; i++)
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
			clearAttachments[renderer->colorAttachmentCount].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

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
		renderer->commandBuffers[renderer->commandBufferCount - 1],
		renderer->colorAttachmentCount + renderer->depthStencilAttachmentActive,
		clearAttachments,
		1,
		&clearRect
	);
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
		renderer->commandBuffers[renderer->commandBufferCount - 1],
		indexBuffer->handle,
		totalIndexOffset,
		XNAToVK_IndexType[indexElementSize]
	);

	VulkanBuffer *vertexBuffer;
	int32_t offset;

	VkBuffer updatedVertexBuffers[MAX_BOUND_VERTEX_BUFFERS];
	VkDeviceSize updatedOffsets[MAX_BOUND_VERTEX_BUFFERS];
	uint32_t updatedVertexBufferCount = 0;

	for (uint32_t i = 0; i < renderer->numVertexBindings; i++)
	{
		vertexBuffer = (VulkanBuffer*) renderer->vertexBindings[i].vertexBuffer;
		if (vertexBuffer == NULL)
		{
			continue;
		}

		offset = vertexBuffer->internalOffset + (
			(renderer->vertexBindings[i].vertexOffset + baseVertex) *
			renderer->vertexBindings[i].vertexDeclaration.vertexStride
		);

		vertexBuffer->boundThisFrame = 1;
		if (	renderer->ldVertexBuffers[i] != &vertexBuffer->handle ||
				renderer->ldVertexBufferOffsets[i] != offset	)
		{
			updatedVertexBuffers[updatedVertexBufferCount] = vertexBuffer->handle;
			updatedOffsets[updatedVertexBufferCount] = offset;
		}

		updatedVertexBufferCount++;
	}

	renderer->vkCmdBindVertexBuffers(
		renderer->commandBuffers[renderer->commandBufferCount - 1],
		0,
		updatedVertexBufferCount,
		updatedVertexBuffers,
		updatedOffsets
	);

	renderer->vkCmdDrawIndexed(
		renderer->commandBuffers[renderer->commandBufferCount - 1],
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
		renderer->commandBuffers[renderer->commandBufferCount - 1],
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
		renderer->commandBuffers[renderer->commandBufferCount - 1],
		renderer->userIndexBuffer->handle,
		renderer->userIndexBuffer->internalOffset,
		XNAToVK_IndexType[indexElementSize]
	);

	firstIndex = indexOffset / indexSize;
	
	renderer->vkCmdDrawIndexed(
		renderer->commandBuffers[renderer->commandBufferCount - 1],
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
		renderer->commandBuffers[renderer->commandBufferCount - 1],
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

		VkViewport vulkanViewport;
		vulkanViewport.x = viewport->x;
		vulkanViewport.y = viewport->y;
		vulkanViewport.width = viewport->w;
		vulkanViewport.height = viewport->h;
		vulkanViewport.minDepth = viewport->minDepth;
		vulkanViewport.maxDepth = viewport->maxDepth;

		/* dynamic state */
		if (renderer->frameInProgress)
		{
			renderer->vkCmdSetViewport(
				renderer->commandBuffers[renderer->commandBufferCount - 1],
				0,
				1,
				&vulkanViewport
			);
		}
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
	if (	blendFactor->r != renderer->blendState.blendFactor.r ||
			blendFactor->g != renderer->blendState.blendFactor.g ||
			blendFactor->b != renderer->blendState.blendFactor.b ||
			blendFactor->a != renderer->blendState.blendFactor.a	)
	{
		renderer->blendState.blendFactor = *blendFactor;

		const float blendConstants[] =
		{
			blendFactor->r,
			blendFactor->g,
			blendFactor->b,
			blendFactor->a
		};

		if (renderer->frameInProgress)
		{
			renderer->vkCmdSetBlendConstants(
				renderer->commandBuffers[renderer->commandBufferCount - 1],
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
		SDL_LogWarn(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s\n%s\n",
			"Using a 32-bit multisample mask for a 64-sample rasterizer",
			"Last 32 bits of the mask will all be 1"
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
	SDL_memcpy(&renderer->blendState, blendState, sizeof(FNA3D_BlendState));

	/* Dynamic state */
	if (renderer->frameInProgress) {
		const float blendConstants[] =
			{
				blendState->blendFactor.r,
				blendState->blendFactor.g,
				blendState->blendFactor.b,
				blendState->blendFactor.a
			};

		renderer->vkCmdSetBlendConstants(
			renderer->commandBuffers[renderer->commandBufferCount - 1],
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

	uint32_t fragArrayOffset = (renderer->currentSwapChainIndex * MAX_TOTAL_SAMPLERS) + MAX_VERTEXTEXTURE_SAMPLERS;
	uint32_t textureIndex = fragArrayOffset + index;

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

	VulkanResourceAccessType nextAccess = RESOURCE_ACCESS_FRAGMENT_SHADER_READ_SAMPLED_IMAGE;

	if (vulkanTexture->imageData->resourceAccessType != nextAccess)
	{
		ImageMemoryBarrierCreateInfo memoryBarrierCreateInfo;
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
	/* TODO */
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

	CheckVertexBufferBindingsAndBindPipeline(
		renderer,
		bindings,
		numBindings
	);

	UpdateRenderPass(driverData);
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

static void DestroyBuffer(
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

static void EndPass(
	FNAVulkanRenderer *renderer
) {
	if (renderer->renderPassInProgress && renderer->commandBufferCount > 0)
	{
		renderer->vkCmdEndRenderPass(
			renderer->commandBuffers[renderer->commandBufferCount - 1]
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

	/* Perform any pending clears before switching render targets */

	if (	renderer->shouldClearColor ||
			renderer->shouldClearDepth ||
			renderer->shouldClearStencil	)
	{
		UpdateRenderPass(driverData);
	}

	renderer->needNewRenderPass = 1;

	for (uint32_t i = 0; i < MAX_RENDERTARGET_BINDINGS; i++)
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
			renderer->commandBuffers[renderer->commandBufferCount - 1],
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

		VkRect2D vulkanScissorRect = { offset, extent };

		renderer->vkCmdSetScissor(
			renderer->commandBuffers[renderer->commandBufferCount - 1],
			0,
			1,
			&vulkanScissorRect
		);
	}
}

static void SetStencilReferenceValueCommand(FNAVulkanRenderer *renderer)
{
	if (renderer->renderPassInProgress)
	{
		renderer->vkCmdSetStencilReference(
			renderer->commandBuffers[renderer->commandBufferCount - 1],
			VK_STENCIL_FACE_FRONT_AND_BACK,
			renderer->stencilRef
		);
	}
}


static void Stall(FNAVulkanRenderer *renderer)
{
	VkResult result;
	VulkanBuffer *buf;

	EndPass(renderer);

	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.waitSemaphoreCount = 0;
	submitInfo.pWaitSemaphores = NULL;
	submitInfo.pWaitDstStageMask = NULL;
	submitInfo.signalSemaphoreCount = 0;
	submitInfo.pSignalSemaphores = NULL;
	submitInfo.commandBufferCount = renderer->commandBufferCount;
	submitInfo.pCommandBuffers = renderer->commandBuffers;

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

	renderer->commandBufferCount = 0;
	AllocateAndBeginCommandBuffer(renderer);
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
	if (renderer->bufferMemoryBarrierCount + renderer->imageMemoryBarrierCount > 0)
	{
		uint8_t renderPassWasInProgress = renderer->renderPassInProgress;

		if (renderPassWasInProgress)
		{
			EndPass(renderer);
		}

		renderer->vkCmdPipelineBarrier(
			renderer->commandBuffers[renderer->commandBufferCount - 1],
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

void VULKAN_ResetBackbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_PresentationParameters *presentationParameters
) {
	/* TODO */
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
}

FNA3D_Texture* VULKAN_CreateTextureCube(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t size,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	/* TODO */
}

void VULKAN_AddDisposeTexture(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture
) {
	/* TODO */
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

	renderer->vkMapMemory(
		renderer->logicalDevice,
		stagingBuffer->deviceMemory,
		stagingBuffer->internalOffset,
		stagingBuffer->size,
		0,
		&stagingData
	);

	SDL_memcpy(stagingData, data, stagingBuffer->size);

	renderer->vkUnmapMemory(
		renderer->logicalDevice,
		stagingBuffer->deviceMemory
	);

	if (!renderer->commandBufferBegunThisFrame)
	{
		AllocateAndBeginCommandBuffer(renderer);
	}

	VulkanResourceAccessType nextResourceAccessType = RESOURCE_ACCESS_TRANSFER_WRITE;

	if (vulkanTexture->imageData->resourceAccessType != nextResourceAccessType) 
	{
		ImageMemoryBarrierCreateInfo imageBarrierCreateInfo;
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

	VulkanResourceAccessType nextAccessType = RESOURCE_ACCESS_TRANSFER_READ;

	if (stagingBuffer->resourceAccessType != nextAccessType)
	{
		BufferMemoryBarrierCreateInfo bufferBarrierCreateInfo;
		bufferBarrierCreateInfo.pPrevAccesses = &stagingBuffer->resourceAccessType;
		bufferBarrierCreateInfo.prevAccessCount = 1;
		bufferBarrierCreateInfo.pNextAccesses = &nextAccessType;
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

		stagingBuffer->resourceAccessType = nextAccessType;
	}

	SubmitPipelineBarrier(renderer);

	VkBufferImageCopy imageCopy;
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
		renderer->commandBuffers[renderer->commandBufferCount - 1],
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

	VkExtent2D dimensions;
	dimensions.width = width;
	dimensions.height = height;

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
	VulkanRenderbuffer *renderbuffer = SDL_malloc(sizeof(VulkanRenderbuffer));
	renderbuffer->depthBuffer = NULL;
	renderbuffer->colorBuffer = SDL_malloc(sizeof(VulkanColorBuffer));
	renderbuffer->colorBuffer->dimensions = dimensions;

	VkResult result = renderer->vkCreateImageView(
		renderer->logicalDevice,
		&imageViewInfo,
		NULL,
		&renderbuffer->colorBuffer->handle
	);

	if (result != VK_SUCCESS) {
		LogVulkanResult("vkCreateImageView", result);

		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s\n",
			"Failed to create color renderbuffer image view"
		);

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

	VulkanRenderbuffer *renderbuffer = SDL_malloc(sizeof(VulkanRenderbuffer));
	renderbuffer->colorBuffer = NULL;
	renderbuffer->depthBuffer = SDL_malloc(sizeof(VulkanDepthStencilBuffer));

	if (
		!CreateImage(
			renderer,
			width,
			height,
			XNAToVK_SampleCount(multiSampleCount),
			depthFormat,
			IDENTITY_SWIZZLE,
			VK_IMAGE_ASPECT_DEPTH_BIT,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&renderbuffer->depthBuffer->handle
		)
	) {
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s\n",
			"Failed to create depth stencil image"
		);

		return NULL;
	}

	return (FNA3D_Renderbuffer*) renderbuffer;
}

void VULKAN_AddDisposeRenderbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Renderbuffer *renderbuffer
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanRenderbuffer *vlkRenderBuffer = (VulkanRenderbuffer*) renderbuffer;
	uint8_t isDepthStencil = (vlkRenderBuffer->colorBuffer == NULL);

	if (isDepthStencil)
	{
		if (renderer->depthStencilAttachment == vlkRenderBuffer->depthBuffer)
		{
			renderer->depthStencilAttachment = NULL;
			renderer->depthStencilAttachmentActive = 0;
		}
		renderer->vkDestroyImageView(
			renderer->logicalDevice,
			vlkRenderBuffer->depthBuffer->handle.view,
			NULL
		);

		renderer->vkDestroyImage(
			renderer->logicalDevice,
			vlkRenderBuffer->depthBuffer->handle.image,
			NULL
		);

		renderer->vkFreeMemory(
			renderer->logicalDevice,
			vlkRenderBuffer->depthBuffer->handle.memory,
			NULL
		);

		SDL_free(vlkRenderBuffer->depthBuffer);
	} else
	{
		// Iterate through color attachments
		for (int i = 0; i < MAX_RENDERTARGET_BINDINGS; ++i)
		{
			if (renderer->colorAttachments[i] == vlkRenderBuffer->colorBuffer)
			{
				renderer->colorAttachments[i] = NULL;
			}

		}

		renderer->vkDestroyImageView(
			renderer->logicalDevice,
			vlkRenderBuffer->colorBuffer->handle,
			NULL
		);

		/* The image is owned by the texture it's from, so we don't free it here. */

		SDL_free(vlkRenderBuffer->colorBuffer);
	}

	SDL_free(vlkRenderBuffer);
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

void VULKAN_AddDisposeVertexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	DestroyBuffer(driverData, buffer);
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

	void *contents;
	renderer->vkMapMemory(
		renderer->logicalDevice,
		vulkanBuffer->deviceMemory,
		vulkanBuffer->internalOffset,
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

FNA3DAPI void VULKAN_AddDisposeIndexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	/* TODO */
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
		vulkanBuffer->internalOffset,
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

	for (uint32_t i = 0; i < (*effectData)->error_count; i++)
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
		FNA3D_LogError(
			"%s", MOJOSHADER_vkGetError()
		);
	}

	result = (VulkanEffect*) SDL_malloc(sizeof(VulkanEffect));
	result->effect = *effectData;
	*effect = (FNA3D_Effect*) result;
}

void VULKAN_AddDisposeEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	VulkanEffect *fnaEffect = (VulkanEffect*) effect;
	MOJOSHADER_effect *effectData = fnaEffect->effect;

	if (effectData == renderer->currentEffect) {
		MOJOSHADER_effectEndPass(renderer->currentEffect);
		MOJOSHADER_effectEnd(renderer->currentEffect);
		renderer->currentEffect = NULL;
		renderer->currentTechnique = NULL;
		renderer->currentPass = 0;
	}
	MOJOSHADER_deleteEffect(effectData);
	SDL_free(effect);
}

void VULKAN_SetEffectTechnique(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique
) {
	VulkanEffect *mtlEffect = (VulkanEffect*) effect;
	MOJOSHADER_effectSetTechnique(mtlEffect->effect, technique);
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
	VulkanQuery *query = SDL_malloc(sizeof(VulkanQuery));

	if (renderer->freeQueryIndexStackHead == -1) {
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"Query limit of %d has been exceeded",
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
		renderer->commandBuffers[renderer->commandBufferCount - 1],
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
		renderer->commandBuffers[renderer->commandBufferCount - 1],
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
		renderer->commandBuffers[renderer->commandBufferCount - 1],
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
	/* TODO */
}

uint8_t VULKAN_SupportsS3TC(FNA3D_Renderer *driverData)
{
	/* TODO */
}

uint8_t VULKAN_SupportsHardwareInstancing(FNA3D_Renderer *driverData)
{
	/* TODO */
}

uint8_t VULKAN_SupportsNoOverwrite(FNA3D_Renderer *driverData)
{
	/* TODO */
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
	/* TODO */
}

/* Debugging */

void VULKAN_SetStringMarker(FNA3D_Renderer *driverData, const char *text)
{
	/* TODO */
}

/* Buffer Objects */

intptr_t VULKAN_GetBufferSize(FNA3D_Buffer *buffer)
{
	/* TODO */
}

static uint8_t LoadGlobalFunctions(void)
{
    vkGetInstanceProcAddr = SDL_Vulkan_GetVkGetInstanceProcAddr();
    if(!vkGetInstanceProcAddr)
    {
        SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"SDL_Vulkan_GetVkGetInstanceProcAddr(): %s\n",
            SDL_GetError()
		);

        return 0;
    }

	#define VULKAN_GLOBAL_FUNCTION(name)                                               		\
		name = (PFN_##name)vkGetInstanceProcAddr(VK_NULL_HANDLE, #name);                  	\
		if(!name)                                                                         	\
		{                                                                                 	\
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,                                    	\
						"vkGetInstanceProcAddr(VK_NULL_HANDLE, \"" #name "\") failed\n");	\
			return 0;                                                             		   	\
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
	uint32_t extensionCount;
	vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);

	VkExtensionProperties availableExtensions[extensionCount];
	vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, availableExtensions);

	for (uint32_t i = 0; i < requiredExtensionsLength; i++)
	{
		uint8_t extensionFound = 0;

		for (uint32_t j = 0; j < extensionCount; j++)
		{
			if (SDL_strcmp(requiredExtensions[i], availableExtensions[j].extensionName) == 0)
			{
				extensionFound = 1;
				break;
			}
		}

		if (!extensionFound)
		{
			return 0;
		}
	}

	return 1;
}

static uint8_t CheckDeviceExtensionSupport(
	FNAVulkanRenderer *renderer,
	VkPhysicalDevice physicalDevice,
	const char** requiredExtensions,
	uint32_t requiredExtensionsLength
) {
	uint32_t extensionCount;
	renderer->vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &extensionCount, NULL);

	VkExtensionProperties availableExtensions[extensionCount];
	renderer->vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &extensionCount, availableExtensions);

	for (uint32_t i = 0; i < requiredExtensionsLength; i++)
	{
		uint8_t extensionFound = 0;

		for (uint32_t j = 0; j < extensionCount; j++)
		{
			if (SDL_strcmp(requiredExtensions[i], availableExtensions[j].extensionName) == 0)
			{
				extensionFound = 1;
				break;
			}
		}

		if (!extensionFound)
		{
			return 0;
		}
	}

	return 1;
}

static uint8_t QuerySwapChainSupport(
	FNAVulkanRenderer *renderer,
	VkPhysicalDevice physicalDevice,
	VkSurfaceKHR surface,
	SwapChainSupportDetails *outputDetails
) {
	VkResult result;

	result = renderer->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &outputDetails->capabilities);
	if (result != VK_SUCCESS)
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"vkGetPhysicalDeviceSurfaceCapabilitiesKHR: %s\n",
			VkErrorMessages(result)
		);

		return 0;
	}

	uint32_t formatCount;
	renderer->vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, NULL);

	if (formatCount != 0)
	{
		outputDetails->formats = SDL_malloc(sizeof(VkSurfaceFormatKHR) * formatCount);
		outputDetails->formatsLength = formatCount;

		if (!outputDetails->formats)
		{
			SDL_OutOfMemory();
			return 0;
		}

		result = renderer->vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, outputDetails->formats);
		if (result != VK_SUCCESS)
		{
			SDL_LogError(
				SDL_LOG_CATEGORY_APPLICATION,
				"vkGetPhysicalDeviceSurfaceFormatsKHR: %s\n",
				VkErrorMessages(result)
			);

			return 0;
		}
	}

	uint32_t presentModeCount;
	renderer->vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, NULL);

	if (presentModeCount != 0)
	{
		outputDetails->presentModes = SDL_malloc(sizeof(VkPresentModeKHR) * presentModeCount);
		outputDetails->presentModesLength = presentModeCount;

		if (!outputDetails->presentModes)
		{
			SDL_OutOfMemory();
			return 0;
		}

		result = renderer->vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, outputDetails->presentModes);
		if (result != VK_SUCCESS)
		{
			SDL_LogError(
				SDL_LOG_CATEGORY_APPLICATION,
				"vkGetPhysicalDeviceSurfacePresentModesKHR: %s\n",
				VkErrorMessages(result)
			);

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
	queueFamilyIndices->graphicsFamily = UINT32_MAX;
	queueFamilyIndices->presentFamily = UINT32_MAX;

	VkPhysicalDeviceProperties deviceProperties;
	renderer->vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);

	if (deviceProperties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
	{
		return 0;
	}

	if (!CheckDeviceExtensionSupport(renderer, physicalDevice, requiredExtensionNames, requiredExtensionNamesLength))
	{
		return 0;
	}

	uint32_t queueFamilyCount;
	renderer->vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, NULL);

	/* FIXME: need better structure for checking vs storing support details */
	SwapChainSupportDetails swapChainSupportDetails;
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

	VkQueueFamilyProperties queueProps[queueFamilyCount];
	renderer->vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueProps);

	for (uint32_t i = 0; i < queueFamilyCount; i++) {
		VkBool32 supportsPresent;
		renderer->vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &supportsPresent);
		if (supportsPresent && (queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
			queueFamilyIndices->graphicsFamily = i;
			queueFamilyIndices->presentFamily = i;
			return 1;
		}
	}

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
	queueFamilyIndices->graphicsFamily = UINT32_MAX;
	queueFamilyIndices->presentFamily = UINT32_MAX;

	if (!CheckDeviceExtensionSupport(renderer, physicalDevice, requiredExtensionNames, requiredExtensionNamesLength))
	{
		return 0;
	}

	VkPhysicalDeviceProperties deviceProperties;
	renderer->vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);

	uint32_t queueFamilyCount;
	renderer->vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, NULL);

	SwapChainSupportDetails swapChainSupportDetails;
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

	VkQueueFamilyProperties queueProps[queueFamilyCount];
	renderer->vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueProps);

	for (uint32_t i = 0; i < queueFamilyCount; i++) {
		VkBool32 supportsPresent;
		renderer->vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &supportsPresent);
		if (supportsPresent && (queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
			queueFamilyIndices->graphicsFamily = i;
			queueFamilyIndices->presentFamily = i;
			return 1;
		}
	}

	return 0;
}

static uint8_t CheckValidationLayerSupport(
	const char** validationLayers,
	uint32_t length
) {
	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties(&layerCount, NULL);

	VkLayerProperties availableLayers[layerCount];
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);

	for (uint32_t i = 0; i < length; i++)
	{
		uint8_t layerFound = 0;

		for (uint32_t j = 0; j < layerCount; j++)
		{
			if (SDL_strcmp(validationLayers[i], availableLayers[j].layerName) == 0)
			{
				layerFound = 1;
				break;
			}
		}

		if (!layerFound)
		{
			return 0;
		}
	}

	return 1;
}

static uint8_t ChooseSwapSurfaceFormat(
	VkFormat desiredFormat,
	VkSurfaceFormatKHR *availableFormats,
	uint32_t availableFormatsLength,
	VkSurfaceFormatKHR *outputFormat
) {
	for (uint32_t i = 0; i < availableFormatsLength; i++)
	{
		if (	availableFormats[i].format == desiredFormat &&
				availableFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR	)
		{
			*outputFormat = availableFormats[i];
			return 1;
		}
	}

	SDL_LogError(
		SDL_LOG_CATEGORY_APPLICATION,
		"%s\n",
		"Desired surface format is unavailable."
	);

	return 0;
}

static uint8_t ChooseSwapPresentMode(
	FNA3D_PresentInterval desiredPresentInterval,
	VkPresentModeKHR *availablePresentModes,
	uint32_t availablePresentModesLength,
	VkPresentModeKHR *outputPresentMode
) {
	if (	desiredPresentInterval == FNA3D_PRESENTINTERVAL_DEFAULT ||
			desiredPresentInterval == FNA3D_PRESENTINTERVAL_ONE	)
	{
		for (uint32_t i = 0; i < availablePresentModesLength; i++)
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
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s\n",
			"FNA3D_PRESENTINTERVAL_TWO not supported in Vulkan"
		);
	}
	else /* FNA3D_PRESENTINTERVAL_IMMEDIATE */
	{
		for (uint32_t i = 0; i < availablePresentModesLength; i++)
		{
			if (availablePresentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
			{
				*outputPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
				return 1;
			}
		}
	}

	SDL_LogInfo(
		SDL_LOG_CATEGORY_APPLICATION,
		"%s\n",
		"Could not find desired presentation interval, falling back to VK_PRESENT_MODE_FIFO_KHR"
	);

	*outputPresentMode = VK_PRESENT_MODE_FIFO_KHR;
	return 1;
}

static VkExtent2D ChooseSwapExtent(
	const VkSurfaceCapabilitiesKHR capabilities,
	uint32_t width,
	uint32_t height
) {
	if (capabilities.currentExtent.width != UINT32_MAX)
	{
		return capabilities.currentExtent;
	}
	else
	{
		VkExtent2D actualExtent = { width, height };

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
	renderer->vkGetPhysicalDeviceMemoryProperties(renderer->physicalDevice, &memoryProperties);

	for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
	{
		if ((typeFilter & (1 << i)) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
		{
			*result = i;
			return 1;
		}
	}

	SDL_LogError(
		SDL_LOG_CATEGORY_APPLICATION,
		"%s\n",
		"Failed to find suitable memory type"
	);

	return 0;
}

static void GenerateUserVertexInputInfo(
	FNAVulkanRenderer *renderer,
	VkVertexInputBindingDescription *bindingDescriptions,
	VkVertexInputAttributeDescription* attributeDescriptions,
	uint32_t *attributeDescriptionCount
) {
	MOJOSHADER_vkShader *vertexShader, *blah;
	MOJOSHADER_vkGetBoundShaders(&vertexShader, &blah);

	bindingDescriptions[0].binding = 0;
	bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	bindingDescriptions[0].stride = renderer->userVertexDeclaration.vertexStride;

	uint32_t attributeDescriptionCounter = 0;

	for (uint32_t i = 0; i < renderer->userVertexDeclaration.elementCount; i++)
	{
		FNA3D_VertexElement element = renderer->userVertexDeclaration.elements[i];
		FNA3D_VertexElementUsage usage = element.vertexElementUsage;
		int32_t index = element.usageIndex;

		if (renderer->attrUse[usage][index])
		{
			index = -1;

			for (uint32_t j = 0; j < MAX_VERTEX_ATTRIBUTES; j++)
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

		int32_t attribLoc = MOJOSHADER_vkGetVertexAttribLocation(
			vertexShader,
			VertexAttribUsage(usage),
			index
		);
		
		if (attribLoc == -1)
		{
			/* Stream not in use! */
			continue;
		}

		VkVertexInputAttributeDescription vInputAttribDescription;
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
	MOJOSHADER_vkGetBoundShaders(&vertexShader, &blah);

	uint32_t attributeDescriptionCounter = 0;

	for (uint32_t i = 0; i < renderer->numVertexBindings; i++)
	{
		FNA3D_VertexDeclaration vertexDeclaration = renderer->vertexBindings[i].vertexDeclaration;

		for (uint32_t j = 0; j < vertexDeclaration.elementCount; j++)
		{
			FNA3D_VertexElement element = vertexDeclaration.elements[j];
			FNA3D_VertexElementUsage usage = element.vertexElementUsage;
			int32_t index = element.usageIndex;

			if (renderer->attrUse[usage][index])
			{
				index = -1;

				for (uint32_t k = 0; k < MAX_VERTEX_ATTRIBUTES; k++)
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

			int32_t attribLoc = MOJOSHADER_vkGetVertexAttribLocation(
				vertexShader,
				VertexAttribUsage(usage),
				index
			);
			
			if (attribLoc == -1)
			{
				/* Stream not in use! */
				continue;
			}

			VkVertexInputAttributeDescription vInputAttribDescription;
			vInputAttribDescription.location = attribLoc;
			vInputAttribDescription.format = XNAToVK_VertexAttribType[
				element.vertexElementFormat
			];
			vInputAttribDescription.offset = element.offset;
			vInputAttribDescription.binding = i;

			attributeDescriptions[attributeDescriptionCounter] = vInputAttribDescription;
			attributeDescriptionCounter++;
		}

		VkVertexInputBindingDescription vertexInputBindingDescription;
		vertexInputBindingDescription.binding = i;
		vertexInputBindingDescription.stride = vertexDeclaration.vertexStride;
		vertexInputBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		bindingDescriptions[i] = vertexInputBindingDescription;

		*attributeDescriptionCount = attributeDescriptionCounter;
	}
}

FNA3D_Device* VULKAN_CreateDevice(
	FNA3D_PresentationParameters *presentationParameters,
	uint8_t debugMode
) {
	FNAVulkanRenderer *renderer;
	FNA3D_Device *result;
	VkResult vulkanResult;
	uint32_t physicalDeviceCount;
	VkPhysicalDevice physicalDevice;
	VkDevice logicalDevice;
	uint32_t instanceExtensionCount;

	/* Create the FNA3D_Device */
	result = (FNA3D_Device*) SDL_malloc(sizeof(FNA3D_Device));
	ASSIGN_DRIVER(VULKAN)

	/* Init the FNAVulkanRenderer */
	renderer = (FNAVulkanRenderer*) SDL_malloc(sizeof(FNAVulkanRenderer));
	SDL_memset(renderer, '\0', sizeof(FNAVulkanRenderer));

	if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s\n",
			"Video system not initialized"
		);

		return NULL;
	}

	/* load library so we can load vk functions dynamically */
	if (SDL_Vulkan_LoadLibrary(NULL) == -1)
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s\n%s\n",
			SDL_GetError(),
			"Failed to load Vulkan library"
		);

		return NULL;
	}

	if (!LoadGlobalFunctions())
	{
		return NULL;
	}

	renderer->debugMode = debugMode;

	/* The FNA3D_Device and FNAVulkanRenderer need to reference each other */
	renderer->parentDevice = result;
	result->driverData = (FNA3D_Renderer*) renderer;

	renderer->fauxBackbufferWidth = presentationParameters->backBufferWidth;
	renderer->fauxBackbufferHeight = presentationParameters->backBufferHeight;

	/* create instance */
	VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	appInfo.pApplicationName = "FNA";
	appInfo.apiVersion = VK_MAKE_VERSION(1, 2, 137);

	if (
		!SDL_Vulkan_GetInstanceExtensions(
			presentationParameters->deviceWindowHandle,
			&instanceExtensionCount,
			NULL
		)
	) {
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"SDL_Vulkan_GetInstanceExtensions(): getExtensionCount: %s\n",
			SDL_GetError()
		);
		return NULL;
	}

	const char *instanceExtensionNames[instanceExtensionCount];

	if (
		!SDL_Vulkan_GetInstanceExtensions(
			presentationParameters->deviceWindowHandle,
			&instanceExtensionCount,
			instanceExtensionNames
		)
	) {
		SDL_free((void*)instanceExtensionNames);
        SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"SDL_Vulkan_GetInstanceExtensions(): getExtensions %s\n",
			SDL_GetError()
		);
		return NULL;
	}

	if (!CheckInstanceExtensionSupport(instanceExtensionNames, instanceExtensionCount))
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s",
			"Required Vulkan instance extensions not supported"
		);

		return NULL;
	}

	/* create info structure */

	VkInstanceCreateInfo createInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };

	char const* layerNames[] = { "VK_LAYER_KHRONOS_validation" };
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledExtensionCount = instanceExtensionCount;
	createInfo.ppEnabledExtensionNames = instanceExtensionNames;
	createInfo.ppEnabledLayerNames = layerNames;

	if (debugMode)
	{
		createInfo.enabledLayerCount = sizeof(layerNames)/sizeof(layerNames[0]);
		if (!CheckValidationLayerSupport(layerNames, createInfo.enabledLayerCount))
		{
			SDL_LogWarn(
				SDL_LOG_CATEGORY_APPLICATION,
				"%s",
				"Validation layers not found, continuing without validation"
			);

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
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"vkCreateInstance failed: %s\n",
			VkErrorMessages(vulkanResult)
		);

		return NULL;
	}

	/* create surface */

	if (
		!SDL_Vulkan_CreateSurface(
			presentationParameters->deviceWindowHandle,
			renderer->instance,
			&renderer->surface
		)
	) {
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"SDL_Vulkan_CreateSurface failed: %s\n",
			SDL_GetError()
		);

		return NULL;
	}

	/* load function entry points */

	LoadInstanceFunctions(renderer);

	/* designate required device extensions */

	char const* deviceExtensionNames[] = { "VK_KHR_swapchain" };
	uint32_t deviceExtensionCount = sizeof(deviceExtensionNames)/sizeof(deviceExtensionNames[0]);

	/* determine a suitable physical device */

	vulkanResult = renderer->vkEnumeratePhysicalDevices(
		renderer->instance,
		&physicalDeviceCount,
		NULL
	);

	if (vulkanResult != VK_SUCCESS)
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"vkEnumeratePhysicalDevices failed: %s\n",
			VkErrorMessages(vulkanResult)
		);

		return NULL;
	}

	if (physicalDeviceCount == 0)
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"Failed to find any GPUs with Vulkan support\n"
		);
		return NULL;
	}

	VkPhysicalDevice physicalDevices[physicalDeviceCount];

	vulkanResult = renderer->vkEnumeratePhysicalDevices(
		renderer->instance,
		&physicalDeviceCount,
		physicalDevices
	);

	if (vulkanResult != VK_SUCCESS)
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"vkEnumeratePhysicalDevices failed: %s\n",
			VkErrorMessages(vulkanResult)
		);

		return NULL;
	}

	QueueFamilyIndices queueFamilyIndices;
	uint8_t physicalDeviceAssigned = 0;
	for (uint32_t i = 0; i < physicalDeviceCount; i++)
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
		for (uint32_t i = 0; i < physicalDeviceCount; i++)
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
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"No suitable physical devices found."
		);

		return NULL;
	}

	renderer->physicalDevice = physicalDevice;

	VkPhysicalDeviceProperties deviceProperties;

	renderer->vkGetPhysicalDeviceProperties(
		renderer->physicalDevice,
		&deviceProperties
	);

	/* Setting up Queue Info */
	int queueInfoCount = 1;
	VkDeviceQueueCreateInfo queueCreateInfos[2];
	float queuePriority = 1.0f;

	VkDeviceQueueCreateInfo queueCreateInfoGraphics = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	queueCreateInfoGraphics.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
	queueCreateInfoGraphics.queueCount = 1;
	queueCreateInfoGraphics.pQueuePriorities = &queuePriority;

	queueCreateInfos[0] = queueCreateInfoGraphics;

	if (queueFamilyIndices.presentFamily != queueFamilyIndices.graphicsFamily)
	{
		VkDeviceQueueCreateInfo queueCreateInfoPresent = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		queueCreateInfoPresent.queueFamilyIndex = queueFamilyIndices.presentFamily;
		queueCreateInfoPresent.queueCount = 1;
		queueCreateInfoPresent.pQueuePriorities = &queuePriority;

		queueCreateInfos[1] = queueCreateInfoPresent;
		queueInfoCount++;
	}

	/* specifying used device features */
	VkPhysicalDeviceFeatures deviceFeatures = { 0 };
	deviceFeatures.occlusionQueryPrecise = VK_TRUE;

	/* creating the logical device */

	VkDeviceCreateInfo deviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	deviceCreateInfo.queueCreateInfoCount = queueInfoCount;
	deviceCreateInfo.pQueueCreateInfos = queueCreateInfos;
	deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
	deviceCreateInfo.ppEnabledExtensionNames = deviceExtensionNames;
	deviceCreateInfo.enabledExtensionCount = deviceExtensionCount;

	vulkanResult = renderer->vkCreateDevice(physicalDevice, &deviceCreateInfo, NULL, &logicalDevice);
   	if (vulkanResult != VK_SUCCESS)
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"vkCreateDevice failed: %s\n",
			VkErrorMessages(vulkanResult)
		);

		return NULL;
	}

	/* assign logical device to the renderer and load entry points */

	renderer->logicalDevice = logicalDevice;
	LoadDeviceFunctions(renderer);

	renderer->vkGetDeviceQueue(logicalDevice, queueFamilyIndices.graphicsFamily, 0, &renderer->graphicsQueue);
	renderer->vkGetDeviceQueue(logicalDevice, queueFamilyIndices.presentFamily, 0, &renderer->presentQueue);

	/* create swap chain */

	SwapChainSupportDetails swapChainSupportDetails;
	if (
		!QuerySwapChainSupport(
			renderer,
			physicalDevice,
			renderer->surface,
			&swapChainSupportDetails
		)
	) {
		return NULL;
	}

	SurfaceFormatMapping surfaceFormatMapping = XNAToVK_SurfaceFormat[
		presentationParameters->backBufferFormat
	];

	VkSurfaceFormatKHR surfaceFormat;

	if (
		!ChooseSwapSurfaceFormat(
			surfaceFormatMapping.formatColor,
			swapChainSupportDetails.formats,
			swapChainSupportDetails.formatsLength,
			&surfaceFormat
		)
	) {
		return NULL;
	}

	renderer->surfaceFormatMapping = surfaceFormatMapping;

	VkPresentModeKHR presentMode;
	if (
		!ChooseSwapPresentMode(
			presentationParameters->presentationInterval,
			swapChainSupportDetails.presentModes,
			swapChainSupportDetails.presentModesLength,
			&presentMode
		)
	) {
		return NULL;
	}

	VkExtent2D extent = ChooseSwapExtent(
		swapChainSupportDetails.capabilities,
		presentationParameters->backBufferWidth,
		presentationParameters->backBufferHeight
	);

	uint32_t imageCount = swapChainSupportDetails.capabilities.minImageCount + 1;

	if (	swapChainSupportDetails.capabilities.maxImageCount > 0 &&
			imageCount > swapChainSupportDetails.capabilities.maxImageCount	)
	{
		imageCount = swapChainSupportDetails.capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR swapChainCreateInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
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
	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateSwapchainKHR", vulkanResult);
		return NULL;
	}

	SDL_free(swapChainSupportDetails.formats);
	SDL_free(swapChainSupportDetails.presentModes);

	uint32_t swapChainImageCount;
	renderer->vkGetSwapchainImagesKHR(renderer->logicalDevice, renderer->swapChain, &swapChainImageCount, NULL);

	renderer->swapChainImages = SDL_malloc(sizeof(FNAVulkanImageData) * swapChainImageCount);
	if (!renderer->swapChainImages)
	{
		SDL_OutOfMemory();
		return NULL;
	}

	VkImage swapChainImages[swapChainImageCount];
	renderer->vkGetSwapchainImagesKHR(renderer->logicalDevice, renderer->swapChain, &swapChainImageCount, swapChainImages);
	renderer->swapChainImageCount = swapChainImageCount;
	renderer->swapChainExtent = extent;

	for (uint32_t i = 0; i < swapChainImageCount; i++)
	{
		VkImageViewCreateInfo createInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		createInfo.image = swapChainImages[i];
		createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		createInfo.format = surfaceFormat.format;
		createInfo.components = surfaceFormatMapping.swizzle;
		createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		createInfo.subresourceRange.baseMipLevel = 0;
		createInfo.subresourceRange.levelCount = 1;
		createInfo.subresourceRange.baseArrayLayer = 0;
		createInfo.subresourceRange.layerCount = 1;

		VkImageView swapChainImageView;

		vulkanResult = renderer->vkCreateImageView(
			renderer->logicalDevice,
			&createInfo,
			NULL,
			&swapChainImageView
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateImageView", vulkanResult);
			return NULL;
		}

		renderer->swapChainImages[i].image = swapChainImages[i];
		renderer->swapChainImages[i].view = swapChainImageView;
		renderer->swapChainImages[i].memory = NULL;
		renderer->swapChainImages[i].memorySize = 0; /* FIXME: is this correct? */
		renderer->swapChainImages[i].dimensions = renderer->swapChainExtent;
		renderer->swapChainImages[i].resourceAccessType = RESOURCE_ACCESS_NONE;
	}

	VkExtent3D imageExtent;
	imageExtent.width = presentationParameters->backBufferWidth;
	imageExtent.height = presentationParameters->backBufferHeight;
	imageExtent.depth = 1;

	VkSampleCountFlagBits multiSampleCount =
			XNAToVK_SampleCount(presentationParameters->multiSampleCount);

	/* create faux backbuffer color image */
	if (
		!CreateImage(
			renderer,
			presentationParameters->backBufferWidth,
			presentationParameters->backBufferHeight,
			multiSampleCount,
			surfaceFormatMapping.formatColor,
			surfaceFormatMapping.swizzle,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_TYPE_2D,
			/* FIXME: transfer bit probably only needs to be set on 0? */
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&renderer->fauxBackbufferColorImageData
		)
	) {
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s\n",
			"Failed to create color attachment image"
		);

		return NULL;
	}

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

		VkFormat vulkanDepthStencilFormat = XNAToVK_DepthFormat(presentationParameters->depthStencilFormat);

		if (
			!CreateImage(
				renderer,
				presentationParameters->backBufferWidth,
				presentationParameters->backBufferHeight,
				multiSampleCount,
				vulkanDepthStencilFormat,
				IDENTITY_SWIZZLE,
				VK_IMAGE_ASPECT_DEPTH_BIT,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_TYPE_2D,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				&renderer->fauxBackbufferDepthStencil.handle
			)
		) {
			SDL_LogError(
				SDL_LOG_CATEGORY_APPLICATION,
				"%s\n",
				"Failed to create depth stencil image"
			);

			return NULL;
		}

		renderer->depthStencilAttachment = &renderer->fauxBackbufferDepthStencil;
		renderer->depthStencilAttachmentActive = 1;
	}
	else
	{
		renderer->depthStencilAttachmentActive = 0;
	}

	VkPipelineCacheCreateInfo pipelineCacheCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	vulkanResult = renderer->vkCreatePipelineCache(renderer->logicalDevice, &pipelineCacheCreateInfo, NULL, &renderer->pipelineCache);
	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreatePipelineCache", vulkanResult);
		return NULL;
	}

	/* initialize mojoshader */

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

	MOJOSHADER_vkMakeContextCurrent(renderer->mojoshaderContext);

	/* set up sampler description */

	renderer->numSamplers = SDL_min(
		deviceProperties.limits.maxSamplerAllocationCount,
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

	/* define descriptor set layouts */

	for (uint32_t i = 0; i < 2; i++)
	{
		VkDescriptorSetLayoutBinding vertexUniformBufferLayoutBindings[i];

		for (uint32_t j = 0; j < i; j++)
		{
			VkDescriptorSetLayoutBinding vertexUniformBufferLayoutBinding;
			vertexUniformBufferLayoutBinding.binding = 0;
			vertexUniformBufferLayoutBinding.descriptorCount = i;
			vertexUniformBufferLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			vertexUniformBufferLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			vertexUniformBufferLayoutBinding.pImmutableSamplers = NULL;

			vertexUniformBufferLayoutBindings[j] = vertexUniformBufferLayoutBinding;
		}

		VkDescriptorSetLayoutCreateInfo vertexUniformLayoutCreateInfo = {
			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
		};

		vertexUniformLayoutCreateInfo.bindingCount = i;
		vertexUniformLayoutCreateInfo.pBindings = vertexUniformBufferLayoutBindings;

		vulkanResult = renderer->vkCreateDescriptorSetLayout(
			renderer->logicalDevice,
			&vertexUniformLayoutCreateInfo,
			NULL,
			&renderer->vertUniformBufferDescriptorSetLayouts[i]
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateDescriptorSetLayout", vulkanResult);
			return NULL;
		}
	}

	/* define all possible vert sampler layouts */
	for (uint32_t i = 0; i < MAX_VERTEXTEXTURE_SAMPLERS; i++)
	{
		VkDescriptorSetLayoutBinding vertexSamplerLayoutBindings[i];

		for (uint32_t j = 0; j < i; j++)
		{
			VkDescriptorSetLayoutBinding vertexSamplerLayoutBinding;
			vertexSamplerLayoutBinding.binding = j;
			vertexSamplerLayoutBinding.descriptorCount = 1;
			vertexSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			vertexSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			vertexSamplerLayoutBinding.pImmutableSamplers = NULL;

			vertexSamplerLayoutBindings[j] = vertexSamplerLayoutBinding;
		}

		VkDescriptorSetLayoutCreateInfo vertexSamplerLayoutCreateInfo = {
			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
		};

		vertexSamplerLayoutCreateInfo.bindingCount = i;
		vertexSamplerLayoutCreateInfo.pBindings = vertexSamplerLayoutBindings;

		vulkanResult = renderer->vkCreateDescriptorSetLayout(
			renderer->logicalDevice,
			&vertexSamplerLayoutCreateInfo,
			NULL,
			&renderer->vertSamplerDescriptorSetLayouts[i]
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateDescriptorSetLayout", vulkanResult);
			return NULL;
		}
	}

	for (uint32_t i = 0; i < 2; i++)
	{
		VkDescriptorSetLayoutBinding fragUniformBufferLayoutBindings[i];

		for (uint32_t j = 0; j < i; j++)
		{
			VkDescriptorSetLayoutBinding fragUniformBufferLayoutBinding;
			fragUniformBufferLayoutBinding.binding = 0;
			fragUniformBufferLayoutBinding.descriptorCount = 1;
			fragUniformBufferLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			fragUniformBufferLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			fragUniformBufferLayoutBinding.pImmutableSamplers = NULL;

			fragUniformBufferLayoutBindings[j] = fragUniformBufferLayoutBinding;
		}

		VkDescriptorSetLayoutCreateInfo fragUniformBufferLayoutCreateInfo = {
			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
		};

		fragUniformBufferLayoutCreateInfo.bindingCount = i;
		fragUniformBufferLayoutCreateInfo.pBindings = fragUniformBufferLayoutBindings;

		vulkanResult = renderer->vkCreateDescriptorSetLayout(
			renderer->logicalDevice,
			&fragUniformBufferLayoutCreateInfo,
			NULL,
			&renderer->fragUniformBufferDescriptorSetLayouts[i]
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateDescriptorSetLayout", vulkanResult);
			return NULL;
		}
	}

	/* define all possible frag sampler layouts */
	for (uint32_t i = 0; i < MAX_TEXTURE_SAMPLERS; i++)
	{
		VkDescriptorSetLayoutBinding fragSamplerLayoutBindings[i];

		for (uint32_t j = 0; j < i; j++)
		{
			VkDescriptorSetLayoutBinding fragSamplerLayoutBinding;
			fragSamplerLayoutBinding.binding = j;
			fragSamplerLayoutBinding.descriptorCount = 1;
			fragSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			fragSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			fragSamplerLayoutBinding.pImmutableSamplers = NULL;

			fragSamplerLayoutBindings[j] = fragSamplerLayoutBinding;
		}

		VkDescriptorSetLayoutCreateInfo fragSamplerLayoutCreateInfo = {
			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
		};

		fragSamplerLayoutCreateInfo.bindingCount = i;
		fragSamplerLayoutCreateInfo.pBindings = fragSamplerLayoutBindings;

		vulkanResult = renderer->vkCreateDescriptorSetLayout(
			renderer->logicalDevice,
			&fragSamplerLayoutCreateInfo,
			NULL,
			&renderer->fragSamplerDescriptorSetLayouts[i]
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateDescriptorSetLayout", vulkanResult);
			return NULL;
		}
	}

	/* set up descriptor pools */

	renderer->samplerDescriptorPools = SDL_malloc(
		sizeof(VkDescriptorPool)
	);

	renderer->uniformBufferDescriptorPools = SDL_malloc(
		sizeof(VkDescriptorPool)
	);

	VkDescriptorPoolSize uniformBufferPoolSize;
	uniformBufferPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uniformBufferPoolSize.descriptorCount = UNIFORM_BUFFER_DESCRIPTOR_POOL_SIZE;

	VkDescriptorPoolCreateInfo uniformBufferPoolInfo = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
	};
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

	renderer->uniformBufferDescriptorPoolCount = 1;
	renderer->activeUniformBufferDescriptorPoolIndex = 0;
	renderer->activeUniformBufferPoolUsage = 0;

	VkDescriptorPoolSize samplerPoolSize;
	samplerPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerPoolSize.descriptorCount = SAMPLER_DESCRIPTOR_POOL_SIZE;

	VkDescriptorPoolCreateInfo samplerPoolInfo = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
	};
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

	renderer->samplerDescriptorPoolCount = 1;
	renderer->activeSamplerDescriptorPoolIndex = 0;
	renderer->activeSamplerPoolUsage = 0;

	/* allocate descriptor set memory */

	renderer->activeDescriptorSetCapacity = swapChainImageCount * (MAX_TOTAL_SAMPLERS + 2);

	renderer->activeDescriptorSets = SDL_malloc(
		sizeof(VkDescriptorSet) *
		renderer->activeDescriptorSetCapacity
	);

	renderer->activeDescriptorSetCount = 0;

	/* set up UBO pointer storage */

	renderer->ldVertUniformBuffers = SDL_malloc(
		sizeof(VkBuffer*) *
		renderer->swapChainImageCount
	);

	if (!renderer->ldVertUniformBuffers)
	{
		SDL_OutOfMemory();
		return NULL;
	}

	renderer->ldFragUniformBuffers = SDL_malloc(
		sizeof(VkBuffer*) *
		renderer->swapChainImageCount
	);

	if (!renderer->ldFragUniformBuffers)
	{
		SDL_OutOfMemory();
		return NULL;
	}

	renderer->ldVertUniformOffsets = SDL_malloc(
		sizeof(int32_t) *
		renderer->swapChainImageCount
	);

	if (!renderer->ldVertUniformOffsets)
	{
		SDL_OutOfMemory();
		return NULL;
	}

	renderer->ldFragUniformOffsets = SDL_malloc(
		sizeof(int32_t) *
		renderer->swapChainImageCount
	);

	if (!renderer->ldFragUniformOffsets)
	{
		SDL_OutOfMemory();
		return NULL;
	}

	VkSamplerCreateInfo dummySamplerCreateInfo = {
		VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO
	};
	dummySamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	dummySamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	dummySamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	dummySamplerCreateInfo.magFilter = VK_FILTER_LINEAR;
	dummySamplerCreateInfo.minFilter = VK_FILTER_LINEAR;
	dummySamplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	dummySamplerCreateInfo.flags = 0;

	renderer->vkCreateSampler(
		renderer->logicalDevice,
		&dummySamplerCreateInfo,
		NULL,
		&DummySampler
	);

	/* set up texture storage */

	renderer->ldVertexBufferCount = MAX_BOUND_VERTEX_BUFFERS * renderer->swapChainImageCount;

	renderer->ldVertexBuffers = SDL_malloc(
		sizeof(VkBuffer*) * 
		renderer->ldVertexBufferCount
	);
	
	if (!renderer->ldVertexBuffers)
	{
		SDL_OutOfMemory();
		return NULL;
	}

	renderer->ldVertexBufferOffsets = SDL_malloc(
		sizeof(int32_t) * 
		renderer->ldVertexBufferCount
	);

	if (!renderer->ldVertexBufferOffsets)
	{
		SDL_OutOfMemory();
		return NULL;
	}

	renderer->textureCount = MAX_TOTAL_SAMPLERS * renderer->swapChainImageCount;

	renderer->textures = SDL_malloc(
		sizeof(VulkanTexture*) *
		renderer->textureCount
	);

	if (!renderer->textures)
	{
		SDL_OutOfMemory();
		return NULL;
	}

	renderer->samplers = SDL_malloc(
		sizeof(VkSampler) *
		renderer->textureCount
	);

	if (!renderer->samplers)
	{
		SDL_OutOfMemory();
		return NULL;
	}

	renderer->textureNeedsUpdate = SDL_malloc(
		sizeof(uint8_t) *
		renderer->textureCount
	);

	if (!renderer->textureNeedsUpdate)
	{
		SDL_OutOfMemory();
		return NULL;
	}

	renderer->samplerNeedsUpdate = SDL_malloc(
		sizeof(uint8_t) *
		renderer->textureCount
	);

	if (!renderer->samplerNeedsUpdate)
	{
		SDL_OutOfMemory();
		return NULL;
	};

	for (uint32_t i = 0; i < renderer->textureCount; i++)
	{
		renderer->textures[i] = &NullTexture;
		renderer->samplers[i] = DummySampler;
		renderer->textureNeedsUpdate[i] = 0;
		renderer->samplerNeedsUpdate[i] = 0;
	}

	/* set up command pool */

	VkCommandPoolCreateInfo commandPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	commandPoolCreateInfo.queueFamilyIndex = renderer->queueFamilyIndices.graphicsFamily;
	commandPoolCreateInfo.flags = 0;

	vulkanResult = renderer->vkCreateCommandPool(renderer->logicalDevice, &commandPoolCreateInfo, NULL, &renderer->commandPool);
	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateCommandPool", vulkanResult);
		return NULL;
	}

	renderer->currentDepthFormat = presentationParameters->depthStencilFormat;

	/* initialize various render object caches */

	hmdefault(renderer->pipelineHashMap, NULL);
	hmdefault(renderer->pipelineLayoutHashMap, NULL);
	hmdefault(renderer->renderPassHashMap, NULL);
	hmdefault(renderer->framebufferHashMap, NULL);
	hmdefault(renderer->samplerStateHashMap, NULL);

	/* create fence and semaphores */

	VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
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
		return NULL;
	}

	VkSemaphoreCreateInfo semaphoreInfo = {
		VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
	};

	vulkanResult = renderer->vkCreateSemaphore(
		renderer->logicalDevice,
		&semaphoreInfo,
		NULL,
		&renderer->imageAvailableSemaphore
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateSemaphore", vulkanResult);
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

	renderer->commandBuffers = SDL_malloc(sizeof(VkCommandBuffer));
	renderer->commandBufferCount = 0;
	renderer->commandBufferCapacity = 1;

	renderer->needNewRenderPass = 1;
	renderer->frameInProgress = 0;

	/* Set up query pool */
	VkQueryPoolCreateInfo queryPoolCreateInfo = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
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

	/* Set up the stack, the value at each index is the next available index, or -1 if no such index exists. */
	for (int i = 0; i < MAX_QUERIES - 1; ++i){
		renderer->freeQueryIndexStack[i] = i + 1;
	}
	renderer->freeQueryIndexStack[MAX_QUERIES - 1] = -1;

	renderer->currentPipeline = NULL;

	/* Initialize renderer members not covered by SDL_memset('\0') */
	SDL_memset(renderer->multiSampleMask, -1, sizeof(renderer->multiSampleMask)); /* AKA 0xFFFFFFFF */

	/* initialize dummy data */
	CreateImage(
		renderer,
		1,
		1,
		VK_SAMPLE_COUNT_1_BIT,
		VK_FORMAT_R32G32B32A32_SFLOAT,
		IDENTITY_SWIZZLE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		&NullImageData
	);

	NullBuffer = *CreateBuffer(
		renderer,
		FNA3D_BUFFERUSAGE_NONE,
		16,
		RESOURCE_ACCESS_TRANSFER_WRITE
	);

	/* initialize binding infos */
	renderer->vertSamplerImageInfoCount = renderer->swapChainImageCount * MAX_VERTEXTEXTURE_SAMPLERS;

	renderer->vertSamplerImageInfos = SDL_malloc(
		sizeof(VkDescriptorImageInfo) *
		renderer->vertSamplerImageInfoCount
	);

	renderer->fragSamplerImageInfoCount = renderer->swapChainImageCount * MAX_TEXTURE_SAMPLERS;

	renderer->fragSamplerImageInfos = SDL_malloc(
		sizeof(VkDescriptorImageInfo) *
		renderer->fragSamplerImageInfoCount
	);

	renderer->vertUniformBufferInfo = SDL_malloc(
		sizeof(VkDescriptorBufferInfo) *
		renderer->swapChainImageCount
	);

	renderer->fragUniformBufferInfo = SDL_malloc(
		sizeof(VkDescriptorBufferInfo) *
		renderer->swapChainImageCount
	);

	for (uint32_t i = 0; i < renderer->swapChainImageCount * MAX_VERTEXTEXTURE_SAMPLERS; i++)
	{
		renderer->vertSamplerImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		renderer->vertSamplerImageInfos[i].imageView = NullImageData.view;
		renderer->vertSamplerImageInfos[i].sampler = renderer->samplers[i];
	}

	for (uint32_t i = 0; i < renderer->swapChainImageCount * MAX_TEXTURE_SAMPLERS; i++)
	{
		renderer->fragSamplerImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		renderer->fragSamplerImageInfos[i].imageView = NullImageData.view;
		renderer->fragSamplerImageInfos[i].sampler = renderer->samplers[i];
	}

	for (uint32_t i = 0; i < renderer->swapChainImageCount; i++)
	{
		renderer->vertUniformBufferInfo[i].buffer = NullBuffer.handle;
		renderer->vertUniformBufferInfo[i].offset = 0;
		renderer->vertUniformBufferInfo[i].range = 0;

		renderer->fragUniformBufferInfo[i].buffer = NullBuffer.handle;
		renderer->fragUniformBufferInfo[i].offset = 0;
		renderer->fragUniformBufferInfo[i].range = 0;
	}

	/* initialize memory barrier storage */
	renderer->imageMemoryBarrierCapacity = 256;

	renderer->imageMemoryBarriers = SDL_malloc(
		sizeof(VkImageMemoryBarrier) *
		renderer->imageMemoryBarrierCapacity
	);

	renderer->imageMemoryBarrierCount = 0;

	renderer->bufferMemoryBarrierCapacity = 256;

	renderer->bufferMemoryBarriers = SDL_malloc(
		sizeof(VkBufferMemoryBarrier) *
		renderer->bufferMemoryBarrierCapacity
	);

	renderer->bufferMemoryBarrierCount = 0;

	return result;
}

FNA3D_Driver VulkanDriver = {
	"Vulkan",
	VULKAN_PrepareWindowAttributes,
	VULKAN_GetDrawableSize,
	VULKAN_CreateDevice
};

#endif /* FNA_3D_DRIVER_VULKAN */
