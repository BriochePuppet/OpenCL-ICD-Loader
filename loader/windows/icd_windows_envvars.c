/*
 * Copyright (c) 2016-2019 The Khronos Group Inc.
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

#include <icd.h>
#include <stdbool.h>
#include <windows.h>

static WCHAR *khrIcd_getenv(const WCHAR *name) {
    WCHAR *retVal;
    DWORD valSize;

    valSize = GetEnvironmentVariableW(name, NULL, 0);

    // valSize DOES include the null terminator, so for any set variable
    // will always be at least 1. If it's 0, the variable wasn't set.
    if (valSize == 0) return NULL;

    // Allocate the space necessary for the registry entry
    retVal = (WCHAR *)malloc(sizeof(WCHAR)*valSize);

    if (NULL != retVal) {
        GetEnvironmentVariableW(name, retVal, valSize);
    }

    return retVal;
}

static bool khrIcd_IsHighIntegrityLevel()
{
    bool isHighIntegrityLevel = false;

    HANDLE processToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_QUERY_SOURCE, &processToken)) {
        // Maximum possible size of SID_AND_ATTRIBUTES is maximum size of a SID + size of attributes DWORD.
        char mandatoryLabelBuffer[SECURITY_MAX_SID_SIZE + sizeof(DWORD)] = {0};
        DWORD bufferSize;
        if (GetTokenInformation(processToken, TokenIntegrityLevel, mandatoryLabelBuffer, sizeof(mandatoryLabelBuffer),
                                &bufferSize) != 0) {
            const TOKEN_MANDATORY_LABEL* mandatoryLabel = (const TOKEN_MANDATORY_LABEL*)(mandatoryLabelBuffer);
            const DWORD subAuthorityCount = *GetSidSubAuthorityCount(mandatoryLabel->Label.Sid);
            const DWORD integrityLevel = *GetSidSubAuthority(mandatoryLabel->Label.Sid, subAuthorityCount - 1);

            isHighIntegrityLevel = integrityLevel > SECURITY_MANDATORY_MEDIUM_RID;
        }

        CloseHandle(processToken);
    }

    return isHighIntegrityLevel;
}

static WCHAR *khrIcd_secure_getenv(const WCHAR *name) {
    if (khrIcd_IsHighIntegrityLevel()) {
        KHR_ICD_TRACE("Running at a high integrity level, so secure_getenv is returning NULL\n");
        return NULL;
    }

    return khrIcd_getenv(name);
}

static void khrIcd_free_getenv(WCHAR *val) {
    free((void *)val);
}

// entrypoint to check and initialize trace.
void khrIcdInitializeTrace(void)
{
    WCHAR *enableTrace = khrIcd_getenv(L"OCL_ICD_ENABLE_TRACE");
    if (enableTrace && (wcscmp(enableTrace, L"True") == 0 ||
            wcscmp(enableTrace, L"true") == 0 ||
            wcscmp(enableTrace, L"T") == 0 ||
            wcscmp(enableTrace, L"1") == 0))
    {
        khrEnableTrace = 1;
    }
}

// Get next file or dirname given a string list or registry key path.
// Note: the input string may be modified!
static WCHAR *loader_get_next_path(WCHAR *path) {
    size_t len;
    WCHAR *next;

    if (path == NULL) return NULL;
    next = wcschr(path, PATH_SEPARATOR);
    if (next == NULL) {
        len = wcslen(path);
        next = path + len;
    } else {
        *next = L'\0';
        next++;
    }

    return next;
}

// add a vendor's implementation to the list of libraries
void khrIcdVendorAdd(const WCHAR *libraryName);

void khrIcdVendorsEnumerateEnv(void)
{
    WCHAR* icdFilenames = khrIcd_secure_getenv(L"OCL_ICD_FILENAMES");
    WCHAR* cur_file = NULL;
    WCHAR* next_file = NULL;
    if (icdFilenames)
    {
        KHR_ICD_TRACE("Found OCL_ICD_FILENAMES environment variable.\n");

        next_file = icdFilenames;
        while (NULL != next_file && *next_file != '\0') {
            cur_file = next_file;
            next_file = loader_get_next_path(cur_file);

            khrIcdVendorAdd(cur_file);
        }

        khrIcd_free_getenv(icdFilenames);
    }
}

// add a layer to the layer chain
void khrIcdLayerAdd(const WCHAR *libraryName);

#if defined(CL_ENABLE_LAYERS)
void khrIcdLayersEnumerateEnv(void)
{
    WCHAR* layerFilenames = khrIcd_secure_getenv(L"OPENCL_LAYERS");
    WCHAR* cur_file = NULL;
    WCHAR* next_file = NULL;
    if (layerFilenames)
    {
        KHR_ICD_TRACE("Found OPENCL_LAYERS environment variable.\n");

        next_file = layerFilenames;
        while (NULL != next_file && *next_file != '\0') {
            cur_file = next_file;
            next_file = loader_get_next_path(cur_file);

            khrIcdLayerAdd(cur_file);
        }

        khrIcd_free_getenv(layerFilenames);
    }
}
#endif // defined(CL_ENABLE_LAYERS)
