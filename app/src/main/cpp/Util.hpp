#pragma once

#include <android/log.h>
#include <camera/NdkCameraError.h>
#include <camera/NdkCameraManager.h>

#include <source_location>

namespace camera::util {

constexpr auto TAG = "VkWatCam";

inline void logI(const char* text) {
    __android_log_write(ANDROID_LOG_INFO, TAG, text);
}

template <typename... Args>
inline void logI(Args&&... args) {
    __android_log_print(ANDROID_LOG_INFO, TAG, args...);
}

inline void logW(const char* text) {
    __android_log_write(ANDROID_LOG_WARN, TAG, text);
}

template <typename... Args>
inline void logW(Args&&... args) {
    __android_log_print(ANDROID_LOG_WARN, TAG, args...);
}

inline void logE(const char* text) {
    __android_log_write(ANDROID_LOG_ERROR, TAG, text);
}

template <typename... Args>
inline void logE(Args&&... args) {
    __android_log_print(ANDROID_LOG_ERROR, TAG, args...);
}

void callCamera(
    camera_status_t status,
    std::source_location location = std::source_location::current()
);

// A few debugging functions for error code strings etc
const char* getErrorStr(camera_status_t err);
const char* getTagStr(acamera_metadata_tag_t tag);
void printMetadataTags(int32_t entries, const uint32_t* pTags);
void printLensFacing(ACameraMetadata_const_entry& lensData);
void printCameras(ACameraManager* cameraMgr);
void printCameraDeviceError(int err);

void printRequestMetadata(ACaptureRequest* req);

}  // namespace camera::util

#define ASSERT(cond, fmt, ...)                                              \
    if (!(cond)) {                                                          \
        __android_log_assert(#cond, camera::util::TAG, fmt, ##__VA_ARGS__); \
    }
