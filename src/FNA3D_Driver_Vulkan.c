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
#include "FNA3D_Driver_Vulkan_passthrough_vert.h"
#include "FNA3D_PipelineCache.h"
#include "stb_ds.h"

#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_vulkan.h>

/* constants */

/* should be equivalent to the number of values in FNA3D_PrimitiveType */
const int PRIMITIVE_TYPES_COUNT = 5;

/* Internal Structures */

typedef struct VulkanTexture VulkanTexture;
typedef struct PipelineHashMap PipelineHashMap;
typedef struct RenderPassHashMap RenderPassHashMap;
typedef struct FramebufferHashMap FramebufferHashMap;

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
} FNAVulkanImageData;

typedef struct FNAVulkanFramebuffer
{
	VkFramebuffer framebuffer;
	FNAVulkanImageData color;
	FNAVulkanImageData depth;
	int32_t width;
	int32_t height;
} FNAVulkanFramebuffer;

typedef struct PipelineHash
{
	StateHash blendState;
	StateHash rasterizerState;
	FNA3D_PrimitiveType primitiveType;
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

struct VulkanTexture {
	VkImage *handle;
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

typedef struct FNAVulkanRenderer
{
	FNA3D_Device *parentDevice;
	VkInstance instance;
	VkPhysicalDevice physicalDevice;
	VkDevice logicalDevice;

	QueueFamilyIndices queueFamilyIndices;
	VkQueue graphicsQueue;
	VkQueue presentQueue;

	SurfaceFormatMapping surfaceFormatMapping;
	VkSurfaceKHR surface;
	VkSwapchainKHR swapChain;
	FNAVulkanImageData *swapChainImages;
	uint32_t swapChainImageCount;
	VkExtent2D swapChainExtent;
	uint32_t currentSwapChainIndex;

	VkRenderPass backbufferRenderPass;
	VkFramebuffer fauxBackbuffer;

	VkCommandPool commandPool;
	VkPipelineCache pipelineCache;

	VkRenderPass renderPass;
	VkFramebuffer framebuffer;
	VkPipeline currentPipeline;
	VkCommandBuffer *commandBuffers;
	uint32_t commandBufferCapacity;
	uint32_t commandBufferCount;

	FNA3D_Vec4 clearColor;

	int32_t currentAttachmentWidth;
	int32_t currentAttachmentHeight;

	FNAVulkanImageData colorAttachments[MAX_RENDERTARGET_BINDINGS];
	FNAVulkanImageData depthStencilAttachment;

	uint32_t currentAttachmentCount;

	FNA3D_DepthFormat currentDepthFormat;

	FNA3D_Viewport viewport;
	FNA3D_Rect scissorRect;

	FNA3D_BlendState blendState;

	FNA3D_DepthStencilState depthStencilState;
	FNA3D_RasterizerState rasterizerState;
	FNA3D_PrimitiveType currentPrimitiveType;

	VulkanTexture *textures[MAX_TEXTURE_SAMPLERS];
	VkSampler *samplers[MAX_TEXTURE_SAMPLERS];
	uint8_t textureNeedsUpdate[MAX_TEXTURE_SAMPLERS];
	uint8_t samplerNeedsUpdate[MAX_TEXTURE_SAMPLERS];

	uint32_t fauxBackbufferWidth;
	uint32_t fauxBackbufferHeight;
	FNAVulkanFramebuffer *framebuffers;
	uint32_t framebufferCount;

	VkPipelineLayout pipelineLayout;
	PipelineHashMap *pipelineHashMap;
	RenderPassHashMap *renderPassHashMap;
	FramebufferHashMap *framebufferHashMap;

	VkFence renderQueueFence;
	VkSemaphore imageAvailableSemaphore;
	VkSemaphore renderFinishedSemaphore;

	uint8_t frameInProgress;
	uint8_t renderPassInProgress;
	uint8_t shouldClearColor;
	uint8_t needNewRenderPass;

	#define VULKAN_INSTANCE_FUNCTION(ext, ret, func, params) \
		vkfntype_##func func;
	#include "FNA3D_Driver_Vulkan_instance_funcs.h"
	#undef VULKAN_INSTANCE_FUNCTION

	#define VULKAN_DEVICE_FUNCTION(ext, ret, func, params) \
		vkfntype_##func func;
	#include "FNA3D_Driver_Vulkan_device_funcs.h"
	#undef VULKAN_DEVICE_FUNCTION
} FNAVulkanRenderer;

/* forward declarations */

static void BeginRenderPass(
	FNAVulkanRenderer *renderer
);

static void BindBackbuffer(
	FNAVulkanRenderer *renderer
);

static uint8_t CreateImage(
	FNAVulkanRenderer *renderer,
	uint32_t width,
	uint32_t height,
	VkFormat format,
	VkComponentMapping swizzle,
	VkImageAspectFlags aspectMask,
	VkImageTiling tiling,
	VkImageUsageFlags usage,
	VkMemoryPropertyFlags memoryProperties,
	FNAVulkanImageData *imageData
);

static VulkanTexture* CreateTexture(
	FNAVulkanRenderer *renderer,
	VkImage *texture,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget
);

static void EndPass(
	FNAVulkanRenderer *renderer
);

static VkPipeline FetchPipeline(
	FNAVulkanRenderer *renderer
);

static VkRenderPass FetchRenderPass(
	FNAVulkanRenderer *renderer
);

static VkFramebuffer FetchFramebuffer(
	FNAVulkanRenderer *renderer,
	VkRenderPass renderPass
);

static PipelineHash GetPipelineHash(
	FNAVulkanRenderer *renderer
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

static uint8_t InitBackbufferPass(
	FNAVulkanRenderer *renderer,
	uint32_t backbufferWidth,
	uint32_t backbufferHeight
);

static void UpdateRenderPass(
	FNA3D_Renderer *driverData
);

static void ResetAttachments(
	FNAVulkanRenderer *renderer
);

void VULKAN_SetRenderTargets(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *renderbuffer,
	FNA3D_DepthFormat depthFormat
);

/* static vars */

static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = NULL;

#define VULKAN_GLOBAL_FUNCTION(name) static PFN_##name name = NULL;
#include "FNA3D_Driver_Vulkan_global_funcs.h"
#undef VULKAN_GLOBAL_FUNCTION

/* translations arrays go here */

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
	/* TODO: check device compatibility with renderer */
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
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s: %s\n",
			vulkanFunctionName,
			VkErrorMessages(result)
		);
	}
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
	/* TODO: INCOMPLETE */

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

	for (uint32_t i = 0; i < hmlenu(renderer->pipelineHashMap); i++)
	{
		renderer->vkDestroyPipeline(
			renderer->logicalDevice,
			renderer->pipelineHashMap[i].value,
			NULL
		);
	}

	renderer->vkDestroyPipelineLayout(
		renderer->logicalDevice,
		renderer->pipelineLayout,
		NULL
	);

	renderer->vkDestroyPipelineCache(
		renderer->logicalDevice,
		renderer->pipelineCache,
		NULL
	);

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

	renderer->vkDestroyFramebuffer(
		renderer->logicalDevice,
		renderer->fauxBackbuffer,
		NULL
	);

	for (uint32_t i = 0; i < MAX_RENDERTARGET_BINDINGS; i++)
	{
		renderer->vkDestroyImageView(
			renderer->logicalDevice,
			renderer->colorAttachments[i].view,
			NULL
		);

		renderer->vkDestroyImage(
			renderer->logicalDevice,
			renderer->colorAttachments[i].image,
			NULL
		);

		renderer->vkFreeMemory(
			renderer->logicalDevice,
			renderer->colorAttachments[i].memory,
			NULL
		);
	}

	renderer->vkDestroyImageView(
		renderer->logicalDevice,
		renderer->depthStencilAttachment.view,
		NULL
	);

	renderer->vkDestroyImage(
		renderer->logicalDevice,
		renderer->depthStencilAttachment.image,
		NULL
	);

	renderer->vkFreeMemory(
		renderer->logicalDevice,
		renderer->depthStencilAttachment.memory,
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

	SDL_free(renderer->commandBuffers);
	SDL_free(renderer->swapChainImages);
	SDL_free(renderer);
	SDL_free(device);
}

static uint8_t CreateImage(
	FNAVulkanRenderer *renderer,
	uint32_t width,
	uint32_t height,
	VkFormat format,
	VkComponentMapping swizzle,
	VkImageAspectFlags aspectMask,
	VkImageTiling tiling,
	VkImageUsageFlags usage,
	VkMemoryPropertyFlags memoryProperties,
	FNAVulkanImageData *imageData
) {
	VkResult result;

	VkImageCreateInfo imageCreateInfo = {
		VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO
	};

	imageCreateInfo.flags = 0;
	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	imageCreateInfo.format = format;
	imageCreateInfo.extent.width = width;
	imageCreateInfo.extent.height = height;
	imageCreateInfo.extent.depth = 1;
	imageCreateInfo.mipLevels = 1;
	imageCreateInfo.arrayLayers = 1;
	imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCreateInfo.tiling = tiling;
	imageCreateInfo.usage = usage;
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCreateInfo.queueFamilyIndexCount = 0;
	/* new images must be created with undefined layout */
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

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

	allocInfo.allocationSize = memoryRequirements.size;

	if (
		!FindMemoryType(
			renderer,
			memoryRequirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
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

	return 1;
}

static VulkanTexture* CreateTexture(
	FNAVulkanRenderer *renderer,
	VkImage *texture,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	VulkanTexture *result = SDL_malloc(sizeof(VulkanTexture));
	result->handle = texture;
	result->width = width;
	result->height = height;
	result->format = format;
	result->hasMipmaps = levelCount > 1;
	result->isPrivate = isRenderTarget;
	result->wrapS = FNA3D_TEXTUREADDRESSMODE_WRAP;
	result->wrapT = FNA3D_TEXTUREADDRESSMODE_WRAP;
	result->wrapR = FNA3D_TEXTUREADDRESSMODE_WRAP;
	result->filter = FNA3D_TEXTUREFILTER_LINEAR;
	result->anisotropy = 4.0f;
	result->maxMipmapLevel = 0;
	result->lodBias = 0.0f;
	result->next = NULL;
	return result;
}

static void ImageLayoutTransition(
	FNAVulkanRenderer *renderer,
	FNAVulkanImageData *imageData,
	VkImageLayout oldLayout,
	VkImageLayout targetLayout,
	VkImageAspectFlags aspectMask
) {
	if (oldLayout != targetLayout)
	{
		VkImageMemoryBarrier barrier = {
			VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER
		};

		barrier.oldLayout = oldLayout;
		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.levelCount = 1;
		barrier.image = imageData->image;

		barrier.subresourceRange.aspectMask = aspectMask;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

		barrier.newLayout = targetLayout;

		VkPipelineStageFlags srcStage = 0;
		VkPipelineStageFlags dstStage = 0;

		/* FIXME: a lot of these settings are probably broken */

		switch (barrier.oldLayout)
		{
			case VK_IMAGE_LAYOUT_UNDEFINED:
			case VK_IMAGE_LAYOUT_PREINITIALIZED:
				barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
				srcStage = VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;

			case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
				barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				break;

			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
				barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				srcStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				break;

			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;

			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;

			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; /* FIXME: ?? */
				break;

			case VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR:
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;

			case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;

			default:
				SDL_LogError(
					SDL_LOG_CATEGORY_APPLICATION,
					"%s is %s\n",
					VkImageLayoutString(barrier.oldLayout),
					"Invalid old layout for image layout transition"
				);
				return;
		}

		switch (barrier.newLayout)
		{
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;

			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
				barrier.srcAccessMask |= VK_ACCESS_TRANSFER_READ_BIT;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				srcStage |= VK_PIPELINE_STAGE_TRANSFER_BIT;
				dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;

			case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
				barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				break;

			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
				barrier.dstAccessMask |=
					VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				dstStage |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				break;

			/* FIXME: check this */
			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				barrier.srcAccessMask =
					VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
				dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				break;

			case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
				barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				break;

			default:
				SDL_LogError(
					SDL_LOG_CATEGORY_APPLICATION,
					"%s is %s\n",
					VkImageLayoutString(barrier.newLayout),
					"Invalid new layout for image layout transition"
				);
				return;
		}

		renderer->vkCmdPipelineBarrier(
			renderer->commandBuffers[renderer->commandBufferCount - 1],
			srcStage,
			dstStage,
			0,
			0,
			NULL,
			0,
			NULL,
			1,
			&barrier
		);
	}
}

static uint8_t BlitFramebuffer(
	FNAVulkanRenderer *renderer,
	FNAVulkanImageData *srcImage,
	FNA3D_Rect srcRect,
	FNAVulkanImageData *dstImage,
	FNA3D_Rect dstRect
) {
	VkResult vulkanResult;

	renderer->commandBufferCount++;

	if (renderer->commandBufferCount > renderer->commandBufferCapacity)
	{
		renderer->commandBufferCapacity *= 2;

		renderer->commandBuffers = SDL_realloc(
			renderer->commandBuffers,
			sizeof(VkCommandBuffer) * renderer->commandBufferCapacity
		);

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

	ImageLayoutTransition(
		renderer,
		srcImage,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_IMAGE_ASPECT_COLOR_BIT
	);

	ImageLayoutTransition(
		renderer,
		dstImage,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_ASPECT_COLOR_BIT
	);

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

	ImageLayoutTransition(
		renderer,
		dstImage,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_IMAGE_ASPECT_COLOR_BIT
	);

	vulkanResult = renderer->vkEndCommandBuffer(
		renderer->commandBuffers[renderer->commandBufferCount - 1]
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkEndCommandBuffer", vulkanResult);
		return 0;
	}

	return 1;
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

	VkPipelineRasterizationStateCreateInfo rasterizerInfo = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rasterizerInfo.depthClampEnable = VK_FALSE;
	rasterizerInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterizerInfo.polygonMode = XNAToVK_PolygonMode[renderer->rasterizerState.fillMode];
	rasterizerInfo.lineWidth = 1.0f;
	rasterizerInfo.cullMode = XNAToVK_CullMode[renderer->rasterizerState.cullMode];
	rasterizerInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizerInfo.depthBiasEnable = VK_TRUE;

	/* TODO: actually implement multisampling */
	VkPipelineMultisampleStateCreateInfo multisamplingInfo = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisamplingInfo.sampleShadingEnable = VK_FALSE;
	multisamplingInfo.minSampleShading = 1.0f;
	multisamplingInfo.pSampleMask = NULL; /* FIXME: comes from FNA3D_BLENDSTATE? */
	multisamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; /* FIXME: comes from FNA3D_PresentationParameters? */
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

	VkDynamicState dynamicStates[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_BLEND_CONSTANTS,
		VK_DYNAMIC_STATE_STENCIL_REFERENCE,
		VK_DYNAMIC_STATE_DEPTH_BIAS
	};

	VkPipelineDynamicStateCreateInfo dynamicStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicStateInfo.dynamicStateCount = sizeof(dynamicStates)/sizeof(dynamicStates[0]);
	dynamicStateInfo.pDynamicStates = dynamicStates;

	VkGraphicsPipelineCreateInfo pipelineCreateInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipelineCreateInfo.stageCount = 0;
	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyInfo;
	pipelineCreateInfo.pViewportState = &viewportStateInfo;
	pipelineCreateInfo.pRasterizationState = &rasterizerInfo;
	pipelineCreateInfo.pMultisampleState = &multisamplingInfo;
	pipelineCreateInfo.pDepthStencilState = NULL; /* TODO */
	pipelineCreateInfo.pColorBlendState = &colorBlendStateInfo;
	pipelineCreateInfo.pDynamicState = &dynamicStateInfo;
	pipelineCreateInfo.layout = renderer->pipelineLayout;
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

	for (uint32_t i = 0; i < MAX_RENDERTARGET_BINDINGS; i++)
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

	VkAttachmentReference colorAttachmentReferences[MAX_RENDERTARGET_BINDINGS];
	for (uint32_t i = 0; i < MAX_RENDERTARGET_BINDINGS; i++)
	{
		colorAttachmentReferences[i].attachment = i;
		colorAttachmentReferences[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

	VkAttachmentReference depthStencilAttachmentReference;
	if (renderer->currentDepthFormat != FNA3D_DEPTHFORMAT_NONE)
	{
		depthStencilAttachmentReference.attachment = MAX_RENDERTARGET_BINDINGS;
		depthStencilAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attachmentDescriptions[MAX_RENDERTARGET_BINDINGS].flags = 0;
		attachmentDescriptions[MAX_RENDERTARGET_BINDINGS].format = XNAToVK_DepthFormat(renderer->currentDepthFormat);
		attachmentDescriptions[MAX_RENDERTARGET_BINDINGS].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescriptions[MAX_RENDERTARGET_BINDINGS].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescriptions[MAX_RENDERTARGET_BINDINGS].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescriptions[MAX_RENDERTARGET_BINDINGS].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescriptions[MAX_RENDERTARGET_BINDINGS].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescriptions[MAX_RENDERTARGET_BINDINGS].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescriptions[MAX_RENDERTARGET_BINDINGS].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		renderer->currentAttachmentCount = MAX_RENDERTARGET_BINDINGS + 1;
	}
	else
	{
		renderer->currentAttachmentCount = MAX_RENDERTARGET_BINDINGS;
	}

	if (renderer->currentDepthFormat == FNA3D_DEPTHFORMAT_D24S8)
	{
		/* TODO: update stencil */
	}

	VkSubpassDescription subpass;
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.flags = 0;
	subpass.inputAttachmentCount = 0;
	subpass.pInputAttachments = NULL;
	subpass.colorAttachmentCount = MAX_RENDERTARGET_BINDINGS;
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
	renderPassCreateInfo.attachmentCount = renderer->currentAttachmentCount;
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

	for (uint32_t i = 0; i < MAX_RENDERTARGET_BINDINGS; i++)
	{
		imageViewAttachments[i] = renderer->colorAttachments[i].view;
	}
	imageViewAttachments[MAX_RENDERTARGET_BINDINGS] = renderer->depthStencilAttachment.view;

	VkFramebufferCreateInfo framebufferInfo = {
		VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
	};

	framebufferInfo.flags = 0;
	framebufferInfo.renderPass = renderPass;
	framebufferInfo.attachmentCount = renderer->currentAttachmentCount;
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

static PipelineHash GetPipelineHash(
	FNAVulkanRenderer *renderer
) {
	PipelineHash hash;
	hash.blendState = GetBlendStateHash(renderer->blendState);
	hash.rasterizerState = GetRasterizerStateHash(renderer->rasterizerState);
	hash.primitiveType = renderer->currentPrimitiveType;
	hash.renderPass = renderer->renderPass;
	return hash;
}

static RenderPassHash GetRenderPassHash(
	FNAVulkanRenderer *renderer
) {
	RenderPassHash hash;
	hash.attachmentCount = renderer->currentAttachmentCount;
	return hash;
}

static void BeginRenderPass(
	FNAVulkanRenderer *renderer
)
{
	VkResult vulkanResult;

	renderer->commandBufferCount++;

	if (renderer->commandBufferCount > renderer->commandBufferCapacity)
	{
		renderer->commandBufferCapacity *= 2;

		renderer->commandBuffers = SDL_realloc(
			renderer->commandBuffers,
			sizeof(VkCommandBuffer) * renderer->commandBufferCapacity
		);

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
			return;
		}
	}

	renderer->renderPass = FetchRenderPass(renderer);
	renderer->framebuffer = FetchFramebuffer(renderer, renderer->renderPass);

	/* TODO: is this necessary? will the attachment ever differ from the backbuffer size? */
	renderer->currentAttachmentWidth = renderer->fauxBackbufferWidth;
	renderer->currentAttachmentHeight = renderer->fauxBackbufferHeight;

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
		return;
	}

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

	VkOffset2D scissorOffset = { renderer->scissorRect.x, renderer->scissorRect.y };
	VkExtent2D scissorExtent = { renderer->scissorRect.w, renderer->scissorRect.h };
	VkRect2D scissor = { scissorOffset, scissorExtent };

	renderer->vkCmdSetScissor(
		renderer->commandBuffers[renderer->commandBufferCount - 1],
		0,
		1,
		&scissor
	);

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

	/* TODO: depth stencil state */

	renderer->vkCmdSetDepthBias(
		renderer->commandBuffers[renderer->commandBufferCount - 1],
		renderer->rasterizerState.depthBias,
		0, /* unused */
		renderer->rasterizerState.slopeScaleDepthBias
	);

	/* TODO: visibility buffer */

	/* Reset bindings */

	for (uint32_t i = 0; i < MAX_TEXTURE_SAMPLERS; i++)
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
	renderer->needNewRenderPass = 0;
}

void VULKAN_BeginFrame(FNA3D_Renderer *driverData)
{
	/* TODO */

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

	renderer->vkResetCommandPool(
		renderer->logicalDevice,
		renderer->commandPool,
		VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT
	);

	renderer->frameInProgress = 1;
	renderer->commandBufferCount = 0;
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

	SDL_LogDebug(
		SDL_LOG_CATEGORY_APPLICATION,
		"%s\n",
		"blah"
	);

	/* TODO */
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;

	VULKAN_BeginFrame(driverData);
	VULKAN_SetRenderTargets(driverData, NULL, 0, NULL, FNA3D_DEPTHFORMAT_NONE);
	EndPass(renderer);

	uint32_t image_index;
	result = renderer->vkAcquireNextImageKHR(
		renderer->logicalDevice,
		renderer->swapChain,
		UINT64_MAX,
		renderer->imageAvailableSemaphore,
		VK_NULL_HANDLE,
		&image_index
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkAcquireNextImageKHR", result);
		return;
	}

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

	BlitFramebuffer(
		renderer,
		&renderer->colorAttachments[0],
		srcRect,
		&renderer->swapChainImages[image_index],
		dstRect
	);

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

	VkSwapchainKHR swapChains[] = { renderer->swapChain };
	uint32_t imageIndices[] = { image_index };

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

	renderer->frameInProgress = 0;
}

void VULKAN_SetPresentationInterval(
	FNA3D_Renderer *driverData,
	FNA3D_PresentInterval presentInterval
) {
	/* TODO */
}

/* Drawing */

void VULKAN_Clear(
	FNA3D_Renderer *driverData,
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
) {
	/* TODO */
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;
	uint8_t clearTarget = (options & FNA3D_CLEAROPTIONS_TARGET) == FNA3D_CLEAROPTIONS_TARGET;

	if (clearTarget)
	{
		renderer->clearColor = *color;
		renderer->shouldClearColor = 1;

		if (renderer->frameInProgress)
		{
			renderer->needNewRenderPass = 1;
		}
	}
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
	/* TODO */
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
	/* TODO */
}

void VULKAN_DrawPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
) {
	/* TODO */
}

FNA3DAPI void VULKAN_DrawUserIndexedPrimitives(
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
	/* TODO */
}

FNA3DAPI void VULKAN_DrawUserPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	void* vertexData,
	int32_t vertexOffset,
	int32_t primitiveCount
) {
	/* TODO */
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

		VkOffset2D offset;
		VkExtent2D extent;

		if (renderer->frameInProgress)
		{
			if (!renderer->rasterizerState.scissorTestEnable)
			{
				offset.x = 0;
				offset.y = 0;
				extent.width = renderer->currentAttachmentWidth;
				extent.height = renderer->currentAttachmentHeight;
			}
			else
			{
				offset.x = scissor->x;
				offset.y = scissor->y;
				extent.width = scissor->w;
				extent.height = scissor->h;
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
}

void VULKAN_GetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
	/* TODO */
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
	/* TODO */
}

void VULKAN_SetMultiSampleMask(FNA3D_Renderer *driverData, int32_t mask)
{
	/* TODO */
}

int32_t VULKAN_GetReferenceStencil(FNA3D_Renderer *driverData)
{
	/* TODO */
}

void VULKAN_SetReferenceStencil(FNA3D_Renderer *driverData, int32_t ref)
{
	/* TODO */
}

/* Immutable Render States */

void VULKAN_SetBlendState(
	FNA3D_Renderer *driverData,
	FNA3D_BlendState *blendState
) {
	/* TODO */
}

void VULKAN_SetDepthStencilState(
	FNA3D_Renderer *driverData,
	FNA3D_DepthStencilState *depthStencilState
) {
	/* TODO */
}

void VULKAN_ApplyRasterizerState(
	FNA3D_Renderer *driverData,
	FNA3D_RasterizerState *rasterizerState
) {
	/* TODO */
}

void VULKAN_VerifySampler(
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
	/* TODO */
}

void VULKAN_ApplyVertexDeclaration(
	FNA3D_Renderer *driverData,
	FNA3D_VertexDeclaration *vertexDeclaration,
	void* ptr,
	int32_t vertexOffset
) {
	/* TODO */
}

/* Render Targets */

static void UpdateRenderPass(
	FNA3D_Renderer *driverData
) {
	/* TODO */
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;

	if (!renderer->needNewRenderPass) { return; }

	VULKAN_BeginFrame(driverData);

	if (renderer->renderPassInProgress)
	{
		EndPass(renderer);
	}

	BeginRenderPass(renderer);

	/* TODO: reset depth stencil binding here */

	VkClearAttachment clearAttachments[MAX_RENDERTARGET_BINDINGS + 1];

	uint32_t nextIndex = 0;
	if (renderer->shouldClearColor)
	{
		for (uint32_t i = 0; i < MAX_RENDERTARGET_BINDINGS; i++)
		{
			VkClearValue clearColor = {{{
				renderer->clearColor.x,
				renderer->clearColor.y,
				renderer->clearColor.z,
				renderer->clearColor.w
			}}};

			VkClearAttachment clearAttachment;
			clearAttachment.clearValue = clearColor;
			clearAttachment.colorAttachment = i;
			clearAttachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

			clearAttachments[nextIndex++] = clearAttachment;
		}
	}

	if (nextIndex > 0)
	{
		VkRect2D rect;
		rect.offset.x = 0;
		rect.offset.y = 0;
		rect.extent.width = renderer->fauxBackbufferWidth;
		rect.extent.height = renderer->fauxBackbufferHeight;

		VkClearRect clearRect;
		clearRect.baseArrayLayer = 0;
		clearRect.layerCount = 1;
		clearRect.rect = rect;

		renderer->vkCmdClearAttachments(
			renderer->commandBuffers[renderer->commandBufferCount - 1],
			nextIndex,
			clearAttachments,
			1,
			&clearRect
		);
	}

	renderer->needNewRenderPass = 0;
	renderer->shouldClearColor = 0;
}

static void ResetAttachments(
	FNAVulkanRenderer *renderer
) {
	/* TODO */
}

static void BindBackbuffer(
	FNAVulkanRenderer *renderer
) {
	/* TODO */
}

static void EndPass(
	FNAVulkanRenderer *renderer
) {
	if (renderer->commandBufferCount > 0)
	{
		renderer->vkCmdEndRenderPass(
			renderer->commandBuffers[renderer->commandBufferCount - 1]
		);

		VkResult result = renderer->vkEndCommandBuffer(
			renderer->commandBuffers[renderer->commandBufferCount - 1]
		);

		if (result != VK_SUCCESS)
		{
			LogVulkanResult("vkEndCommandBuffer", result);
		}
	}
}

void VULKAN_SetRenderTargets(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *renderbuffer,
	FNA3D_DepthFormat depthFormat
) {
	/* TODO */
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;

	if (renderer->shouldClearColor)
	{
		UpdateRenderPass(driverData);
	}

	renderer->needNewRenderPass = 1;

	ResetAttachments(renderer);

	if (renderTargets == NULL)
	{
		BindBackbuffer(renderer);
		return;
	}

	/* TODO: update color and depth stencil buffers */
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
	/* TODO */
}

FNA3D_SurfaceFormat VULKAN_GetBackbufferSurfaceFormat(
	FNA3D_Renderer *driverData
) {
	/* TODO */
}

FNA3D_DepthFormat VULKAN_GetBackbufferDepthFormat(FNA3D_Renderer *driverData)
{
	/* TODO */
}

int32_t VULKAN_GetBackbufferMultiSampleCount(FNA3D_Renderer *driverData)
{
	/* TODO */
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
	/* TODO */
	FNAVulkanRenderer *renderer = (FNAVulkanRenderer*) driverData;

	/* TODO: store swizzle on VulkanTexture */
	SurfaceFormatMapping surfaceFormatMapping = XNAToVK_SurfaceFormat[format];
	VkExtent3D extent;
	extent.width = width;
	extent.height = height;
	extent.depth = 1;

	VkImageCreateInfo createInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	createInfo.format = surfaceFormatMapping.formatColor;
	createInfo.extent = extent;
	createInfo.mipLevels = levelCount;
	createInfo.imageType = VK_IMAGE_TYPE_2D;

	if (isRenderTarget)
	{
		createInfo.usage = 	VK_IMAGE_USAGE_TRANSFER_DST_BIT |
							VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
							VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}

	VkImage texture;
	renderer->vkCreateImage(
		renderer->logicalDevice,
		&createInfo,
		NULL,
		&texture
	);

	return (FNA3D_Texture*) CreateTexture(
		renderer,
		&texture,
		format,
		width,
		height,
		levelCount,
		isRenderTarget
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
	/* TODO */
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
	/* TODO */
}

FNA3D_Renderbuffer* VULKAN_GenDepthStencilRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_DepthFormat format,
	int32_t multiSampleCount
) {
	/* TODO */
}

void VULKAN_AddDisposeRenderbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Renderbuffer *renderbuffer
) {
	/* TODO */
}

/* Vertex Buffers */

FNA3D_Buffer* VULKAN_GenVertexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t vertexCount,
	int32_t vertexStride
) {
	/* TODO */
}
void VULKAN_AddDisposeVertexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	/* TODO */
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
	/* TODO */
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
	/* TODO */
}

/* Index Buffers */

FNA3D_Buffer* VULKAN_GenIndexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t indexCount,
	FNA3D_IndexElementSize indexElementSize
) {
	/* TODO */
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
	/* TODO */
}

void VULKAN_GetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength
) {
	/* TODO */
}

/* Effects */

typedef struct MOJOSHADER_effect MOJOSHADER_effect;
typedef struct MOJOSHADER_effectTechnique MOJOSHADER_effectTechnique;
typedef struct MOJOSHADER_effectStateChanges MOJOSHADER_effectStateChanges;

void VULKAN_CreateEffect(
	FNA3D_Renderer *driverData,
	uint8_t *effectCode,
	uint32_t effectCodeLength,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **result
) {
	/* TODO */
}

void VULKAN_CloneEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *cloneSource,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **result
) {
	/* TODO */
}

void VULKAN_AddDisposeEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	/* TODO */
}

void VULKAN_SetEffectTechnique(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique
) {
	/* TODO */
}

void VULKAN_ApplyEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	uint32_t pass,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	/* TODO */
}

void VULKAN_BeginPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	/* TODO */
}

void VULKAN_EndPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	/* TODO */
}

/* Queries */

FNA3D_Query* VULKAN_CreateQuery(FNA3D_Renderer *driverData)
{
	/* TODO */
}

void VULKAN_AddDisposeQuery(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	/* TODO */
}

void VULKAN_QueryBegin(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	/* TODO */
}

void VULKAN_QueryEnd(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	/* TODO */
}

uint8_t VULKAN_QueryComplete(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	/* TODO */
}

int32_t VULKAN_QueryPixelCount(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	/* TODO */
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

int32_t VULKAN_GetMaxTextureSlots(FNA3D_Renderer *driverData)
{
	/* TODO */
}

int32_t VULKAN_GetMaxMultiSampleCount(FNA3D_Renderer *driverData)
{
	/* TODO */
}

/* Debugging */

FNA3DAPI void VULKAN_SetStringMarker(FNA3D_Renderer *driverData, const char *text)
{
	/* TODO */
}

/* Buffer Objects */

intptr_t VULKAN_GetBufferSize(FNA3D_Buffer *buffer)
{
	/* TODO */
}

/* Effect Objects */

MOJOSHADER_effect* VULKAN_GetEffectData(FNA3D_Effect *effect)
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

	if (queueFamilyCount == 0)
	{
		return 0;
	}

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

	if (queueFamilyCount == 0)
	{
		return 0;
	}

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

static uint8_t InitBackbufferPass(
	FNAVulkanRenderer *renderer,
	uint32_t backbufferWidth,
	uint32_t backbufferHeight
) {
	VkResult vulkanResult;

	VkAttachmentDescription attachments[2];
	attachments[0].format = renderer->surfaceFormatMapping.formatColor;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	attachments[0].flags = 0;

	attachments[1].format = XNAToVK_DepthFormat(renderer->currentDepthFormat);
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].flags = 0;

	VkAttachmentReference colorReference;
	colorReference.attachment = 0;
	colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthReference;
	depthReference.attachment = 1;
	depthReference.layout = attachments[1].finalLayout;

	VkSubpassDescription subpass;
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.flags = 0;
	subpass.inputAttachmentCount = 0;
	subpass.pInputAttachments = NULL;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorReference;
	subpass.pResolveAttachments = NULL;
	subpass.pDepthStencilAttachment = &depthReference;
	subpass.preserveAttachmentCount = 0;
	subpass.pPreserveAttachments = NULL;

	VkSubpassDependency subpassDependency;
	subpassDependency.srcSubpass = 0;
	subpassDependency.dstSubpass = 0;
	subpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.srcAccessMask = 0;
	subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo renderPassInfo = {
		VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO
	};
	renderPassInfo.attachmentCount = 2;
	renderPassInfo.pAttachments = attachments;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &subpassDependency;

	vulkanResult = renderer->vkCreateRenderPass(
		renderer->logicalDevice,
		&renderPassInfo,
		NULL,
		&renderer->backbufferRenderPass
	);

	if (vulkanResult != VK_SUCCESS)
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s\n",
			"Failed to create backbuffer render pass"
		);

		return 0;
	}

	/* init the faux backbuffer */

	VkImageView imageViewAttachments[2] = {
		renderer->colorAttachments[0].view,
		renderer->depthStencilAttachment.view
	};

	VkFramebufferCreateInfo framebufferInfo = {
		VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
	};

	framebufferInfo.flags = 0;
	framebufferInfo.renderPass = renderer->backbufferRenderPass;
	framebufferInfo.attachmentCount = 2;
	framebufferInfo.pAttachments = imageViewAttachments;
	framebufferInfo.width = backbufferWidth;
	framebufferInfo.height = backbufferHeight;
	framebufferInfo.layers = 1;

	vulkanResult = renderer->vkCreateFramebuffer(
		renderer->logicalDevice,
		&framebufferInfo,
		NULL,
		&renderer->fauxBackbuffer
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateFramebuffer", vulkanResult);
		return 0;
	}

	return 1;
}

FNA3D_Device* VULKAN_CreateDevice(
	FNA3D_PresentationParameters *presentationParameters,
	uint8_t debugMode
) {
	/* TODO */
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
	/* empty for now because i don't know what we need yet... --cosmonaut */
	VkPhysicalDeviceFeatures deviceFeatures = { 0 };
	//SDL_memset(&deviceFeatures, '\0', sizeof(VkPhysicalDeviceFeatures));

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
	}

	VkExtent3D imageExtent;
	imageExtent.width = presentationParameters->backBufferWidth;
	imageExtent.height = presentationParameters->backBufferHeight;
	imageExtent.depth = 1;

	/* create color attachment images */

	for (uint32_t i = 0; i < MAX_RENDERTARGET_BINDINGS; i++)
	{
		if (
			!CreateImage(
				renderer,
				presentationParameters->backBufferWidth,
				presentationParameters->backBufferHeight,
				surfaceFormatMapping.formatColor,
				surfaceFormatMapping.swizzle,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_TILING_OPTIMAL,
				/* FIXME: transfer bit probably only needs to be set on 0? */
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				&renderer->colorAttachments[i]
			)
		) {
			SDL_LogError(
				SDL_LOG_CATEGORY_APPLICATION,
				"%s\n",
				"Failed to create color attachment image"
			);

			return NULL;
		}
	}

	/* create depth stencil image */

	if (presentationParameters->depthStencilFormat != FNA3D_DEPTHFORMAT_NONE)
	{
		VkComponentMapping identitySwizzle;
		identitySwizzle.r = VK_COMPONENT_SWIZZLE_R;
		identitySwizzle.g = VK_COMPONENT_SWIZZLE_G;
		identitySwizzle.b = VK_COMPONENT_SWIZZLE_B;
		identitySwizzle.a = VK_COMPONENT_SWIZZLE_A;

		VkFormat vulkanDepthStencilFormat = XNAToVK_DepthFormat(presentationParameters->depthStencilFormat);

		if (
			!CreateImage(
				renderer,
				presentationParameters->backBufferWidth,
				presentationParameters->backBufferHeight,
				vulkanDepthStencilFormat,
				identitySwizzle,
				VK_IMAGE_ASPECT_DEPTH_BIT,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				&renderer->depthStencilAttachment
			)
		) {
			SDL_LogError(
				SDL_LOG_CATEGORY_APPLICATION,
				"%s\n",
				"Failed to create depth stencil image"
			);

			return NULL;
		}
	}

	VkPipelineCacheCreateInfo pipelineCacheCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	vulkanResult = renderer->vkCreatePipelineCache(renderer->logicalDevice, &pipelineCacheCreateInfo, NULL, &renderer->pipelineCache);
	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreatePipelineCache", vulkanResult);
		return NULL;
	}

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
	};

	vulkanResult = renderer->vkCreatePipelineLayout(
		renderer->logicalDevice,
		&pipelineLayoutInfo,
		NULL,
		&renderer->pipelineLayout
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreatePipelineLayout", vulkanResult);
		return NULL;
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

	hmdefault(renderer->pipelineHashMap, NULL);
	hmdefault(renderer->renderPassHashMap, NULL);
	hmdefault(renderer->framebufferHashMap, NULL);

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

	InitBackbufferPass(
		renderer,
		presentationParameters->backBufferWidth,
		presentationParameters->backBufferHeight
	);

	renderer->commandBuffers = SDL_malloc(sizeof(VkCommandBuffer));
	renderer->commandBufferCount = 0;
	renderer->commandBufferCapacity = 1;

	VkCommandBufferAllocateInfo commandBufferAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	commandBufferAllocateInfo.commandPool = renderer->commandPool;
	commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandBufferAllocateInfo.commandBufferCount = 1; /* TODO: change for frames in flight */

	vulkanResult = renderer->vkAllocateCommandBuffers(
		renderer->logicalDevice,
		&commandBufferAllocateInfo,
		&renderer->commandBuffers[0]
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkAllocateCommandBuffers", vulkanResult);
		return NULL;
	}

	renderer->shouldClearColor = 0;
	renderer->needNewRenderPass = 1;
	renderer->frameInProgress = 0;

	return result;
}

FNA3D_Driver VulkanDriver = {
	"Vulkan",
	VULKAN_PrepareWindowAttributes,
	VULKAN_GetDrawableSize,
	VULKAN_CreateDevice
};

#endif /* FNA_3D_DRIVER_VULKAN */
