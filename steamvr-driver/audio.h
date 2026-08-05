#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

class SvrtAudioTransport {
 public:
  SvrtAudioTransport() = default;
  ~SvrtAudioTransport();
  bool Start(const std::string &host, uint16_t port);
  void SetReceiverAvailable(bool available) { receiver_available_ = available; }
  void Stop();

 private:
  void Run();
  std::string host_;
  uint16_t port_ = 9946;
  std::atomic<bool> running_{false};
  std::atomic<bool> receiver_available_{false};
  std::thread worker_;
};
