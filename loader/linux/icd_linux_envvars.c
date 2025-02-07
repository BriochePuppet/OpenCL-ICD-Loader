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

// for secure_getenv():
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "icd.h"
#include "icd_cmake_config.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>

static char *khrIcd_getenv(const char *name) {
    // No allocation of memory necessary for Linux.
    return getenv(name);
}

static char *khrIcd_secure_getenv(const char *name) {
#if defined(__APPLE__)
    // Apple does not appear to have a secure getenv implementation.
    // The main difference between secure getenv and getenv is that secure getenv
    // returns NULL if the process is being run with elevated privileges by a normal user.
    // The idea is to prevent the reading of malicious environment variables by a process
    // that can do damage.
    // This algorithm is derived from glibc code that sets an internal
    // variable (__libc_enable_secure) if the process is running under setuid or setgid.
    return geteuid() != getuid() || getegid() != getgid() ? NULL : khrIcd_getenv(name);
#else
// Linux
#ifdef HAVE_SECURE_GETENV
    return secure_getenv(name);
#elif defined(HAVE___SECURE_GETENV)
    return __secure_getenv(name);
#else
#pragma message(                                                                       \
    "Warning:  Falling back to non-secure getenv for environmental lookups!  Consider" \
    " updating to a different libc.")
    return khrIcd_getenv(name);
#endif
#endif
}

static void khrIcd_free_getenv(char *val) {
    // No freeing of memory necessary for Linux, but we should at least touch
    // val to get rid of compiler warnings.
    (void)val;
}

// entrypoint to check and initialize trace.
void khrIcdInitializeTrace(void)
{
    char *enableTrace = khrIcd_getenv("OCL_ICD_ENABLE_TRACE");
    if (enableTrace && (strcmp(enableTrace, "True") == 0 ||
            strcmp(enableTrace, "true") == 0 ||
            strcmp(enableTrace, "T") == 0 ||
            strcmp(enableTrace, "1") == 0))
    {
        khrEnableTrace = 1;
    }
}

// Get next file or dirname given a string list or registry key path.
// Note: the input string may be modified!
static char *loader_get_next_path(char *path) {
    size_t len;
    char *next;

    if (path == NULL) return NULL;
    next = strchr(path, PATH_SEPARATOR);
    if (next == NULL) {
        len = strlen(path);
        next = path + len;
    } else {
        *next = '\0';
        next++;
    }

    return next;
}

// add a vendor's implementation to the list of libraries
void khrIcdVendorAdd(const char *libraryName);

void khrIcdVendorsEnumerateEnv(void)
{
    char* icdFilenames = khrIcd_secure_getenv("OCL_ICD_FILENAMES");
    char* cur_file = NULL;
    char* next_file = NULL;
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
void khrIcdLayerAdd(const char *libraryName);

#if defined(CL_ENABLE_LAYERS)
void khrIcdLayersEnumerateEnv(void)
{
    char* layerFilenames = khrIcd_secure_getenv("OPENCL_LAYERS");
    char* cur_file = NULL;
    char* next_file = NULL;
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

/*
 *
 * Vendor enumeration functions
 *
 */

typedef void khrIcdFileAdd(const char *);

static inline void khrIcdOsDirEntryValidateAndAdd(const char *d_name, const char *path,
                                                  const char *extension, khrIcdFileAdd addFunc)
{
    struct stat statBuff;
    char* fileName = NULL;

    // make sure the file name ends in `extension` (eg. .icd, or .lay)
    if (strlen(extension) > strlen(d_name))
    {
        return;
    }
    if (strcmp(d_name + strlen(d_name) - strlen(extension), extension))
    {
        return;
    }

    // allocate space for the full path of the vendor library name
    fileName = malloc(strlen(d_name) + strlen(path) + 2);
    if (!fileName)
    {
        KHR_ICD_TRACE("Failed allocate space for ICD file path\n");
        return;
    }
    sprintf(fileName, "%s/%s", path, d_name);

    if (stat(fileName, &statBuff))
    {
        KHR_ICD_TRACE("Failed stat for: %s, continuing\n", fileName);
        free(fileName);
        return;
    }

    if (S_ISREG(statBuff.st_mode) || S_ISLNK(statBuff.st_mode))
    {
        FILE *fin = NULL;
        char* buffer = NULL;
        long bufferSize = 0;

        // open the file and read its contents
        fin = fopen(fileName, "r");
        if (!fin)
        {
            free(fileName);
            return;
        }
        fseek(fin, 0, SEEK_END);
        bufferSize = ftell(fin);

        buffer = malloc(bufferSize+1);
        if (!buffer)
        {
            free(fileName);
            fclose(fin);
            return;
        }
        memset(buffer, 0, bufferSize+1);
        fseek(fin, 0, SEEK_SET);
        if (bufferSize != (long)fread(buffer, 1, bufferSize, fin))
        {
            free(fileName);
            free(buffer);
            fclose(fin);
            return;
        }
        // ignore a newline at the end of the file
        if (buffer[bufferSize-1] == '\n') buffer[bufferSize-1] = '\0';

        // load the string read from the file
        addFunc(buffer);

        free(fileName);
        free(buffer);
        fclose(fin);
     }
     else
     {
         KHR_ICD_TRACE("File %s is not a regular file nor symbolic link, continuing\n", fileName);
         free(fileName);
     }
}

struct dirElem
{
    char *d_name;
    unsigned char d_type;
};

static int compareDirElem(const void *a, const void *b)
{
    // sort files the same way libc alpahnumerically sorts directory entries.
    return strcoll(((const struct dirElem *)a)->d_name, ((const struct dirElem *)b)->d_name);
}

static inline void khrIcdOsDirEnumerate(const char *path, const char *env,
                                        const char *extension,
                                        khrIcdFileAdd addFunc, int bSort)
{
    DIR *dir = NULL;
    char* envPath = NULL;

    envPath = khrIcd_secure_getenv(env);
    if (NULL != envPath)
    {
        path = envPath;
    }

    dir = opendir(path);
    if (NULL == dir) 
    {
        KHR_ICD_TRACE("Failed to open path %s, continuing\n", path);
    }
    else
    {
        struct dirent *dirEntry = NULL;

        // attempt to load all files in the directory
        if (bSort) {
            // store the entries name and type in a buffer for sorting
            size_t sz = 0;
            size_t elemCount = 0;
            size_t elemAlloc = 0;
            struct dirElem *dirElems = NULL;
            struct dirElem *newDirElems = NULL;
            const size_t startupAlloc = 8;

            // start with a small buffer
            dirElems = (struct dirElem *)malloc(startupAlloc*sizeof(struct dirElem));
            if (NULL != dirElems) {
                elemAlloc = startupAlloc;
                for (dirEntry = readdir(dir); dirEntry; dirEntry = readdir(dir) ) {
                    char *nameCopy = NULL;

                    if (elemCount + 1 > elemAlloc) {
                        // double buffer size if necessary and possible
                        if (elemAlloc >= UINT_MAX/2)
                            break;
                        newDirElems = (struct dirElem *)realloc(dirElems, elemAlloc*2*sizeof(struct dirElem));
                        if (NULL == newDirElems)
                            break;
                        dirElems = newDirElems;
                        elemAlloc *= 2;
                    }
                    sz = strlen(dirEntry->d_name) + 1;
                    nameCopy = (char *)malloc(sz);
                    if (NULL == nameCopy)
                         break;
                    memcpy(nameCopy, dirEntry->d_name, sz);
                    dirElems[elemCount].d_name = nameCopy;
                    dirElems[elemCount].d_type = dirEntry->d_type;
                    elemCount++;
                }
                qsort(dirElems, elemCount, sizeof(struct dirElem), compareDirElem);
                for (struct dirElem *elem = dirElems; elem < dirElems + elemCount; ++elem) {
                    khrIcdOsDirEntryValidateAndAdd(elem->d_name, path, extension, addFunc);
                    free(elem->d_name);
                }
                free(dirElems);
            }
        } else
            // use system provided ordering
            for (dirEntry = readdir(dir); dirEntry; dirEntry = readdir(dir) )
                khrIcdOsDirEntryValidateAndAdd(dirEntry->d_name, path, extension, addFunc);

        closedir(dir);
    }

    if (NULL != envPath)
    {
        khrIcd_free_getenv(envPath);
    }
}

// add a vendor's implementation to the list of libraries
void khrIcdVendorAdd(const char *libraryName);

// add a layer to the layer chain
void khrIcdLayerAdd(const char *libraryName);

// go through the list of vendors in the two configuration files
void khrIcdOsVendorsEnumerate(void)
{
    khrIcdInitializeTrace();
    khrIcdVendorsEnumerateEnv();

    khrIcdOsDirEnumerate(ICD_VENDOR_PATH, "OCL_ICD_VENDORS", ".icd", khrIcdVendorAdd, 0);

    // within steam pressure vessel linux
    khrIcdOsDirEnumerate("/run/host/" ICD_VENDOR_PATH, "OCL_ICD_VENDORS", ".icd", khrIcdVendorAdd, 0);

#if defined(CL_ENABLE_LAYERS)
    // system layers should be closer to the driver
    khrIcdOsDirEnumerate(LAYER_PATH, "OPENCL_LAYER_PATH", ".lay", khrIcdLayerAdd, 1);

    // within steam pressure vessel linux
    khrIcdOsDirEnumerate("/run/host/" LAYER_PATH, "OPENCL_LAYER_PATH", ".lay", khrIcdLayerAdd, 1);

    khrIcdLayersEnumerateEnv();
#endif // defined(CL_ENABLE_LAYERS)
}

