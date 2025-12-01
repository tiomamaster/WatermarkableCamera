#include <android/asset_manager.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <assert.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <array>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

/**
 * HelloVK contains the core of Vulkan pipeline setup. It includes recording
 * draw commands as well as screen clearing during the render pass.
 *
 * Please refer to: https://vulkan-tutorial.com/ for a gentle Vulkan
 * introduction.
 */

namespace vkt {
#define LOG_TAG "WatermarkableCameraJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define VK_CHECK(x)                           \
  do {                                        \
    VkResult err = x;                         \
    if (err) {                                \
      LOGE("Detected Vulkan error: %d", err); \
      abort();                                \
    }                                         \
  } while (0)

    class HelloVK {
    public:

    private:
    };

}  // namespace vkt