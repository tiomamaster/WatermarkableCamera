#pragma once

#include <android/asset_manager.h>

#include <cstdint>
#include <glm/glm.hpp>

// clang-format off
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_profiles.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_android.h>
#include <vulkan/vulkan_core.h>
// clang-format on

namespace camera {

struct Vertex {
    glm::vec2 pos;
    glm::vec3 color;
    glm::vec2 texCoord;

    static vk::VertexInputBindingDescription getBindingDescription() {
        return {0, sizeof(Vertex), vk::VertexInputRate::eVertex};
    }

    static std::array<vk::VertexInputAttributeDescription, 3>
    getAttributeDescriptions() {
        return {
            vk::VertexInputAttributeDescription(
                0, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, pos)
            ),
            vk::VertexInputAttributeDescription(
                1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)
            ),
            vk::VertexInputAttributeDescription(
                2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, texCoord)
            )
        };
    }

    bool operator==(const Vertex& other) const {
        return pos == other.pos && color == other.color &&
               texCoord == other.texCoord;
    }
};

struct UniformBufferObject {
    alignas(16) glm::mat4 camModel;
    alignas(16) glm::mat4 watModel;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

class VkRenderer {
  public:
    bool initialized = false;

    void init();
    void setMediaWindow(ANativeWindow* win);
    void camHwBufferToTexture(AHardwareBuffer* buf);
    void watHwBufferToTexture(AHardwareBuffer* buf);
    void reset(ANativeWindow* newWindow, AAssetManager* newManager);
    void cleanup();

  private:
    static constexpr uint64_t FENCE_TIMEOUT = 100000000;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    // Required device extensions
    const std::vector<const char*> deviceExtensions_{
        vk::KHRSwapchainExtensionName,
        vk::ANDROIDExternalMemoryAndroidHardwareBufferExtensionName,
        vk::EXTQueueFamilyForeignExtensionName,
        /*vk::EXTDescriptorIndexingExtensionName*/
    };

    // Model data
    const std::vector<Vertex> vertices_{
        // cam vertices
        {{-1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},

        // tex vertices
        {{-1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}}
    };
    const std::vector<uint16_t> indices_{0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4};

    ANativeWindow* displayWindow_ = nullptr;
    ANativeWindow* mediaWindow_ = nullptr;
    AAssetManager* assetManager_ = nullptr;

    bool framebufferResized_ = false;

    std::atomic_bool isRecording_ = false;

    // Vulkan objects
    vk::raii::Context context_;
    vk::raii::Instance instance_ = nullptr;
    vk::raii::SurfaceKHR displaySurface_ = nullptr;
    vk::raii::SurfaceKHR mediaSurface_ = nullptr;
    vk::raii::PhysicalDevice physicalDevice_ = nullptr;
    vk::raii::Device device_ = nullptr;
    // todo: separate to transfer and graphics queues
    uint32_t queueIndex_ = ~0;
    vk::raii::Queue queue_ = nullptr;
    vk::raii::SwapchainKHR swapChain_ = nullptr;
    std::vector<vk::Image> swapChainImages_;
    vk::SurfaceFormatKHR swapChainSurfaceFormat_;
    vk::Extent2D swapChainExtent_;
    std::vector<vk::raii::ImageView> swapChainImageViews_;

    vk::raii::SwapchainKHR mediaSwapChain_ = nullptr;
    std::vector<vk::Image> mediaSwapChainImages_;
    vk::SurfaceFormatKHR mediaSwapChainSurfaceFormat_;
    vk::Extent2D mediaSwapChainExtent_;
    std::vector<vk::raii::ImageView> mediaSwapChainImageViews_;

    vk::raii::RenderPass renderPass_ = nullptr;
    vk::raii::DescriptorSetLayout descriptorSetLayout_ = nullptr;
    vk::raii::PipelineLayout pipelineLayout_ = nullptr;
    vk::raii::Pipeline graphicsPipeline_ = nullptr;
    std::vector<vk::raii::Framebuffer> swapChainFramebuffers_;
    std::vector<vk::raii::Framebuffer> mediaSwapChainFramebuffers_;
    vk::raii::CommandPool commandPool_ = nullptr;
    std::vector<vk::raii::CommandBuffer> commandBuffers_;
    vk::raii::Buffer vertexBuffer_ = nullptr;
    vk::raii::DeviceMemory vertexBufferMemory_ = nullptr;
    vk::raii::Buffer indexBuffer_ = nullptr;
    vk::raii::DeviceMemory indexBufferMemory_ = nullptr;

    struct TextureData {
        vk::raii::Image image = nullptr;
        vk::raii::DeviceMemory memory = nullptr;
        vk::raii::ImageView imageView = nullptr;
    };
    std::vector<TextureData> watTextures_;

    vk::raii::Sampler watTextureSampler_ = nullptr;

    std::vector<TextureData> camTextures_;
    vk::raii::SamplerYcbcrConversion camTexConversion_ = nullptr;
    vk::raii::Sampler camTextureSampler_ = nullptr;

    std::vector<vk::raii::Buffer> uniformBuffers_;
    std::vector<vk::raii::DeviceMemory> uniformBuffersMemory_;
    vk::raii::DescriptorPool descriptorPool_ = nullptr;
    std::vector<vk::raii::DescriptorSet> descriptorSets_;

    std::vector<vk::raii::Semaphore> imageAvailableSemaphores_;
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores_;
    std::vector<vk::raii::Fence> inFlightFences_;

    std::vector<vk::raii::Semaphore> mediaImageAvailableSemaphores_;
    std::vector<vk::raii::Semaphore> mediaRenderFinishedSemaphores_;
    std::vector<vk::raii::Fence> mediaInFlightFences_;

    uint32_t semaphoreIndex_ = 0;
    uint32_t mediaSemaphoreIndex_ = 0;
    uint32_t currentFrame_ = 0;

    // Swap chain support details
    struct SwapChainSupportDetails {
        vk::SurfaceCapabilitiesKHR capabilities;
        std::vector<vk::SurfaceFormatKHR> formats;
        std::vector<vk::PresentModeKHR> presentModes;
    };

    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapChain();
    void createImageViews();
    void createRenderPass();
    void createDescriptorSetLayout();
    std::vector<char> readFile(const std::string& filename);
    [[nodiscard]] vk::raii::ShaderModule createShaderModule(
        const std::vector<char>& code
    ) const;
    void createGraphicsPipeline();
    void createFramebuffers();
    void createCommandPool();
    void createTextureSamplers();
    void createVertexBuffer();
    void createIndexBuffer();
    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void createCommandBuffers();
    void createSyncObjects();
    void cleanupSwapChain();
    void recreateSwapChain();
    static uint32_t chooseSwapMinImageCount(
        vk::SurfaceCapabilitiesKHR const& surfaceCapabilities
    );
    static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(
        const std::vector<vk::SurfaceFormatKHR>& availableFormats
    );
    static vk::PresentModeKHR chooseSwapPresentMode(
        const std::vector<vk::PresentModeKHR>& availablePresentModes
    );
    vk::Extent2D chooseSwapExtent(
        const vk::SurfaceCapabilitiesKHR& capabilities
    );
    static SwapChainSupportDetails querySwapChainSupport(
        const vk::raii::PhysicalDevice& device, const vk::SurfaceKHR& surface
    );
    void createBuffer(
        vk::DeviceSize size,
        vk::BufferUsageFlags usage,
        vk::MemoryPropertyFlags properties,
        vk::raii::Buffer& buffer,
        vk::raii::DeviceMemory& bufferMemory
    );
    void copyBuffer(
        vk::raii::Buffer& srcBuffer,
        vk::raii::Buffer& dstBuffer,
        vk::DeviceSize size
    );
    uint32_t findMemoryType(
        uint32_t typeFilter, vk::MemoryPropertyFlags properties
    );
    vk::raii::ImageView createImageView(
        vk::raii::Image& image, vk::Format format
    );
    void transitionImageLayout(
        vk::raii::Image& image,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout
    );
    void copyBufferToImage(
        vk::raii::Buffer& buffer,
        vk::raii::Image& image,
        uint32_t width,
        uint32_t height
    );
    void updateUniformBuffer(uint32_t currentImage);
};

}  // namespace camera
