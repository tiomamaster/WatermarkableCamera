#include "vulkan_renderer.hpp"

#include <vulkan/vulkan.hpp>

#include "util.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_CXX11
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"

using namespace camera::util;

namespace camera {

void VkRenderer::init() {
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createTextureSamplers();
    createSwapChain();
    createImageViews();
    createRenderPass();
    createDescriptorSetLayout();
    createGraphicsPipeline();
    createFramebuffers();
    createCommandPool();
    createVertexBuffer();
    createIndexBuffer();
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();
    createSyncObjects();

    initialized = true;
}

void VkRenderer::setMediaWindow(ANativeWindow* win) {
    mediaWindow_ = win;

    vk::AndroidSurfaceCreateInfoKHR createInfo{
        .sType = vk::StructureType::eAndroidSurfaceCreateInfoKHR,
        .pNext = nullptr,
        .flags = vk::AndroidSurfaceCreateFlagsKHR(),
        .window = mediaWindow_
    };

    mediaSurface_ = vk::raii::SurfaceKHR(instance_, createInfo);

    SwapChainSupportDetails swapChainSupport =
        querySwapChainSupport(physicalDevice_, *mediaSurface_);
    mediaSwapChainExtent_ = chooseSwapExtent(swapChainSupport.capabilities);
    mediaSwapChainSurfaceFormat_ =
        chooseSwapSurfaceFormat(swapChainSupport.formats);
    vk::SwapchainCreateInfoKHR swapChainCreateInfo{
        .surface = *mediaSurface_,
        .minImageCount = chooseSwapMinImageCount(swapChainSupport.capabilities),
        .imageFormat = mediaSwapChainSurfaceFormat_.format,
        .imageColorSpace = mediaSwapChainSurfaceFormat_.colorSpace,
        .imageExtent = mediaSwapChainExtent_,
        .imageArrayLayers = 1,
        // ??? if render to it, eTransferDst if copy
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = swapChainSupport.capabilities.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eInherit,
        .presentMode = chooseSwapPresentMode(swapChainSupport.presentModes),
        .clipped = true
    };

    logI(
        "media swapchain min image count = %i",
        swapChainCreateInfo.minImageCount
    );

    mediaSwapChain_ = device_.createSwapchainKHR(swapChainCreateInfo);
    mediaSwapChainImages_ = mediaSwapChain_.getImages();

    logI(
        "media swapchain images count = %i", (int)mediaSwapChainImages_.size()
    );
    logI("swapchain images count = %i", (int)swapChainImages_.size());
    logI("swapchain format = %i", (int)swapChainSurfaceFormat_.format);
    logI(
        "swapchain media format = %i", (int)mediaSwapChainSurfaceFormat_.format
    );
    logI(
        "media swapchain w = %i, h = %i",
        mediaSwapChainExtent_.width,
        mediaSwapChainExtent_.height
    );

    assert(mediaSwapChainImageViews_.empty());
    mediaSwapChainImageViews_.reserve(mediaSwapChainImages_.size());

    for (const auto& image : mediaSwapChainImages_) {
        vk::ImageViewCreateInfo ivCreateInfo{
            .image = image,
            .viewType = vk::ImageViewType::e2D,
            .format = mediaSwapChainSurfaceFormat_.format,
            .components =
                {.r = vk::ComponentSwizzle::eIdentity,
                 .g = vk::ComponentSwizzle::eIdentity,
                 .b = vk::ComponentSwizzle::eIdentity,
                 .a = vk::ComponentSwizzle::eIdentity},
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        mediaSwapChainImageViews_.emplace_back(
            device_.createImageView(ivCreateInfo)
        );
    }

    assert(mediaSwapChainFramebuffers_.empty());
    mediaSwapChainFramebuffers_.reserve(mediaSwapChainImageViews_.size());

    for (const auto& swapChainImageView : mediaSwapChainImageViews_) {
        vk::ImageView attachments[] = {*swapChainImageView};

        vk::FramebufferCreateInfo framebufferInfo{
            .renderPass = *renderPass_,
            .attachmentCount = 1,
            .pAttachments = attachments,
            .width = mediaSwapChainExtent_.width,
            .height = mediaSwapChainExtent_.height,
            .layers = 1
        };

        mediaSwapChainFramebuffers_.emplace_back(
            device_.createFramebuffer(framebufferInfo)
        );
    }

    mediaImageAvailableSemaphores_.clear();
    mediaRenderFinishedSemaphores_.clear();
    mediaInFlightFences_.clear();

    for (size_t i = 0; i < mediaSwapChainImages_.size(); ++i) {
        mediaImageAvailableSemaphores_.emplace_back(
            device_, vk::SemaphoreCreateInfo()
        );
        mediaRenderFinishedSemaphores_.emplace_back(
            device_, vk::SemaphoreCreateInfo()
        );
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        mediaInFlightFences_.emplace_back(
            device_,
            vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled}
        );
    }
}

void VkRenderer::camHwBufferToTexture(AHardwareBuffer* buf) {
    if (watTextures_.empty()) return;

    while (vk::Result::eTimeout ==
           device_.waitForFences(
               *inFlightFences_[currentFrame_], vk::True, FENCE_TIMEOUT
           ));

    uint32_t imageIndex;
    try {
        auto [_, idx] = swapChain_.acquireNextImage(
            FENCE_TIMEOUT, *imageAvailableSemaphores_[semaphoreIndex_], nullptr
        );
        imageIndex = idx;
    } catch (vk::OutOfDateKHRError&) {
        recreateSwapChain();
        return;
    }

    // Update uniform buffer with current transformation
    updateUniformBuffer(currentFrame_);

    // device.resetFences({*inFlightFences[currentFrame]});
    // commandBuffers[currentFrame].reset();

    vk::CommandBufferBeginInfo beginInfo{};
    commandBuffers_[currentFrame_].begin(beginInfo);

    vk::RenderPassBeginInfo renderPassInfo{
        .renderPass = *renderPass_,
        .framebuffer = *swapChainFramebuffers_[imageIndex],
        .renderArea = {.offset = {0, 0}, .extent = swapChainExtent_}
    };

    vk::ClearValue clearColor;
    clearColor.color.float32 = std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    commandBuffers_[currentFrame_].beginRenderPass(
        renderPassInfo, vk::SubpassContents::eInline
    );

    if (camTextures_.empty()) {
        logI("Resize camTextures");
        camTextures_.resize(MAX_FRAMES_IN_FLIGHT);
    }

    TextureData& camTexture = camTextures_[currentFrame_];

    auto hwBufProps = device_.getAndroidHardwareBufferPropertiesANDROID<
        vk::AndroidHardwareBufferPropertiesANDROID,
        vk::AndroidHardwareBufferFormatPropertiesANDROID>(*buf);
    vk::ExternalFormatANDROID extFormatAndroid{
        .externalFormat =
            hwBufProps.get<vk::AndroidHardwareBufferFormatPropertiesANDROID>()
                .externalFormat
    };

    vk::ExternalMemoryImageCreateInfo externalMemoryImageCreateInfo{
        .pNext = &extFormatAndroid,
        .handleTypes =
            vk::ExternalMemoryHandleTypeFlagBits::eAndroidHardwareBufferANDROID
    };

    AHardwareBuffer_Desc hardwareBufferDesc;
    AHardwareBuffer_describe(buf, &hardwareBufferDesc);

    vk::ImageCreateInfo imageInfo{
        .pNext = &externalMemoryImageCreateInfo,
        .imageType = vk::ImageType::e2D,
        .format =
            hwBufProps.get<vk::AndroidHardwareBufferFormatPropertiesANDROID>()
                .format,
        .extent =
            {static_cast<uint32_t>(hardwareBufferDesc.width),
             static_cast<uint32_t>(hardwareBufferDesc.height),
             1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eSampled,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined
    };

    camTexture.image = device_.createImage(imageInfo);

    vk::ImportAndroidHardwareBufferInfoANDROID importBufferInfo{.buffer = buf};
    vk::MemoryDedicatedAllocateInfo dedicatedAllocateInfo{
        .pNext = &importBufferInfo, .image = *camTexture.image
    };
    vk::MemoryAllocateInfo allocInfo{
        .pNext = &dedicatedAllocateInfo,
        .allocationSize =
            hwBufProps.get<vk::AndroidHardwareBufferPropertiesANDROID>()
                .allocationSize,
        .memoryTypeIndex = findMemoryType(
            hwBufProps.get<vk::AndroidHardwareBufferPropertiesANDROID>()
                .memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        )
    };

    camTexture.memory = device_.allocateMemory(allocInfo);

    vk::BindImageMemoryInfo bindInfo{
        .image = *camTexture.image,
        .memory = *camTexture.memory,
        .memoryOffset = 0
    };

    device_.bindImageMemory2(bindInfo);

    vk::SamplerYcbcrConversionInfo samplerYcbcrConversionInfo{
        .conversion = *camTexConversion_
    };

    vk::ImageViewCreateInfo viewInfo{
        .pNext = &samplerYcbcrConversionInfo,
        .image = *camTexture.image,
        .viewType = vk::ImageViewType::e2D,
        .format =
            hwBufProps.get<vk::AndroidHardwareBufferFormatPropertiesANDROID>()
                .format,
        //            .components = {.r =
        //            vk::ComponentSwizzle::eR,
        //                           .g =
        //                           vk::ComponentSwizzle::eG,
        //                           .b =
        //                           vk::ComponentSwizzle::eB,
        //                           .a =
        //                           vk::ComponentSwizzle::eA},
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    camTexture.imageView = device_.createImageView(viewInfo);

    transitionImageLayout(
        camTexture.image,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eShaderReadOnlyOptimal
    );

    // vk::DescriptorImageInfo watDescriptorImageInfo{
    //     .sampler = *watTextureSampler,
    //     .imageView = *watTextures[currentFrame].imageView,
    //     .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    // };
    vk::DescriptorImageInfo camDescriptorImageInfo{
        .sampler = *camTextureSampler_,
        .imageView = *camTexture.imageView,
        .imageLayout = vk::ImageLayout::eGeneral
    };

    vk::WriteDescriptorSet descriptorWrites{
        .dstSet = *descriptorSets_[currentFrame_],
        .dstBinding = 2,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .pImageInfo = &camDescriptorImageInfo
    };

    device_.updateDescriptorSets(descriptorWrites, nullptr);

    // if (watTextures.size() > 0) {
    //     vk::DescriptorImageInfo watDescriptorImageInfo{
    //         .sampler = *watTextureSampler,
    //         .imageView = *watTextures[currentFrame].imageView,
    //         .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    //     };
    //
    //     vk::WriteDescriptorSet watDescriptorWrites{
    //         .dstSet = *descriptorSets[currentFrame],
    //         .dstBinding = 1,
    //         .dstArrayElement = 0,
    //         .descriptorCount = 1,
    //         .descriptorType = vk::DescriptorType::eCombinedImageSampler,
    //         .pImageInfo = &watDescriptorImageInfo
    //     };
    //
    //     device.updateDescriptorSets(watDescriptorWrites, nullptr);
    // }

    commandBuffers_[currentFrame_].bindPipeline(
        vk::PipelineBindPoint::eGraphics, *graphicsPipeline_
    );

    vk::Viewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(swapChainExtent_.width),
        .height = static_cast<float>(swapChainExtent_.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    commandBuffers_[currentFrame_].setViewport(0, viewport);

    vk::Rect2D scissor{.offset = {0, 0}, .extent = swapChainExtent_};
    commandBuffers_[currentFrame_].setScissor(0, scissor);

    commandBuffers_[currentFrame_].bindVertexBuffers(0, {*vertexBuffer_}, {0});
    commandBuffers_[currentFrame_].bindIndexBuffer(
        *indexBuffer_,
        0,
        vk::IndexTypeValue<decltype(indices_)::value_type>::value
    );
    commandBuffers_[currentFrame_].bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *pipelineLayout_,
        0,
        {*descriptorSets_[currentFrame_]},
        nullptr
    );

    auto indexCount = static_cast<uint32_t>(indices_.size() / 2);
    if (watTextures_.empty()) {
        indexCount = static_cast<uint32_t>(indices_.size());
    }
    commandBuffers_[currentFrame_].drawIndexed(indexCount, 1, 0, 0, 0);

    commandBuffers_[currentFrame_].endRenderPass();
    commandBuffers_[currentFrame_].end();

    device_.resetFences({*inFlightFences_[currentFrame_]});

    vk::PipelineStageFlags waitDestinationStageMask(
        vk::PipelineStageFlagBits::eColorAttachmentOutput
    );
    vk::SubmitInfo submitInfo{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*imageAvailableSemaphores_[semaphoreIndex_],
        .pWaitDstStageMask = &waitDestinationStageMask,
        .commandBufferCount = 1,
        .pCommandBuffers = &*commandBuffers_[currentFrame_],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &*renderFinishedSemaphores_[imageIndex]
    };
    queue_.submit(submitInfo, *inFlightFences_[currentFrame_]);

    vk::PresentInfoKHR presentInfoKHR{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*renderFinishedSemaphores_[imageIndex],
        .swapchainCount = 1,
        .pSwapchains = &*swapChain_,
        .pImageIndices = &imageIndex
    };

    vk::Result result;
    try {
        result = queue_.presentKHR(presentInfoKHR);
    } catch (vk::OutOfDateKHRError&) {
        result = vk::Result::eErrorOutOfDateKHR;
    }

    if (result == vk::Result::eErrorOutOfDateKHR ||
        result == vk::Result::eSuboptimalKHR || framebufferResized_) {
        framebufferResized_ = false;
        recreateSwapChain();
    } else if (result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to present swap chain image");
    }

    semaphoreIndex_ = (semaphoreIndex_ + 1) % imageAvailableSemaphores_.size();

    if (isRecording_) {
        while (vk::Result::eTimeout ==
               device_.waitForFences(
                   *mediaInFlightFences_[currentFrame_], vk::True, FENCE_TIMEOUT
               ));

        // uint32_t imageIndex;
        try {
            auto [_, idx] = mediaSwapChain_.acquireNextImage(
                FENCE_TIMEOUT,
                *mediaImageAvailableSemaphores_[mediaSemaphoreIndex_],
                nullptr
            );
            imageIndex = idx;
        } catch (vk::OutOfDateKHRError&) {
            // recreateSwapChain();
            return;
        }

        // Update uniform buffer with current transformation
        // updateUniformBuffer(currentFrame);

        // device.resetFences({*inFlightFences[currentFrame]});
        // commandBuffers[currentFrame].reset();

        // vk::CommandBufferBeginInfo beginInfo{};
        commandBuffers_[currentFrame_].begin(beginInfo);

        renderPassInfo = {
            .renderPass = *renderPass_,
            .framebuffer = *mediaSwapChainFramebuffers_[imageIndex],
            .renderArea = {.offset = {0, 0}, .extent = mediaSwapChainExtent_}
        };

        // vk::ClearValue clearColor;
        // clearColor.color.float32 = std::array<float, 4>{0.0f, 0.0f,
        // 0.0f, 1.0f}; renderPassInfo.clearValueCount = 1;
        // renderPassInfo.pClearValues = &clearColor;

        commandBuffers_[currentFrame_].beginRenderPass(
            renderPassInfo, vk::SubpassContents::eInline
        );

        commandBuffers_[currentFrame_].bindPipeline(
            vk::PipelineBindPoint::eGraphics, *graphicsPipeline_
        );

        viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(mediaSwapChainExtent_.width),
            .height = static_cast<float>(mediaSwapChainExtent_.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };
        commandBuffers_[currentFrame_].setViewport(0, viewport);

        scissor = {.offset = {0, 0}, .extent = mediaSwapChainExtent_};
        commandBuffers_[currentFrame_].setScissor(0, scissor);

        commandBuffers_[currentFrame_].bindVertexBuffers(
            0, {*vertexBuffer_}, {0}
        );
        commandBuffers_[currentFrame_].bindIndexBuffer(
            *indexBuffer_,
            0,
            vk::IndexTypeValue<decltype(indices_)::value_type>::value
        );
        commandBuffers_[currentFrame_].bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            *pipelineLayout_,
            0,
            {*descriptorSets_[currentFrame_]},
            nullptr
        );

        indexCount = static_cast<uint32_t>(indices_.size() / 2);
        if (watTextures_.empty()) {
            indexCount = static_cast<uint32_t>(indices_.size());
        }
        commandBuffers_[currentFrame_].drawIndexed(indexCount, 1, 0, 0, 0);

        commandBuffers_[currentFrame_].endRenderPass();
        commandBuffers_[currentFrame_].end();

        device_.resetFences({*mediaInFlightFences_[currentFrame_]});

        // vk::PipelineStageFlags waitDestinationStageMask(
        //     vk::PipelineStageFlagBits::eColorAttachmentOutput
        // );
        submitInfo = {
            .waitSemaphoreCount = 1,
            .pWaitSemaphores =
                &*mediaImageAvailableSemaphores_[mediaSemaphoreIndex_],
            .pWaitDstStageMask = &waitDestinationStageMask,
            .commandBufferCount = 1,
            .pCommandBuffers = &*commandBuffers_[currentFrame_],
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &*mediaRenderFinishedSemaphores_[imageIndex]
        };
        queue_.submit(submitInfo, *mediaInFlightFences_[currentFrame_]);

        presentInfoKHR = {
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*mediaRenderFinishedSemaphores_[imageIndex],
            .swapchainCount = 1,
            .pSwapchains = &*mediaSwapChain_,
            .pImageIndices = &imageIndex
        };

        try {
            result = queue_.presentKHR(presentInfoKHR);
        } catch (vk::OutOfDateKHRError&) {
            result = vk::Result::eErrorOutOfDateKHR;
        }

        if (result == vk::Result::eErrorOutOfDateKHR ||
            result == vk::Result::eSuboptimalKHR || framebufferResized_) {
            framebufferResized_ = false;
            recreateSwapChain();
        } else if (result != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to present swap chain image");
        }
        mediaSemaphoreIndex_ =
            (semaphoreIndex_ + 1) % imageAvailableSemaphores_.size();
    }

    currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VkRenderer::watHwBufferToTexture(AHardwareBuffer* buf) {
    device_.waitIdle();

    if (watTextures_.empty()) {
        logI("Resize watTextures");
        // watTextures.resize(MAX_FRAMES_IN_FLIGHT);
        watTextures_.resize(1);
    }

    TextureData& watTexture = watTextures_[0];

    auto hwBufProps = device_.getAndroidHardwareBufferPropertiesANDROID<
        vk::AndroidHardwareBufferPropertiesANDROID,
        vk::AndroidHardwareBufferFormatPropertiesANDROID>(*buf);
    // vk::ExternalFormatANDROID extFormatAndroid{
    //     .externalFormat =
    //         hwBufProps.get<vk::AndroidHardwareBufferFormatPropertiesANDROID>()
    //             .externalFormat
    // };

    vk::ExternalMemoryImageCreateInfo externalMemoryImageCreateInfo{
        // .pNext = &extFormatAndroid,
        .handleTypes =
            vk::ExternalMemoryHandleTypeFlagBits::eAndroidHardwareBufferANDROID
    };

    AHardwareBuffer_Desc hardwareBufferDesc;
    AHardwareBuffer_describe(buf, &hardwareBufferDesc);

    vk::ImageCreateInfo imageInfo{
        .pNext = &externalMemoryImageCreateInfo,
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR8G8B8A8Unorm,
        // .format =
        //     hwBufProps
        //         .get<vk::AndroidHardwareBufferFormatPropertiesANDROID>()
        //         .format,
        .extent =
            {static_cast<uint32_t>(hardwareBufferDesc.width),
             static_cast<uint32_t>(hardwareBufferDesc.height),
             1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eSampled,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined
    };

    watTexture.image = device_.createImage(imageInfo);

    vk::ImportAndroidHardwareBufferInfoANDROID importBufferInfo{.buffer = buf};
    vk::MemoryDedicatedAllocateInfo dedicatedAllocateInfo{
        .pNext = &importBufferInfo, .image = *watTexture.image
    };
    vk::MemoryAllocateInfo allocInfo{
        .pNext = &dedicatedAllocateInfo,
        .allocationSize =
            hwBufProps.get<vk::AndroidHardwareBufferPropertiesANDROID>()
                .allocationSize,
        .memoryTypeIndex = findMemoryType(
            hwBufProps.get<vk::AndroidHardwareBufferPropertiesANDROID>()
                .memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        )
    };

    watTexture.memory = device_.allocateMemory(allocInfo);

    vk::BindImageMemoryInfo bindInfo{
        .image = *watTexture.image,
        .memory = *watTexture.memory,
        .memoryOffset = 0
    };

    device_.bindImageMemory2(bindInfo);

    vk::ImageViewCreateInfo viewInfo{
        .image = *watTexture.image,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR8G8B8A8Unorm,
        // .format =
        //     hwBufProps
        //         .get<vk::AndroidHardwareBufferFormatPropertiesANDROID>()
        //         .format,
        //            .components = {.r =
        //            vk::ComponentSwizzle::eR,
        //                           .g =
        //                           vk::ComponentSwizzle::eG,
        //                           .b =
        //                           vk::ComponentSwizzle::eB,
        //                           .a =
        //                           vk::ComponentSwizzle::eA},
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    watTexture.imageView = device_.createImageView(viewInfo);

    transitionImageLayout(
        watTexture.image,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eShaderReadOnlyOptimal
    );

    vk::DescriptorImageInfo descriptorImageInfo{
        .sampler = *watTextureSampler_,
        .imageView = *watTexture.imageView,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };

    std::array descriptorWrites{
        vk::WriteDescriptorSet{
            .dstSet = *descriptorSets_[0],
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &descriptorImageInfo
        },
        vk::WriteDescriptorSet{
            .dstSet = *descriptorSets_[1],
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &descriptorImageInfo
        }
    };
    // vk::WriteDescriptorSet descriptorWrites{
    //     .dstSet = *descriptorSets[currentFrame],
    //     .dstBinding = 1,
    //     .dstArrayElement = 0,
    //     .descriptorCount = 1,
    //     .descriptorType = vk::DescriptorType::eCombinedImageSampler,
    //     .pImageInfo = &descriptorImageInfo
    // };

    device_.updateDescriptorSets(descriptorWrites, nullptr);
}

void VkRenderer::reset(ANativeWindow* newWindow, AAssetManager* newManager) {
    displayWindow_ = newWindow;
    assetManager_ = newManager;
    if (initialized) {
        device_.waitIdle();
        cleanupSwapChain();
        createSurface();
        createSwapChain();
        createImageViews();
        createFramebuffers();
    }
}

void VkRenderer::cleanup() {
    if (initialized) {
        // Wait for device to finish operations
        if (*device_) {
            device_.waitIdle();
        }

        // Cleanup resources
        cleanupSwapChain();

        initialized = false;
    }
}

void VkRenderer::createInstance() {
    vk::ApplicationInfo appInfo{
        .pApplicationName = "VkWatCam",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = vk::ApiVersion11
    };

    std::vector<const char*> extensions = {
        vk::KHRSurfaceExtensionName, vk::KHRAndroidSurfaceExtensionName
    };

    const std::vector validationLayers = {"VK_LAYER_KHRONOS_validation"};

    // Create instance
    vk::InstanceCreateInfo createInfo{
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = 1,
        .ppEnabledLayerNames = validationLayers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data()
    };

    instance_ = vk::raii::Instance(context_, createInfo);
    logI("Vulkan instance created");
}

void VkRenderer::createSurface() {
    vk::AndroidSurfaceCreateInfoKHR createInfo{
        .sType = vk::StructureType::eAndroidSurfaceCreateInfoKHR,
        .pNext = nullptr,
        .flags = vk::AndroidSurfaceCreateFlagsKHR(),
        .window = displayWindow_
    };

    displaySurface_ = vk::raii::SurfaceKHR(instance_, createInfo);
}

void VkRenderer ::pickPhysicalDevice() {
    std::vector<vk::raii::PhysicalDevice> devices =
        instance_.enumeratePhysicalDevices();
    const auto devIter = std::ranges::find_if(devices, [&](auto const& device) {
        // Check if any of the queue families support graphics operations
        auto queueFamilies = device.getQueueFamilyProperties();
        bool supportsGraphics =
            std::ranges::any_of(queueFamilies, [](auto const& qfp) {
                return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
            });

        // Check if all required device extensions
        // are available
        auto availableDeviceExtensions =
            device.enumerateDeviceExtensionProperties();
        bool supportsAllRequiredExtensions = std::ranges::all_of(
            deviceExtensions_,
            [&availableDeviceExtensions](auto const& requiredDeviceExtension) {
                return std::ranges::any_of(
                    availableDeviceExtensions,
                    [requiredDeviceExtension](
                        auto const& availableDeviceExtension
                    ) {
                        return strcmp(
                                   availableDeviceExtension.extensionName,
                                   requiredDeviceExtension
                               ) == 0;
                    }
                );
            }
        );

        return supportsGraphics && supportsAllRequiredExtensions;
    });

    if (devIter != devices.end()) {
        physicalDevice_ = *devIter;

        // Print device information
        vk::PhysicalDeviceProperties deviceProperties =
            physicalDevice_.getProperties();
        logI("Selected GPU: %s", deviceProperties.deviceName.data());
    } else {
        throw std::runtime_error("Failed to find a suitable GPU");
    }
}

void VkRenderer::createLogicalDevice() {
    std::vector<vk::QueueFamilyProperties> queueFamilyProperties =
        physicalDevice_.getQueueFamilyProperties();

    // Get the first index into queueFamilyProperties which supports both
    // graphics and present
    for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size();
         qfpIndex++) {
        if ((queueFamilyProperties[qfpIndex].queueFlags &
             vk::QueueFlagBits::eGraphics) &&
            physicalDevice_.getSurfaceSupportKHR(qfpIndex, *displaySurface_)) {
            // Found a queue family that supports both graphics and present
            queueIndex_ = qfpIndex;
            break;
        }
    }
    if (queueIndex_ == ~0u) {
        throw std::runtime_error(
            "Could not find a queue for graphics and present -> terminating"
        );
    }

    float queuePriority = 1.0f;
    vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
        .queueFamilyIndex = queueIndex_,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };

    // Manual device creation
    vk::PhysicalDeviceVulkan11Features vk11features{
        .samplerYcbcrConversion = vk::True, .shaderDrawParameters = vk::True
    };
    vk::PhysicalDeviceFeatures deviceFeatures{
        .sampleRateShading = vk::True,
        .samplerAnisotropy = vk::True,
    };

    vk::DeviceCreateInfo createInfo{
        .pNext = &vk11features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &deviceQueueCreateInfo,
        .enabledExtensionCount =
            static_cast<uint32_t>(deviceExtensions_.size()),
        .ppEnabledExtensionNames = deviceExtensions_.data(),
        .pEnabledFeatures = &deviceFeatures
    };

    device_ = vk::raii::Device(physicalDevice_, createInfo);

    queue_ = device_.getQueue(queueIndex_, 0);
}

void VkRenderer::createSwapChain() {
    SwapChainSupportDetails swapChainSupport =
        querySwapChainSupport(physicalDevice_, *displaySurface_);
    swapChainExtent_ = chooseSwapExtent(swapChainSupport.capabilities);
    swapChainSurfaceFormat_ = chooseSwapSurfaceFormat(swapChainSupport.formats);
    vk::SwapchainCreateInfoKHR swapChainCreateInfo{
        .surface = *displaySurface_,
        .minImageCount = chooseSwapMinImageCount(swapChainSupport.capabilities),
        .imageFormat = swapChainSurfaceFormat_.format,
        .imageColorSpace = swapChainSurfaceFormat_.colorSpace,
        .imageExtent = swapChainExtent_,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = swapChainSupport.capabilities.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eInherit,
        .presentMode = chooseSwapPresentMode(swapChainSupport.presentModes),
        .clipped = true
    };

    swapChain_ = device_.createSwapchainKHR(swapChainCreateInfo);
    swapChainImages_ = swapChain_.getImages();
}

void VkRenderer::createImageViews() {
    assert(swapChainImageViews_.empty());
    swapChainImageViews_.reserve(swapChainImages_.size());

    for (const auto& image : swapChainImages_) {
        vk::ImageViewCreateInfo createInfo{
            .image = image,
            .viewType = vk::ImageViewType::e2D,
            .format = swapChainSurfaceFormat_.format,
            .components =
                {.r = vk::ComponentSwizzle::eIdentity,
                 .g = vk::ComponentSwizzle::eIdentity,
                 .b = vk::ComponentSwizzle::eIdentity,
                 .a = vk::ComponentSwizzle::eIdentity},
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        swapChainImageViews_.emplace_back(device_.createImageView(createInfo));
    }
}

void VkRenderer::createRenderPass() {
    vk::AttachmentDescription colorAttachment{
        .format = swapChainSurfaceFormat_.format,
        .samples = vk::SampleCountFlagBits::e1,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout = vk::ImageLayout::eUndefined,
        .finalLayout = vk::ImageLayout::ePresentSrcKHR
    };

    vk::AttachmentReference colorAttachmentRef{
        .attachment = 0, .layout = vk::ImageLayout::eColorAttachmentOptimal
    };

    vk::SubpassDescription subpass{
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentRef
    };

    vk::SubpassDependency dependency{
        .srcSubpass = vk::SubpassExternal,
        .dstSubpass = 0,
        .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits::eNone,
        .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite
    };

    vk::RenderPassCreateInfo renderPassInfo{
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency
    };

    renderPass_ = device_.createRenderPass(renderPassInfo);
}

void VkRenderer::createDescriptorSetLayout() {
    vk::DescriptorSetLayoutBinding uboLayoutBinding{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex
    };

    vk::DescriptorSetLayoutBinding watSamplerLayoutBinding{
        .binding = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    };

    vk::DescriptorSetLayoutBinding camSamplerLayoutBinding{
        .binding = 2,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment,
        .pImmutableSamplers = &*camTextureSampler_
    };

    std::array bindings = {
        uboLayoutBinding, watSamplerLayoutBinding, camSamplerLayoutBinding
    };

    vk::DescriptorBindingFlags bindingFlags{
        vk::DescriptorBindingFlagBits::eUpdateAfterBind
    };
    vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfo{
        .pBindingFlags = &bindingFlags
    };
    vk::DescriptorSetLayoutCreateInfo layoutInfo{
        .pNext = &bindingFlagsCreateInfo,
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data()
    };

    descriptorSetLayout_ = device_.createDescriptorSetLayout(layoutInfo);
}
std::vector<char> VkRenderer::readFile(const std::string& filename) {
    // Open the asset
    AAsset* asset =
        AAssetManager_open(assetManager_, filename.c_str(), AASSET_MODE_BUFFER);
    if (!asset) {
        logE("Failed to open asset: %s", filename.c_str());
        throw std::runtime_error("Failed to open file: " + filename);
    }

    // Get the file size
    off_t fileSize = AAsset_getLength(asset);
    std::vector<char> buf(fileSize);

    // Read the file data
    AAsset_read(asset, buf.data(), fileSize);

    // Close the asset
    AAsset_close(asset);

    return buf;
}

[[nodiscard]] vk::raii::ShaderModule VkRenderer::createShaderModule(
    const std::vector<char>& code
) const {
    vk::ShaderModuleCreateInfo createInfo{
        .codeSize = code.size() * sizeof(char),
        .pCode = reinterpret_cast<const uint32_t*>(code.data())
    };
    vk::raii::ShaderModule shaderModule{device_, createInfo};

    return shaderModule;
}

void VkRenderer::createGraphicsPipeline() {
    logI("Loading shaders from assets");

    auto shaderModule = createShaderModule(readFile("shaders/tex.spv"));

    logI("Shaders loaded successfully");

    vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = *shaderModule,
        .pName = "vertMain"
    };
    vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = *shaderModule,
        .pName = "fragMain"
    };
    vk::PipelineShaderStageCreateInfo shaderStages[] = {
        vertShaderStageInfo, fragShaderStageInfo
    };

    // Vertex input
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDescription,
        .vertexAttributeDescriptionCount =
            static_cast<uint32_t>(attributeDescriptions.size()),
        .pVertexAttributeDescriptions = attributeDescriptions.data()
    };

    // Input assembly
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology = vk::PrimitiveTopology::eTriangleList,
        .primitiveRestartEnable = vk::False
    };

    // Viewport and scissor
    vk::PipelineViewportStateCreateInfo viewportState{
        .viewportCount = 1, .scissorCount = 1
    };

    // Rasterization
    vk::PipelineRasterizationStateCreateInfo rasterizer{
        .depthClampEnable = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eClockwise,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0f
    };

    // Multisampling
    vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False
    };

    // Color blending
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = vk::True,
        .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    };

    vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment
    };

    // Dynamic states
    std::vector<vk::DynamicState> dynamicStates = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    // Pipeline layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
        .setLayoutCount = 1, .pSetLayouts = &*descriptorSetLayout_
    };

    pipelineLayout_ = device_.createPipelineLayout(pipelineLayoutInfo);

    // Create the graphics pipeline
    vk::GraphicsPipelineCreateInfo pipelineInfo{
        .stageCount = 2,
        .pStages = shaderStages,
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = nullptr,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = *pipelineLayout_,
        .renderPass = *renderPass_,
        .subpass = 0
    };

    // Create the pipeline
    graphicsPipeline_ = device_.createGraphicsPipeline(nullptr, pipelineInfo);
}

void VkRenderer::createFramebuffers() {
    assert(swapChainFramebuffers_.empty());
    swapChainFramebuffers_.reserve(swapChainImageViews_.size());

    for (const auto& swapChainImageView : swapChainImageViews_) {
        vk::ImageView attachments[] = {*swapChainImageView};

        vk::FramebufferCreateInfo framebufferInfo{
            .renderPass = *renderPass_,
            .attachmentCount = 1,
            .pAttachments = attachments,
            .width = swapChainExtent_.width,
            .height = swapChainExtent_.height,
            .layers = 1
        };

        swapChainFramebuffers_.emplace_back(
            device_.createFramebuffer(framebufferInfo)
        );
    }
}

void VkRenderer::createCommandPool() {
    vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = queueIndex_
    };

    commandPool_ = device_.createCommandPool(poolInfo);
}

void VkRenderer::createTextureSamplers() {
    // vk::PhysicalDeviceProperties properties =
    //     physicalDevice.getProperties();
    vk::SamplerCreateInfo samplerInfo{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .mipLodBias = 0.0f,
        .anisotropyEnable = vk::False,
        .maxAnisotropy = 1,
        .compareEnable = vk::False,
        .compareOp = vk::CompareOp::eNever,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = vk::BorderColor::eFloatOpaqueWhite,
        .unnormalizedCoordinates = vk::False
    };

    watTextureSampler_ = device_.createSampler(samplerInfo);

    // todo: create with correct format
    vk::ExternalFormatANDROID extFormatAndroid{.externalFormat = 647};

    vk::SamplerYcbcrConversionCreateInfo samplerYcbcrConversionCreateInfo{
        .pNext = &extFormatAndroid,
        .format = vk::Format::eUndefined,
        .ycbcrModel = vk::SamplerYcbcrModelConversion::eYcbcr709,
        .ycbcrRange = vk::SamplerYcbcrRange::eItuFull,
        .components =
            {vk::ComponentSwizzle::eB,
             vk::ComponentSwizzle::eIdentity,
             vk::ComponentSwizzle::eR,
             vk::ComponentSwizzle::eIdentity},
        .xChromaOffset = vk::ChromaLocation::eCositedEven,
        .yChromaOffset = vk::ChromaLocation::eCositedEven,
        .chromaFilter = vk::Filter::eLinear,
        .forceExplicitReconstruction = vk::False
    };

    camTexConversion_ =
        device_.createSamplerYcbcrConversion(samplerYcbcrConversionCreateInfo);

    vk::SamplerYcbcrConversionInfo samplerYcbcrConversionInfo{
        .conversion = *camTexConversion_
    };
    samplerInfo.pNext = &samplerYcbcrConversionInfo;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.minLod = 0.0f;
    samplerInfo.anisotropyEnable = vk::False;
    samplerInfo.maxAnisotropy = 1;
    samplerInfo.compareOp = vk::CompareOp::eNever;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;

    camTextureSampler_ = device_.createSampler(samplerInfo);
}

void VkRenderer::createVertexBuffer() {
    vk::DeviceSize bufferSize = sizeof(vertices_[0]) * vertices_.size();

    vk::raii::Buffer stagingBuffer = nullptr;
    vk::raii::DeviceMemory stagingBufferMemory = nullptr;
    createBuffer(
        bufferSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        stagingBuffer,
        stagingBufferMemory
    );

    void* data = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(data, vertices_.data(), (size_t)bufferSize);
    stagingBufferMemory.unmapMemory();

    createBuffer(
        bufferSize,
        vk::BufferUsageFlagBits::eTransferDst |
            vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vertexBuffer_,
        vertexBufferMemory_
    );

    copyBuffer(stagingBuffer, vertexBuffer_, bufferSize);
}

void VkRenderer::createIndexBuffer() {
    vk::DeviceSize bufferSize = sizeof(indices_[0]) * indices_.size();

    vk::raii::Buffer stagingBuffer = nullptr;
    vk::raii::DeviceMemory stagingBufferMemory = nullptr;
    createBuffer(
        bufferSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        stagingBuffer,
        stagingBufferMemory
    );

    void* data = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(data, indices_.data(), (size_t)bufferSize);
    stagingBufferMemory.unmapMemory();

    createBuffer(
        bufferSize,
        vk::BufferUsageFlagBits::eTransferDst |
            vk::BufferUsageFlagBits::eIndexBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        indexBuffer_,
        indexBufferMemory_
    );

    copyBuffer(stagingBuffer, indexBuffer_, bufferSize);
}

void VkRenderer::createUniformBuffers() {
    vk::DeviceSize bufferSize = sizeof(UniformBufferObject);

    uniformBuffers_.clear();
    uniformBuffersMemory_.clear();

    // todo: see desktop tutor
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        uniformBuffers_.push_back(nullptr);
        uniformBuffersMemory_.push_back(nullptr);
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        createBuffer(
            bufferSize,
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent,
            uniformBuffers_[i],
            uniformBuffersMemory_[i]
        );
    }
}

void VkRenderer::createDescriptorPool() {
    std::array poolSizes = {
        vk::DescriptorPoolSize{
            .type = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT)
        },
        vk::DescriptorPoolSize{
            .type = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT)
        },
        vk::DescriptorPoolSize{
            .type = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT)
        }
    };

    vk::DescriptorPoolCreateInfo poolInfo{
        .maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data()
    };

    descriptorPool_ = device_.createDescriptorPool(poolInfo);
}

void VkRenderer::createDescriptorSets() {
    std::vector<vk::DescriptorSetLayout> layouts(
        MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout_
    );
    vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = *descriptorPool_,
        .descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
        .pSetLayouts = layouts.data()
    };

    descriptorSets_ = device_.allocateDescriptorSets(allocInfo);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk::DescriptorBufferInfo bufferInfo{
            .buffer = *uniformBuffers_[i],
            .offset = 0,
            .range = sizeof(UniformBufferObject)
        };

        // vk::DescriptorImageInfo imageInfo{
        //     .sampler = *watTextureSampler,
        //     .imageView = *watTextures[0].imageView,
        //     .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        // };

        std::array descriptorWrites{
            vk::WriteDescriptorSet{
                .dstSet = *descriptorSets_[i],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo = &bufferInfo
            },
            // vk::WriteDescriptorSet{
            //     .dstSet = *descriptorSets[i],
            //     .dstBinding = 1,
            //     .dstArrayElement = 0,
            //     .descriptorCount = 1,
            //     .descriptorType =
            //     vk::DescriptorType::eCombinedImageSampler, .pImageInfo =
            //     &imageInfo
            // }
        };

        device_.updateDescriptorSets(descriptorWrites, nullptr);
    }
}

void VkRenderer::createCommandBuffers() {
    commandBuffers_.reserve(MAX_FRAMES_IN_FLIGHT);

    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = *commandPool_,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT)
    };

    commandBuffers_ = device_.allocateCommandBuffers(allocInfo);
}

void VkRenderer::createSyncObjects() {
    imageAvailableSemaphores_.clear();
    renderFinishedSemaphores_.clear();
    inFlightFences_.clear();

    for (size_t i = 0; i < swapChainImages_.size(); ++i) {
        imageAvailableSemaphores_.emplace_back(
            device_, vk::SemaphoreCreateInfo()
        );
        renderFinishedSemaphores_.emplace_back(
            device_, vk::SemaphoreCreateInfo()
        );
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        inFlightFences_.emplace_back(
            device_,
            vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled}
        );
    }
}

void VkRenderer::cleanupSwapChain() {
    for (auto& framebuffer : swapChainFramebuffers_) {
        framebuffer = nullptr;
    }

    for (auto& imageView : swapChainImageViews_) {
        imageView = nullptr;
    }

    swapChainImageViews_.clear();
    swapChainFramebuffers_.clear();
    swapChain_ = nullptr;
}

void VkRenderer::recreateSwapChain() {
    // Wait for device to finish operations
    device_.waitIdle();

    // Clean up old swap chain
    cleanupSwapChain();

    // Create new swap chain
    createSwapChain();
    createImageViews();
    createFramebuffers();
}

uint32_t VkRenderer::chooseSwapMinImageCount(
    vk::SurfaceCapabilitiesKHR const& surfaceCapabilities
) {
    auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
    if ((0 < surfaceCapabilities.maxImageCount) &&
        (surfaceCapabilities.maxImageCount < minImageCount)) {
        minImageCount = surfaceCapabilities.maxImageCount;
    }
    return minImageCount;
}

vk::SurfaceFormatKHR VkRenderer::chooseSwapSurfaceFormat(
    const std::vector<vk::SurfaceFormatKHR>& availableFormats
) {
    assert(!availableFormats.empty());
    const auto formatIt =
        std::ranges::find_if(availableFormats, [](const auto& format) {
            return format.format == vk::Format::eB8G8R8A8Srgb &&
                   format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
        });
    return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
}

vk::PresentModeKHR VkRenderer::chooseSwapPresentMode(
    const std::vector<vk::PresentModeKHR>& availablePresentModes
) {
    assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) {
        return presentMode == vk::PresentModeKHR::eFifo;
    }));
    return std::ranges::any_of(
               availablePresentModes,
               [](const vk::PresentModeKHR value) {
                   return vk::PresentModeKHR::eMailbox == value;
               }
           )
               ? vk::PresentModeKHR::eMailbox
               : vk::PresentModeKHR::eFifo;
}

vk::Extent2D VkRenderer::chooseSwapExtent(
    const vk::SurfaceCapabilitiesKHR& capabilities
) {
    if (capabilities.currentExtent.width != 0xFFFFFFFF) {
        return capabilities.currentExtent;
    } else {
        int32_t width = ANativeWindow_getWidth(displayWindow_);
        int32_t height = ANativeWindow_getHeight(displayWindow_);

        vk::Extent2D actualExtent = {
            static_cast<uint32_t>(width), static_cast<uint32_t>(height)
        };

        actualExtent.width = std::clamp(
            actualExtent.width,
            capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width
        );
        actualExtent.height = std::clamp(
            actualExtent.height,
            capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height
        );

        return actualExtent;
    }
}

VkRenderer::SwapChainSupportDetails VkRenderer::querySwapChainSupport(
    const vk::raii::PhysicalDevice& device, const vk::SurfaceKHR& surface
) {
    SwapChainSupportDetails details;
    details.capabilities = device.getSurfaceCapabilitiesKHR(surface);
    details.formats = device.getSurfaceFormatsKHR(surface);
    details.presentModes = device.getSurfacePresentModesKHR(surface);
    return details;
}

void VkRenderer::createBuffer(
    vk::DeviceSize size,
    vk::BufferUsageFlags usage,
    vk::MemoryPropertyFlags properties,
    vk::raii::Buffer& buffer,
    vk::raii::DeviceMemory& bufferMemory
) {
    vk::BufferCreateInfo bufferInfo{
        .size = size, .usage = usage, .sharingMode = vk::SharingMode::eExclusive
    };

    buffer = device_.createBuffer(bufferInfo);

    vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();

    vk::MemoryAllocateInfo allocInfo{
        .allocationSize = memRequirements.size,
        .memoryTypeIndex =
            findMemoryType(memRequirements.memoryTypeBits, properties)
    };

    bufferMemory = device_.allocateMemory(allocInfo);
    buffer.bindMemory(*bufferMemory, 0);
}

void VkRenderer::copyBuffer(
    vk::raii::Buffer& srcBuffer,
    vk::raii::Buffer& dstBuffer,
    vk::DeviceSize size
) {
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = *commandPool_,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1
    };

    vk::raii::CommandBuffer commandBuffer =
        std::move(device_.allocateCommandBuffers(allocInfo)[0]);

    vk::CommandBufferBeginInfo beginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    };
    commandBuffer.begin(beginInfo);

    vk::BufferCopy copyRegion{.srcOffset = 0, .dstOffset = 0, .size = size};
    commandBuffer.copyBuffer(*srcBuffer, *dstBuffer, copyRegion);

    commandBuffer.end();

    vk::SubmitInfo submitInfo{
        .commandBufferCount = 1, .pCommandBuffers = &*commandBuffer
    };

    queue_.submit(submitInfo, nullptr);
    queue_.waitIdle();
}

uint32_t VkRenderer::findMemoryType(
    uint32_t typeFilter, vk::MemoryPropertyFlags properties
) {
    vk::PhysicalDeviceMemoryProperties memProperties =
        physicalDevice_.getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) ==
                properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type");
}

vk::raii::ImageView VkRenderer::createImageView(
    vk::raii::Image& image, vk::Format format
) {
    vk::ImageViewCreateInfo viewInfo{
        .image = *image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    return device_.createImageView(viewInfo);
}

void VkRenderer::transitionImageLayout(
    vk::raii::Image& image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout
) {
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = *commandPool_,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1
    };

    vk::raii::CommandBuffer commandBuffer =
        std::move(device_.allocateCommandBuffers(allocInfo)[0]);

    vk::CommandBufferBeginInfo beginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    };
    commandBuffer.begin(beginInfo);

    vk::ImageMemoryBarrier barrier{
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = *image,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    vk::PipelineStageFlags sourceStage;
    vk::PipelineStageFlags destinationStage;

    if (oldLayout == vk::ImageLayout::eUndefined &&
        newLayout == vk::ImageLayout::eTransferDstOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eNone;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
        destinationStage = vk::PipelineStageFlagBits::eTransfer;
    } else if (
        oldLayout == vk::ImageLayout::eTransferDstOptimal &&
        newLayout == vk::ImageLayout::eShaderReadOnlyOptimal
    ) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        sourceStage = vk::PipelineStageFlagBits::eTransfer;
        destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else if (
        oldLayout == vk::ImageLayout::eUndefined &&
        newLayout == vk::ImageLayout::eShaderReadOnlyOptimal
    ) {
        barrier.srcAccessMask = vk::AccessFlagBits::eNone;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        sourceStage = vk::PipelineStageFlagBits::eHost;
        destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else {
        throw std::invalid_argument("Unsupported layout transition");
    }

    commandBuffer.pipelineBarrier(
        sourceStage,
        destinationStage,
        vk::DependencyFlagBits::eByRegion,
        nullptr,
        nullptr,
        barrier
    );

    commandBuffer.end();

    vk::SubmitInfo submitInfo{
        .commandBufferCount = 1, .pCommandBuffers = &*commandBuffer
    };

    queue_.submit(submitInfo, nullptr);
    queue_.waitIdle();
}

void VkRenderer::copyBufferToImage(
    vk::raii::Buffer& buffer,
    vk::raii::Image& image,
    uint32_t width,
    uint32_t height
) {
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = *commandPool_,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1
    };

    vk::raii::CommandBuffer commandBuffer =
        std::move(device_.allocateCommandBuffers(allocInfo)[0]);

    vk::CommandBufferBeginInfo beginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    };
    commandBuffer.begin(beginInfo);

    vk::BufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource =
            {.aspectMask = vk::ImageAspectFlagBits::eColor,
             .mipLevel = 0,
             .baseArrayLayer = 0,
             .layerCount = 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1}
    };

    commandBuffer.copyBufferToImage(
        *buffer, *image, vk::ImageLayout::eTransferDstOptimal, region
    );

    commandBuffer.end();

    vk::SubmitInfo submitInfo{
        .commandBufferCount = 1, .pCommandBuffers = &*commandBuffer
    };

    queue_.submit(submitInfo, nullptr);
    queue_.waitIdle();
}

void VkRenderer::updateUniformBuffer(uint32_t currentImage) {
    // static auto startTime = std::chrono::high_resolution_clock::now();
    //
    // auto currentTime = std::chrono::high_resolution_clock::now();
    // float time = std::chrono::duration<float>(currentTime -
    // startTime).count();

    // auto angle = time * glm::radians(45.0f);
    // static const float maxEyeY = 3.0f;
    // float eyeY = 0.0f;
    // int i = static_cast<int>(time * maxEyeY) / static_cast<int>(maxEyeY);
    // if (i % 2 == 0)
    //     eyeY = time * maxEyeY - i * maxEyeY;
    // else
    //     eyeY = maxEyeY - (time * maxEyeY - i * maxEyeY);

    UniformBufferObject ubo{};
    // ubo.camModel = glm::identity<glm::mat4>();
    ubo.camModel = glm::rotate(
        glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)
    );
    ubo.watModel = glm::translate(glm::mat4(1), glm::vec3(0.0f, 0.0f, 0.0f)) *
                   glm::rotate(
                       glm::mat4(1),
                       /*angle*/ glm::radians(0.0f),
                       glm::vec3(0.0f, 0.0f, 1.0f)
                   ) *
                   glm::scale(glm::mat4(1), glm::vec3(1.0f, 1.0f, 0.0f));
    ubo.view = glm::lookAt(
        glm::vec3(0.0f, /*eyeY maxEyeY*/ 0.0f, 4.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    ubo.proj = glm::perspective(
        glm::radians(45.0f),
        static_cast<float>(swapChainExtent_.width) /
            static_cast<float>(swapChainExtent_.height),
        0.1f,
        10.0f
    );
    //         ubo.proj[1][1] *= -1;

    void* data = uniformBuffersMemory_[currentImage].mapMemory(0, sizeof(ubo));
    memcpy(data, &ubo, sizeof(ubo));
    uniformBuffersMemory_[currentImage].unmapMemory();
}

}  // namespace camera
