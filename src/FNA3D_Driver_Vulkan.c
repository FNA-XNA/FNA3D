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

#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_vulkan.h>

#include "vulkan.h"

/* Internal Structures */

/* translations arrays */

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

typedef struct QueueFamilyIndices {
	uint32_t graphicsFamily;
	uint32_t presentFamily;
} QueueFamilyIndices;

static uint8_t isDeviceSuitable(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, QueueFamilyIndices* queueFamilyIndices)
{
	queueFamilyIndices->graphicsFamily = UINT32_MAX;
	queueFamilyIndices->presentFamily = UINT32_MAX;

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);

	if (deviceProperties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
	{
		return 0;
	}

    uint32_t queueFamilyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, NULL);

	if (queueFamilyCount == 0)
	{
		return 0;
	}

    VkQueueFamilyProperties queueProps[queueFamilyCount];
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueProps);

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        VkBool32 supportsPresent;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &supportsPresent);
        if (supportsPresent && (queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
			queueFamilyIndices->graphicsFamily = i;
			queueFamilyIndices->presentFamily = i;
			return 1;
        }
    }

    return 0;
}

/* Init/Quit */

uint32_t VULKAN_PrepareWindowAttributes(uint8_t debugMode, uint32_t *flags)
{
    /* TODO */
}

void VULKAN_GetDrawableSize(void* window, int32_t *x, int32_t *y)
{
    SDL_Vulkan_GetDrawableSize((SDL_Window*) window, x, y);
}

void VULKAN_DestroyDevice(void* driverData)
{
    /* TODO */
}

/* Begin/End Frame */

void VULKAN_BeginFrame(void* driverData)
{
    /* TODO */
}

void VULKAN_SwapBuffers(
	void* driverData,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
    /* TODO */
}

void VULKAN_SetPresentationInterval(
	void* driverData,
	FNA3D_PresentInterval presentInterval
) {
    /* TODO */
}

/* Drawing */

void VULKAN_Clear(
	void* driverData,
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
) {
    /* TODO */
}

void VULKAN_DrawIndexedPrimitives(
	void* driverData,
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
	void* driverData,
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
	void* driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
) {
    /* TODO */
}

FNA3DAPI void VULKAN_DrawUserIndexedPrimitives(
	void* driverData,
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
	void* driverData,
	FNA3D_PrimitiveType primitiveType,
	void* vertexData,
	int32_t vertexOffset,
	int32_t primitiveCount
) {
    /* TODO */
}

/* Mutable Render States */

void VULKAN_SetViewport(void* driverData, FNA3D_Viewport *viewport)
{
    /* TODO */
}

void VULKAN_SetScissorRect(void* driverData, FNA3D_Rect *scissor)
{
    /* TODO */
}

void VULKAN_GetBlendFactor(
	void* driverData,
	FNA3D_Color *blendFactor
) {
    /* TODO */
}

void VULKAN_SetBlendFactor(
	void* driverData,
	FNA3D_Color *blendFactor
) {
    /* TODO */
}

int32_t VULKAN_GetMultiSampleMask(void* driverData)
{
    /* TODO */
}

void VULKAN_SetMultiSampleMask(void* driverData, int32_t mask)
{
    /* TODO */
}

int32_t VULKAN_GetReferenceStencil(void* driverData)
{
    /* TODO */
}

void VULKAN_SetReferenceStencil(void* driverData, int32_t ref)
{
    /* TODO */
}

/* Immutable Render States */

void VULKAN_SetBlendState(
	void* driverData,
	FNA3D_BlendState *blendState
) {
    /* TODO */
}

void VULKAN_SetDepthStencilState(
	void* driverData,
	FNA3D_DepthStencilState *depthStencilState
) {
    /* TODO */
}

void VULKAN_ApplyRasterizerState(
	void* driverData,
	FNA3D_RasterizerState *rasterizerState
) {
    /* TODO */
}

void VULKAN_VerifySampler(
	void* driverData,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
    /* TODO */
}

/* Vertex State */

void VULKAN_ApplyVertexBufferBindings(
	void* driverData,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings,
	uint8_t bindingsUpdated,
	int32_t baseVertex
) {
    /* TODO */
}

void VULKAN_ApplyVertexDeclaration(
	void* driverData,
	FNA3D_VertexDeclaration *vertexDeclaration,
	void* ptr,
	int32_t vertexOffset
) {
    /* TODO */
}

/* Render Targets */

void VULKAN_SetRenderTargets(
	void* driverData,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *renderbuffer,
	FNA3D_DepthFormat depthFormat
) {
    /* TODO */
}

void VULKAN_ResolveTarget(
	void* driverData,
	FNA3D_RenderTargetBinding *target
) {
    /* TODO */
}

/* Backbuffer Functions */

void VULKAN_ResetBackbuffer(
	void* driverData,
	FNA3D_PresentationParameters *presentationParameters
) {
    /* TODO */
}

void VULKAN_ReadBackbuffer(
	void* driverData,
	void* data,
	int32_t dataLen,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h
) {
    /* TODO */
}

void VULKAN_GetBackbufferSize(
	void* driverData,
	int32_t *w,
	int32_t *h
) {
    /* TODO */
}

FNA3D_SurfaceFormat VULKAN_GetBackbufferSurfaceFormat(
	void* driverData
) {
    /* TODO */
}

FNA3D_DepthFormat VULKAN_GetBackbufferDepthFormat(void* driverData)
{
    /* TODO */
}

int32_t VULKAN_GetBackbufferMultiSampleCount(void* driverData)
{
    /* TODO */
}

/* Textures */

FNA3D_Texture* VULKAN_CreateTexture2D(
	void* driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
    /* TODO */
}

FNA3D_Texture* VULKAN_CreateTexture3D(
	void* driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t depth,
	int32_t levelCount
) {
    /* TODO */
}

FNA3D_Texture* VULKAN_CreateTextureCube(
	void* driverData,
	FNA3D_SurfaceFormat format,
	int32_t size,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
    /* TODO */
}

void VULKAN_AddDisposeTexture(
	void* driverData,
	FNA3D_Texture *texture
) {
    /* TODO */
}

void VULKAN_SetTextureData2D(
	void* driverData,
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
	void* driverData,
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
	void* driverData,
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
	void* driverData,
	FNA3D_Texture *y,
	FNA3D_Texture *u,
	FNA3D_Texture *v,
	int32_t w,
	int32_t h,
	void* ptr
) {
    /* TODO */
}

void VULKAN_GetTextureData2D(
	void* driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t textureWidth,
	int32_t textureHeight,
	int32_t level,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes
) {
    /* TODO */
}

void VULKAN_GetTextureData3D(
	void* driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t left,
	int32_t top,
	int32_t front,
	int32_t right,
	int32_t bottom,
	int32_t back,
	int32_t level,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes
) {
    /* TODO */
}

void VULKAN_GetTextureDataCube(
	void* driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t textureSize,
	FNA3D_CubeMapFace cubeMapFace,
	int32_t level,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes
) {
    /* TODO */
}

/* Renderbuffers */

FNA3D_Renderbuffer* VULKAN_GenColorRenderbuffer(
	void* driverData,
	int32_t width,
	int32_t height,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount,
	FNA3D_Texture *texture
) {
    /* TODO */
}

FNA3D_Renderbuffer* VULKAN_GenDepthStencilRenderbuffer(
	void* driverData,
	int32_t width,
	int32_t height,
	FNA3D_DepthFormat format,
	int32_t multiSampleCount
) {
    /* TODO */
}

void VULKAN_AddDisposeRenderbuffer(
	void* driverData,
	FNA3D_Renderbuffer *renderbuffer
) {
    /* TODO */
}

/* Vertex Buffers */

FNA3D_Buffer* VULKAN_GenVertexBuffer(
	void* driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t vertexCount,
	int32_t vertexStride
) {
    /* TODO */
}
void VULKAN_AddDisposeVertexBuffer(
	void* driverData,
	FNA3D_Buffer *buffer
) {
    /* TODO */
}

void VULKAN_SetVertexBufferData(
	void* driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
    /* TODO */
}

void VULKAN_GetVertexBufferData(
	void* driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride
) {
    /* TODO */
}

/* Index Buffers */

FNA3D_Buffer* VULKAN_GenIndexBuffer(
	void* driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t indexCount,
	FNA3D_IndexElementSize indexElementSize
) {
    /* TODO */
}

FNA3DAPI void VULKAN_AddDisposeIndexBuffer(
	void* driverData,
	FNA3D_Buffer *buffer
) {
    /* TODO */
}

void VULKAN_SetIndexBufferData(
	void* driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
    /* TODO */
}

void VULKAN_GetIndexBufferData(
	void* driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes
) {
    /* TODO */
}

/* Effects */

typedef struct MOJOSHADER_effect MOJOSHADER_effect;
typedef struct MOJOSHADER_effectTechnique MOJOSHADER_effectTechnique;
typedef struct MOJOSHADER_effectStateChanges MOJOSHADER_effectStateChanges;

FNA3D_Effect* VULKAN_CreateEffect(
	void* driverData,
	uint8_t *effectCode,
	uint32_t effectCodeLength
) {
    /* TODO */
}

FNA3D_Effect* VULKAN_CloneEffect(
	void* driverData,
	FNA3D_Effect *effect
) {
    /* TODO */
}

void VULKAN_AddDisposeEffect(
	void* driverData,
	FNA3D_Effect *effect
) {
    /* TODO */
}

void VULKAN_ApplyEffect(
	void* driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique,
	uint32_t pass,
	MOJOSHADER_effectStateChanges *stateChanges
) {
    /* TODO */
}

void VULKAN_BeginPassRestore(
	void* driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectStateChanges *stateChanges
) {
    /* TODO */
}

void VULKAN_EndPassRestore(
	void* driverData,
	FNA3D_Effect *effect
) {
    /* TODO */
}

/* Queries */

FNA3D_Query* VULKAN_CreateQuery(void* driverData)
{
    /* TODO */
}

void VULKAN_AddDisposeQuery(void* driverData, FNA3D_Query *query)
{
    /* TODO */
}

void VULKAN_QueryBegin(void* driverData, FNA3D_Query *query)
{
    /* TODO */
}

void VULKAN_QueryEnd(void* driverData, FNA3D_Query *query)
{
    /* TODO */
}

uint8_t VULKAN_QueryComplete(void* driverData, FNA3D_Query *query)
{
    /* TODO */
}

int32_t VULKAN_QueryPixelCount(
	void* driverData,
	FNA3D_Query *query
) {
    /* TODO */
}

/* Feature Queries */

uint8_t VULKAN_SupportsDXT1(void* driverData)
{
    /* TODO */
}

uint8_t VULKAN_SupportsS3TC(void* driverData)
{
    /* TODO */
}

uint8_t VULKAN_SupportsHardwareInstancing(void* driverData)
{
    /* TODO */
}

uint8_t VULKAN_SupportsNoOverwrite(void* driverData)
{
    /* TODO */
}

int32_t VULKAN_GetMaxTextureSlots(void* driverData)
{
    /* TODO */
}

int32_t VULKAN_GetMaxMultiSampleCount(void* driverData)
{
    /* TODO */
}

/* Debugging */

FNA3DAPI void VULKAN_SetStringMarker(void* driverData, const char *text)
{
    /* TODO */
}

/* Buffer Objects */

intptr_t VULKAN_GetBufferSize(
	void* driverData,
	FNA3D_Buffer *buffer
) {
    /* TODO */
}

/* Effect Objects */

MOJOSHADER_effect* VULKAN_GetEffectData(
	void* driverData,
	FNA3D_Effect *effect
) {
    /* TODO */
}

FNA3D_Device* VULKAN_CreateDevice(
    FNA3D_PresentationParameters *presentationParameters
) {
    /* TODO */
    FNA3D_Device *result;
    VkResult vulkanResult;
    VkInstance instance;
	VkSurfaceKHR surface;
    uint32_t physicalDeviceCount;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDevice physicalDevices[physicalDeviceCount];
    VkDevice device;
    VkQueue graphicsQueue;
	VkQueue presentQueue;
    VkCommandPool commandPool;
	uint32_t extensionCount;
	char* extensionNames[64];

    /* create instance */

    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "FNA";
    appInfo.apiVersion = VK_MAKE_VERSION(1, 2, 136);

    SDL_Vulkan_GetInstanceExtensions(
        presentationParameters->deviceWindowHandle,
        &extensionCount,
        NULL
    );

    SDL_Vulkan_GetInstanceExtensions(
        presentationParameters->deviceWindowHandle,
        &extensionCount,
        &extensionNames
    );

    VkInstanceCreateInfo createInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensionNames;

    vulkanResult = vkCreateInstance(&createInfo, NULL, &instance);
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
            instance,
            &surface
        )
    ) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "SDL_Vulkan_CreateSurface failed: %s\n",
            SDL_GetError()
        );

        return NULL;
    }

    /* determine a suitable physical device */

    vulkanResult = vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, NULL);
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

    vulkanResult = vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices);
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
    for (int i = 0; i < physicalDeviceCount; i++)
    {
        if (isDeviceSuitable(physicalDevices[i], surface, &queueFamilyIndices))
        {
            physicalDevice = physicalDevices[i];
            physicalDeviceAssigned = 1;
            break;
        }
    }

    if (!physicalDeviceAssigned)
    {
        physicalDevice = physicalDevices[0];
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfoGraphics = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueCreateInfoGraphics.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
    queueCreateInfoGraphics.queueCount = 1;
    queueCreateInfoGraphics.pQueuePriorities = &queuePriority;

    VkDeviceQueueCreateInfo queueCreateInfoPresent = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueCreateInfoPresent.queueFamilyIndex = queueFamilyIndices.presentFamily;
    queueCreateInfoPresent.queueCount = 1;
    queueCreateInfoPresent.pQueuePriorities = &queuePriority;

	/* specifying used device features */
	/* empty for now because i don't know what we need yet... --cosmonaut */
	VkPhysicalDeviceFeatures deviceFeatures;

    /* creating the logical device */

	VkDeviceQueueCreateInfo queueCreateInfos[2] = { queueCreateInfoGraphics, queueCreateInfoPresent };

    VkDeviceCreateInfo deviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	deviceCreateInfo.queueCreateInfoCount = 2;
	deviceCreateInfo.pQueueCreateInfos = &queueCreateInfos;
	deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
    deviceCreateInfo.enabledExtensionCount = 0;

    vulkanResult = vkCreateDevice(physicalDevice, &deviceCreateInfo, NULL, &device);
   	if (vulkanResult != VK_SUCCESS)
	{
		SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "vkCreateDevice failed: %s\n",
            VkErrorMessages(vulkanResult)
        );

        return NULL;
	}

    vkGetDeviceQueue(device, queueFamilyIndices.graphicsFamily, 0, &graphicsQueue);
	vkGetDeviceQueue(device, queueFamilyIndices.presentFamily, 0, &presentQueue);

    result = (FNA3D_Device*) SDL_malloc(sizeof(FNA3D_Device));
    ASSIGN_DRIVER(VULKAN)
    result->driverData = device;
    return result;
}

FNA3D_Driver VulkanDriver = {
	"Vulkan",
	VULKAN_PrepareWindowAttributes,
	VULKAN_GetDrawableSize,
	VULKAN_CreateDevice
};

#endif /* FNA_3D_DRIVER_VULKAN */
