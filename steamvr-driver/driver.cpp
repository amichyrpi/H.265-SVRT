#include "audio.h"
#include "direct_mode.h"
#include "receiver_link.h"

#include <openvr_driver.h>

#include <algorithm>
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

float float_setting(const char *key, float fallback) {
  const float value = vr::VRSettings()->GetFloat("driver_svrt", key);
  return std::isfinite(value) && value > 0.0f ? value : fallback;
}

bool receiver_connected_state(SvrtLinkState state) {
  // Searching means the receiver is absent.  Starting and ReceiverError mean
  // the link/device is still present but not currently trackable.
  return state != SvrtLinkState::Searching;
}

bool receiver_stream_ready(SvrtLinkState state) {
  return state == SvrtLinkState::Ready || state == SvrtLinkState::Degraded;
}

// The Pi pose is expressed in raw driver space with Y=0 at the floor.  A
// driver-provided universe keeps SteamVR from falling into Room Setup (C200)
// when the first valid pose arrives.
constexpr char kChaperoneJson[] = R"svrt({
  "json_id": "chaperone_info",
  "version": 5,
  "time": "2026-08-07T00:00:00Z",
  "universes": [
    {
      "universeID": 1,
      "collision_bounds": [
        [[-1.5, 0.0, -1.5], [1.5, 0.0, -1.5], [1.5, 2.5, -1.5], [-1.5, 2.5, -1.5]],
        [[1.5, 0.0, -1.5], [1.5, 0.0, 1.5], [1.5, 2.5, 1.5], [1.5, 2.5, -1.5]],
        [[1.5, 0.0, 1.5], [-1.5, 0.0, 1.5], [-1.5, 2.5, 1.5], [1.5, 2.5, 1.5]],
        [[-1.5, 0.0, 1.5], [-1.5, 0.0, -1.5], [-1.5, 2.5, -1.5], [-1.5, 2.5, 1.5]]
      ],
      "play_area": [3.0, 3.0],
      "seated": {
        "translation": [0.0, 0.0, 0.0],
        "yaw": 0.0
      },
      "standing": {
        "translation": [0.0, 0.0, 0.0],
        "yaw": 0.0
      },
      "setup_standing2": {
        "translation": [0.0, 0.0, 0.0],
        "yaw": 0.0
      }
    }
  ]
})svrt";
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
    tracking_seen_ = false;
    properties_.store(vr::VRProperties()->TrackedDeviceToPropertyContainer(id));
    const auto properties = properties_.load();
    vr::VRProperties()->SetStringProperty(properties, vr::Prop_ModelNumber_String,
                                           setting("model_number", "SVRT Wi-Fi HMD").c_str());
    vr::VRProperties()->SetStringProperty(properties,
                                           vr::Prop_ManufacturerName_String, "SVRT");
    // Keep the device identity explicit while SteamVR activates the virtual
    // display and builds its status entry.
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_TrackingSystemName_String, "svrt");
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_SerialNumber_String, serial_.c_str());
    vr::VRProperties()->SetStringProperty(properties,
                                           vr::Prop_ResourceRoot_String, "svrt");
    vr::VRProperties()->SetStringProperty(properties,
                                           vr::Prop_RegisteredDeviceType_String, "svrt/SVRT-PI4-001");
    vr::VRProperties()->SetUint64Property(
        properties, vr::Prop_CurrentUniverseId_Uint64, 1);
    vr::VRProperties()->SetStringProperty(
        properties, vr::Prop_DriverProvidedChaperoneJson_String,
        kChaperoneJson);
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
        vr::Prop_ReportsTimeSinceVSync_Bool, true);
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
    vr::VRDriverInput()->CreateBooleanComponent(
        properties, "/input/system/touch", &system_touch_);
    vr::VRDriverInput()->CreateBooleanComponent(
        properties, "/input/system/click", &system_click_);
    // Apply this after all static properties so the offline proximity and icon
    // state cannot be overwritten by activation defaults above.
    icons_receiver_available_ = true;
    UpdateConnectionIcons(false);
    const std::string host = setting("receiver_host", "ROOT.local");
    const uint16_t video_port =
        static_cast<uint16_t>(int_setting("receiver_port", 9944));
    direct_.Start(host, video_port, fps(), int_setting("bitrate_mbps", 12),
                  setting("ffmpeg_path", "ffmpeg.exe"),
                  setting("encoder", "hevc_nvenc"));
    audio_.Start(host, static_cast<uint16_t>(
                           int_setting("audio_port", video_port + 2)));
    receiver_.Start(host,
                    static_cast<uint16_t>(int_setting("status_port", video_port + 1)),
                    int_setting("tracking_poll_ms", 10),
                    int_setting("latency_warning_ms", 80));
    return vr::VRInitError_None;
  }

  void Deactivate() override {
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
    receiver_.Stop();
    audio_.Stop();
    direct_.Stop();
    tracking_seen_ = false;
    properties_.store(vr::k_ulInvalidPropertyContainer);
    id_.store(vr::k_unTrackedDeviceIndexInvalid);
  }
  void EnterStandby() override {}
  void *GetComponent(const char *version) override {
    if (!std::strcmp(version, vr::IVRDisplayComponent_Version))
      return static_cast<vr::IVRDisplayComponent *>(this);
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
    // The Pi supplies a complete 6DoF pose.  Do not add SteamVR's neck/head
    // model translation on top of the supplied position.
    pose.shouldApplyHeadModel = false;
    // A reachable status service means the HMD is present even while the Pi
    // is still opening its video listener.  Treating STARTING as disconnected
    // makes SteamVR report HmdNotFound during the normal boot transition.
    const bool receiver_connected = receiver_connected_state(status.state);
    pose.deviceIsConnected = receiver_connected;
    if (status.pose.fresh) tracking_seen_ = true;
    const bool pose_available = receiver_connected && status.pose.connected &&
                                status.pose.valid && status.pose.fresh;
    pose.poseIsValid = pose_available;
    if (pose_available) {
      pose.vecPosition[0] = status.pose.position[0];
      pose.vecPosition[1] = status.pose.position[1];
      pose.vecPosition[2] = status.pose.position[2];
      pose.vecVelocity[0] = status.pose.velocity[0];
      pose.vecVelocity[1] = status.pose.velocity[1];
      pose.vecVelocity[2] = status.pose.velocity[2];
      pose.qRotation.x = status.pose.quaternion[0];
      pose.qRotation.y = status.pose.quaternion[1];
      pose.qRotation.z = status.pose.quaternion[2];
      pose.qRotation.w = status.pose.quaternion[3];
      pose.vecAngularVelocity[0] = status.pose.angular_velocity[0];
      pose.vecAngularVelocity[1] = status.pose.angular_velocity[1];
      pose.vecAngularVelocity[2] = status.pose.angular_velocity[2];
    } else {
      pose.qRotation.w = 1;
    }
    pose.willDriftInYaw = false;
    if (pose_available) {
      pose.result = vr::TrackingResult_Running_OK;
    } else if (!receiver_connected) {
      pose.result = vr::TrackingResult_Uninitialized;
    } else if (tracking_seen_) {
      pose.result = vr::TrackingResult_Running_OutOfRange;
    } else {
      pose.result = vr::TrackingResult_Calibrating_OutOfRange;
    }
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
          const vr::DriverPose_t pose = GetPose();
          std::snprintf(message, sizeof(message),
                        "SVRT: pose state=%s connected=%d valid=%d result=%d stream=%d",
                        SvrtReceiverLink::StateName(status.state),
                        pose.deviceIsConnected ? 1 : 0,
                        pose.poseIsValid ? 1 : 0,
                        static_cast<int>(pose.result),
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

      if (system_touch_ != vr::k_ulInvalidInputComponentHandle) {
          vr::VRDriverInput()->UpdateBooleanComponent(
              system_touch_, false, 0.0);
      }
      
      if (system_click_ != vr::k_ulInvalidInputComponentHandle) {
          vr::VRDriverInput()->UpdateBooleanComponent(
              system_click_, false, 0.0);
      }
      
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
    // SteamVR applies late rotational reprojection after the eye image has
    // been rendered. With an exactly edge-to-edge projection, that rotation
    // exposes pixels outside the rectangular source texture as mirrored black
    // triangles in the outer top corners (and a strip at the opposite edge).
    // Render a modest symmetric guard band so the reprojected visible area
    // remains covered. This does not alter the streamed output dimensions.
    const float tangent = std::clamp(
        float_setting("projection_tangent", 1.15f), 1.0f, 1.5f);
    *left = *top = -tangent;
    *right = *bottom = tangent;
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
  bool tracking_seen_ = false;
  vr::VRInputComponentHandle_t proximity_ =
      vr::k_ulInvalidInputComponentHandle;
  vr::VRInputComponentHandle_t system_touch_ =
      vr::k_ulInvalidInputComponentHandle;
  vr::VRInputComponentHandle_t system_click_ =
      vr::k_ulInvalidInputComponentHandle;
  int last_pose_state_ = -1;
  std::string serial_;
  SvrtDirectMode direct_;
  SvrtAudioTransport audio_;
  SvrtReceiverLink receiver_;
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
};

static Provider provider;
extern "C" __declspec(dllexport) void *HmdDriverFactory(const char *name,
                                                         int *error) {
  if (!std::strcmp(name, vr::IServerTrackedDeviceProvider_Version))
    return &provider;
  if (error) *error = vr::VRInitError_Init_InterfaceNotFound;
  return nullptr;
}
