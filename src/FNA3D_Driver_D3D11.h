/* FNA3D - 3D Graphics Library for FNA
 *
 * Copyright (c) 2020-2023 Ethan Lee
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

#ifndef FNA3D_DRIVER_D3D11_H
#define FNA3D_DRIVER_D3D11_H

#define D3D11_NO_HELPERS
#define CINTERFACE
#define COBJMACROS
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <d3d11.h>
#pragma GCC diagnostic pop

/* IID Imports from https://magnumdb.com */

static const IID D3D_IID_IDXGIFactory1 = {0x770aae78,0xf26f,0x4dba,{0xa8,0x29,0x25,0x3c,0x83,0xd1,0xb3,0x87}};
static const IID D3D_IID_IDXGIFactory2 = {0x50c83a1c,0xe072,0x4c48,{0x87,0xb0,0x36,0x30,0xfa,0x36,0xa6,0xd0}};
static const IID D3D_IID_IDXGIFactory4 = {0x7632e1f5,0xee65,0x4dca,{0x87,0xfd,0x84,0xcd,0x75,0xf8,0x83,0x8d}};
static const IID D3D_IID_IDXGIFactory6 = {0xc1b6694f,0xff09,0x44a9,{0xb0,0x3c,0x77,0x90,0x0a,0x0a,0x1d,0x17}};
static const IID D3D_IID_IDXGIFactory5 = {0x7632e1f5,0xee65,0x4dca,{0x87,0xfd,0x84,0xcd,0x75,0xf8,0x83,0x8d}};
static const IID D3D_IID_IDXGIAdapter1 = {0x29038f61,0x3839,0x4626,{0x91,0xfd,0x08,0x68,0x79,0x01,0x1a,0x05}};
static const IID D3D_IID_ID3D11Texture2D = {0x6f15aaf2,0xd208,0x4e89,{0x9a,0xb4,0x48,0x95,0x35,0xd3,0x4f,0x9c}};
static const IID D3D_IID_ID3DUserDefinedAnnotation = {0xb2daad8b,0x03d4,0x4dbf,{0x95,0xeb,0x32,0xab,0x4b,0x63,0xd0,0xab}};

/* VS2010 / DirectX SDK Fallback Defines */

#ifndef DXGI_FORMAT_B4G4R4A4_UNORM
#define DXGI_FORMAT_B4G4R4A4_UNORM (DXGI_FORMAT) 115
#endif

#ifndef D3D_FEATURE_LEVEL_11_1
#define D3D_FEATURE_LEVEL_11_1 (D3D_FEATURE_LEVEL) 0xb100
#endif

/* D3D Function Typedefs */

typedef HRESULT(WINAPI *PFN_D3DCOMPILE)(
    LPCVOID pSrcData,
    SIZE_T SrcDataSize,
    LPCSTR pSourceName,
    const D3D_SHADER_MACRO *pDefines,
    ID3DInclude *pInclude,
    LPCSTR pEntrypoint,
    LPCSTR pTarget,
    UINT Flags1,
    UINT Flags2,
    ID3DBlob **ppCode,
    ID3DBlob **ppErrorMsgs
);

/* ID3DUserDefinedAnnotation */
/* From d3d11_1.h, cleaned up a bit... */

typedef struct ID3DUserDefinedAnnotation ID3DUserDefinedAnnotation;
typedef struct ID3DUserDefinedAnnotationVtbl
{
	HRESULT ( STDMETHODCALLTYPE *QueryInterface )(
		ID3DUserDefinedAnnotation * This,
		REFIID riid,
		void **ppvObject);

	ULONG ( STDMETHODCALLTYPE *AddRef )(
		ID3DUserDefinedAnnotation * This);

	ULONG ( STDMETHODCALLTYPE *Release )(
		ID3DUserDefinedAnnotation * This);

	INT ( STDMETHODCALLTYPE *BeginEvent )(
		ID3DUserDefinedAnnotation * This,
		LPCWSTR Name);

        INT ( STDMETHODCALLTYPE *EndEvent )(
            ID3DUserDefinedAnnotation * This);

	void ( STDMETHODCALLTYPE *SetMarker )(
		ID3DUserDefinedAnnotation * This,
		LPCWSTR Name);

	BOOL ( STDMETHODCALLTYPE *GetStatus )(
		ID3DUserDefinedAnnotation * This);
} ID3DUserDefinedAnnotationVtbl;

struct ID3DUserDefinedAnnotation
{
	struct ID3DUserDefinedAnnotationVtbl *lpVtbl;
};

#define ID3DUserDefinedAnnotation_QueryInterface(This,riid,ppvObject)	\
	( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) )

#define ID3DUserDefinedAnnotation_AddRef(This)	\
	( (This)->lpVtbl -> AddRef(This) )

#define ID3DUserDefinedAnnotation_Release(This)	\
	( (This)->lpVtbl -> Release(This) )

#define ID3DUserDefinedAnnotation_BeginEvent(This,Name)	\
	( (This)->lpVtbl -> BeginEvent(This,Name) )

#define ID3DUserDefinedAnnotation_EndEvent(This)	\
	( (This)->lpVtbl -> EndEvent(This) )

#define ID3DUserDefinedAnnotation_SetMarker(This,Name)	\
	( (This)->lpVtbl -> SetMarker(This,Name) )

#define ID3DUserDefinedAnnotation_GetStatus(This)	\
	( (This)->lpVtbl -> GetStatus(This) )

/* IDXGIFactory4 */
/* From dxgi1_4.h, cleaned up a bit... */

typedef struct IDXGIFactory4 IDXGIFactory4;
typedef struct IDXGIFactory4Vtbl
{
	HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
		IDXGIFactory4 * This,
		REFIID riid,
		void **ppvObject);
		
	ULONG ( STDMETHODCALLTYPE *AddRef )( 
		IDXGIFactory4 * This);
		
	ULONG ( STDMETHODCALLTYPE *Release )( 
		IDXGIFactory4 * This);
		
	HRESULT ( STDMETHODCALLTYPE *SetPrivateData )( 
		IDXGIFactory4 * This,
		REFGUID Name,
		UINT DataSize,
		const void *pData);
		
	HRESULT ( STDMETHODCALLTYPE *SetPrivateDataInterface )( 
		IDXGIFactory4 * This,
		REFGUID Name,
		const IUnknown *pUnknown);
		
	HRESULT ( STDMETHODCALLTYPE *GetPrivateData )( 
		IDXGIFactory4 * This,
		REFGUID Name,
		UINT *pDataSize,
		void *pData);
		
	HRESULT ( STDMETHODCALLTYPE *GetParent )( 
		IDXGIFactory4 * This,
		REFIID riid,
		void **ppParent);
		
	HRESULT ( STDMETHODCALLTYPE *EnumAdapters )( 
		IDXGIFactory4 * This,
		UINT Adapter,
		IDXGIAdapter **ppAdapter);
		
	HRESULT ( STDMETHODCALLTYPE *MakeWindowAssociation )( 
		IDXGIFactory4 * This,
		HWND WindowHandle,
		UINT Flags);
		
	HRESULT ( STDMETHODCALLTYPE *GetWindowAssociation )( 
		IDXGIFactory4 * This,
		HWND *pWindowHandle);
		
	HRESULT ( STDMETHODCALLTYPE *CreateSwapChain )( 
		IDXGIFactory4 * This,
		IUnknown *pDevice,
		DXGI_SWAP_CHAIN_DESC *pDesc,
		IDXGISwapChain **ppSwapChain);
		
	HRESULT ( STDMETHODCALLTYPE *CreateSoftwareAdapter )( 
		IDXGIFactory4 * This,
		HMODULE Module,
		IDXGIAdapter **ppAdapter);
		
	HRESULT ( STDMETHODCALLTYPE *EnumAdapters1 )( 
		IDXGIFactory4 * This,
		UINT Adapter,
		IDXGIAdapter1 **ppAdapter);
		
	BOOL ( STDMETHODCALLTYPE *IsCurrent )( 
		IDXGIFactory4 * This);
		
	BOOL ( STDMETHODCALLTYPE *IsWindowedStereoEnabled )( 
		IDXGIFactory4 * This);
		
	HRESULT ( STDMETHODCALLTYPE *CreateSwapChainForHwnd )( 
		IDXGIFactory4 * This,
		IUnknown *pDevice,
		HWND hWnd,
		void *pDesc,
		void *pFullscreenDesc,
		void *pRestrictToOutput,
		void **ppSwapChain);
		
	HRESULT ( STDMETHODCALLTYPE *CreateSwapChainForCoreWindow )( 
		IDXGIFactory4 * This,
		IUnknown *pDevice,
		IUnknown *pWindow,
		void *pDesc,
		void *pRestrictToOutput,
		void **ppSwapChain);
		
	HRESULT ( STDMETHODCALLTYPE *GetSharedResourceAdapterLuid )( 
		IDXGIFactory4 * This,
		HANDLE hResource,
		LUID *pLuid);
		
	HRESULT ( STDMETHODCALLTYPE *RegisterStereoStatusWindow )( 
		IDXGIFactory4 * This,
		HWND WindowHandle,
		UINT wMsg,
		DWORD *pdwCookie);
		
	HRESULT ( STDMETHODCALLTYPE *RegisterStereoStatusEvent )( 
		IDXGIFactory4 * This,
		HANDLE hEvent,
		DWORD *pdwCookie);
		
	void ( STDMETHODCALLTYPE *UnregisterStereoStatus )( 
		IDXGIFactory4 * This,
		DWORD dwCookie);
		
	HRESULT ( STDMETHODCALLTYPE *RegisterOcclusionStatusWindow )( 
		IDXGIFactory4 * This,
		HWND WindowHandle,
		UINT wMsg,
		DWORD *pdwCookie);
		
	HRESULT ( STDMETHODCALLTYPE *RegisterOcclusionStatusEvent )( 
		IDXGIFactory4 * This,
		HANDLE hEvent,
		DWORD *pdwCookie);
		
	void ( STDMETHODCALLTYPE *UnregisterOcclusionStatus )( 
		IDXGIFactory4 * This,
		DWORD dwCookie);
		
	HRESULT ( STDMETHODCALLTYPE *CreateSwapChainForComposition )( 
		IDXGIFactory4 * This,
		IUnknown *pDevice,
		void *pDesc,
		void *pRestrictToOutput,
		void **ppSwapChain);
		
	UINT ( STDMETHODCALLTYPE *GetCreationFlags )( 
		IDXGIFactory4 * This);
		
	HRESULT ( STDMETHODCALLTYPE *EnumAdapterByLuid )( 
		IDXGIFactory4 * This,
		LUID AdapterLuid,
		REFIID riid,
		void **ppvAdapter);
		
	HRESULT ( STDMETHODCALLTYPE *EnumWarpAdapter )( 
		IDXGIFactory4 * This,
		REFIID riid,
		void **ppvAdapter);
} IDXGIFactory4Vtbl;

struct IDXGIFactory4
{
	struct IDXGIFactory4Vtbl *lpVtbl;
};

#define IDXGIFactory4_Release(This)	\
	( (This)->lpVtbl -> Release(This) )

/* IDXGIFactory5 */
/* From dxgi1_5.h, cleaned up a bit... */

typedef enum
{
	DXGI_FEATURE_PRESENT_ALLOW_TEARING	= 0
} DXGI_FEATURE;

typedef struct IDXGIFactory5 IDXGIFactory5;
typedef struct IDXGIFactory5Vtbl
{
	HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
		IDXGIFactory5 * This,
		REFIID riid,
		void **ppvObject);
		
	ULONG ( STDMETHODCALLTYPE *AddRef )( 
		IDXGIFactory5 * This);
		
	ULONG ( STDMETHODCALLTYPE *Release )( 
		IDXGIFactory5 * This);
		
	HRESULT ( STDMETHODCALLTYPE *SetPrivateData )( 
		IDXGIFactory5 * This,
		REFGUID Name,
		UINT DataSize,
		const void *pData);
		
	HRESULT ( STDMETHODCALLTYPE *SetPrivateDataInterface )( 
		IDXGIFactory5 * This,
		REFGUID Name,
		const IUnknown *pUnknown);
		
	HRESULT ( STDMETHODCALLTYPE *GetPrivateData )( 
		IDXGIFactory5 * This,
		REFGUID Name,
		UINT *pDataSize,
		void *pData);
		
	HRESULT ( STDMETHODCALLTYPE *GetParent )( 
		IDXGIFactory5 * This,
		REFIID riid,
		void **ppParent);
		
	HRESULT ( STDMETHODCALLTYPE *EnumAdapters )( 
		IDXGIFactory5 * This,
		UINT Adapter,
		IDXGIAdapter **ppAdapter);
		
	HRESULT ( STDMETHODCALLTYPE *MakeWindowAssociation )( 
		IDXGIFactory5 * This,
		HWND WindowHandle,
		UINT Flags);
		
	HRESULT ( STDMETHODCALLTYPE *GetWindowAssociation )( 
		IDXGIFactory5 * This,
		HWND *pWindowHandle);
		
	HRESULT ( STDMETHODCALLTYPE *CreateSwapChain )( 
		IDXGIFactory5 * This,
		IUnknown *pDevice,
		DXGI_SWAP_CHAIN_DESC *pDesc,
		IDXGISwapChain **ppSwapChain);
		
	HRESULT ( STDMETHODCALLTYPE *CreateSoftwareAdapter )( 
		IDXGIFactory5 * This,
		HMODULE Module,
		IDXGIAdapter **ppAdapter);
		
	HRESULT ( STDMETHODCALLTYPE *EnumAdapters1 )( 
		IDXGIFactory5 * This,
		UINT Adapter,
		IDXGIAdapter1 **ppAdapter);
		
	BOOL ( STDMETHODCALLTYPE *IsCurrent )( 
		IDXGIFactory5 * This);
		
	BOOL ( STDMETHODCALLTYPE *IsWindowedStereoEnabled )( 
		IDXGIFactory5 * This);
		
	HRESULT ( STDMETHODCALLTYPE *CreateSwapChainForHwnd )( 
		IDXGIFactory5 * This,
		IUnknown *pDevice,
		HWND hWnd,
		void *pDesc,
		void *pFullscreenDesc,
		void *pRestrictToOutput,
		void **ppSwapChain);
		
	HRESULT ( STDMETHODCALLTYPE *CreateSwapChainForCoreWindow )( 
		IDXGIFactory5 * This,
		IUnknown *pDevice,
		IUnknown *pWindow,
		void *pDesc,
		void *pRestrictToOutput,
		void **ppSwapChain);
		
	HRESULT ( STDMETHODCALLTYPE *GetSharedResourceAdapterLuid )( 
		IDXGIFactory5 * This,
		HANDLE hResource,
		LUID *pLuid);
		
	HRESULT ( STDMETHODCALLTYPE *RegisterStereoStatusWindow )( 
		IDXGIFactory5 * This,
		HWND WindowHandle,
		UINT wMsg,
		DWORD *pdwCookie);
		
	HRESULT ( STDMETHODCALLTYPE *RegisterStereoStatusEvent )( 
		IDXGIFactory5 * This,
		HANDLE hEvent,
		DWORD *pdwCookie);
		
	void ( STDMETHODCALLTYPE *UnregisterStereoStatus )( 
		IDXGIFactory5 * This,
		DWORD dwCookie);
		
	HRESULT ( STDMETHODCALLTYPE *RegisterOcclusionStatusWindow )( 
		IDXGIFactory5 * This,
		HWND WindowHandle,
		UINT wMsg,
		DWORD *pdwCookie);
		
	HRESULT ( STDMETHODCALLTYPE *RegisterOcclusionStatusEvent )( 
		IDXGIFactory5 * This,
		HANDLE hEvent,
		DWORD *pdwCookie);
		
	void ( STDMETHODCALLTYPE *UnregisterOcclusionStatus )( 
		IDXGIFactory5 * This,
		DWORD dwCookie);
		
	HRESULT ( STDMETHODCALLTYPE *CreateSwapChainForComposition )( 
		IDXGIFactory5 * This,
		IUnknown *pDevice,
		void *pDesc,
		void *pRestrictToOutput,
		void **ppSwapChain);
		
	UINT ( STDMETHODCALLTYPE *GetCreationFlags )( 
		IDXGIFactory5 * This);
		
	HRESULT ( STDMETHODCALLTYPE *EnumAdapterByLuid )( 
		IDXGIFactory5 * This,
		LUID AdapterLuid,
		REFIID riid,
		void **ppvAdapter);
		
	HRESULT ( STDMETHODCALLTYPE *EnumWarpAdapter )( 
		IDXGIFactory5 * This,
		REFIID riid,
		void **ppvAdapter);
		
	HRESULT ( STDMETHODCALLTYPE *CheckFeatureSupport )( 
		IDXGIFactory5 * This,
		DXGI_FEATURE Feature,
		void *pFeatureSupportData,
		UINT FeatureSupportDataSize);
} IDXGIFactory5Vtbl;

struct IDXGIFactory5
{
	struct IDXGIFactory5Vtbl *lpVtbl;
};

#define IDXGIFactory5_Release(This)	\
	( (This)->lpVtbl -> Release(This) )

#define IDXGIFactory5_CheckFeatureSupport(This,Feature,pFeatureSupportData,FeatureSupportDataSize)	\
	( (This)->lpVtbl -> CheckFeatureSupport(This,Feature,pFeatureSupportData,FeatureSupportDataSize) )

/* IDXGIFactory6 */
/* From dxgi1_6.h, cleaned up a bit... */

typedef enum
{
	DXGI_GPU_PREFERENCE_UNSPECIFIED	= 0,
	DXGI_GPU_PREFERENCE_MINIMUM_POWER	= ( DXGI_GPU_PREFERENCE_UNSPECIFIED + 1 ) ,
	DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE	= ( DXGI_GPU_PREFERENCE_MINIMUM_POWER + 1 ) 
} DXGI_GPU_PREFERENCE;

typedef struct IDXGIFactory6 IDXGIFactory6;
typedef struct IDXGIFactory6Vtbl
{
	HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
		IDXGIFactory6 * This,
		REFIID riid,
		void **ppvObject);
		
	ULONG ( STDMETHODCALLTYPE *AddRef )( 
		IDXGIFactory6 * This);
		
	ULONG ( STDMETHODCALLTYPE *Release )( 
		IDXGIFactory6 * This);
		
	HRESULT ( STDMETHODCALLTYPE *SetPrivateData )( 
		IDXGIFactory6 * This,
		REFGUID Name,
		UINT DataSize,
		const void *pData);
		
	HRESULT ( STDMETHODCALLTYPE *SetPrivateDataInterface )( 
		IDXGIFactory6 * This,
		REFGUID Name,
		const IUnknown *pUnknown);
		
	HRESULT ( STDMETHODCALLTYPE *GetPrivateData )( 
		IDXGIFactory6 * This,
		REFGUID Name,
		UINT *pDataSize,
		void *pData);
		
	HRESULT ( STDMETHODCALLTYPE *GetParent )( 
		IDXGIFactory6 * This,
		REFIID riid,
		void **ppParent);
		
	HRESULT ( STDMETHODCALLTYPE *EnumAdapters )( 
		IDXGIFactory6 * This,
		UINT Adapter,
		IDXGIAdapter **ppAdapter);
		
	HRESULT ( STDMETHODCALLTYPE *MakeWindowAssociation )( 
		IDXGIFactory6 * This,
		HWND WindowHandle,
		UINT Flags);
		
	HRESULT ( STDMETHODCALLTYPE *GetWindowAssociation )( 
		IDXGIFactory6 * This,
		HWND *pWindowHandle);
		
	HRESULT ( STDMETHODCALLTYPE *CreateSwapChain )( 
		IDXGIFactory6 * This,
		IUnknown *pDevice,
		DXGI_SWAP_CHAIN_DESC *pDesc,
		IDXGISwapChain **ppSwapChain);
		
	HRESULT ( STDMETHODCALLTYPE *CreateSoftwareAdapter )( 
		IDXGIFactory6 * This,
		HMODULE Module,
		IDXGIAdapter **ppAdapter);
		
	HRESULT ( STDMETHODCALLTYPE *EnumAdapters1 )( 
		IDXGIFactory6 * This,
		UINT Adapter,
		IDXGIAdapter1 **ppAdapter);
		
	BOOL ( STDMETHODCALLTYPE *IsCurrent )( 
		IDXGIFactory6 * This);
		
	BOOL ( STDMETHODCALLTYPE *IsWindowedStereoEnabled )( 
		IDXGIFactory6 * This);
		
	HRESULT ( STDMETHODCALLTYPE *CreateSwapChainForHwnd )( 
		IDXGIFactory6 * This,
		IUnknown *pDevice,
		HWND hWnd,
		void *pDesc,
		void *pFullscreenDesc,
		void *pRestrictToOutput,
		void **ppSwapChain);
		
	HRESULT ( STDMETHODCALLTYPE *CreateSwapChainForCoreWindow )( 
		IDXGIFactory6 * This,
		IUnknown *pDevice,
		IUnknown *pWindow,
		void *pDesc,
		void *pRestrictToOutput,
		void **ppSwapChain);
		
	HRESULT ( STDMETHODCALLTYPE *GetSharedResourceAdapterLuid )( 
		IDXGIFactory6 * This,
		HANDLE hResource,
		LUID *pLuid);
		
	HRESULT ( STDMETHODCALLTYPE *RegisterStereoStatusWindow )( 
		IDXGIFactory6 * This,
		HWND WindowHandle,
		UINT wMsg,
		DWORD *pdwCookie);
		
	HRESULT ( STDMETHODCALLTYPE *RegisterStereoStatusEvent )( 
		IDXGIFactory6 * This,
		HANDLE hEvent,
		DWORD *pdwCookie);
		
	void ( STDMETHODCALLTYPE *UnregisterStereoStatus )( 
		IDXGIFactory6 * This,
		DWORD dwCookie);
		
	HRESULT ( STDMETHODCALLTYPE *RegisterOcclusionStatusWindow )( 
		IDXGIFactory6 * This,
		HWND WindowHandle,
		UINT wMsg,
		DWORD *pdwCookie);
		
	HRESULT ( STDMETHODCALLTYPE *RegisterOcclusionStatusEvent )( 
		IDXGIFactory6 * This,
		HANDLE hEvent,
		DWORD *pdwCookie);
		
	void ( STDMETHODCALLTYPE *UnregisterOcclusionStatus )( 
		IDXGIFactory6 * This,
		DWORD dwCookie);
		
	HRESULT ( STDMETHODCALLTYPE *CreateSwapChainForComposition )( 
		IDXGIFactory6 * This,
		IUnknown *pDevice,
		void *pDesc,
		void *pRestrictToOutput,
		void **ppSwapChain);
		
	UINT ( STDMETHODCALLTYPE *GetCreationFlags )( 
		IDXGIFactory6 * This);
		
	HRESULT ( STDMETHODCALLTYPE *EnumAdapterByLuid )( 
		IDXGIFactory6 * This,
		LUID AdapterLuid,
		REFIID riid,
		void **ppvAdapter);
		
	HRESULT ( STDMETHODCALLTYPE *EnumWarpAdapter )( 
		IDXGIFactory6 * This,
		REFIID riid,
		void **ppvAdapter);
		
	HRESULT ( STDMETHODCALLTYPE *CheckFeatureSupport )( 
		IDXGIFactory6 * This,
		DXGI_FEATURE Feature,
		void *pFeatureSupportData,
		UINT FeatureSupportDataSize);
		
	HRESULT ( STDMETHODCALLTYPE *EnumAdapterByGpuPreference )( 
		IDXGIFactory6 * This,
		UINT Adapter,
		DXGI_GPU_PREFERENCE GpuPreference,
		REFIID riid,
		void **ppvAdapter);
} IDXGIFactory6Vtbl;

struct IDXGIFactory6
{
	struct IDXGIFactory6Vtbl *lpVtbl;
};

#define IDXGIFactory6_Release(This)	\
	( (This)->lpVtbl -> Release(This) )

#define IDXGIFactory6_EnumAdapterByGpuPreference(This,Adapter,GpuPreference,riid,ppvAdapter)	\
	( (This)->lpVtbl -> EnumAdapterByGpuPreference(This,Adapter,GpuPreference,riid,ppvAdapter) ) 

#endif /* FNA3D_DRIVER_D3D11_H */
