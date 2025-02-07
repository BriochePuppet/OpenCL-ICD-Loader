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

#include "icd.h"
#include "icd_dispatch.h"
#if defined(CL_ENABLE_LAYERS)
#include <CL/cl_layer.h>
#endif // defined(CL_ENABLE_LAYERS)
#include <stdlib.h>
#include <string.h>

KHRicdVendor *khrIcdVendors = NULL;
int khrEnableTrace = 0;

#if defined(CL_ENABLE_LAYERS)
struct KHRLayer *khrFirstLayer = NULL;
#endif // defined(CL_ENABLE_LAYERS)

// entrypoint to initialize the ICD and add all vendors
void khrIcdInitialize(void)
{
    // enumerate vendors present on the system
    khrIcdOsVendorsEnumerateOnce();
}

void khrIcdContextPropertiesGetPlatform(const cl_context_properties *properties, cl_platform_id *outPlatform)
{
    if (properties == NULL && khrIcdVendors != NULL)
    {
        *outPlatform = khrIcdVendors[0].platform;
    }
    else
    {
        const cl_context_properties *property = (cl_context_properties *)NULL;
        *outPlatform = NULL;
        for (property = properties; property && property[0]; property += 2)
        {
            if ((cl_context_properties)CL_CONTEXT_PLATFORM == property[0])
            {
                *outPlatform = (cl_platform_id)property[1];
            }
        }
    }
}
