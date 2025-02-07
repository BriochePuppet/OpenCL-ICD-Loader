/*
 * Copyright (c) 2016-2020 The Khronos Group Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * OpenCL is a trademark of Apple Inc. used under license by Khronos.
 */

#include <initguid.h>

#include "icd.h"
#include "icd_dispatch.h"
#include <CL/cl_layer.h>
#include "icd_windows.h"
#include "icd_windows_hkr.h"
#include "icd_windows_dxgk.h"
#include "icd_windows_apppackage.h"
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <winreg.h>

#include <dxgi.h>
typedef HRESULT (WINAPI *PFN_CREATE_DXGI_FACTORY)(REFIID, void **);

static INIT_ONCE initialized = INIT_ONCE_STATIC_INIT;

typedef struct WinAdapter
{
    WCHAR * szName;
    LUID luid;
} WinAdapter;

const LUID ZeroLuid = { 0, 0 };

static WinAdapter* pWinAdapterBegin = NULL;
static WinAdapter* pWinAdapterEnd = NULL;
static WinAdapter* pWinAdapterCapacity = NULL;

BOOL adapterAdd(const WCHAR* szName, LUID luid)
{
    BOOL result = TRUE;
    if (pWinAdapterEnd == pWinAdapterCapacity)
    {
        size_t oldCapacity = pWinAdapterCapacity - pWinAdapterBegin;
        size_t newCapacity = oldCapacity;
        if (0 == newCapacity)
        {
            newCapacity = 1;
        }
        else if(newCapacity < UINT_MAX/2)
        {
            newCapacity *= 2;
        }

        WinAdapter* pNewBegin = malloc(newCapacity * sizeof(*pWinAdapterBegin));
        if (!pNewBegin)
            result = FALSE;
        else
        {
            if (pWinAdapterBegin)
            {
                memcpy(pNewBegin, pWinAdapterBegin, oldCapacity * sizeof(*pWinAdapterBegin));
                free(pWinAdapterBegin);
            }
            pWinAdapterCapacity = pNewBegin + newCapacity;
            pWinAdapterEnd = pNewBegin + oldCapacity;
            pWinAdapterBegin = pNewBegin;
        }
    }
    if (pWinAdapterEnd != pWinAdapterCapacity)
    {
        size_t nameLen = (wcslen(szName) + 1)*sizeof(szName[0]);
        pWinAdapterEnd->szName = malloc(nameLen);
        if (!pWinAdapterEnd->szName)
            result = FALSE;
        else
        {
            memcpy(pWinAdapterEnd->szName, szName, nameLen);
            pWinAdapterEnd->luid = luid;
            ++pWinAdapterEnd;
        }
    }
    return result;
}

void adapterFree(WinAdapter *pWinAdapter)
{
    free(pWinAdapter->szName);
    pWinAdapter->szName = NULL;
}

#if defined(CL_ENABLE_LAYERS)
typedef struct WinLayer
{
    WCHAR * szName;
    DWORD priority;
} WinLayer;

static WinLayer* pWinLayerBegin;
static WinLayer* pWinLayerEnd;
static WinLayer* pWinLayerCapacity;

static int __cdecl compareLayer(const void *a, const void *b)
{
    return ((WinLayer *)a)->priority < ((WinLayer *)b)->priority ? -1 :
           ((WinLayer *)a)->priority > ((WinLayer *)b)->priority ? 1 : 0;
}

static BOOL layerAdd(const WCHAR* szName, DWORD priority)
{
    BOOL result = TRUE;
    if (pWinLayerEnd == pWinLayerCapacity)
    {
        size_t oldCapacity = pWinLayerCapacity - pWinLayerBegin;
        size_t newCapacity = oldCapacity;
        if (0 == newCapacity)
        {
            newCapacity = 1;
        }
        else if(newCapacity < UINT_MAX/2)
        {
            newCapacity *= 2;
        }

        WinLayer* pNewBegin = malloc(newCapacity * sizeof(*pWinLayerBegin));
        if (!pNewBegin)
        {
            KHR_ICD_TRACE("Failed allocate space for Layers array\n");
            result = FALSE;
        }
        else
        {
            if (pWinLayerBegin)
            {
                memcpy(pNewBegin, pWinLayerBegin, oldCapacity * sizeof(*pWinLayerBegin));
                free(pWinLayerBegin);
            }
            pWinLayerCapacity = pNewBegin + newCapacity;
            pWinLayerEnd = pNewBegin + oldCapacity;
            pWinLayerBegin = pNewBegin;
        }
    }
    if (pWinLayerEnd != pWinLayerCapacity)
    {
        size_t nameLen = (wcslen(szName) + 1)*sizeof(szName[0]);
        pWinLayerEnd->szName = malloc(nameLen);
        if (!pWinLayerEnd->szName)
        {
            KHR_ICD_TRACE("Failed allocate space for Layer file path\n");
            result = FALSE;
        }
        else
        {
            memcpy(pWinLayerEnd->szName, szName, nameLen);
            pWinLayerEnd->priority = priority;
            ++pWinLayerEnd;
        }
    }
    return result;
}

void layerFree(WinLayer *pWinLayer)
{
    free(pWinLayer->szName);
    pWinLayer->szName = NULL;
}
#endif // defined(CL_ENABLE_LAYERS)

// add a vendor's implementation to the list of libraries
void khrIcdVendorAdd(const WCHAR *libraryName);

// add a layer to the layer chain
void khrIcdLayerAdd(const WCHAR *libraryName);

/*
 *
 * Vendor enumeration functions
 *
 */

// go through the list of vendors in the registry and call khrIcdVendorAdd
// for each vendor encountered
BOOL CALLBACK khrIcdOsVendorsEnumerate(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *lpContext)
{
    (void)InitOnce;
    (void)Parameter;
    (void)lpContext;

    LONG result;
    BOOL status = FALSE, currentStatus = FALSE;
    const WCHAR* platformsName = L"SOFTWARE\\Khronos\\OpenCL\\Vendors";
    HKEY platformsKey = NULL;
    DWORD dwIndex;

    khrIcdInitializeTrace();
    khrIcdVendorsEnumerateEnv();

    currentStatus = khrIcdOsVendorsEnumerateDXGK();
    status |= currentStatus;
    if (!currentStatus)
    {
        KHR_ICD_TRACE("Failed to load via DXGK interface on RS4, continuing\n");
    }

    currentStatus = khrIcdOsVendorsEnumerateHKR();
    status |= currentStatus;
    if (!currentStatus)
    {
        KHR_ICD_TRACE("Failed to enumerate HKR entries, continuing\n");
    }

    currentStatus = khrIcdOsVendorsEnumerateAppPackage();
    status |= currentStatus;
    if (!currentStatus)
    {
        KHR_ICD_TRACE("Failed to enumerate App package entry, continuing\n");
    }

    KHR_ICD_WIDE_TRACE(L"Opening key HKLM\\%ls...\n", platformsName);
    result = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        platformsName,
        0,
        KEY_READ,
        &platformsKey);
    if (ERROR_SUCCESS != result)
    {
        KHR_ICD_WIDE_TRACE(L"Failed to open platforms key %ls, continuing\n", platformsName);
    }
    else
    {
        // for each value
        for (dwIndex = 0;; ++dwIndex)
        {
            WCHAR cszLibraryName[1024] = {0};
            DWORD dwLibraryNameSize = sizeof(cszLibraryName);
            DWORD dwLibraryNameType = 0;
            DWORD dwValue = 0;
            DWORD dwValueSize = sizeof(dwValue);

            // read the value name
            KHR_ICD_TRACE("Reading value %"PRIuDW"...\n", dwIndex);
            result = RegEnumValueW(
                  platformsKey,
                  dwIndex,
                  cszLibraryName,
                  &dwLibraryNameSize,
                  NULL,
                  &dwLibraryNameType,
                  (LPBYTE)&dwValue,
                  &dwValueSize);
            // if RegEnumKeyEx fails, we are done with the enumeration
            if (ERROR_SUCCESS != result)
            {
                KHR_ICD_TRACE("Failed to read value %"PRIuDW", done reading key.\n", dwIndex);
                break;
            }
            KHR_ICD_WIDE_TRACE(L"Value %ls found...\n", cszLibraryName);

            // Require that the value be a DWORD and equal zero
            if (REG_DWORD != dwLibraryNameType)
            {
                KHR_ICD_TRACE("Value not a DWORD, skipping\n");
                continue;
            }
            if (dwValue)
            {
                KHR_ICD_TRACE("Value not zero, skipping\n");
                continue;
            }
            // add the library
            status |= adapterAdd(cszLibraryName, ZeroLuid);
        }
    }

    // Add adapters according to DXGI's preference order
    HMODULE hDXGI = LoadLibraryW(L"dxgi.dll");
    if (hDXGI)
    {
        IDXGIFactory* pFactory = NULL;
        PFN_CREATE_DXGI_FACTORY pCreateDXGIFactory = (PFN_CREATE_DXGI_FACTORY)GetProcAddress(hDXGI, "CreateDXGIFactory");
        if (pCreateDXGIFactory)
        {
            HRESULT hr = pCreateDXGIFactory(&IID_IDXGIFactory, (void **)&pFactory);
            if (SUCCEEDED(hr))
            {
                UINT i = 0;
                IDXGIAdapter* pAdapter = NULL;
                while (SUCCEEDED(pFactory->lpVtbl->EnumAdapters(pFactory, i++, &pAdapter)))
                {
                    DXGI_ADAPTER_DESC AdapterDesc;
                    if (SUCCEEDED(pAdapter->lpVtbl->GetDesc(pAdapter, &AdapterDesc)))
                    {
                        for (WinAdapter* iterAdapter = pWinAdapterBegin; iterAdapter != pWinAdapterEnd; ++iterAdapter)
                        {
                            if (iterAdapter->luid.LowPart == AdapterDesc.AdapterLuid.LowPart
                                && iterAdapter->luid.HighPart == AdapterDesc.AdapterLuid.HighPart)
                            {
                                khrIcdVendorAdd(iterAdapter->szName);
                                break;
                            }
                        }
                    }

                    pAdapter->lpVtbl->Release(pAdapter);
                }
                pFactory->lpVtbl->Release(pFactory);
            }
        }
        FreeLibrary(hDXGI);
    }

    // Go through the list again, putting any remaining adapters at the end of the list in an undefined order
    for (WinAdapter* iterAdapter = pWinAdapterBegin; iterAdapter != pWinAdapterEnd; ++iterAdapter)
    {
        khrIcdVendorAdd(iterAdapter->szName);
        adapterFree(iterAdapter);
    }

    free(pWinAdapterBegin);
    pWinAdapterBegin = NULL;
    pWinAdapterEnd = NULL;
    pWinAdapterCapacity = NULL;

    result = RegCloseKey(platformsKey);
    if (ERROR_SUCCESS != result)
    {
        KHR_ICD_WIDE_TRACE(L"Failed to close platforms key %ls, ignoring\n", platformsName);
    }

#if defined(CL_ENABLE_LAYERS)
    const WCHAR* layersName = L"SOFTWARE\\Khronos\\OpenCL\\Layers";
    HKEY layersKey = NULL;

    KHR_ICD_WIDE_TRACE(L"Opening key HKLM\\%ls...\n", layersName);
    result = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        layersName,
        0,
        KEY_READ,
        &layersKey);
    if (ERROR_SUCCESS != result)
    {
        KHR_ICD_WIDE_TRACE(L"Failed to open layers key %ls, continuing\n", layersName);
    }
    else
    {
        // for each value
        for (dwIndex = 0;; ++dwIndex)
        {
            WCHAR cszLibraryName[1024] = {0};
            DWORD dwLibraryNameSize = sizeof(cszLibraryName);
            DWORD dwLibraryNameType = 0;
            DWORD dwValue = 0;
            DWORD dwValueSize = sizeof(dwValue);

            // read the value name
            KHR_ICD_TRACE("Reading value %"PRIuDW"...\n", dwIndex);
            result = RegEnumValueW(
                  layersKey,
                  dwIndex,
                  cszLibraryName,
                  &dwLibraryNameSize,
                  NULL,
                  &dwLibraryNameType,
                  (LPBYTE)&dwValue,
                  &dwValueSize);
            // if RegEnumKeyEx fails, we are done with the enumeration
            if (ERROR_SUCCESS != result)
            {
                KHR_ICD_TRACE("Failed to read value %"PRIuDW", done reading key.\n", dwIndex);
                break;
            }
            KHR_ICD_WIDE_TRACE(L"Value %ls found...\n", cszLibraryName);

            // Require that the value be a DWORD
            if (REG_DWORD != dwLibraryNameType)
            {
                KHR_ICD_TRACE("Value not a DWORD, skipping\n");
                continue;
            }
            // add the library
            status |= layerAdd(cszLibraryName, dwValue);
        }
        qsort(pWinLayerBegin, pWinLayerEnd - pWinLayerBegin, sizeof(WinLayer), compareLayer);
        for (WinLayer* iterLayer = pWinLayerBegin; iterLayer != pWinLayerEnd; ++iterLayer)
        {
            khrIcdLayerAdd(iterLayer->szName);
            layerFree(iterLayer);
        }
    }

    free(pWinLayerBegin);
    pWinLayerBegin = NULL;
    pWinLayerEnd = NULL;
    pWinLayerCapacity = NULL;

    result = RegCloseKey(layersKey);

    khrIcdLayersEnumerateEnv();
#endif // defined(CL_ENABLE_LAYERS)
    return status;
}

// go through the list of vendors only once
void khrIcdOsVendorsEnumerateOnce()
{
    InitOnceExecuteOnce(&initialized, khrIcdOsVendorsEnumerate, NULL, NULL);
}

/*
 *
 * Dynamic library loading functions
 *
 */

// dynamically load a library.  returns NULL on failure
void *khrIcdOsLibraryLoad(const WCHAR *libraryName)
{
    HMODULE hTemp = LoadLibraryExW(libraryName, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!hTemp && GetLastError() == ERROR_INVALID_PARAMETER)
    {
        hTemp = LoadLibraryExW(libraryName, NULL, 0);
    }
    if (!hTemp)
    {
        KHR_ICD_TRACE("Failed to load driver. Windows error code is %"PRIuDW".\n", GetLastError());
    }
    return (void*)hTemp;
}

// get a function pointer from a loaded library.  returns NULL on failure.
void *khrIcdOsLibraryGetFunctionAddress(void *library, const char *functionName)
{
    if (!library || !functionName)
    {
        return NULL;
    }
    return GetProcAddress( (HMODULE)library, functionName);
}

// unload a library.
void khrIcdOsLibraryUnload(void *library)
{
    FreeLibrary( (HMODULE)library);
}

void khrIcdVendorAdd(const WCHAR *libraryName)
{
    void *library = NULL;
    cl_int result = CL_SUCCESS;
    pfn_clGetExtensionFunctionAddress p_clGetExtensionFunctionAddress = NULL;
    pfn_clIcdGetPlatformIDs p_clIcdGetPlatformIDs = NULL;
    cl_uint i = 0;
    cl_uint platformCount = 0;
    cl_platform_id *platforms = NULL;
    KHRicdVendor *vendorIterator = NULL;

    // require that the library name be valid
    if (!libraryName) 
    {
        goto Done;
    }
    KHR_ICD_WIDE_TRACE(L"attempting to add vendor %ls...\n", libraryName);

    // load its library and query its function pointers
    library = khrIcdOsLibraryLoad(libraryName);
    if (!library)
    {
        KHR_ICD_WIDE_TRACE(L"failed to load library %ls\n", libraryName);
        goto Done;
    }

    // ensure that we haven't already loaded this vendor
    for (vendorIterator = khrIcdVendors; vendorIterator; vendorIterator = vendorIterator->next)
    {
        if (vendorIterator->library == library)
        {
            KHR_ICD_WIDE_TRACE(L"already loaded vendor %ls, nothing to do here\n", libraryName);
            goto Done;
        }
    }

    // get the library's clGetExtensionFunctionAddress pointer
    p_clGetExtensionFunctionAddress = (pfn_clGetExtensionFunctionAddress)(size_t)khrIcdOsLibraryGetFunctionAddress(library, "clGetExtensionFunctionAddress");
    if (!p_clGetExtensionFunctionAddress)
    {
        KHR_ICD_TRACE("failed to get function address clGetExtensionFunctionAddress\n");
        goto Done;
    }

    // use that function to get the clIcdGetPlatformIDsKHR function pointer
    p_clIcdGetPlatformIDs = (pfn_clIcdGetPlatformIDs)(size_t)p_clGetExtensionFunctionAddress("clIcdGetPlatformIDsKHR");
    if (!p_clIcdGetPlatformIDs)
    {
        KHR_ICD_TRACE("failed to get extension function address clIcdGetPlatformIDsKHR\n");
        goto Done;
    }

    // query the number of platforms available and allocate space to store them
    result = p_clIcdGetPlatformIDs(0, NULL, &platformCount);
    if (CL_SUCCESS != result)
    {
        KHR_ICD_TRACE("failed clIcdGetPlatformIDs\n");
        goto Done;
    }
    platforms = (cl_platform_id *)malloc(platformCount * sizeof(cl_platform_id) );
    if (!platforms)
    {
        KHR_ICD_TRACE("failed to allocate memory\n");
        goto Done;
    }
    memset(platforms, 0, platformCount * sizeof(cl_platform_id) );
    result = p_clIcdGetPlatformIDs(platformCount, platforms, NULL);
    if (CL_SUCCESS != result)
    {
        KHR_ICD_TRACE("failed clIcdGetPlatformIDs\n");
        goto Done;
    }

    // for each platform, add it
    for (i = 0; i < platformCount; ++i)
    {
        KHRicdVendor* vendor = NULL;
        char *suffix;
        size_t suffixSize;

        // call clGetPlatformInfo on the returned platform to get the suffix
        if (!platforms[i])
        {
            continue;
        }
        result = platforms[i]->dispatch->clGetPlatformInfo(
            platforms[i],
            CL_PLATFORM_ICD_SUFFIX_KHR,
            0,
            NULL,
            &suffixSize);
        if (CL_SUCCESS != result)
        {
            continue;
        }
        suffix = (char *)malloc(suffixSize);
        if (!suffix)
        {
            continue;
        }
        result = platforms[i]->dispatch->clGetPlatformInfo(
            platforms[i],
            CL_PLATFORM_ICD_SUFFIX_KHR,
            suffixSize,
            suffix,
            NULL);            
        if (CL_SUCCESS != result)
        {
            free(suffix);
            continue;
        }

        // allocate a structure for the vendor
        vendor = (KHRicdVendor*)malloc(sizeof(*vendor) );
        if (!vendor) 
        {
            free(suffix);
            KHR_ICD_TRACE("failed to allocate memory\n");
            continue;
        }
        memset(vendor, 0, sizeof(*vendor) );

        // populate vendor data
        vendor->library = khrIcdOsLibraryLoad(libraryName);
        if (!vendor->library) 
        {
            free(suffix);
            free(vendor);
            KHR_ICD_TRACE("failed get platform handle to library\n");
            continue;
        }
        vendor->clGetExtensionFunctionAddress = p_clGetExtensionFunctionAddress;
        vendor->platform = platforms[i];
        vendor->suffix = suffix;

        // add this vendor to the list of vendors at the tail
        {
            KHRicdVendor **prevNextPointer = NULL;
            for (prevNextPointer = &khrIcdVendors; *prevNextPointer; prevNextPointer = &( (*prevNextPointer)->next) );
            *prevNextPointer = vendor;
        }

        KHR_ICD_WIDE_TRACE(L"successfully added vendor %ls ", libraryName);
        KHR_ICD_TRACE("with suffix %s\n", suffix);

    }

Done:

    if (library)
    {
        khrIcdOsLibraryUnload(library);
    }
    if (platforms)
    {
        free(platforms);
    }
}

#if defined(CL_ENABLE_LAYERS)
void khrIcdLayerAdd(const WCHAR *libraryName)
{
    void *library = NULL;
    cl_int result = CL_SUCCESS;
    pfn_clGetLayerInfo p_clGetLayerInfo = NULL;
    pfn_clInitLayer p_clInitLayer = NULL;
    struct KHRLayer *layerIterator = NULL;
    struct KHRLayer *layer = NULL;
    cl_layer_api_version api_version = 0;
    const struct _cl_icd_dispatch *targetDispatch = NULL;
    const struct _cl_icd_dispatch *layerDispatch = NULL;
    cl_uint layerDispatchNumEntries = 0;
    cl_uint loaderDispatchNumEntries = 0;

    // require that the library name be valid
    if (!libraryName)
    {
        goto Done;
    }
    KHR_ICD_WIDE_TRACE(L"attempting to add layer %ls...\n", libraryName);

    // load its library and query its function pointers
    library = khrIcdOsLibraryLoad(libraryName);
    if (!library)
    {
        KHR_ICD_WIDE_TRACE(L"failed to load library %ls\n", libraryName);
        goto Done;
    }

    // ensure that we haven't already loaded this layer
    for (layerIterator = khrFirstLayer; layerIterator; layerIterator = layerIterator->next)
    {
        if (layerIterator->library == library)
        {
            KHR_ICD_WIDE_TRACE(L"already loaded layer %ls, nothing to do here\n", libraryName);
            goto Done;
        }
    }

    // get the library's clGetLayerInfo pointer
    p_clGetLayerInfo = (pfn_clGetLayerInfo)(size_t)khrIcdOsLibraryGetFunctionAddress(library, "clGetLayerInfo");
    if (!p_clGetLayerInfo)
    {
        KHR_ICD_TRACE("failed to get function address clGetLayerInfo\n");
        goto Done;
    }

    // use that function to get the clInitLayer function pointer
    p_clInitLayer = (pfn_clInitLayer)(size_t)khrIcdOsLibraryGetFunctionAddress(library, "clInitLayer");
    if (!p_clInitLayer)
    {
        KHR_ICD_TRACE("failed to get function address clInitLayer\n");
        goto Done;
    }

    result = p_clGetLayerInfo(CL_LAYER_API_VERSION, sizeof(api_version), &api_version, NULL);
    if (CL_SUCCESS != result)
    {
        KHR_ICD_TRACE("failed to query layer version\n");
        goto Done;
    }

    if (CL_LAYER_API_VERSION_100 != api_version)
    {
        KHR_ICD_TRACE("unsupported api version\n");
        goto Done;
    }

    layer = (struct KHRLayer*)calloc(sizeof(struct KHRLayer), 1);
    if (!layer)
    {
        KHR_ICD_TRACE("failed to allocate memory\n");
        goto Done;
    }
#ifdef CL_LAYER_INFO
    {
        // Not using strdup as it is not standard c
        size_t sz_name = (wcslen(libraryName) + 1)*sizeof(libraryName[0]);
        layer->libraryName = malloc(sz_name);
        if (!layer->libraryName)
        {
            KHR_ICD_TRACE("failed to allocate memory\n");
            goto Done;
        }
        memcpy(layer->libraryName, libraryName, sz_name);
        layer->p_clGetLayerInfo = (void *)(size_t)p_clGetLayerInfo;
    }
#endif

    if (khrFirstLayer) {
        targetDispatch = &(khrFirstLayer->dispatch);
    } else {
        targetDispatch = &khrMasterDispatch;
    }

    loaderDispatchNumEntries = sizeof(khrMasterDispatch)/sizeof(void*);
    result = p_clInitLayer(
        loaderDispatchNumEntries,
        targetDispatch,
        &layerDispatchNumEntries,
        &layerDispatch);
    if (CL_SUCCESS != result)
    {
        KHR_ICD_TRACE("failed to initialize layer\n");
        goto Done;
    }

    layer->next = khrFirstLayer;
    khrFirstLayer = layer;
    layer->library = library;

    cl_uint limit = layerDispatchNumEntries < loaderDispatchNumEntries ? layerDispatchNumEntries : loaderDispatchNumEntries;

    for (cl_uint i = 0; i < limit; i++) {
        ((void **)&(layer->dispatch))[i] =
            ((void *const*)layerDispatch)[i] ?
                ((void *const*)layerDispatch)[i] : ((void *const*)targetDispatch)[i];
    }
    for (cl_uint i = limit; i < loaderDispatchNumEntries; i++) {
        ((void **)&(layer->dispatch))[i] = ((void *const*)targetDispatch)[i];
    }

    KHR_ICD_WIDE_TRACE(L"successfully added layer %ls\n", libraryName);
    return;
Done:
    if (library)
    {
        khrIcdOsLibraryUnload(library);
    }
    if (layer)
    {
        free(layer);
    }
}
#endif // defined(CL_ENABLE_LAYERS)
