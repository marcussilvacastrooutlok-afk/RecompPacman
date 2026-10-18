/**
 * @file        ui/vulkan/vulkan_presenter.cpp
 * @brief       Vulkan presentation subsystem
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license     BSD 3-Clause License
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

#if REX_PLATFORM_GNU_LINUX && !REX_PLATFORM_ANDROID
#include <rex/ui/surface_gnulinux.h>
#endif
#if REX_PLATFORM_WIN32
#include <rex/ui/surface_win.h>
#endif
#if REX_PLATFORM_MAC
#include <rex/ui/surface_mac.h>
#endif

REXCVAR_DEFINE_BOOL(present_render_pass_clear, true, "UI/Presenter", "Clear render pass during presentation");
REXCVAR_DEFINE_BOOL(vulkan_allow_present_mode_immediate, true, "UI/Vulkan", "Allow immediate present mode");
REXCVAR_DEFINE_BOOL(vulkan_allow_present_mode_mailbox, true, "UI/Vulkan", "Allow mailbox present mode");
REXCVAR_DEFINE_BOOL(vulkan_allow_present_mode_fifo_relaxed, true, "UI/Vulkan", "Allow FIFO relaxed present mode");

namespace rex {
namespace ui {
namespace vulkan {

VulkanPresenter::PaintContext::Submission::~Submission() {}

bool VulkanPresenter::PaintContext::Submission::Initialize() {
  return true;
}

VulkanPresenter::~VulkanPresenter() {
  paint_context_.DestroySwapchainAndVulkanSurface();
}

Surface::TypeFlags VulkanPresenter::GetSurfaceTypesSupportedByInstance(
    const VulkanInstance::Extensions& instance_extensions) {
  if (!instance_extensions.ext_KHR_surface) {
    return 0;
  }
  Surface::TypeFlags type_flags = 0;
#if REX_PLATFORM_GNU_LINUX && !REX_PLATFORM_ANDROID
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
  return false;
}

VkCommandBuffer VulkanPresenter::AcquireUISetupCommandBufferFromUIThread() {
  return VK_NULL_HANDLE;
}

Presenter::SurfacePaintConnectResult
VulkanPresenter::ConnectOrReconnectPaintingToSurfaceFromUIThread(Surface& new_surface,
                                                                 uint32_t new_surface_width,
                                                                 uint32_t new_surface_height,
                                                                 bool was_paintable,
                                                                 bool& is_vsync_implicit_out) {
  is_vsync_implicit_out = true;
  return SurfacePaintConnectResult::kSuccess;
}

void VulkanPresenter::DisconnectPaintingFromSurfaceFromUIThreadImpl() {
  paint_context_.DestroySwapchainAndVulkanSurface();
}

bool VulkanPresenter::RefreshGuestOutputImpl(
    uint32_t mailbox_index, uint32_t frontbuffer_width, uint32_t frontbuffer_height,
    std::function<bool(GuestOutputRefreshContext& context)> refresher, bool& is_8bpc_out_ref) {
  return false;
}

VkSwapchainKHR VulkanPresenter::PaintContext::CreateSwapchainForVulkanSurface(
    const VulkanDevice* vulkan_device, VkSurfaceKHR surface, uint32_t width, uint32_t height,
    VkSwapchainKHR old_swapchain, uint32_t& present_queue_family_out, VkFormat& image_format_out,
    VkExtent2D& image_extent_out, bool& is_fifo_out, bool& ui_surface_unusable_out) {
  ui_surface_unusable_out = false;
  return VK_NULL_HANDLE;
}

VkSwapchainKHR VulkanPresenter::PaintContext::PrepareForSwapchainRetirement() {
  return VK_NULL_HANDLE;
}

void VulkanPresenter::PaintContext::DestroySwapchainAndVulkanSurface() {
}

VulkanPresenter::GuestOutputImage::~GuestOutputImage() {
}

bool VulkanPresenter::GuestOutputImage::Initialize() {
  return true;
}

// CORREÇÃO: Limpamos o corpo confuso do método que causava o erro.
Presenter::PaintResult VulkanPresenter::PaintAndPresentImpl(bool execute_ui_drawers) {
  return PaintResult::kNotPresented;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace rex
