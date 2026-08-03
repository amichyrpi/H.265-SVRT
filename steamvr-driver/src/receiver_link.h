#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

enum class SvrtLinkState : int {
  Starting,
  Searching,
  Ready,
  Degraded,
  ReceiverError,
};

struct SvrtLinkStatus {
  SvrtLinkState state = SvrtLinkState::Searching;
  unsigned latency_ms = 0;
  uint64_t decoded = 0;
  uint64_t presented = 0;
  uint64_t dropped = 0;
  uint64_t bytes = 0;
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
  bool Poll(uint64_t nonce, SvrtLinkStatus &status);

  std::string host_;
  uint16_t port_ = 9945;
  unsigned poll_ms_ = 1000;
  unsigned latency_warning_ms_ = 80;
  std::atomic<bool> running_{false};
  std::atomic<int> state_{static_cast<int>(SvrtLinkState::Searching)};
  std::atomic<unsigned> latency_ms_{0};
  std::atomic<uint64_t> decoded_{0}, presented_{0}, dropped_{0}, bytes_{0};
  std::thread thread_;
};
