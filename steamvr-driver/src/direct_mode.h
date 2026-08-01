#pragma once
#include <openvr_driver.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class SvrtDirectMode final : public vr::IVRDriverDirectModeComponent {
 public:
  SvrtDirectMode(); ~SvrtDirectMode();
  bool Start(const std::string &host, uint16_t port, unsigned fps,
             unsigned bitrate_mbps, const std::string &ffmpeg,
             const std::string &encoder);
  void Stop();
  void CreateSwapTextureSet(uint32_t pid,const SwapTextureSetDesc_t *desc,SwapTextureSet_t *out) override;
  void DestroySwapTextureSet(vr::SharedTextureHandle_t handle) override;
  void DestroyAllSwapTextureSets(uint32_t pid) override;
  void GetNextSwapTextureSetIndex(vr::SharedTextureHandle_t handles[2],uint32_t (*indices)[2]) override;
  void SubmitLayer(const SubmitLayerPerEye_t (&eyes)[2]) override;
  void Present(vr::SharedTextureHandle_t sync) override;
  void PostPresent(const Throttling_t *) override {}
  void GetFrameTiming(vr::DriverDirectMode_FrameTiming *timing) override { timing->m_nReprojectionFlags=0; }
 private:
  struct Texture { uint32_t pid=0; uint64_t group=0; HANDLE shared=nullptr; Microsoft::WRL::ComPtr<ID3D11Texture2D> texture; };
  struct Slot { Microsoft::WRL::ComPtr<ID3D11Texture2D> staging; bool pending=false; uint64_t sequence=0; };
  bool EnsureSlots(unsigned eye_width,unsigned height,DXGI_FORMAT format);
  bool StartEncoder(unsigned width,unsigned height);
  void EncoderThread(); void CloseEncoder();
  Microsoft::WRL::ComPtr<ID3D11Device> device_; Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  std::unordered_map<uint64_t,Texture> textures_; std::vector<Slot> slots_;
  vr::SharedTextureHandle_t submitted_[2]{}; uint32_t next_[2]{};
  std::mutex mutex_,d3d_mutex_; std::condition_variable ready_; std::thread worker_;
  std::atomic<bool> running_{false}; uint64_t sequence_=0; unsigned width_=0,height_=0,fps_=60,bitrate_=35;
  std::string host_,ffmpeg_,encoder_,pixel_format_="bgra"; uint16_t port_=9944; HANDLE pipe_=INVALID_HANDLE_VALUE,process_=nullptr;
};
