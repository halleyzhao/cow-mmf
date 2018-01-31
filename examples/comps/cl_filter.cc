/**
 * Copyright (C) 2017 Alibaba Group Holding Limited. All Rights Reserved.
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>

#include "cl_filter.h"
#include "multimedia/mm_debug.h"
#include <vector>
#include <cutils/native_target.h>
#include <yalloc_drm.h>
#include <cstring>

MM_LOG_DEFINE_MODULE_NAME("CL-FILTER")
// helper define for ocl
#define OCL_CALL(FN, ...) \
  do { \
    int status = FN(__VA_ARGS__); \
    if (status != CL_SUCCESS) ERROR("error calling %s with error %d \n", #FN, status); \
  } while (0)

void clCheckPlatformInfo(cl_platform_id clPlatform, const char* name, uint32_t id)
{
    size_t size = 0;
    char *str = NULL;

    OCL_CALL(clGetPlatformInfo, clPlatform, id, 0, 0, &size);
    str = (char*)malloc(size);
    if (str) {
        OCL_CALL(clGetPlatformInfo, clPlatform, id, size, str, &size);
        INFO("cl platform %s: %s", name, str);
        free(str);
    }
}

void clCheckDeviceInfo(cl_device_id clDevice, const char* name, uint32_t id)
{
    size_t size = 0;
    char *str = NULL;

    OCL_CALL(clGetDeviceInfo, clDevice, id, 0, 0, &size);
    str = (char*)malloc(size);
    if (str) {
        OCL_CALL(clGetDeviceInfo, clDevice, id, size, str, &size);
        INFO("cl device %s: %s", name, str);
        free(str);
    }
}

#define FUNC_TRACK() FuncTracker tracker(MM_LOG_TAG, __FUNCTION__, __LINE__)
// #define FUNC_TRACK()

// shader source
#define SHADER(Src) #Src
const char* shader_source = SHADER(
__kernel void
testClShader(__read_only image2d_t y_left, __write_only image2d_t y_right, __read_only image2d_t uv_left, __write_only image2d_t uv_right, int width, int height)
{
    const sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_FILTER_NEAREST;
    int2 src_loc = (int2)(width/2-1-get_global_id(0), get_global_id(1));
    int2 dst_loc = (int2)(get_global_id(0), get_global_id(1));

    uint4 color_y = read_imageui(y_left, sampler, src_loc);
    write_imageui(y_right, dst_loc, color_y);
}
);
#undef SHADER
const int kPartialFactor = 4;

namespace YUNOS_MM {
// ################################################################
int cl_check_beignet(cl_device_id device)
{
    size_t param_value_size;
    size_t ret_sz;
    OCL_CALL(clGetDeviceInfo, device, CL_DEVICE_VERSION, 0, 0, &param_value_size);
    if(param_value_size == 0) {
        return 0;
    }
    char* device_version_str = (char* )malloc(param_value_size * sizeof(char) );
    OCL_CALL(clGetDeviceInfo, device, CL_DEVICE_VERSION, param_value_size, (void*)device_version_str, &ret_sz);
    MMASSERT(ret_sz == param_value_size);

    if(!strstr(device_version_str, "beignet")) {
        free(device_version_str);
        return 0;
    }
    free(device_version_str);
    return 1;
}

OCLProcessor::OCLProcessor()
    : mWidth(0), mHeight(0)
    , mPlatform(NULL), mDevice(NULL), mContext(NULL)
    , mProgram(NULL), mKernel(NULL), mQueue(NULL)
    , mOclCreateImageFromDrmNameIntel(NULL)
    , m_pYalloc(NULL)
    , mY_CLImage(NULL), mUV_CLImage(NULL)
{
    FUNC_TRACK();
}

bool OCLProcessor::init()
{
    FUNC_TRACK();
    cl_int status = CL_SUCCESS;
    cl_uint platform_n;
    cl_context_properties *props = NULL;

    yalloc_open(&m_pYalloc);
    MMASSERT(m_pYalloc);

    // pass drm fd to beignet through env var
    int drmFd = -1;
    int ret = m_pYalloc->dispose(m_pYalloc, YALLOC_MODULE_PERFORM_GET_DRM_FD, &drmFd);
    ASSERT((!ret) && (drmFd >= 0));
    char fdStr[20];
    sprintf(fdStr, "%d", drmFd);
    setenv("MM_CL_FILTER_DRM_FD", fdStr, 1);
    DEBUG("drm fd: %d", drmFd);

    /* Get the platform number */
    OCL_CALL (clGetPlatformIDs, 0, NULL, &platform_n);
    INFO("platform number %u", platform_n);
    MMASSERT(platform_n >= 1);

    /* Get a valid platform */
    OCL_CALL (clGetPlatformIDs, 1, &mPlatform, &platform_n);
    MMASSERT(mPlatform);
    clCheckPlatformInfo(mPlatform, "platform profile", CL_PLATFORM_PROFILE);
    clCheckPlatformInfo(mPlatform, "platform name", CL_PLATFORM_NAME);
    clCheckPlatformInfo(mPlatform, "platform vendor", CL_PLATFORM_VENDOR);
    clCheckPlatformInfo(mPlatform, "platform version", CL_PLATFORM_VERSION);
    clCheckPlatformInfo(mPlatform, "platform extensions", CL_PLATFORM_EXTENSIONS);


    /* Get the device (only GPU device is supported right now) */
    OCL_CALL (clGetDeviceIDs, mPlatform, CL_DEVICE_TYPE_GPU, 1, &mDevice, NULL);
    MMASSERT(mDevice);
    {
        size_t param_value_size;
        clCheckDeviceInfo(mDevice, "profile", CL_DEVICE_PROFILE);
        clCheckDeviceInfo(mDevice, "name", CL_DEVICE_NAME);
        clCheckDeviceInfo(mDevice, "vendor", CL_DEVICE_VENDOR);
        clCheckDeviceInfo(mDevice, "version", CL_DEVICE_VERSION);
        clCheckDeviceInfo(mDevice, "extensions", CL_DEVICE_EXTENSIONS);
        clCheckDeviceInfo(mDevice, "opencl_c_version", CL_DEVICE_OPENCL_C_VERSION);
    }

    cl_check_beignet(mDevice);

    /* Now create a context */
    mContext = clCreateContext(props, 1, &mDevice, NULL, NULL, &status);
    MMASSERT(status == CL_SUCCESS);

    /* All image types currently supported by the context */
    cl_image_format fmt[256];
    cl_uint fmt_n;
    clGetSupportedImageFormats(mContext, 0, CL_MEM_OBJECT_IMAGE2D, 256, fmt, &fmt_n);
    DEBUG("%u image formats are supported", fmt_n);
    /*
    for (i = 0; i < fmt_n; ++i)
        DEBUG("[%s %s]",
        cl_test_channel_order_string(fmt[i].image_channel_order),
        cl_test_channel_type_string(fmt[i].image_channel_data_type));
    */

    /* We are going to push NDRange kernels here */
    mQueue = clCreateCommandQueue(mContext, mDevice, 0, &status);
    MMASSERT(status == CL_SUCCESS);

#ifdef CL_VERSION_1_2
    mOclCreateImageFromDrmNameIntel = (OCLCREATEIMAGEFROMLIBVAINTEL *)clGetExtensionFunctionAddressForPlatform(mPlatform, "clCreateImageFromLibvaIntel");
#else
    mOclCreateImageFromDrmNameIntel = (OCLCREATEIMAGEFROMLIBVAINTEL *)clGetExtensionFunctionAddress("clCreateImageFromLibvaIntel");
#endif
    MMASSERT(mOclCreateImageFromDrmNameIntel);

    const size_t shader_sz = strlen(shader_source);
    mProgram = clCreateProgramWithSource(mContext, 1, &shader_source, &shader_sz, &status);
    MMASSERT(status == CL_SUCCESS);
    OCL_CALL (clBuildProgram, mProgram, 1, &mDevice, NULL, NULL, NULL);

    // print shader build log
    const size_t buildLogSize = 1024*8;
    size_t buildLogSizeRet = 0;
    char buildLog[buildLogSize];
    status = clGetProgramBuildInfo( mProgram, mDevice, CL_PROGRAM_BUILD_LOG,
                                    buildLogSize, buildLog, &buildLogSizeRet);
    DEBUG("build info: %s", buildLog);

    mKernel = clCreateKernel(mProgram, "testClShader", &status);
    DEBUG("status: %d", status);
    MMASSERT(status == CL_SUCCESS);

    if (props)
        delete[] props;
    return status == CL_SUCCESS;
}

bool OCLProcessor::prepareCLImage(MMBufferHandleT src, MMBufferHandleT dst, uint32_t width, uint32_t height)
{
    FUNC_TRACK();
    int32_t ret = 0;

    // populate buffer handle information
    uint32_t format = 0;
    ret = m_pYalloc->dispose(m_pYalloc, YALLOC_DISPOSE_GET_FORMAT, src, &format);
    DEBUG_FOURCC("inputFourcc", format);
    ASSERT((ret == 0) && (format == YUN_HAL_FORMAT_NV12 || format == YUN_HAL_FORMAT_DRM_NV12 || format == YUN_HAL_FORMAT_RGBX_8888));

    DEBUG("original width = %d, height = %d\n", width, height);
    uint32_t w = 0;
    ret = m_pYalloc->dispose(m_pYalloc, YALLOC_DISPOSE_GET_WIDTH, src, &w);
    DEBUG("yalloc dispose : width = %d\n", w);
    ASSERT(ret == 0);

    uint32_t h = 0;
    ret = m_pYalloc->dispose(m_pYalloc, YALLOC_DISPOSE_GET_HEIGHT, src, &h);
    DEBUG("yalloc dispose : height = %d\n", h);
    ASSERT(ret == 0);

    uint32_t tiling = 0;
    ret = m_pYalloc->dispose(m_pYalloc, YALLOC_DISPOSE_GET_TILING_MODE, src, &tiling);
    DEBUG("yalloc dispose : tiling = %d\n", tiling);
    ASSERT(ret == 0);

    uint32_t plane_num = 0;
    ret = m_pYalloc->dispose(m_pYalloc, YALLOC_DISPOSE_GET_PLANE_NUM, src, &plane_num);
    DEBUG("yalloc dispose : plane_num = %d\n", plane_num);
    ASSERT(ret == 0);

    uint32_t offset[2];
    uint32_t x_stride[2];
    uint32_t y_stride[2];
    uint32_t bpp[2];

    ret = m_pYalloc->dispose(m_pYalloc, YALLOC_DISPOSE_GET_PLANE_OFFSETS, src, offset);
    ASSERT(ret == 0);
    ret = m_pYalloc->dispose(m_pYalloc, YALLOC_DISPOSE_GET_X_STRIDES, src, x_stride);
    ASSERT(ret == 0);
    ret = m_pYalloc->dispose(m_pYalloc, YALLOC_DISPOSE_GET_Y_STRIDES, src, y_stride);
    ASSERT(ret == 0);
    ret = m_pYalloc->dispose(m_pYalloc, YALLOC_DISPOSE_GET_PLANE_BPPS, src, bpp);
    ASSERT(ret == 0);

    uint32_t i=0;
    for (i = 0; i < plane_num; i++)
    {
        DEBUG("yalloc dispose : offset[%d] = %d\n", i, offset[i]);
        DEBUG("yalloc dispose : X_stride[%d] = %d\n", i, x_stride[i]);
        DEBUG("yalloc dispose : Y_stride[%d] = %d\n", i, y_stride[i]);
        DEBUG("yalloc dispose : bpp[%d] = %d\n", i, bpp[i]);
    }

    int fd = 0, fd2 = 0;
    int name = 0, name2 = 0;
    ret = m_pYalloc->dispose(m_pYalloc, YALLOC_DISPOSE_GET_BUFFER_FD, src, &fd);
    if (fd < 0) {
        ret = m_pYalloc->dispose(m_pYalloc, YALLOC_DISPOSE_GET_BUFFER_NAME, src, &name);
    }
    ret = m_pYalloc->dispose(m_pYalloc, YALLOC_DISPOSE_GET_BUFFER_FD, src, &fd2);
    if (fd < 0) {
        ret = m_pYalloc->dispose(m_pYalloc, YALLOC_DISPOSE_GET_BUFFER_NAME, src, &name2);
    }
    DEBUG("yalloc dispose : fd = %d, name = %d, fd2 = %d, name2 = %d", fd, name, fd2, name2);
    ASSERT(ret == 0);

    // create CL memory object
    cl_int cl_status = CL_SUCCESS;
    cl_libva_image info_image;

    // #### left half of video frame
    // Y plane
    info_image.bo_name = name;
    info_image.offset = offset[0];
    info_image.width = w/2;
    info_image.height = h;
    info_image.fmt.image_channel_order = CL_R;
    info_image.fmt.image_channel_data_type = CL_UNSIGNED_INT8;
    info_image.row_pitch = x_stride[0];
    DEBUG("[drm buffer info] bo_name: %d, offset: %d, width: %d, height: %d, row_pitch: %d",
        info_image.bo_name, info_image.offset, info_image.width, info_image.height, info_image.row_pitch);
    mY_CLImage = mOclCreateImageFromDrmNameIntel(mContext, &info_image, &cl_status);
    DEBUG("create ocl image object from y plane of video frame done mY_CLImage: %p", mY_CLImage);
    MMASSERT(cl_status == CL_SUCCESS && mY_CLImage);
    // UV plane
    info_image.bo_name = name;
    info_image.offset = offset[1];
    info_image.width = w/2/2;
    info_image.height = h /2/2;
    info_image.fmt.image_channel_order = CL_R;
    info_image.fmt.image_channel_data_type = CL_UNSIGNED_INT16;
    info_image.row_pitch = x_stride[1];
    mUV_CLImage = mOclCreateImageFromDrmNameIntel(mContext, &info_image, &cl_status);
    DEBUG("create ocl image object from uv plane of video frame mUV_CLImage: %p", mUV_CLImage);
    MMASSERT(cl_status == CL_SUCCESS && mUV_CLImage);

    // right half of image
    // Y plane
    info_image.bo_name = name2;
    info_image.offset = offset[0]+w/2;
    info_image.width = w/2;
    info_image.height = h;
    info_image.fmt.image_channel_order = CL_R;
    info_image.fmt.image_channel_data_type = CL_UNSIGNED_INT8;
    info_image.row_pitch = x_stride[0];
    DEBUG("[drm buffer info] bo_name: %d, offset: %d, width: %d, height: %d, row_pitch: %d",
        info_image.bo_name, info_image.offset, info_image.width, info_image.height, info_image.row_pitch);
    mY_CLImage2 = mOclCreateImageFromDrmNameIntel(mContext, &info_image, &cl_status);
    DEBUG("create ocl image object from y plane of video frame done mY_CLImage2: %p", mY_CLImage2);
    MMASSERT(cl_status == CL_SUCCESS && mY_CLImage2);
    // UV plane
    info_image.bo_name = name2;
    info_image.offset = offset[1]+w/2;
    info_image.width = w/2/2;
    info_image.height = h /2/2;
    info_image.fmt.image_channel_order = CL_R;
    info_image.fmt.image_channel_data_type = CL_UNSIGNED_INT16;
    info_image.row_pitch = x_stride[1];
    mUV_CLImage2 = mOclCreateImageFromDrmNameIntel(mContext, &info_image, &cl_status);
    DEBUG("create ocl image object from uv plane of video frame mUV_CLImage2: %p", mUV_CLImage2);
    MMASSERT(cl_status == CL_SUCCESS && mUV_CLImage2);

    return true;
}


bool OCLProcessor::processBuffer(MMBufferHandleT src, MMBufferHandleT dst, uint32_t width, uint32_t height)
{
    FUNC_TRACK();
    int index=0;

    if (!dst)
        dst = src;
    prepareCLImage(src, dst, width, height);
    if (!mY_CLImage || !mUV_CLImage)
        return false;

    OCL_CALL (clSetKernelArg, mKernel, index++, sizeof(cl_mem), &mY_CLImage);
    OCL_CALL (clSetKernelArg, mKernel, index++, sizeof(cl_mem), &mY_CLImage2);
    OCL_CALL (clSetKernelArg, mKernel, index++, sizeof(cl_mem), &mUV_CLImage);
    OCL_CALL (clSetKernelArg, mKernel, index++, sizeof(cl_mem), &mUV_CLImage2);
    OCL_CALL (clSetKernelArg, mKernel, index++, sizeof(int), &width);
    OCL_CALL (clSetKernelArg, mKernel, index++, sizeof(int), &height);

    size_t global_size[2];
    global_size[0] = width/2;
    global_size[1] = height;
    OCL_CALL (clEnqueueNDRangeKernel, mQueue, mKernel, 2, NULL,
              global_size, NULL, 0, NULL, NULL);
    OCL_CALL (clFinish, mQueue);
    DEBUG("Successfully use ocl to do processing, global_size: %dx%d", global_size[0], global_size[1]);

    OCL_CALL (clReleaseMemObject, mY_CLImage);
    OCL_CALL (clReleaseMemObject, mUV_CLImage);
    OCL_CALL (clReleaseMemObject, mY_CLImage2);
    OCL_CALL (clReleaseMemObject, mUV_CLImage2);
    mY_CLImage = NULL;
    mUV_CLImage = NULL;
    mY_CLImage2 = NULL;
    mUV_CLImage2 = NULL;

    return true;
}
bool OCLProcessor::deinit()
{
    FUNC_TRACK();

    if (mKernel) {
      clReleaseKernel(mKernel);
      mKernel = NULL;
    }
    if (mProgram) {
      clReleaseProgram(mProgram);
      mProgram = NULL;
    }

    if (mQueue) {
        clReleaseCommandQueue(mQueue);
        mQueue = NULL;
    }
    if (mContext) {
        ERROR("skip clReleaseContext to reduce crash");
        // clReleaseContext(mContext);
        mContext = NULL;
    }
    return true;
}

} // end of namespace YUNOS_MM
