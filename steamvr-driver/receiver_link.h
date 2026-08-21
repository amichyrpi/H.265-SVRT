#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <mutex>
#include <thread>

enum class SvrtLinkState : int {
  Starting,
  Searching,
  Ready,
  Degraded,
  ReceiverError,
};

struct SvrtPose {
  bool valid = false;
  bool connected = false;
  bool fresh = false;
  int result = 101;
  uint64_t sequence = 0;
  uint64_t timestamp_us = 0;
  double position[3]{};
  double quaternion[4]{0.0, 0.0, 0.0, 1.0};
  double velocity[3]{};
  double angular_velocity[3]{};
};

struct SvrtLinkStatus {
  SvrtLinkState state = SvrtLinkState::Searching;
  unsigned latency_ms = 0;
  uint64_t decoded = 0;
  uint64_t presented = 0;
  uint64_t dropped = 0;
  uint64_t bytes = 0;
  uint64_t invalid_packets = 0;
  uint64_t fec_recovered = 0;
  uint64_t network_dropped = 0;
  SvrtPose pose;
};

class SvrtReceiverLink {
 public:
  SvrtReceiverLink() = default;
  ~SvrtReceiverLink();
  bool Start(std::string host, uint16_t port, unsigned poll_ms,
             unsigned latency_warning_ms);
  void Stop();
  SvrtLinkStatus GetStatus() const;
  static const char *StateName(SvrtLinkState state);

 private:
  void Run();
  void TrackingRun();
  bool Poll(uint64_t nonce, SvrtLinkStatus &status);

  std::string host_;
  uint16_t port_ = 9945;
  unsigned poll_ms_ = 1000;
  unsigned pose_freshness_ms_ = 3000;
  unsigned latency_warning_ms_ = 80;
  std::atomic<bool> running_{false};
  std::atomic<int> state_{static_cast<int>(SvrtLinkState::Searching)};
  std::atomic<unsigned> latency_ms_{0};
  std::atomic<uint64_t> decoded_{0}, presented_{0}, dropped_{0}, bytes_{0};
  std::atomic<uint64_t> invalid_packets_{0},fec_recovered_{0},network_dropped_{0};
  std::atomic<uint16_t> tracking_port_{0};
  std::atomic<int64_t> clock_offset_us_{0};
  uint32_t session_id_=0;
  mutable std::mutex pose_mutex_;
  SvrtPose pose_;
  uint64_t pose_received_ms_ = 0;
  std::thread thread_,tracking_thread_;
};
