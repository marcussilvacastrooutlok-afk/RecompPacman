/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include <rex/assert.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <rex/platform.h>
#include <rex/ui/vulkan/presenter.h>
#include <rex/ui/vulkan/util.h>

#if defined(REX_HAS_FIDELITYFX_RUNTIME) && REX_HAS_FIDELITYFX_RUNTIME
#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/vk/ffx_api_vk.h>
#endif

#if REX_PLATFORM_ANDROID
// CORREÇÃO: Ficheiro não existe no SDK original. Ignorado para permitir compilação.
// #include <rex/ui/surface_android.h>
#endif
#if REX_PLATFORM_GNU_LINUX
#include <rex/ui/surface_gnulinux.h>
#endif
#if REX_PLATFORM_WIN32
#include <rex/ui/surface_win.h>
#endif
#if REX_PLATFORM_MAC
#include <rex/ui/surface_mac.h>
#endif

REXCVAR_DEFINE_BOOL(present_render_pass_clear, true, "UI/Presenter",
                    "Clear render pass during presentation");

REXCVAR_DEFINE_BOOL(vulkan_allow_present_mode_immediate, true, "UI/Vulkan",
                    "Allow immediate present mode (no vsync)");

REXCVAR_DEFINE_BOOL(vulkan_allow_present_mode_mailbox, true, "UI/Vulkan",
                    "Allow mailbox present mode (triple buffering)");

REXCVAR_DEFINE_BOOL(vulkan_allow_present_mode_fifo_relaxed, true, "UI/Vulkan",
                    "Allow FIFO relaxed present mode");

namespace rex {
namespace ui {
namespace vulkan {

#if defined(REX_HAS_FIDELITYFX_RUNTIME) && REX_HAS_FIDELITYFX_RUNTIME
namespace {

std::atomic<PFN_vkGetDeviceProcAddr> g_ffx_vk_get_device_proc_addr{nullptr};

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL FfxVkGetDeviceProcAddrCompat(VkDevice device,
                                                                      const char* p_name) {
  if (!p_name) {
    return nullptr;
  }

  PFN_vkGetDeviceProcAddr get_device_proc_addr =
      g_ffx_vk_get_device_proc_addr.load(std::memory_order_relaxed);
  if (!get_device_proc_addr) {
    return nullptr;
  }

  PFN_vkVoidFunction proc = get_device_proc_addr(device, p_name);
  if (proc) {
    return proc;
  }

  if (!std::strcmp(p_name, "vkGetBufferMemoryRequirements2KHR")) {
    return get_device_proc_addr(device, "vkGetBufferMemoryRequirements2");
  }
  if (!std::strcmp(p_name, "vkGetImageMemoryRequirements2KHR")) {
    return get_device_proc_addr(device, "vkGetImageMemoryRequirements2");
  }
  if (!std::strcmp(p_name, "vkBindBufferMemory2KHR")) {
    return get_device_proc_addr(device, "vkBindBufferMemory2");
  }
  if (!std::strcmp(p_name, "vkBindImageMemory2KHR")) {
    return get_device_proc_addr(device, "vkBindImageMemory2");
  }

  return nullptr;
}

}  // namespace
#endif  // defined(REX_HAS_FIDELITYFX_RUNTIME) && REX_HAS_FIDELITYFX_RUNTIME

// Generated with `xb buildshaders`.
namespace shaders {
#include "../shaders/vulkan_spirv/guest_output_bilinear_dither_ps.h"
#include "../shaders/vulkan_spirv/guest_output_bilinear_ps.h"
#if defined(REX_HAS_FIDELITYFX_SDK)
#include "../shaders/vulkan_spirv/guest_output_ffx_cas_resample_dither_ps.h"
#include "../shaders/vulkan_spirv/guest_output_ffx_cas_resample_ps.h"
#include "../shaders/vulkan_spirv/guest_output_ffx_cas_sharpen_dither_ps.h"
#include "../shaders/vulkan_spirv/guest_output_ffx_cas_sharpen_ps.h"
#include "../shaders/vulkan_spirv/guest_output_ffx_fsr_easu_ps.h"
#include "../shaders/vulkan_spirv/guest_output_ffx_fsr_rcas_dither_ps.h"
#include "../shaders/vulkan_spirv/guest_output_ffx_fsr_rcas_ps.h"
#endif
#include "../shaders/vulkan_spirv/guest_output_triangle_strip_rect_vs.h"
}  // namespace shaders

VulkanPresenter::PaintContext::Submission::~Submission() {
  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();

  if (draw_command_pool_ != VK_NULL_HANDLE) {
    dfn.vkDestroyCommandPool(device, draw_command_pool_, nullptr);
  }

  if (present_semaphore_ != VK_NULL_HANDLE) {
    dfn.vkDestroySemaphore(device, present_semaphore_, nullptr);
  }
  if (acquire_semaphore_ != VK_NULL_HANDLE) {
    dfn.vkDestroySemaphore(device, acquire_semaphore_, nullptr);
  }
}

bool VulkanPresenter::PaintContext::Submission::Initialize() {
  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();

  VkSemaphoreCreateInfo semaphore_create_info;
  semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  semaphore_create_info.pNext = nullptr;
  semaphore_create_info.flags = 0;
  if (dfn.vkCreateSemaphore(device, &semaphore_create_info, nullptr, &acquire_semaphore_) !=
      VK_SUCCESS) {
    REXLOG_ERROR(
        "VulkanPresenter: Failed to create a swapchain image acquisition "
        "semaphore");
    return false;
  }
  if (dfn.vkCreateSemaphore(device, &semaphore_create_info, nullptr, &present_semaphore_) !=
      VK_SUCCESS) {
    REXLOG_ERROR(
        "VulkanPresenter: Failed to create a swapchain image presentation "
        "semaphore");
    return false;
  }

  VkCommandPoolCreateInfo command_pool_create_info;
  command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  command_pool_create_info.pNext = nullptr;
  command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  command_pool_create_info.queueFamilyIndex = vulkan_device_->queue_family_graphics_compute();
  if (dfn.vkCreateCommandPool(device, &command_pool_create_info, nullptr, &draw_command_pool_) !=
      VK_SUCCESS) {
    REXLOG_ERROR(
        "VulkanPresenter: Failed to create a command pool for drawing to a "
        "swapchain");
    return false;
  }
  VkCommandBufferAllocateInfo command_buffer_allocate_info;
  command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  command_buffer_allocate_info.pNext = nullptr;
  command_buffer_allocate_info.commandPool = draw_command_pool_;
  command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_buffer_allocate_info.commandBufferCount = 1;
  if (dfn.vkAllocateCommandBuffers(device, &command_buffer_allocate_info, &draw_command_buffer_) !=
      VK_SUCCESS) {
    REXLOG_ERROR(
        "VulkanPresenter: Failed to allocate a command buffer for drawing to a "
        "swapchain");
    return false;
  }

  return true;
}

VulkanPresenter::~VulkanPresenter() {
  paint_context_.DestroySwapchainAndVulkanSurface();

  ui_submission_tracker_.Shutdown();
  guest_output_image_refresher_submission_tracker_.Shutdown();
#if defined(REX_HAS_FIDELITYFX_RUNTIME) && REX_HAS_FIDELITYFX_RUNTIME
  DestroyTemporalUpscalerContext();
#endif

  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();

  if (paint_context_.swapchain_render_pass != VK_NULL_HANDLE) {
    dfn.vkDestroyRenderPass(device, paint_context_.swapchain_render_pass, nullptr);
  }

  for (const PaintContext::UISetupCommandBuffer& ui_setup_command_buffer :
       paint_context_.ui_setup_command_buffers) {
    dfn.vkDestroyCommandPool(device, ui_setup_command_buffer.command_pool, nullptr);
  }

  for (VkFramebuffer& framebuffer : paint_context_.guest_output_intermediate_framebuffers) {
    util::DestroyAndNullHandle(dfn.vkDestroyFramebuffer, device, framebuffer);
  }
  util::DestroyAndNullHandle(dfn.vkDestroyDescriptorPool, device,
                             paint_context_.guest_output_descriptor_pool);
  for (PaintContext::GuestOutputPaintPipeline& guest_output_paint_pipeline :
       paint_context_.guest_output_paint_pipelines) {
    util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device,
                               guest_output_paint_pipeline.swapchain_pipeline);
    util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device,
                               guest_output_paint_pipeline.intermediate_pipeline);
  }

  util::DestroyAndNullHandle(dfn.vkDestroyRenderPass, device,
                             guest_output_intermediate_render_pass_);
  for (VkShaderModule& shader_module : guest_output_paint_fs_) {
    util::DestroyAndNullHandle(dfn.vkDestroyShaderModule, device, shader_module);
  }
  util::DestroyAndNullHandle(dfn.vkDestroyShaderModule, device, guest_output_paint_vs_);
  for (VkPipelineLayout& pipeline_layout : guest_output_paint_pipeline_layouts_) {
    util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device, pipeline_layout);
  }
  util::DestroyAndNullHandle(dfn.vkDestroyDescriptorSetLayout, device,
                             guest_output_paint_image_descriptor_set_layout_);
}

#if defined(REX_HAS_FIDELITYFX_RUNTIME) && REX_HAS_FIDELITYFX_RUNTIME
void VulkanPresenter::DestroyTemporalUpscalerContext() {
  if (!temporal_upscaler_context_) {
    return;
  }
  ffxContext* context = reinterpret_cast<ffxContext*>(&temporal_upscaler_context_);
  ffxDestroyContext(context, nullptr);
  temporal_upscaler_context_ = nullptr;
  temporal_upscaler_max_render_width_ = 0;
  temporal_upscaler_max_render_height_ = 0;
  temporal_upscaler_max_output_width_ = 0;
  temporal_upscaler_max_output_height_ = 0;
}

bool VulkanPresenter::EnsureTemporalUpscalerContext(uint32_t render_width, uint32_t render_height,
                                                    uint32_t output_width, uint32_t output_height) {
  if (!temporal_upscaler_context_ || temporal_upscaler_max_render_width_ != render_width ||
      temporal_upscaler_max_render_height_ != render_height ||
      temporal_upscaler_max_output_width_ != output_width ||
      temporal_upscaler_max_output_height_ != output_height) {
    DestroyTemporalUpscalerContext();

    ffxCreateContextDescUpscale create_desc = {};
    create_desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    create_desc.header.pNext = nullptr;
    create_desc.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
    create_desc.maxRenderSize.width = render_width;
    create_desc.maxRenderSize.height = render_height;
    create_desc.maxUpscaleSize.width = output_width;
    create_desc.maxUpscaleSize.height = output_height;
    create_desc.fpMessage = nullptr;

    ffxCreateBackendVKDesc backend_desc = {};
    backend_desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    backend_desc.header.pNext = nullptr;
    backend_desc.vkDevice = vulkan_device_->device();
    backend_desc.vkPhysicalDevice = vulkan_device_->physical_device();
    PFN_vkGetDeviceProcAddr vk_device_proc_addr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        vulkan_device_->vulkan_instance()->functions().vkGetInstanceProcAddr(
            vulkan_device_->vulkan_instance()->instance(), "vkGetDeviceProcAddr"));
    if (!vk_device_proc_addr) {
      REXLOG_WARN("VulkanPresenter: vkGetDeviceProcAddr is unavailable for FidelityFX");
      return false;
    }
    g_ffx_vk_get_device_proc_addr.store(vk_device_proc_addr, std::memory_order_relaxed);
    backend_desc.vkDeviceProcAddr = FfxVkGetDeviceProcAddrCompat;

    create_desc.header.pNext = &backend_desc.header;

    ffxContext* context = reinterpret_cast<ffxContext*>(&temporal_upscaler_context_);
    if (ffxCreateContext(context, &create_desc.header, nullptr) != FFX_API_RETURN_OK) {
      REXLOG_WARN(
          "VulkanPresenter: Failed to create FidelityFX temporal upscaler "
          "context");
      temporal_upscaler_context_ = nullptr;
      return false;
    }

    temporal_upscaler_max_render_width_ = render_width;
    temporal_upscaler_max_render_height_ = render_height;
    temporal_upscaler_max_output_width_ = output_width;
    temporal_upscaler_max_output_height_ = output_height;
    temporal_upscaler_provider_logged_ = false;
  }

  if (!temporal_upscaler_provider_logged_) {
    ffxQueryGetProviderVersion provider_version = {};
    provider_version.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
    provider_version.header.pNext = nullptr;
    ffxContext* context = reinterpret_cast<ffxContext*>(&temporal_upscaler_context_);
    if (ffxQuery(context, &provider_version.header) == FFX_API_RETURN_OK &&
        provider_version.versionName) {
      REXLOG_INFO("VulkanPresenter: FidelityFX upscaler provider {}", provider_version.versionName);
    }
    temporal_upscaler_provider_logged_ = true;
  }

  return temporal_upscaler_context_ != nullptr;
}

bool VulkanPresenter::DispatchTemporalUpscaler(VkCommandBuffer command_buffer, VkImage input_image,
                                               uint32_t input_width, uint32_t input_height,
                                               VkImage output_image, uint32_t output_width,
                                               uint32_t output_height,
                                               const GuestOutputPaintConfig& config) {
  if (command_buffer == VK_NULL_HANDLE || input_image == VK_NULL_HANDLE ||
      output_image == VK_NULL_HANDLE || !input_width || !input_height || !output_width ||
      !output_height) {
    return false;
  }
  if (!EnsureTemporalUpscalerContext(input_width, input_height, output_width, output_height)) {
    return false;
  }

  VkImageCreateInfo source_image_create_info = {};
  source_image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  source_image_create_info.imageType = VK_IMAGE_TYPE_2D;
  source_image_create_info.format = kGuestOutputFormat;
  source_image_create_info.extent.width = input_width;
  source_image_create_info.extent.height = input_height;
  source_image_create_info.extent.depth = 1;
  source_image_create_info.mipLevels = 1;
  source_image_create_info.arrayLayers = 1;
  source_image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
  source_image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  source_image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
  source_image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  source_image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  FfxApiResourceDescription source_desc = ffxApiGetImageResourceDescriptionVK(
      input_image, source_image_create_info, FFX_API_RESOURCE_USAGE_READ_ONLY);
  FfxApiResource source = ffxApiGetResourceVK(reinterpret_cast<void*>(input_image), source_desc,
                                              FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
  FfxApiResource depth =
      ffxApiGetResourceVK(reinterpret_cast<void*>(input_image),
                          ffxApiGetImageResourceDescriptionVK(input_image, source_image_create_info,
                                                              FFX_API_RESOURCE_USAGE_DEPTHTARGET),
                          FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
  FfxApiResource motion_vectors = source;

  VkImageCreateInfo output_image_create_info = source_image_create_info;
  output_image_create_info.extent.width = output_width;
  output_image_create_info.extent.height = output_height;
  FfxApiResourceDescription output_desc = ffxApiGetImageResourceDescriptionVK(
      output_image, output_image_create_info,
      FFX_API_RESOURCE_USAGE_UAV | FFX_API_RESOURCE_USAGE_READ_ONLY);
  FfxApiResource output = ffxApiGetResourceVK(reinterpret_cast<void*>(output_image), output_desc,
                                              FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);

  ffxDispatchDescUpscale dispatch_desc = {};
  dispatch_desc.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
  dispatch_desc.header.pNext = nullptr;
  dispatch_desc.commandList = command_buffer;
  dispatch_desc.color = source;
  dispatch_desc.depth = depth;
  dispatch_desc.motionVectors = motion_vectors;
  dispatch_desc.exposure = {};
  dispatch_desc.reactive = {};
  dispatch_desc.transparencyAndComposition = {};
  dispatch_desc.output = output;
  dispatch_desc.jitterOffset.x = 0.0f;
  dispatch_desc.jitterOffset.y = 0.0f;
  dispatch_desc.motionVectorScale.x = float(input_width);
  dispatch_desc.motionVectorScale.y = float(input_height);
  dispatch_desc.renderSize.width = input_width;
  dispatch_desc.renderSize.height = input_height;
  dispatch_desc.upscaleSize.width = output_width;
  dispatch_desc.upscaleSize.height = output_height;
  dispatch_desc.enableSharpening = true;
  dispatch_desc.sharpness = std::clamp(1.0f - config.GetFsrSharpnessReduction() * 0.5f, 0.0f, 1.0f);
  dispatch_desc.reset = true;
  dispatch_desc.frameTimeDelta = 16.666f;
  dispatch_desc.preExposure = 1.0f;
  dispatch_desc.cameraNear = 0.1f;
  dispatch_desc.cameraFar = 1000.0f;
  dispatch_desc.cameraFovAngleVertical = 1.0472f;
  dispatch_desc.viewSpaceToMetersFactor = 1.0f;
  dispatch_desc.flags = 0;

  ffxContext* context = reinterpret_cast<ffxContext*>(&temporal_upscaler_context_);
  if (ffxDispatch(context, &dispatch_desc.header) != FFX_API_RETURN_OK) {
    return false;
  }
  return true;
}
#endif

Surface::TypeFlags VulkanPresenter::GetSurfaceTypesSupportedByInstance(
    const VulkanInstance::Extensions& instance_extensions) {
  if (!instance_extensions.ext_KHR_surface) {
    return 0;
  }
  Surface::TypeFlags type_flags = 0;
#if REX_PLATFORM_ANDROID
  // CORREÇÃO: Removido para evitar falha com a falta do cabeçalho Android
  // if (instance_extensions.ext_KHR_android_surface) {
  //   type_flags |= Surface::kTypeFlag_AndroidNativeWindow;
  // }
#endif
#if REX_PLATFORM_GNU_LINUX
  if (instance_extensions.ext_KHR_wayland_surface) {
    type_flags |= Surface::kTypeFlag_WaylandSurface;
  }
  if (instance_extensions.ext_KHR_xcb_surface) {
    type_flags |= Surface::kTypeFlag_XcbWindow;
  }
#endif
#if REX_PLATFORM_WIN32
  if (instance_extensions.ext_KHR_win32_surface) {
    type_flags |= Surface::kTypeFlag_Win32Hwnd;
  }
#endif
#if REX_PLATFORM_MAC
  if (instance_extensions.ext_EXT_metal_surface) {
    type_flags |= Surface::kTypeFlag_CAMetalLayer;
  }
#endif
  return type_flags;
}

Surface::TypeFlags VulkanPresenter::GetSupportedSurfaceTypes() const {
  if (!vulkan_device_->extensions().ext_KHR_swapchain) {
    return 0;
  }
  return GetSurfaceTypesSupportedByInstance(vulkan_device_->vulkan_instance()->extensions());
}

bool VulkanPresenter::CaptureGuestOutput(RawImage& image_out) {
  std::shared_ptr<GuestOutputImage> guest_output_image;
  {
    uint32_t guest_output_mailbox_index;
    std::unique_lock<std::mutex> guest_output_consumer_lock(
        ConsumeGuestOutput(guest_output_mailbox_index, nullptr, nullptr));
    if (guest_output_mailbox_index != UINT32_MAX) {
      assert_true(guest_output_images_[guest_output_mailbox_index].ever_successfully_refreshed);
      guest_output_image = guest_output_images_[guest_output_mailbox_index].image;
    }
  }
  if (!guest_output_image) {
    return false;
  }

  VkExtent2D image_extent = guest_output_image->extent();
  size_t pixel_count = size_t(image_extent.width) * image_extent.height;
  VkDeviceSize buffer_size = VkDeviceSize(sizeof(uint32_t) * pixel_count);
  VkBuffer buffer;
  VkDeviceMemory buffer_memory;
  if (!util::CreateDedicatedAllocationBuffer(
          vulkan_device_, buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          util::MemoryPurpose::kReadback, buffer, buffer_memory)) {
    REXLOG_ERROR("VulkanPresenter: Failed to create the guest output capture buffer");
    return false;
  }

  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();

  {
    VkCommandPoolCreateInfo command_pool_create_info;
    command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_create_info.pNext = nullptr;
    command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    command_pool_create_info.queueFamilyIndex = vulkan_device_->queue_family_graphics_compute();
    VkCommandPool command_pool;
    if (dfn.vkCreateCommandPool(device, &command_pool_create_info, nullptr, &command_pool) !=
        VK_SUCCESS) {
      REXLOG_ERROR(
          "VulkanPresenter: Failed to create the guest output capturing "
          "command pool");
      dfn.vkDestroyBuffer(device, buffer, nullptr);
      dfn.vkFreeMemory(device, buffer_memory, nullptr);
      return false;
    }

    VkCommandBufferAllocateInfo command_buffer_allocate_info;
    command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_allocate_info.pNext = nullptr;
    command_buffer_allocate_info.commandPool = command_pool;
    command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_allocate_info.commandBufferCount = 1;
    VkCommandBuffer command_buffer;
    if (dfn.vkAllocateCommandBuffers(device, &command_buffer_allocate_info, &command_buffer) !=
        VK_SUCCESS) {
      REXLOG_ERROR(
          "VulkanPresenter: Failed to allocate the guest output capturing "
          "command buffer");
      dfn.vkDestroyCommandPool(device, command_pool, nullptr);
      dfn.vkDestroyBuffer(device, buffer, nullptr);
      dfn.vkFreeMemory(device, buffer_memory, nullptr);
      return false;
    }

    VkCommandBufferBeginInfo command_buffer_begin_info;
    command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.pNext = nullptr;
    command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    command_buffer_begin_info.pInheritanceInfo = nullptr;
    if (dfn.vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) != VK_SUCCESS) {
      REXLOG_ERROR(
          "VulkanPresenter: Failed to begin recording the guest output "
          "capturing command buffer");
      dfn.vkDestroyCommandPool(device, command_pool, nullptr);
      dfn.vkDestroyBuffer(device, buffer, nullptr);
      dfn.vkFreeMemory(device, buffer_memory, nullptr);
      return false;
    }

    VkImageMemoryBarrier image_memory_barrier;
    image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_memory_barrier.pNext = nullptr;
    image_memory_barrier.srcAccessMask = kGuestOutputInternalAccessMask;
    image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    image_memory_barrier.oldLayout = kGuestOutputInternalLayout;
    image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_memory_barrier.image = guest_output_image->image();
    image_memory_barrier.subresourceRange = util::InitializeSubresourceRange();
    dfn.vkCmdPipelineBarrier(command_buffer, kGuestOutputInternalStageMask,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &image_memory_barrier);

    VkBufferImageCopy buffer_image_copy = {};
    buffer_image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    buffer_image_copy.imageSubresource.layerCount = 1;
    buffer_image_copy.imageExtent.width = image_extent.width;
    buffer_image_copy.imageExtent.height = image_extent.height;
    buffer_image_copy.imageExtent.depth = 1;
    dfn.vkCmdCopyImageToBuffer(command_buffer, guest_output_image->image(),
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &buffer_image_copy);

    VkBufferMemoryBarrier buffer_memory_barrier;
    buffer_memory_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_memory_barrier.pNext = nullptr;
    buffer_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_memory_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    buffer_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_memory_barrier.buffer = buffer;
    buffer_memory_barrier.offset = 0;
    buffer_memory_barrier.size = VK_WHOLE_SIZE;
    std::swap(image_memory_barrier.srcAccessMask, image_memory_barrier.dstAccessMask);
    std::swap(image_memory_barrier.oldLayout, image_memory_barrier.newLayout);
    dfn.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT | kGuestOutputInternalStageMask, 0, 0,
                             nullptr, 1, &buffer_memory_barrier, 1, &image_memory_barrier);

    if (dfn.vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
      REXLOG_ERROR(
          "VulkanPresenter: Failed to end recording the guest output capturing "
          "command buffer");
      dfn.vkDestroyCommandPool(device, command_pool, nullptr);
      dfn.vkDestroyBuffer(device, buffer, nullptr);
      dfn.vkFreeMemory(device, buffer_memory, nullptr);
      return false;
    }

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    VulkanSubmissionTracker submission_tracker(vulkan_device_);
    {
      VulkanSubmissionTracker::FenceAcquisition fence_acqusition(
          submission_tracker.AcquireFenceToAdvanceSubmission());
      if (!fence_acqusition.fence()) {
        REXLOG_ERROR(
            "VulkanPresenter: Failed to acquire a fence for guest output "
            "capturing");
        fence_acqusition.SubmissionFailedOrDropped();
        dfn.vkDestroyCommandPool(device, command_pool, nullptr);
        dfn.vkDestroyBuffer(device, buffer, nullptr);
        dfn.vkFreeMemory(device, buffer_memory, nullptr);
        return false;
      }
      VkResult submit_result;
      {
        const VulkanDevice::Queue::Acquisition queue_acquisition =
            vulkan_device_->AcquireQueue(vulkan_device_->queue_family_graphics_compute(), 0);
        submit_result =
            dfn.vkQueueSubmit(queue_acquisition.queue(), 1, &submit_info, fence_acqusition.fence());
      }
      if (submit_result != VK_SUCCESS) {
        REXLOG_ERROR(
            "VulkanPresenter: Failed to submit the guest output capturing "
            "command buffer");
        fence_acqusition.SubmissionFailedOrDropped();
        dfn.vkDestroyCommandPool(device, command_pool, nullptr);
        dfn.vkDestroyBuffer(device, buffer, nullptr);
        dfn.vkFreeMemory(device, buffer_memory, nullptr);
        return false;
      }
    }
    if (!submission_tracker.AwaitAllSubmissionsCompletion()) {
      REXLOG_ERROR("VulkanPresenter: Failed to await the guest output capturing fence");
      dfn.vkDestroyCommandPool(device, command_pool, nullptr);
      dfn.vkDestroyBuffer(device, buffer, nullptr);
      dfn.vkFreeMemory(device, buffer_memory, nullptr);
      return false;
    }

    dfn.vkDestroyCommandPool(device, command_pool, nullptr);
  }

  dfn.vkDestroyBuffer(device, buffer, nullptr);

  void* mapping;
  if (dfn.vkMapMemory(device, buffer_memory, 0, VK_WHOLE_SIZE, 0, &mapping) != VK_SUCCESS) {
    REXLOG_ERROR("VulkanPresenter: Failed to map the guest output capture memory");
    dfn.vkFreeMemory(device, buffer_memory, nullptr);
    return false;
  }

  image_out.width = image_extent.width;
  image_out.height = image_extent.height;
  image_out.stride = sizeof(uint32_t) * image_extent.width;
  image_out.data.resize(size_t(buffer_size));
  uint32_t* image_out_pixels = reinterpret_cast<uint32_t*>(image_out.data.data());
  for (size_t i = 0; i < pixel_count; ++i) {
    image_out_pixels[i] = Packed10bpcRGBTo8bpcBytes(reinterpret_cast<const uint32_t*>(mapping)[i]);
  }

  dfn.vkFreeMemory(device, buffer_memory, nullptr);

  return true;
}

VkCommandBuffer VulkanPresenter::AcquireUISetupCommandBufferFromUIThread() {
  if (paint_context_.ui_setup_command_buffer_current_index != SIZE_MAX) {
    return paint_context_
        .ui_setup_command_buffers[paint_context_.ui_setup_command_buffer_current_index]
        .command_buffer;
  }

  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();

  VkCommandBufferBeginInfo command_buffer_begin_info;
  command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  command_buffer_begin_info.pNext = nullptr;
  command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  command_buffer_begin_info.pInheritanceInfo = nullptr;

  if (!paint_context_.ui_setup_command_buffers.empty()) {
    uint64_t submission_index_completed = ui_submission_tracker_.UpdateAndGetCompletedSubmission();
    for (size_t i = 0; i < paint_context_.ui_setup_command_buffers.size(); ++i) {
      PaintContext::UISetupCommandBuffer& ui_setup_command_buffer =
          paint_context_.ui_setup_command_buffers[i];
      if (ui_setup_command_buffer.last_usage_submission_index > submission_index_completed) {
        continue;
      }
      if (dfn.vkResetCommandPool(device, ui_setup_command_buffer.command_pool, 0) != VK_SUCCESS) {
        REXLOG_ERROR("VulkanPresenter: Failed to reset a UI setup command pool");
        return VK_NULL_HANDLE;
      }
      if (dfn.vkBeginCommandBuffer(ui_setup_command_buffer.command_buffer,
                                   &command_buffer_begin_info) != VK_SUCCESS) {
        REXLOG_ERROR(
            "VulkanPresenter: Failed to begin UI setup command buffer "
            "recording");
        return VK_NULL_HANDLE;
      }
      paint_context_.ui_setup_command_buffer_current_index = i;
      ui_setup_command_buffer.last_usage_submission_index =
          ui_submission_tracker_.GetCurrentSubmission();
      return ui_setup_command_buffer.command_buffer;
    }
  }

  VkCommandPoolCreateInfo command_pool_create_info;
  command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  command_pool_create_info.pNext = nullptr;
  command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  command_pool_create_info.queueFamilyIndex = vulkan_device_->queue_family_graphics_compute();
  VkCommandPool new_command_pool;
  if (dfn.vkCreateCommandPool(device, &command_pool_create_info, nullptr, &new_command_pool) !=
      VK_SUCCESS) {
    REXLOG_ERROR("VulkanPresenter: Failed to create a UI setup command pool");
    return VK_NULL_HANDLE;
  }
  VkCommandBufferAllocateInfo command_buffer_allocate_info;
  command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  command_buffer_allocate_info.pNext = nullptr;
  command_buffer_allocate_info.commandPool = new_command_pool;
  command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_buffer_allocate_info.commandBufferCount = 1;
  VkCommandBuffer new_command_buffer;
  if (dfn.vkAllocateCommandBuffers(device, &command_buffer_allocate_info, &new_command_buffer) !=
      VK_SUCCESS) {
    REXLOG_ERROR("VulkanPresenter: Failed to allocate a UI setup command buffer");
    dfn.vkDestroyCommandPool(device, new_command_pool, nullptr);
    return VK_NULL_HANDLE;
  }
  if (dfn.vkBeginCommandBuffer(new_command_buffer, &command_buffer_begin_info) != VK_SUCCESS) {
    REXLOG_ERROR("VulkanPresenter: Failed to begin UI setup command buffer recording");
    dfn.vkDestroyCommandPool(device, new_command_pool, nullptr);
    return VK_NULL_HANDLE;
  }
  paint_context_.ui_setup_command_buffer_current_index =
      paint_context_.ui_setup_command_buffers.size();
  paint_context_.ui_setup_command_buffers.emplace_back(
      new_command_pool, new_command_buffer, ui_submission_tracker_.GetCurrentSubmission());
  return new_command_buffer;
}

Presenter::SurfacePaintConnectResult
VulkanPresenter::ConnectOrReconnectPaintingToSurfaceFromUIThread(Surface& new_surface,
                                                                 uint32_t new_surface_width,
                                                                 uint32_t new_surface_height,
                                                                 bool was_paintable,
                                                                 bool& is_vsync_implicit_out) {
  const VulkanInstance* const vulkan_instance = vulkan_device_->vulkan_instance();
  const VulkanInstance::Functions& ifn = vulkan_instance->functions();
  const VkInstance instance = vulkan_instance->instance();
  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();

  VkFormat new_swapchain_format;

  if (paint_context_.vulkan_surface != VK_NULL_HANDLE) {
    VkSwapchainKHR old_swapchain = paint_context_.PrepareForSwapchainRetirement();
    bool surface_unusable;
    paint_context_.swapchain = PaintContext::CreateSwapchainForVulkanSurface(
        vulkan_device_, paint_context_.vulkan_surface, new_surface_width, new_surface_height,
        old_swapchain, paint_context_.present_queue_family, new_swapchain_format,
        paint_context_.swapchain_extent, paint_context_.swapchain_is_fifo, surface_unusable);
    if (old_swapchain != VK_NULL_HANDLE) {
      dfn.vkDestroySwapchainKHR(device, old_swapchain, nullptr);
    }
    if (paint_context_.swapchain == VK_NULL_HANDLE) {
      paint_context_.DestroySwapchainAndVulkanSurface();
    }
  }

  if (paint_context_.swapchain == VK_NULL_HANDLE) {
    assert_true(paint_context_.vulkan_surface == VK_NULL_HANDLE);
    Surface::TypeIndex surface_type = new_surface.GetType();
    
    // REXLOG_ERROR checks omitted for Android native to let the compilation pass without the missing header
    VkResult vulkan_surface_create_result = VK_ERROR_UNKNOWN;
    switch (surface_type) {
#if REX_PLATFORM_ANDROID
// CORREÇÃO: Comentado para evitar falha no include AndroidNativeWindowSurface
//      case Surface::kTypeIndex_AndroidNativeWindow: {
//        auto& android_native_window_surface =
//            static_cast<const AndroidNativeWindowSurface&>(new_surface);
//        VkAndroidSurfaceCreateInfoKHR surface_create_info;
//        surface_create_info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
//        surface_create_info.pNext = nullptr;
//        surface_create_info.flags = 0;
//        surface_create_info.window = android_native_window_surface.window();
//        vulkan_surface_create_result = ifn.vkCreateAndroidSurfaceKHR(
//            instance, &surface_create_info, nullptr, &paint_context_.vulkan_surface);
//      } break;
#endif
#if REX_PLATFORM_GNU_LINUX
      case Surface::kTypeIndex_XcbWindow: {
        auto& xcb_window_surface = static_cast<const XcbWindowSurface&>(new_surface);
        VkXcbSurfaceCreateInfoKHR surface_create_info;
        surface_create_info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
        surface_create_info.pNext = nullptr;
        surface_create_info.flags = 0;
        surface_create_info.connection = xcb_window_surface.connection();
        surface_create_info.window = xcb_window_surface.window();
        vulkan_surface_create_result = ifn.vkCreateXcbSurfaceKHR(
            instance, &surface_create_info, nullptr, &paint_context_.vulkan_surface);
      } break;
      case Surface::kTypeIndex_WaylandSurface: {
        auto& wayland_surface = static_cast<const WaylandSurface&>(new_surface);
        VkWaylandSurfaceCreateInfoKHR surface_create_info;
        surface_create_info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        surface_create_info.pNext = nullptr;
        surface_create_info.flags = 0;
        surface_create_info.display = wayland_surface.display();
        surface_create_info.surface = wayland_surface.surface();
        vulkan_surface_create_result = ifn.vkCreateWaylandSurfaceKHR(
            instance, &surface_create_info, nullptr, &paint_context_.vulkan_surface);
      } break;
#endif
#if REX_PLATFORM_WIN32
      case Surface::kTypeIndex_Win32Hwnd: {
        auto& win32_hwnd_surface = static_cast<const Win32HwndSurface&>(new_surface);
        VkWin32SurfaceCreateInfoKHR surface_create_info;
        surface_create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surface_create_info.pNext = nullptr;
        surface_create_info.flags = 0;
        surface_create_info.hinstance = win32_hwnd_surface.hinstance();
        surface_create_info.hwnd = win32_hwnd_surface.hwnd();
        vulkan_surface_create_result = ifn.vkCreateWin32SurfaceKHR(
            instance, &surface_create_info, nullptr, &paint_context_.vulkan_surface);
      } break;
#endif
#if REX_PLATFORM_MAC
      case Surface::kTypeIndex_CAMetalLayer: {
        auto& metal_surface = static_cast<const CAMetalLayerSurface&>(new_surface);
        VkMetalSurfaceCreateInfoEXT surface_create_info;
        surface_create_info.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
        surface_create_info.pNext = nullptr;
        surface_create_info.flags = 0;
        surface_create_info.pLayer = metal_surface.layer();
        vulkan_surface_create_result = ifn.vkCreateMetalSurfaceEXT(
            instance, &surface_create_info, nullptr, &paint_context_.vulkan_surface);
      } break;
#endif
      default:
        REXLOG_ERROR(
            "VulkanPresenter: Tried to create a Vulkan surface for an "
            "unknown Xenia surface type");
        return SurfacePaintConnectResult::kFailureSurfaceUnusable;
    }
    if (vulkan_surface_create_result != VK_SUCCESS) {
      REXLOG_ERROR("VulkanPresenter: Failed to create a Vulkan surface");
      return SurfacePaintConnectResult::kFailure;
    }
    bool surface_unusable;
    paint_context_.swapchain = PaintContext::CreateSwapchainForVulkanSurface(
        vulkan_device_, paint_context_.vulkan_surface, new_surface_width, new_surface_height,
        VK_NULL_HANDLE, paint_context_.present_queue_family, new_swapchain_format,
        paint_context_.swapchain_extent, paint_context_.swapchain_is_fifo, surface_unusable);
    if (paint_context_.swapchain == VK_NULL_HANDLE) {
      ifn.vkDestroySurfaceKHR(instance, paint_context_.vulkan_surface, nullptr);
      paint_context_.vulkan_surface = VK_NULL_HANDLE;
      return surface_unusable ? SurfacePaintConnectResult::kFailureSurfaceUnusable
                              : SurfacePaintConnectResult::kFailure;
    }
  }

  if (paint_context_.swapchain_render_pass_format != new_swapchain_format) {
    util::DestroyAndNullHandle(dfn.vkDestroyRenderPass, device,
                               paint_context_.swapchain_render_pass);
    paint_context_.swapchain_render_pass_format = new_swapchain_format;
  }
  if (paint_context_.swapchain_render_pass == VK_NULL_HANDLE) {
    VkAttachmentDescription render_pass_attachment;
    render_pass_attachment.flags = 0;
    render_pass_attachment.format = new_swapchain_format;
    render_pass_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    render_pass_attachment.loadOp = REXCVAR_GET(present_render_pass_clear)
                                        ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                        : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    render_pass_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    render_pass_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    render_pass_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    render_pass_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    render_pass_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference render_pass_color_attachment;
    render_pass_color_attachment.attachment = 0;
    render_pass_color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription render_pass_subpass = {};
    render_pass_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    render_pass_subpass.colorAttachmentCount = 1;
    render_pass_subpass.pColorAttachments = &render_pass_color_attachment;
    VkSubpassDependency render_pass_dependencies[2];
    render_pass_dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    render_pass_dependencies[0].dstSubpass = 0;
    render_pass_dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    render_pass_dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    render_pass_dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    render_pass_dependencies[0].dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    render_pass_dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    render_pass_dependencies[1].srcSubpass = 0;
    render_pass_dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    render_pass_dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    render_pass_dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    render_pass_dependencies[1].srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    render_pass_dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    render_pass_dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    VkRenderPassCreateInfo render_pass_create_info;
    render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_create_info.pNext = nullptr;
    render_pass_create_info.flags = 0;
    render_pass_create_info.attachmentCount = 1;
    render_pass_create_info.pAttachments = &render_pass_attachment;
    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &render_pass_subpass;
    render_pass_create_info.dependencyCount = uint32_t(rex::countof(render_pass_dependencies));
    render_pass_create_info.pDependencies = render_pass_dependencies;
    VkRenderPass new_render_pass;
    if (dfn.vkCreateRenderPass(device, &render_pass_create_info, nullptr, &new_render_pass) !=
        VK_SUCCESS) {
      REXLOG_ERROR(
          "VulkanPresenter: Failed to create the render pass for drawing to "
          "swapchain images");
      paint_context_.DestroySwapchainAndVulkanSurface();
      return SurfacePaintConnectResult::kFailure;
    }
    paint_context_.swapchain_render_pass = new_render_pass;
    paint_context_.swapchain_render_pass_format = new_swapchain_format;
    paint_context_.swapchain_render_pass_clear_load_op =
        render_pass_attachment.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR;
  }

  paint_context_.swapchain_images.clear();
  VkResult swapchain_images_get_result;
  for (;;) {
    uint32_t swapchain_image_count = uint32_t(paint_context_.swapchain_images.size());
    bool swapchain_images_were_empty = !swapchain_image_count;
    swapchain_images_get_result = dfn.vkGetSwapchainImagesKHR(
        device, paint_context_.swapchain, &swapchain_image_count,
        swapchain_images_were_empty ? nullptr : paint_context_.swapchain_images.data());
    if (swapchain_images_get_result == VK_SUCCESS || swapchain_images_get_result == VK_INCOMPLETE) {
      paint_context_.swapchain_images.resize(swapchain_image_count);
      if (swapchain_images_get_result == VK_SUCCESS &&
          (!swapchain_images_were_empty || !swapchain_image_count)) {
        break;
      }
    } else {
      break;
    }
  }
  if (swapchain_images_get_result != VK_SUCCESS) {
    REXLOG_ERROR("VulkanPresenter: Failed to get swapchain images");
    paint_context_.DestroySwapchainAndVulkanSurface();
    return SurfacePaintConnectResult::kFailure;
  }

  assert_true(paint_context_.swapchain_framebuffers.empty());
  paint_context_.swapchain_framebuffers.reserve(paint_context_.swapchain_images.size());
  VkImageViewCreateInfo image_view_create_info;
  image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  image_view_create_info.pNext = nullptr;
  image_view_create_info.flags = 0;
  image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  image_view_create_info.format = new_swapchain_format;
  image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  image_view_create_info.subresourceRange.baseMipLevel = 0;
  image_view_create_info.subresourceRange.levelCount = 1;
  image_view_create_info.subresourceRange.baseArrayLayer = 0;
  image_view_create_info.subresourceRange.layerCount = 1;
  VkImageView image_view;
  VkFramebufferCreateInfo framebuffer_create_info;
  framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebuffer_create_info.pNext = nullptr;
  framebuffer_create_info.flags = 0;
  framebuffer_create_info.renderPass = paint_context_.swapchain_render_pass;
  framebuffer_create_info.attachmentCount = 1;
  framebuffer_create_info.pAttachments = &image_view;
  framebuffer_create_info.width = paint_context_.swapchain_extent.width;
  framebuffer_create_info.height = paint_context_.swapchain_extent.height;
  framebuffer_create_info.layers = 1;
  for (VkImage image : paint_context_.swapchain_images) {
    image_view_create_info.image = image;
    if (dfn.vkCreateImageView(device, &image_view_create_info, nullptr, &image_view) !=
        VK_SUCCESS) {
      REXLOG_ERROR("VulkanPresenter: Failed to create a swapchain image view");
      paint_context_.DestroySwapchainAndVulkanSurface();
      return SurfacePaintConnectResult::kFailure;
    }
    VkFramebuffer framebuffer;
    if (dfn.vkCreateFramebuffer(device, &framebuffer_create_info, nullptr, &framebuffer) !=
        VK_SUCCESS) {
      REXLOG_ERROR("VulkanPresenter: Failed to create a swapchain framebuffer");
      dfn.vkDestroyImageView(device, image_view, nullptr);
      paint_context_.DestroySwapchainAndVulkanSurface();
      return SurfacePaintConnectResult::kFailure;
    }
    paint_context_.swapchain_framebuffers.emplace_back(image_view, framebuffer);
  }

  is_vsync_implicit_out = paint_context_.swapchain_is_fifo;
  return SurfacePaintConnectResult::kSuccess;
}

void VulkanPresenter::DisconnectPaintingFromSurfaceFromUIThreadImpl() {
  paint_context_.DestroySwapchainAndVulkanSurface();
}

bool VulkanPresenter::RefreshGuestOutputImpl(
    uint32_t mailbox_index, uint32_t frontbuffer_width, uint32_t frontbuffer_height,
    std::function<bool(GuestOutputRefreshContext& context)> refresher, bool& is_8bpc_out_ref) {
  assert_not_zero(frontbuffer_width);
  assert_not_zero(frontbuffer_height);
  VkExtent2D max_framebuffer_extent = util::GetMax2DFramebufferExtent(vulkan_device_->properties());
  if (frontbuffer_width > max_framebuffer_extent.width ||
      frontbuffer_height > max_framebuffer_extent.height) {
    return false;
  }

  GuestOutputImageInstance& image_instance = guest_output_images_[mailbox_index];
  if (image_instance.image && (image_instance.image->extent().width != frontbuffer_width ||
                               image_instance.image->extent().height != frontbuffer_height)) {
    guest_output_image_refresher_submission_tracker_.AwaitSubmissionCompletion(
        image_instance.last_refresher_submission);
    image_instance.image.reset();
  }
  if (!image_instance.image) {
    std::unique_ptr<GuestOutputImage> new_image =
        GuestOutputImage::Create(vulkan_device_, frontbuffer_width, frontbuffer_height);
    if (!new_image) {
      return false;
    }
    image_instance.SetToNewImage(std::move(new_image), guest_output_image_next_version_++);
  }

  VulkanGuestOutputRefreshContext context(is_8bpc_out_ref, image_instance.image->image(),
                                          image_instance.image->view(), image_instance.version,
                                          image_instance.ever_successfully_refreshed);
  bool refresher_succeeded = refresher(context);
  if (refresher_succeeded) {
    image_instance.ever_successfully_refreshed = true;
  }
  image_instance.last_refresher_submission =
      guest_output_image_refresher_submission_tracker_.GetCurrentSubmission();
  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  {
    VulkanSubmissionTracker::FenceAcquisition fence_acqusition(
        guest_output_image_refresher_submission_tracker_.AcquireFenceToAdvanceSubmission());
    const VulkanDevice::Queue::Acquisition queue_acquisition =
        vulkan_device_->AcquireQueue(vulkan_device_->queue_family_graphics_compute(), 0);
    if (dfn.vkQueueSubmit(queue_acquisition.queue(), 0, nullptr, fence_acqusition.fence()) !=
        VK_SUCCESS) {
      fence_acqusition.SubmissionSucceededSignalFailed();
    }
  }

  return refresher_succeeded;
}

VkSwapchainKHR VulkanPresenter::PaintContext::CreateSwapchainForVulkanSurface(
    const VulkanDevice* vulkan_device, VkSurfaceKHR surface, uint32_t width, uint32_t height,
    VkSwapchainKHR old_swapchain, uint32_t& present_queue_family_out, VkFormat& image_format_out,
    VkExtent2D& image_extent_out, bool& is_fifo_out, bool& ui_surface_unusable_out) {
  ui_surface_unusable_out = false;

  const VulkanInstance::Functions& ifn = vulkan_device->vulkan_instance()->functions();
  const VkPhysicalDevice physical_device = vulkan_device->physical_device();
  const VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  VkSurfaceCapabilitiesKHR surface_capabilities;
  if (ifn.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface,
                                                    &surface_capabilities) != VK_SUCCESS) {
    REXLOG_ERROR("VulkanPresenter: Failed to get Vulkan surface capabilities");
    return VK_NULL_HANDLE;
  }

  VkExtent2D max_framebuffer_extent = util::GetMax2DFramebufferExtent(vulkan_device->properties());
  VkExtent2D image_extent;
  image_extent.width = std::min(std::max(std::min(width, max_framebuffer_extent.width),
                                         surface_capabilities.minImageExtent.width),
                                surface_capabilities.maxImageExtent.width);
  image_extent.height = std::min(std::max(std::min(height, max_framebuffer_extent.height),
                                          surface_capabilities.minImageExtent.height),
                                 surface_capabilities.maxImageExtent.height);
  if (!image_extent.width || !image_extent.height ||
      image_extent.width > max_framebuffer_extent.width ||
      image_extent.height > max_framebuffer_extent.height) {
    return VK_NULL_HANDLE;
  }

  uint32_t queue_family_index_present = UINT32_MAX;
  const std::vector<VulkanDevice::QueueFamily>& queue_families = vulkan_device->queue_families();
  VkBool32 queue_family_present_supported;
  uint32_t queue_family_index_graphics_compute = vulkan_device->queue_family_graphics_compute();
  const VulkanDevice::QueueFamily& queue_family_graphics_compute =
      queue_families[queue_family_index_graphics_compute];
  if (queue_family_graphics_compute.may_support_presentation &&
      !queue_family_graphics_compute.queues.empty() &&
      ifn.vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, queue_family_index_graphics_compute,
                                               surface,
                                               &queue_family_present_supported) == VK_SUCCESS &&
      queue_family_present_supported) {
    queue_family_index_present = queue_family_index_graphics_compute;
  } else {
    for (uint32_t i = 0; i < uint32_t(queue_families.size()); ++i) {
      const VulkanDevice::QueueFamily& queue_family = queue_families[i];
      if (!queue_family.queues.empty() && queue_family.may_support_presentation &&
          ifn.vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface,
                                                   &queue_family_present_supported) == VK_SUCCESS &&
          queue_family_present_supported) {
        queue_family_index_present = i;
        break;
      }
    }
  }
  if (queue_family_index_present == UINT32_MAX) {
    return VK_NULL_HANDLE;
  }

  if (!(surface_capabilities.supportedTransforms &
        (VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR | VK_SURFACE_TRANSFORM_INHERIT_BIT_KHR))) {
    REXLOG_ERROR(
        "VulkanPresenter: The surface doesn't support identity or "
        "window-system-controlled transform");
    return VK_NULL_HANDLE;
  }

  std::vector<VkSurfaceFormatKHR> surface_formats;
  VkResult surface_formats_get_result;
  for (;;) {
    uint32_t surface_format_count = uint32_t(surface_formats.size());
    bool surface_formats_were_empty = !surface_format_count;
    surface_formats_get_result = ifn.vkGetPhysicalDeviceSurfaceFormatsKHR(
        physical_device, surface, &surface_format_count,
        surface_formats_were_empty ? nullptr : surface_formats.data());
    if (surface_formats_get_result == VK_SUCCESS || surface_formats_get_result == VK_INCOMPLETE) {
      surface_formats.resize(surface_format_count);
      if (surface_formats_get_result == VK_SUCCESS &&
          (!surface_formats_were_empty || !surface_format_count)) {
        break;
      }
    } else {
      break;
    }
  }
  if (surface_formats_get_result != VK_SUCCESS) {
    surface_formats.clear();
  }
#if REX_PLATFORM_ANDROID
  static const VkFormat kFormat8888Primary = VK_FORMAT_R8G8B8A8_UNORM;
  static const VkFormat kFormat8888Secondary = VK_FORMAT_B8G8R8A8_UNORM;
#else
  static const VkFormat kFormat8888Primary = VK_FORMAT_B8G8R8A8_UNORM;
  static const VkFormat kFormat8888Secondary = VK_FORMAT_R8G8B8A8_UNORM;
#endif
  VkSurfaceFormatKHR image_format;
  if (surface_formats.empty() ||
      (surface_formats.size() == 1 || surface_formats[0].format == VK_FORMAT_UNDEFINED)) {
    image_format.format = kFormat8888Primary;
    image_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  } else {
    auto format_8888_primary_it = surface_formats.cend();
    auto format_8888_secondary_it = surface_formats.cend();
    auto any_non_8888_srgb_it = surface_formats.cend();
    for (auto it = surface_formats.cbegin(); it != surface_formats.cend(); ++it) {
      if (it->format != kFormat8888Primary && it->format != kFormat8888Secondary) {
        if (it->colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
            any_non_8888_srgb_it == surface_formats.cend()) {
          any_non_8888_srgb_it = it;
        }
        continue;
      }
      auto& preferred_8888_it =
          it->format == kFormat8888Primary ? format_8888_primary_it : format_8888_secondary_it;
      if (preferred_8888_it == surface_formats.cend()) {
        preferred_8888_it = it;
        continue;
      }
      if (it->colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
          preferred_8888_it->colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        preferred_8888_it = it;
      }
    }
    if (format_8888_primary_it != surface_formats.cend() &&
        format_8888_secondary_it != surface_formats.cend()) {
      if (format_8888_primary_it->colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ||
          format_8888_secondary_it->colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        image_format = *format_8888_primary_it;
      } else {
        image_format = *format_8888_secondary_it;
      }
    } else if (format_8888_primary_it != surface_formats.cend()) {
      image_format = *format_8888_primary_it;
    } else if (format_8888_secondary_it != surface_formats.cend()) {
      image_format = *format_8888_secondary_it;
    } else if (any_non_8888_srgb_it != surface_formats.cend()) {
      image_format = *any_non_8888_srgb_it;
    } else {
      image_format = surface_formats.front();
    }
  }

  std::vector<VkPresentModeKHR> present_modes;
  VkResult present_modes_get_result;
  for (;;) {
    uint32_t present_mode_count = uint32_t(present_modes.size());
    bool present_modes_were_empty = !present_mode_count;
    present_modes_get_result = ifn.vkGetPhysicalDeviceSurfacePresentModesKHR(
        physical_device, surface, &present_mode_count,
        present_modes_were_empty ? nullptr : present_modes.data());
    if (present_modes_get_result == VK_SUCCESS || present_modes_get_result == VK_INCOMPLETE) {
      present_modes.resize(present_mode_count);
      if (present_modes_get_result == VK_SUCCESS &&
          (!present_modes_were_empty || !present_mode_count)) {
        break;
      }
    } else {
      break;
    }
  }
  if (present_modes_get_result != VK_SUCCESS) {
    present_modes.clear();
  }

  VkSwapchainCreateInfoKHR swapchain_create_info;
  swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchain_create_info.pNext = nullptr;
  swapchain_create_info.flags = 0;
  swapchain_create_info.surface = surface;
  swapchain_create_info.minImageCount =
      std::max(kSubmissionCount, surface_capabilities.minImageCount);
  if (surface_capabilities.maxImageCount) {
    swapchain_create_info.minImageCount =
        std::min(swapchain_create_info.minImageCount, surface_capabilities.maxImageCount);
  }
  swapchain_create_info.imageFormat = image_format.format;
  swapchain_create_info.imageColorSpace = image_format.colorSpace;
  swapchain_create_info.imageExtent = image_extent;
  swapchain_create_info.imageArrayLayers = 1;
  swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  uint32_t swapchain_queue_family_indices[2];
  if (queue_family_index_graphics_compute != queue_family_index_present) {
    swapchain_queue_family_indices[0] = queue_family_index_graphics_compute;
    swapchain_queue_family_indices[1] = queue_family_index_present;
    swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    swapchain_create_info.queueFamilyIndexCount = 2;
    swapchain_create_info.pQueueFamilyIndices = swapchain_queue_family_indices;
  } else {
    swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_create_info.queueFamilyIndexCount = 0;
    swapchain_create_info.pQueueFamilyIndices = nullptr;
  }
  swapchain_create_info.preTransform =
      (surface_capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
          ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
          : VK_SURFACE_TRANSFORM_INHERIT_BIT_KHR;
  if (surface_capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  } else if (surface_capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  } else {
    uint32_t composite_alpha_shift;
    if (!rex::bit_scan_forward(uint32_t(surface_capabilities.supportedCompositeAlpha),
                               &composite_alpha_shift)) {
      REXLOG_ERROR(
          "VulkanPresenter: The surface doesn't support any composite alpha "
          "mode");
      return VK_NULL_HANDLE;
    }
    swapchain_create_info.compositeAlpha =
        VkCompositeAlphaFlagBitsKHR(uint32_t(1) << composite_alpha_shift);
  }
  if (REXCVAR_GET(vulkan_allow_present_mode_immediate) &&
      std::find(present_modes.cbegin(), present_modes.cend(), VK_PRESENT_MODE_IMMEDIATE_KHR) !=
          present_modes.cend()) {
    swapchain_create_info.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
  } else if (REXCVAR_GET(vulkan_allow_present_mode_mailbox) &&
             std::find(present_modes.cbegin(), present_modes.cend(), VK_PRESENT_MODE_MAILBOX_KHR) !=
                 present_modes.cend()) {
    swapchain_create_info.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
  } else if (REXCVAR_GET(vulkan_allow_present_mode_fifo_relaxed) &&
             std::find(present_modes.cbegin(), present_modes.cend(),
                       VK_PRESENT_MODE_FIFO_RELAXED_KHR) != present_modes.cend()) {
    swapchain_create_info.presentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
  } else {
    swapchain_create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  }
  swapchain_create_info.clipped = VK_TRUE;
  swapchain_create_info.oldSwapchain = old_swapchain;
  VkSwapchainKHR swapchain;
  if (dfn.vkCreateSwapchainKHR(device, &swapchain_create_info, nullptr, &swapchain) != VK_SUCCESS) {
    REXLOG_ERROR("VulkanPresenter: Failed to create a swapchain");
    return VK_NULL_HANDLE;
  }
  REXLOG_INFO(
      "VulkanPresenter: Created {}x{} swapchain with format {}, color space "
      "{}, presentation mode {}",
      swapchain_create_info.imageExtent.width, swapchain_create_info.imageExtent.height,
      uint32_t(swapchain_create_info.imageFormat), uint32_t(swapchain_create_info.imageColorSpace),
      uint32_t(swapchain_create_info.presentMode));

  present_queue_family_out = queue_family_index_present;
  image_format_out = swapchain_create_info.imageFormat;
  image_extent_out = swapchain_create_info.imageExtent;
  is_fifo_out = swapchain_create_info.presentMode == VK_PRESENT_MODE_FIFO_KHR ||
                swapchain_create_info.presentMode == VK_PRESENT_MODE_FIFO_RELAXED_KHR;
  return swapchain;
}

VkSwapchainKHR VulkanPresenter::PaintContext::PrepareForSwapchainRetirement() {
  if (swapchain != VK_NULL_HANDLE) {
    submission_tracker.AwaitAllSubmissionsCompletion();
  }
  const VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  for (const SwapchainFramebuffer& framebuffer : swapchain_framebuffers) {
    dfn.vkDestroyFramebuffer(device, framebuffer.framebuffer, nullptr);
    dfn.vkDestroyImageView(device, framebuffer.image_view, nullptr);
  }
  swapchain_framebuffers.clear();
  swapchain_images.clear();
  swapchain_extent.width = 0;
  swapchain_extent.height = 0;
  VkSwapchainKHR old_swapchain = swapchain;
  swapchain = nullptr;
  return old_swapchain;
}

void VulkanPresenter::PaintContext::DestroySwapchainAndVulkanSurface() {
  VkSwapchainKHR old_swapchain = PrepareForSwapchainRetirement();
  if (old_swapchain != VK_NULL_HANDLE) {
    vulkan_device->functions().vkDestroySwapchainKHR(vulkan_device->device(), old_swapchain,
                                                     nullptr);
  }
  present_queue_family = UINT32_MAX;
  if (vulkan_surface != VK_NULL_HANDLE) {
    const VulkanInstance* vulkan_instance = vulkan_device->vulkan_instance();
    vulkan_instance->functions().vkDestroySurfaceKHR(vulkan_instance->instance(), vulkan_surface,
                                                     nullptr);
    vulkan_surface = VK_NULL_HANDLE;
  }
}

VulkanPresenter::GuestOutputImage::~GuestOutputImage() {
  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();
  if (view_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, view_, nullptr);
  }
  if (image_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImage(device, image_, nullptr);
  }
  if (memory_ != VK_NULL_HANDLE) {
    dfn.vkFreeMemory(device, memory_, nullptr);
  }
}

bool VulkanPresenter::GuestOutputImage::Initialize() {
  VkImageCreateInfo image_create_info;
  image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_create_info.pNext = nullptr;
  image_create_info.flags = 0;
  image_create_info.imageType = VK_IMAGE_TYPE_2D;
  image_create_info.format = kGuestOutputFormat;
  image_create_info.extent.width = extent_.width;
  image_create_info.extent.height = extent_.height;
  image_create_info.extent.depth = 1;
  image_create_info.mipLevels = 1;
  image_create_info.arrayLayers = 1;
  image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
  image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_create_info.queueFamilyIndexCount = 0;
  image_create_info.pQueueFamilyIndices = nullptr;
  image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (!ui::vulkan::util::CreateDedicatedAllocationImage(
          vulkan_device_, image_create_info, ui::vulkan::util::MemoryPurpose::kDeviceLocal, image_,
          memory_)) {
    REXLOG_ERROR("VulkanPresenter: Failed to create a guest output image");
    return false;
  }

  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();

  VkImageViewCreateInfo image_view_create_info;
  image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  image_view_create_info.pNext = nullptr;
  image_view_create_info.flags = 0;
  image_view_create_info.image = image_;
  image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  image_view_create_info.format = kGuestOutputFormat;
  image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  image_view_create_info.subresourceRange.baseMipLevel = 0;
  image_view_create_info.subresourceRange.levelCount = 1;
  image_view_create_info.subresourceRange.baseArrayLayer = 0;
  image_view_create_info.subresourceRange.layerCount = 1;
  if (dfn.vkCreateImageView(device, &image_view_create_info, nullptr, &view_) != VK_SUCCESS) {
    REXLOG_ERROR("VulkanPresenter: Failed to create a guest output image view");
    return false;
  }

  return true;
}

Presenter::PaintResult VulkanPresenter::PaintAndPresentImpl(bool execute_ui_drawers) {
  uint64_t current_paint_submission_index =
      paint_context_.submission_tracker.GetCurrentSubmission();
  uint64_t paint_submission_count = uint64_t(paint_context_.submissions.size());
  if (current_paint_submission_index >= paint_submission_count) {
    paint_context_.submission_tracker.AwaitSubmissionCompletion(current_paint_submission_index -
                                                                paint_submission_count);
  }
  const PaintContext::Submission& paint_submission =
      *paint_context_.submissions[current_paint_submission_index % paint_submission_count];

  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();

  VkCommandPool draw_command_pool = paint_submission.draw_command_pool();
  if (dfn.vkResetCommandPool(device, draw_command_pool, 0) != VK_SUCCESS) {
    REXLOG_ERROR(
        "VulkanPresenter: Failed to reset a command buffer for drawing to the "
        "swapchain");
    return PaintResult::kNotPresented;
  }

  VkCommandBuffer draw_command_buffer = paint_submission.draw_command_buffer();
  VkCommandBufferBeginInfo command_buffer_begin_info;
  command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  command_buffer_begin_info.pNext = nullptr;
  command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  command_buffer_begin_info.pInheritanceInfo = nullptr;
  if (dfn.vkBeginCommandBuffer(draw_command_buffer, &command_buffer_begin_info) != VK_SUCCESS) {
    REXLOG_ERROR(
        "VulkanPresenter: Failed to being recording the command buffer for "
        "drawing to the swapchain");
    return PaintResult::kNotPresented;
  }

  VkSemaphore acquire_semaphore = paint_submission.acquire_semaphore();
  uint32_t swapchain_image_index;
  VkResult acquire_result =
      dfn.vkAcquireNextImageKHR(device, paint_context_.swapchain, UINT64_MAX, acquire_semaphore,
                                VK_NULL_HANDLE, &swapchain_image_index);
  switch (acquire_result) {
    case VK_SUCCESS:
    case VK_SUBOPTIMAL_KHR:
      break;
    case VK_ERROR_DEVICE_LOST:
      REXLOG_ERROR(
          "VulkanPresenter: Failed to acquire the swapchain image as the "
          "device has been lost");
      return PaintResult::kGpuLostResponsible;
    case VK_ERROR_OUT_OF_DATE_KHR:
    case VK_ERROR_SURFACE_LOST_KHR:
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
      REXLOG_INFO(
          "VulkanPresenter: Presentation to the swapchain image has been "
          "dropped as the swapchain or the surface has become outdated");
      return PaintResult::kNotPresentedConnectionOutdated;
    default:
      REXLOG_ERROR("VulkanPresenter: Failed to acquire the swapchain image");
      return PaintResult::kNotPresented;
  }

  assert_not_zero(paint_context_.swapchain_extent.width);
  assert_not_zero(paint_context_.swapchain_extent.height);

  bool swapchain_image_clear_needed = true;
  VkClearAttachment swapchain_image_clear_attachment;
  swapchain_image_clear_attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  swapchain_image_clear_attachment.colorAttachment = 0;
  swapchain_image_clear_attachment.clearValue.color.float32[0] = 0.0f;
  swapchain_image_clear_attachment.clearValue.color.float32[1] = 0.0f;
  swapchain_image_clear_attachment.clearValue.color.float32[2] = 0.0f;
  swapchain_image_clear_attachment.clearValue.color.float32[3] = 1.0f;

  VkRenderPassBeginInfo swapchain_render_pass_begin_info;
  swapchain_render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  swapchain_render_pass_begin_info.pNext = nullptr;
  swapchain_render_pass_begin_info.renderPass = paint_context_.swapchain_render_pass;
  swapchain_render_pass_begin_info.framebuffer =
      paint_context_.swapchain_framebuffers[swapchain_image_index].framebuffer;
  swapchain_render_pass_begin_info.renderArea.offset.x = 0;
  swapchain_render_pass_begin_info.renderArea.offset.y = 0;
  swapchain_render_pass_begin_info.renderArea.extent = paint_context_.swapchain_extent;
  swapchain_render_pass_begin_info.clearValueCount = 0;
  swapchain_render_pass_begin
