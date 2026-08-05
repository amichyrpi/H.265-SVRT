#include "audio.h"
#include "direct_mode.h"
#include "receiver_link.h"

#include <openvr_driver.h>

#include <chrono>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

namespace {
std::string setting(const char *key, const char *fallback) {
  char value[512]{};
  vr::VRSettings()->GetString("driver_svrt", key, value, sizeof(value));
  return value[0] ? value : fallback;
}

int int_setting(const char *key, int fallback) {
  const int value = vr::VRSettings()->GetInt32("driver_svrt", key);
  return value > 0 ? value : fallback;
}

bool receiver_connected_state(SvrtLinkState state) {
  return state != SvrtLinkState::Searching &&
         state != SvrtLinkState::ReceiverError;
}

bool receiver_stream_ready(SvrtLinkState state) {
  return state == SvrtLinkState::Ready || state == SvrtLinkState::Degraded;
}
}  // namespace

class SvrtHmd final : public vr::ITrackedDeviceServerDriver,
                      public vr::IVRDisplayComponent,
                      public vr::IVRVirtualDisplay {
 public:
  SvrtHmd() : serial_(setting("serial_number", "SVRT-PI4-001")) {}
  const char *serial() const { return serial_.c_str(); }

  vr::EVRInitError Activate(uint32_t id) override {
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
    id_.store(id);
    properties_.store(vr::VRProperties()->TrackedDeviceToPropertyContainer(id));
    const auto properties = properties_.load();
    vr::VRProperties()->SetStringProperty(properties, vr::Prop_ModelNumber_String,
                                           setting("model_number", "SVRT Wi-Fi HMD").c_str());
    vr::VRProperties()->SetStringProperty(properties,
                                           vr::Prop_ManufacturerName_String, "SVRT");
    vr::VRProperties()->SetStringProperty(properties,
                                           vr::Prop_ResourceRoot_String, "svrt");
    vr::VRProperties()->SetStringProperty(properties,
        vr::Prop_RegisteredDeviceType_String, "svrt/SVRT-PI4-001");
    SetIcon(properties, vr::Prop_NamedIconPathDeviceOff_String,
            "{svrt}/icons/headset_status_off.png");
    SetIcon(properties, vr::Prop_NamedIconPathDeviceSearching_String,
            "{svrt}/icons/headset_status_searching.gif");
    SetIcon(properties, vr::Prop_NamedIconPathDeviceSearchingAlert_String,
            "{svrt}/icons/headset_status_searching_alert.gif");
    SetIcon(properties, vr::Prop_NamedIconPathDeviceReady_String,
            "{svrt}/icons/headset_status_ready.png");
    SetIcon(properties, vr::Prop_NamedIconPathDeviceReadyAlert_String,
            "{svrt}/icons/headset_status_ready_alert.png");
    SetIcon(properties, vr::Prop_NamedIconPathDeviceNotReady_String,
            "{svrt}/icons/headset_status_error.png");
    SetIcon(properties, vr::Prop_NamedIconPathDeviceStandby_String,
            "{svrt}/icons/headset_status_standby.png");
    SetIcon(properties, vr::Prop_NamedIconPathDeviceStandbyAlert_String,
            "{svrt}/icons/headset_status_standby_alert.png");
    vr::VRProperties()->SetBoolProperty(properties, vr::Prop_IsOnDesktop_Bool,
                                        false);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_HasDriverDirectModeComponent_Bool, false);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_HasDisplayComponent_Bool, true);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_HasVirtualDisplayComponent_Bool, true);
    vr::VRProperties()->SetBoolProperty(properties,
        vr::Prop_ReportsTimeSinceVSync_Bool, false);
    vr::VRProperties()->SetBoolProperty(properties,
                                        vr::Prop_DeviceIsWireless_Bool, true);
    vr::VRProperties()->SetBoolProperty(properties,
                                        vr::Prop_ContainsProximitySensor_Bool, false);
    vr::VRProperties()->SetBoolProperty(properties,
                                        vr::Prop_DeviceCanPowerOff_Bool, false);
    vr::VRProperties()->SetBoolProperty(properties,
                                        vr::Prop_NeverTracked_Bool, false);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_IgnoreMotionForStandby_Bool, true);
    vr::VRProperties()->SetFloatProperty(properties,
        vr::Prop_DisplayFrequency_Float, static_cast<float>(fps()));
    vr::VRProperties()->SetFloatProperty(
        properties, vr::Prop_SecondsFromVsyncToPhotons_Float, .020f);
    // Apply this after all static properties so the offline proximity and icon
    // state cannot be overwritten by activation defaults above.
    icons_receiver_available_ = true;
    UpdateConnectionIcons(false);
    const std::string host = setting("receiver_host", "ROOT.local");
    const uint16_t video_port =
        static_cast<uint16_t>(int_setting("receiver_port", 9944));
    direct_.Start(host, video_port, fps(), int_setting("bitrate_mbps", 35),
                  setting("ffmpeg_path", "ffmpeg.exe"),
                  setting("encoder", "hevc_nvenc"));
    audio_.Start(host, static_cast<uint16_t>(
                           int_setting("audio_port", video_port + 2)));
    receiver_.Start(host,
                    static_cast<uint16_t>(int_setting("status_port", video_port + 1)),
                    int_setting("health_poll_ms", 1000),
                    int_setting("latency_warning_ms", 80));
    return vr::VRInitError_None;
  }

  void Deactivate() override {
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
    receiver_.Stop();
    audio_.Stop();
    direct_.Stop();
    properties_.store(vr::k_ulInvalidPropertyContainer);
    id_.store(vr::k_unTrackedDeviceIndexInvalid);
  }
  void EnterStandby() override {}
  void *GetComponent(const char *version) override {
    if (!std::strcmp(version, vr::IVRDisplayComponent_Version))
      return static_cast<vr::IVRDisplayComponent *>(this);
    // Keep the component on the HMD as a compatibility path for SteamVR
    // builds that deliver virtual-display callbacks to the active HMD rather
    // than the auxiliary DisplayRedirect device. The redirect device remains
    // registered for current runtimes and for the desktop mirror path.
    if (!std::strcmp(version, vr::IVRVirtualDisplay_Version))
      return static_cast<vr::IVRVirtualDisplay *>(this);
    return nullptr;
  }
  void DebugRequest(const char *, char *out, uint32_t size) override {
    if (!size) return;
    const SvrtLinkStatus status = receiver_.GetStatus();
    std::snprintf(out, size,
                  "receiver=%s latency=%ums decoded=%llu shown=%llu dropped=%llu encoder=%s",
                  SvrtReceiverLink::StateName(status.state), status.latency_ms,
                  static_cast<unsigned long long>(status.decoded),
                  static_cast<unsigned long long>(status.presented),
                  static_cast<unsigned long long>(status.dropped),
                  direct_.EncoderFailed() ? "failed" : "ok");
  }

  vr::DriverPose_t GetPose() override {
    const SvrtLinkStatus status = receiver_.GetStatus();
    vr::DriverPose_t pose{};
    pose.qWorldFromDriverRotation.w = 1;
    pose.qDriverFromHeadRotation.w = 1;
    pose.qRotation.w = 1;
    pose.shouldApplyHeadModel = true;
    // A reachable status service means the HMD is present even while the Pi
    // is still opening its video listener.  Treating STARTING as disconnected
    // makes SteamVR report HmdNotFound during the normal boot transition.
    const bool receiver_connected = receiver_connected_state(status.state);
    pose.deviceIsConnected = receiver_connected;
    pose.poseIsValid = receiver_connected;
    pose.willDriftInYaw = false;
    // The receiver has no optical/tracker source; its stable identity and
    // zero pose are intentional. Reporting OutOfRange for ordinary latency
    // warnings makes SteamVR repeatedly invalidate the scene and mirror. Keep
    // a connected HMD in Running_OK and reserve Uninitialized for a real loss
    // of the status link.
    pose.result = receiver_connected ? vr::TrackingResult_Running_OK
                                     : vr::TrackingResult_Uninitialized;
    return pose;
  }

  void RunFrame() {
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
    const uint32_t id = id_.load();
    if (id != vr::k_unTrackedDeviceIndexInvalid) {
      const SvrtLinkStatus status = receiver_.GetStatus();
      const bool receiver_connected = receiver_connected_state(status.state);
      const bool receiver_available = receiver_stream_ready(status.state);
      const int state = static_cast<int>(status.state);
      if (state != last_pose_state_) {
        last_pose_state_ = state;
        if (vr::VRDriverLog()) {
          char message[160];
          std::snprintf(message, sizeof(message),
                        "SVRT: pose state=%s connected=%d stream=%d",
                        SvrtReceiverLink::StateName(status.state),
                        receiver_connected ? 1 : 0,
                        receiver_available ? 1 : 0);
          vr::VRDriverLog()->Log(message);
        }
      }
      // Send one explicit transition to the off state, then stop publishing
      // the fake identity pose while the physical receiver is absent. Repeated
      // offline poses make SteamVR treat the activated HMD as sleeping.
      if (receiver_connected || receiver_connected != pose_receiver_available_) {
        const vr::DriverPose_t pose = GetPose();
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(
            id, pose, sizeof(pose));
      }
      pose_receiver_available_ = receiver_connected;
      UpdateConnectionIcons(receiver_connected);
      direct_.SetReceiverAvailable(receiver_available);
      audio_.SetReceiverAvailable(receiver_available);
    }
  }

  SvrtDirectMode &direct() { return direct_; }
  bool receiver_available() const {
    return receiver_stream_ready(receiver_.GetStatus().state);
  }

  void GetWindowBounds(int32_t *x, int32_t *y, uint32_t *width,
                       uint32_t *height) override {
    *x = *y = 0;
    *width = eye_width() * 2;
    *height = eye_height();
  }
  bool IsDisplayOnDesktop() override { return false; }
  bool IsDisplayRealDisplay() override { return false; }
  void GetRecommendedRenderTargetSize(uint32_t *width,
                                      uint32_t *height) override {
    *width = eye_width();
    *height = eye_height();
  }
  void GetEyeOutputViewport(vr::EVREye eye, uint32_t *x, uint32_t *y,
                            uint32_t *width, uint32_t *height) override {
    *x = eye == vr::Eye_Left ? 0 : eye_width();
    *y = 0;
    *width = eye_width();
    *height = eye_height();
  }
  void GetProjectionRaw(vr::EVREye, float *left, float *right, float *top,
                        float *bottom) override {
    *left = *top = -1;
    *right = *bottom = 1;
  }
  vr::DistortionCoordinates_t ComputeDistortion(vr::EVREye, float u,
                                                 float v) override {
    vr::DistortionCoordinates_t result{};
    result.rfRed[0] = result.rfGreen[0] = result.rfBlue[0] = u;
    result.rfRed[1] = result.rfGreen[1] = result.rfBlue[1] = v;
    return result;
  }
  bool ComputeInverseDistortion(vr::HmdVector2_t *, vr::EVREye, uint32_t,
                                float, float) override { return false; }

  void Present(const vr::PresentInfo_t *info, uint32_t size) override {
    if (!info || size < sizeof(*info) ||
        id_.load() == vr::k_unTrackedDeviceIndexInvalid ||
        !receiver_available()) return;
    direct_.PresentVirtual(info->backbufferTextureHandle);
    std::lock_guard<std::mutex> lock(vsync_mutex_);
    vsync_ = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    frame_ = info->nFrameId;
  }
  void WaitForPresent() override { direct_.WaitForVirtualPresent(); }
  bool GetTimeSinceLastVsync(float *seconds, uint64_t *frame) override {
    if (!seconds || !frame ||
        id_.load() == vr::k_unTrackedDeviceIndexInvalid)
      return false;
    std::lock_guard<std::mutex> lock(vsync_mutex_);
    if (vsync_ <= 0) return false;
    const double now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    *seconds = static_cast<float>(now - vsync_);
    *frame = frame_;
    return true;
  }

 private:
  static void SetIcon(vr::PropertyContainerHandle_t properties,
                      vr::ETrackedDeviceProperty property, const char *path) {
    vr::VRProperties()->SetStringProperty(properties, property, path);
  }
  void UpdateConnectionIcons(bool available) {
    const auto properties = properties_.load();
    if (available == icons_receiver_available_ ||
        properties == vr::k_ulInvalidPropertyContainer) return;
    icons_receiver_available_ = available;
    // An offline wireless receiver is not a sleeping/worn HMD. Disabling the
    // proximity capability while it is absent prevents SteamVR from replacing
    // the correct searching/offline state with its inactivity standby state.
    if (!available) {
      vr::VRProperties()->SetBoolProperty(
          properties, vr::Prop_ContainsProximitySensor_Bool, false);
      vr::VRProperties()->SetBoolProperty(
          properties, vr::Prop_IgnoreMotionForStandby_Bool, false);
    } else {
      vr::VRProperties()->SetBoolProperty(
          properties, vr::Prop_ContainsProximitySensor_Bool, false);
      vr::VRProperties()->SetBoolProperty(
          properties, vr::Prop_IgnoreMotionForStandby_Bool, true);
    }
    const char *off = "{svrt}/icons/headset_status_off.png";
    SetIcon(properties, vr::Prop_NamedIconPathDeviceOff_String, off);
    SetIcon(properties, vr::Prop_NamedIconPathDeviceSearching_String,
            available ? "{svrt}/icons/headset_status_searching.gif" : off);
    SetIcon(properties, vr::Prop_NamedIconPathDeviceSearchingAlert_String,
            available ? "{svrt}/icons/headset_status_searching_alert.gif" : off);
    SetIcon(properties, vr::Prop_NamedIconPathDeviceReady_String,
            available ? "{svrt}/icons/headset_status_ready.png" : off);
    SetIcon(properties, vr::Prop_NamedIconPathDeviceReadyAlert_String,
            available ? "{svrt}/icons/headset_status_ready_alert.png" : off);
    SetIcon(properties, vr::Prop_NamedIconPathDeviceNotReady_String,
            available ? "{svrt}/icons/headset_status_error.png" : off);
    SetIcon(properties, vr::Prop_NamedIconPathDeviceStandby_String,
            available ? "{svrt}/icons/headset_status_standby.png" : off);
    SetIcon(properties, vr::Prop_NamedIconPathDeviceStandbyAlert_String,
            available ? "{svrt}/icons/headset_status_standby_alert.png" : off);
  }
  unsigned eye_width() const { return int_setting("render_width", 1280); }
  unsigned eye_height() const { return int_setting("render_height", 1440); }
  unsigned fps() const { return int_setting("display_frequency", 60); }

  std::atomic<uint32_t> id_{vr::k_unTrackedDeviceIndexInvalid};
  std::atomic<vr::PropertyContainerHandle_t> properties_{
      vr::k_ulInvalidPropertyContainer};
  mutable std::mutex lifecycle_mutex_;
  bool icons_receiver_available_ = false;
  bool pose_receiver_available_ = true;
  int last_pose_state_ = -1;
  std::string serial_;
  SvrtDirectMode direct_;
  SvrtAudioTransport audio_;
  SvrtReceiverLink receiver_;
  mutable std::mutex vsync_mutex_;
  double vsync_ = 0;
  uint64_t frame_ = 0;
};

// SteamVR routes the final composited backbuffer through a DisplayRedirect
// device. Keeping this component separate from the HMD is required by
// IVRVirtualDisplay and also makes the compositor's desktop mirror use the
// same image that is sent to the wireless receiver.
class SvrtDisplayRedirect final : public vr::ITrackedDeviceServerDriver,
                                  public vr::IVRVirtualDisplay {
 public:
  explicit SvrtDisplayRedirect(SvrtHmd *hmd)
      : hmd_(hmd), serial_(hmd ? std::string(hmd->serial()) + "-redirect"
                               : "SVRT-redirect") {}
  const char *serial() const { return serial_.c_str(); }

  vr::EVRInitError Activate(uint32_t id) override {
    id_.store(id);
    properties_.store(vr::VRProperties()->TrackedDeviceToPropertyContainer(id));
    const auto properties = properties_.load();
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_ModelNumber_String, "SVRT Display Redirect");
    vr::VRProperties()->SetStringProperty(properties,
                                           vr::Prop_ManufacturerName_String,
                                           "SVRT");
    vr::VRProperties()->SetStringProperty(properties,
                                           vr::Prop_ResourceRoot_String, "svrt");
    vr::VRProperties()->SetBoolProperty(properties,
                                        vr::Prop_IsOnDesktop_Bool, false);
    vr::VRProperties()->SetBoolProperty(properties,
                                        vr::Prop_DeviceIsWireless_Bool, true);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_HasVirtualDisplayComponent_Bool, true);
    // A redirect device must identify the adapter that owns its shared
    // textures. Without this property SteamVR can register the device but
    // never route composited frames to IVRVirtualDisplay::Present, leaving VR
    // View on the static gray fallback.
    vr::VRProperties()->SetUint64Property(
        properties, vr::Prop_GraphicsAdapterLuid_Uint64,
        hmd_ ? hmd_->direct().GraphicsAdapterLuid() : 0);
    vr::VRProperties()->SetFloatProperty(
        properties, vr::Prop_SecondsFromVsyncToPhotons_Float, .020f);
    vr::VRProperties()->SetBoolProperty(
        properties, vr::Prop_ReportsTimeSinceVSync_Bool, true);
    return vr::VRInitError_None;
  }

  void Deactivate() override {
    id_.store(vr::k_unTrackedDeviceIndexInvalid);
    properties_.store(vr::k_ulInvalidPropertyContainer);
  }

  void *GetComponent(const char *version) override {
    if (!std::strcmp(version, vr::IVRVirtualDisplay_Version))
      return static_cast<vr::IVRVirtualDisplay *>(this);
    return nullptr;
  }

  void EnterStandby() override {}
  void DebugRequest(const char *, char *out, uint32_t size) override {
    if (size) out[0] = 0;
  }
  vr::DriverPose_t GetPose() override {
    vr::DriverPose_t pose{};
    pose.poseIsValid = true;
    pose.deviceIsConnected = true;
    pose.result = vr::TrackingResult_Running_OK;
    pose.qWorldFromDriverRotation.w = 1;
    pose.qDriverFromHeadRotation.w = 1;
    pose.qRotation.w = 1;
    return pose;
  }

  void Present(const vr::PresentInfo_t *info, uint32_t size) override {
    if (!hmd_ || !info || size < sizeof(*info) ||
        id_.load() == vr::k_unTrackedDeviceIndexInvalid ||
        !hmd_->direct().ReceiverAvailable())
      return;
    static std::atomic<uint64_t> callback_count{0};
    const uint64_t count = ++callback_count;
    if ((count == 1 || count % 120 == 0) && vr::VRDriverLog()) {
      char message[128];
      std::snprintf(message, sizeof(message),
                    "SVRT: display redirect Present callback=%llu",
                    static_cast<unsigned long long>(count));
      vr::VRDriverLog()->Log(message);
    }
    hmd_->direct().PresentVirtual(info->backbufferTextureHandle);
    std::lock_guard<std::mutex> lock(vsync_mutex_);
    vsync_ = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    frame_ = info->nFrameId;
  }

  void WaitForPresent() override {
    if (hmd_) hmd_->direct().WaitForVirtualPresent();
  }

  bool GetTimeSinceLastVsync(float *seconds, uint64_t *frame) override {
    if (!seconds || !frame ||
        id_.load() == vr::k_unTrackedDeviceIndexInvalid)
      return false;
    std::lock_guard<std::mutex> lock(vsync_mutex_);
    if (vsync_ <= 0) return false;
    const double now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    *seconds = static_cast<float>(now - vsync_);
    *frame = frame_;
    return true;
  }

 private:
  SvrtHmd *hmd_ = nullptr;
  std::string serial_;
  std::atomic<uint32_t> id_{vr::k_unTrackedDeviceIndexInvalid};
  std::atomic<vr::PropertyContainerHandle_t> properties_{
      vr::k_ulInvalidPropertyContainer};
  mutable std::mutex vsync_mutex_;
  double vsync_ = 0;
  uint64_t frame_ = 0;
};

class Provider final : public vr::IServerTrackedDeviceProvider {
 public:
  vr::EVRInitError Init(vr::IVRDriverContext *context) override {
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
    active_ = false;
    // Cleanup deliberately leaves the device objects quarantined until the
    // next Init.  This prevents a late vrserver callback from calling through
    // a freed OpenVR vtable during shutdown.  A new Init is the safe point at
    // which the old runtime has finished dispatching callbacks.
    VR_INIT_SERVER_DRIVER_CONTEXT(context);
    hmd_.reset();
    redirect_.reset();
    hmd_ = std::make_unique<SvrtHmd>();
    if (!hmd_->direct().EnsureDevice()) {
      hmd_.reset();
      VR_CLEANUP_SERVER_DRIVER_CONTEXT();
      return vr::VRInitError_Driver_Unknown;
    }
    if (!vr::VRServerDriverHost()->TrackedDeviceAdded(
            hmd_->serial(), vr::TrackedDeviceClass_HMD, hmd_.get())) {
      hmd_.reset();
      VR_CLEANUP_SERVER_DRIVER_CONTEXT();
      return vr::VRInitError_Driver_Unknown;
    }
    redirect_ = std::make_unique<SvrtDisplayRedirect>(hmd_.get());
    if (!vr::VRServerDriverHost()->TrackedDeviceAdded(
            redirect_->serial(), vr::TrackedDeviceClass_DisplayRedirect,
            redirect_.get())) {
      redirect_.reset();
      hmd_->Deactivate();
      hmd_.reset();
      VR_CLEANUP_SERVER_DRIVER_CONTEXT();
      return vr::VRInitError_Driver_Unknown;
    }
    active_ = true;
    return vr::VRInitError_None;
  }

  void Cleanup() override {
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
    active_ = false;
    // Stop every callback-producing worker before dropping the OpenVR
    // context.  Keep the tracked-device objects alive until the next Init
    // (or DLL unload): vrserver can still dispatch a final callback while it
    // is removing the device records, and deleting the vtables here causes
    // the exit-time access violation seen in vrserver.exe.
    if (redirect_) redirect_->Deactivate();
    if (hmd_) hmd_->Deactivate();
    VR_CLEANUP_SERVER_DRIVER_CONTEXT();
  }
  const char *const *GetInterfaceVersions() override {
    return vr::k_InterfaceVersions;
  }
  void RunFrame() override {
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
    if (active_ && hmd_) hmd_->RunFrame();
  }
  bool ShouldBlockStandbyMode() override { return false; }
  void EnterStandby() override {}
  void LeaveStandby() override {}

 private:
  std::mutex lifecycle_mutex_;
  bool active_ = false;
  std::unique_ptr<SvrtHmd> hmd_;
  std::unique_ptr<SvrtDisplayRedirect> redirect_;
};

static Provider provider;
extern "C" __declspec(dllexport) void *HmdDriverFactory(const char *name,
                                                         int *error) {
  if (!std::strcmp(name, vr::IServerTrackedDeviceProvider_Version))
    return &provider;
  if (error) *error = vr::VRInitError_Init_InterfaceNotFound;
  return nullptr;
}
