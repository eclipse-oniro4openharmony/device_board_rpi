/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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
 */

#define GL_GLEXT_PROTOTYPES
#define EGL_EGLEXT_PROTOTYPES

#include "surface_image.h"

#include "securec.h"
#include "sandbox_utils.h"
#include "surface_utils.h"

#include <atomic>
#include <sync_fence.h>
#include <unistd.h>
#include <window.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES/gl.h>
#include <GLES/glext.h>

namespace OHOS {
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
static PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
namespace {
// Get a uniqueID in a process
static int GetProcessUniqueId()
{
    static std::atomic<int> g_counter { 0 };
    return g_counter.fetch_add(1);
}
}

SurfaceImage::SurfaceImage(uint32_t textureId, uint32_t textureTarget)
    : ConsumerSurface("SurfaceImage-" + std::to_string(GetRealPid()) + "-" + std::to_string(GetProcessUniqueId())),
      textureId_(textureId),
      textureTarget_(textureTarget),
      updateSurfaceImage_(false),
      eglDisplay_(EGL_NO_DISPLAY),
      eglContext_(EGL_NO_CONTEXT),
      currentSurfaceImage_(UINT_MAX),
      currentSurfaceBuffer_(nullptr),
      currentTimeStamp_(0)
{
    InitSurfaceImage();
}

SurfaceImage::SurfaceImage()
    : ConsumerSurface("SurfaceImageConsumer-" + std::to_string(GetRealPid()) +
    "-" + std::to_string(GetProcessUniqueId())),
      currentSurfaceBuffer_(nullptr),
      currentTimeStamp_(0)
{
    InitSurfaceImage();
}

SurfaceImage::~SurfaceImage()
{
    for (auto it = imageCacheSeqs_.begin(); it != imageCacheSeqs_.end(); it++) {
        if (it->second.eglImage_ != EGL_NO_IMAGE_KHR) {
            eglDestroyImageKHR(eglDisplay_, it->second.eglImage_);
            it->second.eglImage_ = EGL_NO_IMAGE_KHR;
        }
        if (it->second.eglSync_ != EGL_NO_SYNC_KHR) {
            eglDestroySyncKHR(eglDisplay_, it->second.eglSync_);
            it->second.eglSync_ = EGL_NO_SYNC_KHR;
        }
    }
}

void SurfaceImage::InitSurfaceImage()
{
    std::string name = "SurfaceImage-" + std::to_string(GetRealPid()) + "-" + std::to_string(GetProcessUniqueId());
    auto ret = ConsumerSurface::Init();
    BLOGI("surfaceimage init");
    if (ret != SURFACE_ERROR_OK) {
        BLOGE("init surfaceimage failed");
    }
    surfaceImageName_ = name;
}

SurfaceError SurfaceImage::SetDefaultSize(int32_t width, int32_t height)
{
    return ConsumerSurface::SetDefaultWidthAndHeight(width, height);
}

void SurfaceImage::UpdateSurfaceInfo(uint32_t seqNum, sptr<SurfaceBuffer> buffer, const sptr<SyncFence> &acquireFence,
                                     int64_t timestamp, Rect damage)
{
    // release old buffer
    int releaseFence = -1;
    auto iter = imageCacheSeqs_.find(currentSurfaceImage_);
    if (iter != imageCacheSeqs_.end() && iter->second.eglSync_ != EGL_NO_SYNC_KHR) {
        // PLANNING: use eglDupNativeFenceFDOHOS in the future.
        releaseFence = eglDupNativeFenceFDANDROID(eglDisplay_, iter->second.eglSync_);
    }
    // There is no need to close this fd, because in function ReleaseBuffer it will be closed.
    ReleaseBuffer(currentSurfaceBuffer_, releaseFence);

    currentSurfaceImage_ = seqNum;
    currentSurfaceBuffer_ = buffer;
    currentTimeStamp_ = timestamp;
    currentCrop_ = damage;
    currentTransformType_ = ConsumerSurface::GetTransform();
    auto utils = SurfaceUtils::GetInstance();
    utils->ComputeTransformMatrix(currentTransformMatrix_, TRANSFORM_MATRIX_ELE_COUNT,
        currentSurfaceBuffer_, currentTransformType_, currentCrop_);
    utils->ComputeTransformMatrixV2(currentTransformMatrixV2_, TRANSFORM_MATRIX_ELE_COUNT,
        currentSurfaceBuffer_, currentTransformType_, currentCrop_);

    // wait on this acquireFence.
    if (acquireFence != nullptr) {
        acquireFence->Wait(-1);
    }
}

SurfaceError SurfaceImage::UpdateSurfaceImage()
{
    std::lock_guard<std::mutex> lockGuard(opMutex_);

    // validate egl state
    SurfaceError ret = ValidateEglState();
    if (ret != SURFACE_ERROR_OK) {
        return ret;
    }

    // acquire buffer
    sptr<SurfaceBuffer> buffer = nullptr;
    sptr<SyncFence> acquireFence = SyncFence::INVALID_FENCE;
    int64_t timestamp;
    Rect damage;
    ret = AcquireBuffer(buffer, acquireFence, timestamp, damage);
    if (ret != SURFACE_ERROR_OK) {
        return ret;
    }

    ret = UpdateEGLImageAndTexture(eglDisplay_, buffer);
    if (ret != SURFACE_ERROR_OK) {
        ReleaseBuffer(buffer, -1);
        return ret;
    }

    uint32_t seqNum = buffer->GetSeqNum();
    UpdateSurfaceInfo(seqNum, buffer, acquireFence, timestamp, damage);
    return SURFACE_ERROR_OK;
}

SurfaceError SurfaceImage::AttachContext(uint32_t textureId)
{
    std::lock_guard<std::mutex> lockGuard(opMutex_);
    // validate egl state
    SurfaceError ret = ValidateEglState();
    if (ret != SURFACE_ERROR_OK) {
        return ret;
    }

    textureId_ = textureId;
    auto iter = imageCacheSeqs_.find(currentSurfaceImage_);
    if (iter != imageCacheSeqs_.end()) {
        glBindTexture(textureTarget_, textureId);
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            BLOGE("glBindTexture failed, textureTarget:%{public}d, textureId_:%{public}d, error:%{public}d",
                textureTarget_, textureId_, error);
            return SURFACE_ERROR_EGL_API_FAILED;
        }
        glEGLImageTargetTexture2DOES(textureTarget_, static_cast<GLeglImageOES>(iter->second.eglImage_));
        error = glGetError();
        if (error != GL_NO_ERROR) {
            BLOGE("glEGLImageTargetTexture2DOES failed, textureTarget:%{public}d, error:%{public}d",
                textureTarget_, error);
            return SURFACE_ERROR_EGL_API_FAILED;
        }
    }

    // If there is no EGLImage, we cannot simply return an error.
    // Developers can call OH_NativeImage_UpdateSurfaceImage later to achieve their purpose.
    return SURFACE_ERROR_OK;
}

SurfaceError SurfaceImage::DetachContext()
{
    std::lock_guard<std::mutex> lockGuard(opMutex_);
    // validate egl state
    SurfaceError ret = ValidateEglState();
    if (ret != SURFACE_ERROR_OK) {
        return ret;
    }

    textureId_ = 0;
    glBindTexture(textureTarget_, 0);
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        BLOGE("glBindTexture failed, textureTarget:%{public}d, textureId:%{public}d, error:%{public}d",
            textureTarget_, textureId_, error);
        return SURFACE_ERROR_EGL_API_FAILED;
    }
    return SURFACE_ERROR_OK;
}

int64_t SurfaceImage::GetTimeStamp()
{
    std::lock_guard<std::mutex> lockGuard(opMutex_);
    return currentTimeStamp_;
}

SurfaceError SurfaceImage::GetTransformMatrix(float matrix[16])
{
    std::lock_guard<std::mutex> lockGuard(opMutex_);
    auto ret = memcpy_s(matrix, sizeof(float) * 16,  // 16 is the length of array
                        currentTransformMatrix_, sizeof(currentTransformMatrix_));
    if (ret != EOK) {
        BLOGE("GetTransformMatrix: currentTransformMatrix_ memcpy_s failed");
        return SURFACE_ERROR_UNKOWN;
    }
    return SURFACE_ERROR_OK;
}

SurfaceError SurfaceImage::GetTransformMatrixV2(float matrix[16])
{
    std::lock_guard<std::mutex> lockGuard(opMutex_);
    auto ret = memcpy_s(matrix, sizeof(float) * 16, // 16 is the length of array
                        currentTransformMatrixV2_, sizeof(currentTransformMatrixV2_));
    if (ret != EOK) {
        BLOGE("GetTransformMatrixV2: currentTransformMatrixV2_ memcpy_s failed");
        return SURFACE_ERROR_UNKOWN;
    }
    return SURFACE_ERROR_OK;
}

SurfaceError SurfaceImage::ValidateEglState()
{
    EGLDisplay disp = eglGetCurrentDisplay();
    EGLContext context = eglGetCurrentContext();

    if ((eglDisplay_ != disp && eglDisplay_ != EGL_NO_DISPLAY) || (disp == EGL_NO_DISPLAY)) {
        BLOGE("EGLDisplay is invalid, errno : 0x%{public}x", eglGetError());
        return SURFACE_ERROR_EGL_STATE_UNKONW;
    }
    if ((eglContext_ != context && eglContext_ != EGL_NO_CONTEXT) || (context == EGL_NO_CONTEXT)) {
        BLOGE("EGLContext is invalid, errno : 0x%{public}x", eglGetError());
        return SURFACE_ERROR_EGL_STATE_UNKONW;
    }

    eglDisplay_ = disp;
    eglContext_ = context;
    return SURFACE_ERROR_OK;
}

EGLImageKHR SurfaceImage::CreateEGLImage(EGLDisplay disp, const sptr<SurfaceBuffer>& buffer)
{
    sptr<SurfaceBuffer> bufferImpl = buffer;
    NativeWindowBuffer* nBuffer = CreateNativeWindowBufferFromSurfaceBuffer(&bufferImpl);
    EGLint attrs[] = {
        EGL_IMAGE_PRESERVED,
        EGL_TRUE,
        EGL_NONE,
    };

    EGLImageKHR img = eglCreateImageKHR(disp, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_OHOS, nBuffer, attrs);
    if (img == EGL_NO_IMAGE_KHR) {
        EGLint error = eglGetError();
        BLOGE("failed, error %{public}d", error);
        eglTerminate(disp);
    }
    DestroyNativeWindowBuffer(nBuffer);
    return img;
}

void SurfaceImage::CheckImageCacheNeedClean(uint32_t seqNum)
{
    for (auto it = imageCacheSeqs_.begin(); it != imageCacheSeqs_.end();) {
        bool result = true;
        if (seqNum == it->first) {
            it++;
            continue;
        }
        if (IsSurfaceBufferInCache(it->first, result) == SURFACE_ERROR_OK && !result) {
            if (it->second.eglImage_ != EGL_NO_IMAGE_KHR) {
                eglDestroyImageKHR(eglDisplay_, it->second.eglImage_);
                it->second.eglImage_ = EGL_NO_IMAGE_KHR;
            }
            if (it->second.eglSync_ != EGL_NO_SYNC_KHR) {
                eglDestroySyncKHR(eglDisplay_, it->second.eglSync_);
                it->second.eglSync_ = EGL_NO_SYNC_KHR;
            }
            it = imageCacheSeqs_.erase(it);
        } else {
            it++;
        }
    }
}

void SurfaceImage::DestroyEGLImage(uint32_t seqNum)
{
    auto iter = imageCacheSeqs_.find(seqNum);
    if (iter != imageCacheSeqs_.end() && iter->second.eglImage_ != EGL_NO_IMAGE_KHR) {
        eglDestroyImageKHR(eglDisplay_, iter->second.eglImage_);
    }
    imageCacheSeqs_.erase(seqNum);
}

SurfaceError SurfaceImage::UpdateEGLImageAndTexture(EGLDisplay disp, const sptr<SurfaceBuffer>& buffer)
{
    bool isNewBuffer = false;
    // private function, buffer is always valid.
    uint32_t seqNum = buffer->GetSeqNum();
    // If there was no eglImage binding to this buffer, we create a new one.
    if (imageCacheSeqs_.find(seqNum) == imageCacheSeqs_.end()) {
        isNewBuffer = true;
        EGLImageKHR eglImage = CreateEGLImage(eglDisplay_, buffer);
        if (eglImage == EGL_NO_IMAGE_KHR) {
            return SURFACE_ERROR_EGL_API_FAILED;
        }
        imageCacheSeqs_[seqNum].eglImage_ = eglImage;
    }

    auto &image = imageCacheSeqs_[seqNum];
    glBindTexture(textureTarget_, textureId_);
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        if (isNewBuffer) {
            DestroyEGLImage(seqNum);
        }
        BLOGE("glBindTexture failed, textureTarget:%{public}d, textureId_:%{public}d, error:%{public}d",
            textureTarget_, textureId_, error);
        return SURFACE_ERROR_EGL_API_FAILED;
    }
    glEGLImageTargetTexture2DOES(textureTarget_, static_cast<GLeglImageOES>(image.eglImage_));
    error = glGetError();
    if (error != GL_NO_ERROR) {
        if (isNewBuffer) {
            DestroyEGLImage(seqNum);
        }
        BLOGE("glEGLImageTargetTexture2DOES failed, textureTarget:%{public}d, error:%{public}d",
            textureTarget_, error);
        return SURFACE_ERROR_EGL_API_FAILED;
    }

    // Create fence object for current image
    auto iter = imageCacheSeqs_.find(currentSurfaceImage_);
    if (iter != imageCacheSeqs_.end()) {
        auto &currentImage = iter->second;
        auto preSync = currentImage.eglSync_;
        if (preSync != EGL_NO_SYNC_KHR) {
            eglDestroySyncKHR(disp, preSync);
        }
        currentImage.eglSync_ = eglCreateSyncKHR(disp, EGL_SYNC_NATIVE_FENCE_ANDROID, nullptr);
    }

    if (isNewBuffer) {
        CheckImageCacheNeedClean(seqNum);
    }
    return SURFACE_ERROR_OK;
}

SurfaceError SurfaceImage::SetOnBufferAvailableListener(void *context, OnBufferAvailableListener listener)
{
    std::lock_guard<std::mutex> lockGuard(opMutex_);
    if (listener == nullptr) {
        BLOGE("listener is nullptr");
        return SURFACE_ERROR_INVALID_PARAM;
    }

    listener_ = listener;
    context_ = context;
    return SURFACE_ERROR_OK;
}

SurfaceError SurfaceImage::UnsetOnBufferAvailableListener()
{
    std::lock_guard<std::mutex> lockGuard(opMutex_);
    listener_ = nullptr;
    context_ = nullptr;
    return SURFACE_ERROR_OK;
}

SurfaceImageListener::~SurfaceImageListener()
{
    BLOGE("~SurfaceImageListener");
    surfaceImage_ = nullptr;
}

void SurfaceImageListener::OnBufferAvailable()
{
    BLOGD("SurfaceImageListener::OnBufferAvailable");
    auto surfaceImage = surfaceImage_.promote();
    if (surfaceImage == nullptr) {
        BLOGE("surfaceImage promote failed");
        return;
    }

    // check here maybe a messagequeue, flag instead now
    surfaceImage->OnUpdateBufferAvailableState(true);
    if (surfaceImage->listener_ != nullptr) {
        surfaceImage->listener_(surfaceImage->context_);
    }
}

SurfaceError SurfaceImage::AcquireNativeWindowBuffer(OHNativeWindowBuffer** nativeWindowBuffer, int32_t* fenceFd)
{
    std::lock_guard<std::mutex> lockGuard(opMutex_);
    sptr<SurfaceBuffer> buffer = nullptr;
    sptr<SyncFence> acquireFence = SyncFence::INVALID_FENCE;
    int64_t timestamp;
    Rect damage;
    SurfaceError ret = AcquireBuffer(buffer, acquireFence, timestamp, damage);
    if (ret != SURFACE_ERROR_OK) {
        BLOGE("AcquireBuffer failed: %d", ret);
        return ret;
    }
    currentSurfaceBuffer_ = buffer;
    currentTimeStamp_ = timestamp;
    currentCrop_ = damage;
    currentTransformType_ = ConsumerSurface::GetTransform();
    auto utils = SurfaceUtils::GetInstance();
    utils->ComputeTransformMatrixV2(currentTransformMatrixV2_, TRANSFORM_MATRIX_ELE_COUNT,
        currentSurfaceBuffer_, currentTransformType_, currentCrop_);

    *fenceFd = acquireFence->Dup();
    OHNativeWindowBuffer *nwBuffer = new OHNativeWindowBuffer();
    nwBuffer->sfbuffer = buffer;
    NativeObjectReference(nwBuffer);
    *nativeWindowBuffer = nwBuffer;

    return SURFACE_ERROR_OK;
}

SurfaceError SurfaceImage::ReleaseNativeWindowBuffer(OHNativeWindowBuffer* nativeWindowBuffer, int32_t fenceFd)
{
    std::lock_guard<std::mutex> lockGuard(opMutex_);
    // There is no need to close this fd, because in function ReleaseBuffer it will be closed.
    SurfaceError ret = ReleaseBuffer(nativeWindowBuffer->sfbuffer, fenceFd);
    if (ret != SURFACE_ERROR_OK) {
        BLOGE("ReleaseBuffer failed: %d", ret);
        return ret;
    }
    NativeObjectUnreference(nativeWindowBuffer);
    return SURFACE_ERROR_OK;
}
} // namespace OHOS
