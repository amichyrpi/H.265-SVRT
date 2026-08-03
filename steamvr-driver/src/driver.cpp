#include "audio_transport.h"
#include "direct_mode.h"
#include "receiver_link.h"

#include <openvr_driver.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
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
}  // namespace

class SvrtHmd final : public vr::ITrackedDeviceServerDriver,
                      public vr::IVRDisplayComponent {
 public:
  SvrtHmd() : serial_(setting("serial_number", "SVRT-PI4-001")) {}
  const char *serial() const { return serial_.c_str(); }

  vr::EVRInitError Activate(uint32_t id) override {
    id_ = id;
    const auto properties =
        vr::VRProperties()->TrackedDeviceToPropertyContainer(id);
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
    vr::VRProperties()->SetBoolProperty(properties,
        vr::Prop_ReportsTimeSinceVSync_Bool, false);
    vr::VRProperties()->SetBoolProperty(properties,
                                        vr::Prop_DeviceIsWireless_Bool, true);
    vr::VRProperties()->SetBoolProperty(properties,
                                        vr::Prop_ContainsProximitySensor_Bool, true);
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
    const vr::EVRInputError proximity_error =
        vr::VRDriverInput()->CreateBooleanComponent(
            properties, "/proximity", &proximity_);
    if (proximity_error != vr::VRInputError_None)
      return vr::VRInitError_Driver_Unknown;
    vr::VRDriverInput()->UpdateBooleanComponent(proximity_, true, 0.0);
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
    receiver_.Stop();
    audio_.Stop();
    direct_.Stop();
    proximity_ = vr::k_ulInvalidInputComponentHandle;
    id_ = vr::k_unTrackedDeviceIndexInvalid;
  }
  void EnterStandby() override {}
  void *GetComponent(const char *version) override {
    if (!std::strcmp(version, vr::IVRDisplayComponent_Version))
      return static_cast<vr::IVRDisplayComponent *>(this);
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
    const bool receiver_available = status.state != SvrtLinkState::Searching &&
                                    status.state != SvrtLinkState::ReceiverError;
    pose.deviceIsConnected = receiver_available;
    pose.poseIsValid = receiver_available;
    pose.willDriftInYaw = false;
    pose.result = receiver_available
                      ? (status.state == SvrtLinkState::Degraded
                             ? vr::TrackingResult_Running_OutOfRange
                             : vr::TrackingResult_Running_OK)
                      : vr::TrackingResult_Uninitialized;
    return pose;
  }

  void RunFrame() {
    if (id_ != vr::k_unTrackedDeviceIndexInvalid) {
      vr::VRServerDriverHost()->TrackedDevicePoseUpdated(
          id_, GetPose(), sizeof(vr::DriverPose_t));
      const SvrtLinkStatus status = receiver_.GetStatus();
      const bool receiver_available = status.state != SvrtLinkState::Searching &&
                                      status.state != SvrtLinkState::ReceiverError;
      if (proximity_ != vr::k_ulInvalidInputComponentHandle)
        vr::VRDriverInput()->UpdateBooleanComponent(proximity_, receiver_available, 0.0);
    }
  }

  SvrtDirectMode &direct() { return direct_; }

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

 private:
  static void SetIcon(vr::PropertyContainerHandle_t properties,
                      vr::ETrackedDeviceProperty property, const char *path) {
    vr::VRProperties()->SetStringProperty(properties, property, path);
  }
  unsigned eye_width() const { return int_setting("render_width", 1280); }
  unsigned eye_height() const { return int_setting("render_height", 1440); }
  unsigned fps() const { return int_setting("display_frequency", 60); }

  uint32_t id_ = vr::k_unTrackedDeviceIndexInvalid;
  vr::VRInputComponentHandle_t proximity_ =
      vr::k_ulInvalidInputComponentHandle;
  std::string serial_;
  SvrtDirectMode direct_;
  SvrtAudioTransport audio_;
  SvrtReceiverLink receiver_;
};

class SvrtDisplayRedirect final : public vr::ITrackedDeviceServerDriver, public vr::IVRVirtualDisplay {
 public:
  explicit SvrtDisplayRedirect(SvrtDirectMode &direct) : direct_(direct) {}
  vr::EVRInitError Activate(uint32_t id) override { id_ = id; return vr::VRInitError_None; }
  void Deactivate() override { id_ = vr::k_unTrackedDeviceIndexInvalid; }
  void EnterStandby() override {}
  void *GetComponent(const char *version) override { return !std::strcmp(version, vr::IVRVirtualDisplay_Version) ? static_cast<vr::IVRVirtualDisplay *>(this) : nullptr; }
  void DebugRequest(const char *, char *out, uint32_t size) override { if (size) out[0] = '\0'; }
  vr::DriverPose_t GetPose() override { vr::DriverPose_t pose{}; pose.deviceIsConnected = pose.poseIsValid = true; pose.result = vr::TrackingResult_Running_OK; pose.qWorldFromDriverRotation.w = pose.qDriverFromHeadRotation.w = pose.qRotation.w = 1; return pose; }
  void Present(const vr::PresentInfo_t *info, uint32_t size) override { if (!info || size < sizeof(*info)) return; direct_.PresentVirtual(info->backbufferTextureHandle); vsync_ = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); frame_ = info->nFrameId; }
  void WaitForPresent() override {}
  bool GetTimeSinceLastVsync(float *seconds, uint64_t *frame) override { if (!seconds || !frame || vsync_ <= 0) return false; const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); *seconds = static_cast<float>(now - vsync_); *frame = frame_; return true; }
 private:
  SvrtDirectMode &direct_; uint32_t id_ = vr::k_unTrackedDeviceIndexInvalid; double vsync_ = 0; uint64_t frame_ = 0;
};

class Provider final : public vr::IServerTrackedDeviceProvider {
 public:
  vr::EVRInitError Init(vr::IVRDriverContext *context) override {
    VR_INIT_SERVER_DRIVER_CONTEXT(context);
    hmd_ = std::make_unique<SvrtHmd>();
    if (!vr::VRServerDriverHost()->TrackedDeviceAdded(
        hmd_->serial(), vr::TrackedDeviceClass_HMD, hmd_.get())) return vr::VRInitError_Driver_Unknown;
    redirect_ = std::make_unique<SvrtDisplayRedirect>(hmd_->direct());
    return vr::VRServerDriverHost()->TrackedDeviceAdded("SVRT-PI4-001-display", vr::TrackedDeviceClass_DisplayRedirect, redirect_.get()) ? vr::VRInitError_None : vr::VRInitError_Driver_Unknown;
  }

  void Cleanup() override { redirect_.reset(); hmd_.reset(); }
  const char *const *GetInterfaceVersions() override {
    return vr::k_InterfaceVersions;
  }
  void RunFrame() override { if (hmd_) hmd_->RunFrame(); }
  bool ShouldBlockStandbyMode() override { return true; }
  void EnterStandby() override {}
  void LeaveStandby() override {}

 private:
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
