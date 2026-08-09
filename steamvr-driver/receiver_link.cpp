#include "receiver_link.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <windows.h>

namespace {
using Clock = std::chrono::steady_clock;

bool connect_with_timeout(SOCKET socket, const sockaddr *address, int size) {
  u_long nonblocking = 1;
  ioctlsocket(socket, FIONBIO, &nonblocking);
  if (connect(socket, address, size) == SOCKET_ERROR &&
      WSAGetLastError() != WSAEWOULDBLOCK) {
    return false;
  }
  fd_set writes;
  FD_ZERO(&writes);
  FD_SET(socket, &writes);
  timeval timeout{0, 750000};
  if (select(0, nullptr, &writes, nullptr, &timeout) <= 0) return false;
  int error = 0;
  int length = sizeof(error);
  if (getsockopt(socket, SOL_SOCKET, SO_ERROR,
                 reinterpret_cast<char *>(&error), &length) || error)
    return false;
  nonblocking = 0;
  ioctlsocket(socket, FIONBIO, &nonblocking);
  DWORD io_timeout = 1000;
  setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
             reinterpret_cast<const char *>(&io_timeout), sizeof(io_timeout));
  setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
             reinterpret_cast<const char *>(&io_timeout), sizeof(io_timeout));
  return true;
}

bool send_all(SOCKET socket, const char *data, size_t size) {
  while (size) {
    const int sent = send(socket, data,
                          static_cast<int>(std::min<size_t>(size, 1u << 20)),
                          0);
    if (sent <= 0) return false;
    data += sent;
    size -= static_cast<size_t>(sent);
  }
  return true;
}

bool receive_line(SOCKET socket, char *buffer, size_t capacity) {
  if (!buffer || capacity < 2) return false;
  size_t used = 0;
  while (used + 1 < capacity) {
    const int received = recv(
        socket, buffer + used,
        static_cast<int>(std::min<size_t>(capacity - used - 1, 1u << 20)), 0);
    if (received <= 0) return false;
    used += static_cast<size_t>(received);
    if (std::memchr(buffer, '\n', used)) {
      buffer[used] = '\0';
      return true;
    }
  }
  return false;
}

struct ResolveContext {
  OVERLAPPED overlapped{};
  HANDLE event = nullptr;
  HANDLE cancellation = nullptr;
  PADDRINFOEXW addresses = nullptr;
  std::atomic<DWORD> result{WSA_E_CANCELLED};
  std::atomic<long> references{2};
};

void release_resolve_context(ResolveContext *context) {
  if (context->references.fetch_sub(1) != 1) return;
  if (context->addresses) FreeAddrInfoExW(context->addresses);
  CloseHandle(context->event);
  delete context;
}

void CALLBACK resolve_complete(DWORD error, DWORD, LPWSAOVERLAPPED overlapped) {
  auto *context = reinterpret_cast<ResolveContext *>(overlapped);
  context->result = error;
  SetEvent(context->event);
  release_resolve_context(context);
}

std::wstring wide(const std::string &value) {
  const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring result(size, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size);
  return result;
}

bool resolve_with_timeout(const std::string &host, const char *service,
                          const std::atomic<bool> &running,
                          PADDRINFOEXW *addresses) {
  const std::wstring wide_host = wide(host);
  const std::wstring wide_service = wide(service);
  if (wide_host.empty() || wide_service.empty()) return false;
  ADDRINFOEXW hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  auto *context = new ResolveContext;
  context->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!context->event) { delete context; return false; }
  context->overlapped.hEvent = context->event;
  int result = GetAddrInfoExW(wide_host.c_str(), wide_service.c_str(), NS_ALL,
                              nullptr, &hints, &context->addresses, nullptr,
                              &context->overlapped, resolve_complete,
                              &context->cancellation);
  const bool asynchronous = result == WSA_IO_PENDING;
  if (result == WSA_IO_PENDING) {
    DWORD elapsed = 0;
    while (running && elapsed < 750) {
      if (WaitForSingleObject(context->event, 50) == WAIT_OBJECT_0) break;
      elapsed += 50;
    }
    if (WaitForSingleObject(context->event, 0) != WAIT_OBJECT_0) {
      if (context->cancellation) GetAddrInfoExCancel(&context->cancellation);
      release_resolve_context(context);
      return false;
    }
    result = static_cast<int>(context->result.load());
  } else if (result == 0) {
    context->result = 0;
  } else {
    context->result = static_cast<DWORD>(result);
  }
  if (result == 0) {
    *addresses = context->addresses;
    context->addresses = nullptr;
  }
  const bool success = result == 0 && *addresses;
  release_resolve_context(context);
  if (!asynchronous) release_resolve_context(context);
  return success;
}
}  // namespace

SvrtReceiverLink::~SvrtReceiverLink() { Stop(); }

bool SvrtReceiverLink::Start(std::string host, uint16_t port, unsigned poll_ms,
                             unsigned latency_warning_ms) {
  Stop();
  host_ = std::move(host);
  port_ = port ? port : 9945;
  // This request carries the tracking pose as well as receiver statistics.
  // A health-style one second cadence makes SteamVR hold each pose for a
  // second and then jump to the next one.  Permit a tracking-rate cadence.
  poll_ms_ = std::max(10u, poll_ms);
  // A pose arrives in the status reply, so it cannot be fresher than the
  // health-poll cadence. Keep it valid across ordinary scheduling jitter and
  // one delayed poll; disconnect handling still uses consecutive failures.
  // Retain a valid sample through an isolated Wi-Fi scheduling spike.  New
  // samples still replace it at tracking rate; this only controls when a
  // real sustained outage becomes lost tracking.
  pose_freshness_ms_ = std::max(3000u, poll_ms_ * 3u);
  latency_warning_ms_ = std::max(1u, latency_warning_ms);
  state_ = static_cast<int>(SvrtLinkState::Starting);
  running_ = true;
  thread_ = std::thread(&SvrtReceiverLink::Run, this);
  return true;
}

void SvrtReceiverLink::Stop() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
  state_ = static_cast<int>(SvrtLinkState::Searching);
}

SvrtLinkStatus SvrtReceiverLink::GetStatus() const {
  SvrtLinkStatus status{static_cast<SvrtLinkState>(state_.load()),
                        latency_ms_.load(), decoded_.load(), presented_.load(),
                        dropped_.load(), bytes_.load()};
  std::lock_guard<std::mutex> lock(pose_mutex_);
  status.pose = pose_;
  const uint64_t now_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          Clock::now().time_since_epoch()).count());
  status.pose.fresh = status.pose.valid && pose_received_ms_ != 0 &&
                      now_ms >= pose_received_ms_ &&
                      now_ms - pose_received_ms_ <= pose_freshness_ms_;
  return status;
}

const char *SvrtReceiverLink::StateName(SvrtLinkState state) {
  switch (state) {
    case SvrtLinkState::Starting: return "starting";
    case SvrtLinkState::Ready: return "ready";
    case SvrtLinkState::Degraded: return "degraded";
    case SvrtLinkState::ReceiverError: return "receiver error";
    default: return "searching";
  }
}

void SvrtReceiverLink::Run() {
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data)) {
    running_ = false;
    return;
  }
  SvrtLinkStatus last_good{};
  Clock::time_point failure_deadline{};
  constexpr auto failure_tolerance=std::chrono::seconds(3);
  bool have_last_good = false;
  while (running_) {
    SvrtLinkStatus status;
    const uint64_t previous_dropped = dropped_.load();
    const auto poll_started=Clock::now();
    const auto nonce = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            poll_started.time_since_epoch()).count());
    const bool poll_succeeded = Poll(nonce, status);
    if (!poll_succeeded) {
      // A single lost health probe is normal while the Pi is decoding or
      // accepting a new stream. Do not withdraw the HMD pose for one missed
      // TCP request: SteamVR interprets that as a physical unplug and clears
      // the scene/mirror. Require several consecutive failures before going
      // offline, while still reporting a real shutdown promptly. Use a
      // monotonic deadline based on when the first failed poll began so a
      // blocking DNS/socket call counts toward the three-second tolerance.
      if(failure_deadline==Clock::time_point{})failure_deadline=poll_started+failure_tolerance;
      if (have_last_good && Clock::now()<failure_deadline) {
        status = last_good;
        status.state = SvrtLinkState::Degraded;
      } else {
        status.state = SvrtLinkState::Searching;
      }
    } else if (status.state == SvrtLinkState::Ready &&
               status.dropped > previous_dropped) {
      status.state = SvrtLinkState::Degraded;
      failure_deadline={};
      last_good = status;
      have_last_good = true;
    } else {
      failure_deadline={};
      if (status.state != SvrtLinkState::ReceiverError) {
        last_good = status;
        have_last_good = true;
      }
    }
    state_ = static_cast<int>(status.state);
    latency_ms_ = status.latency_ms;
    decoded_ = status.decoded;
    presented_ = status.presented;
    dropped_ = status.dropped;
    bytes_ = status.bytes;
    {
      std::lock_guard<std::mutex> lock(pose_mutex_);
      // A health reply and a tracking sample are independent.  Keep the last
      // valid tracking sample when one otherwise healthy reply briefly lacks
      // pose data; GetPose() applies the freshness timeout if tracking really
      // remains absent.  Replacing it here made SteamVR show its grey lost-
      // tracking background for a single transient receiver response.
      if (poll_succeeded && status.pose.connected && status.pose.valid &&
          status.pose.sequence != 0) {
        pose_ = status.pose;
        pose_received_ms_ = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now().time_since_epoch()).count());
      } else if (pose_received_ms_ == 0) {
        pose_ = status.pose;
      }
    }
    for (unsigned waited = 0; running_ && waited < poll_ms_; waited += 50)
      Sleep(std::min(50u, poll_ms_ - waited));
  }
  WSACleanup();
}

bool SvrtReceiverLink::Poll(uint64_t nonce, SvrtLinkStatus &status) {
  char service[16];
  std::snprintf(service, sizeof(service), "%u", port_);
  SOCKET socket = INVALID_SOCKET;
  const auto started = Clock::now();
  // Pairing stores literal addresses in the utility.  Avoid the asynchronous
  // resolver for those addresses; it is unnecessary and can leave the first
  // health poll in SEARCHING while the Pi is already accepting connections.
  sockaddr_in numeric4{};
  sockaddr_in6 numeric6{};
  if (InetPtonA(AF_INET, host_.c_str(), &numeric4.sin_addr) == 1) {
    numeric4.sin_family = AF_INET;
    numeric4.sin_port = htons(port_);
    socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, 0);
    if (socket == INVALID_SOCKET ||
        !connect_with_timeout(socket, reinterpret_cast<const sockaddr *>(&numeric4),
                              sizeof(numeric4))) {
      if (socket != INVALID_SOCKET) closesocket(socket);
      socket = INVALID_SOCKET;
    }
  } else if (InetPtonA(AF_INET6, host_.c_str(), &numeric6.sin6_addr) == 1) {
    numeric6.sin6_family = AF_INET6;
    numeric6.sin6_port = htons(port_);
    socket = WSASocketW(AF_INET6, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, 0);
    if (socket == INVALID_SOCKET ||
        !connect_with_timeout(socket, reinterpret_cast<const sockaddr *>(&numeric6),
                              sizeof(numeric6))) {
      if (socket != INVALID_SOCKET) closesocket(socket);
      socket = INVALID_SOCKET;
    }
  } else {
    PADDRINFOEXW addresses = nullptr;
    if (!resolve_with_timeout(host_, service, running_, &addresses)) return false;
    for (PADDRINFOEXW it = addresses; it; it = it->ai_next) {
      socket = WSASocketW(it->ai_family, it->ai_socktype, it->ai_protocol,
                          nullptr, 0, 0);
      if (socket != INVALID_SOCKET &&
          connect_with_timeout(socket, it->ai_addr, static_cast<int>(it->ai_addrlen)))
        break;
      if (socket != INVALID_SOCKET) closesocket(socket);
      socket = INVALID_SOCKET;
    }
    FreeAddrInfoExW(addresses);
  }
  if (socket == INVALID_SOCKET) return false;

  char request[64];
  int request_size = std::snprintf(request, sizeof(request), "SVRT/1 PING %llu\n",
                                   static_cast<unsigned long long>(nonce));
  bool ok = send_all(socket, request, static_cast<size_t>(request_size));
  char response[768]{};
  const bool received = ok && receive_line(socket, response, sizeof(response));
  closesocket(socket);
  if (!received) return false;

  unsigned long long reply_nonce = 0, decoded = 0, presented = 0, dropped = 0,
                     bytes = 0, pose_sequence = 0, pose_timestamp = 0;
  int receiver_state = 0;
  int pose_valid = 0, pose_connected = 0, pose_result = 101;
  double px = 0, py = 0, pz = 0;
  double qx = 0, qy = 0, qz = 0, qw = 1;
  double vx = 0, vy = 0, vz = 0;
  double avx = 0, avy = 0, avz = 0;
  const int fields = std::sscanf(
      response,
      "SVRT/1 STATUS %llu %d %llu %llu %llu %llu "
      "%d %d %d %llu %llu "
      "%lf %lf %lf "
      "%lf %lf %lf %lf "
      "%lf %lf %lf "
      "%lf %lf %lf",
      &reply_nonce, &receiver_state, &decoded, &presented, &dropped, &bytes,
      &pose_valid, &pose_connected, &pose_result, &pose_sequence,
      &pose_timestamp,
      &px, &py, &pz, &qx, &qy, &qz, &qw, &vx, &vy, &vz, &avx, &avy, &avz);
  if ((fields != 6 && fields != 24) || reply_nonce != nonce)
    return false;
  status.latency_ms = static_cast<unsigned>(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started)
          .count());
  status.decoded = decoded;
  status.presented = presented;
  status.dropped = dropped;
  status.bytes = bytes;
  if (fields == 24) {
    status.pose.valid = pose_valid != 0;
    status.pose.connected = pose_connected != 0;
    status.pose.result = pose_result;
    status.pose.sequence = pose_sequence;
    status.pose.timestamp_us = pose_timestamp;
    status.pose.position[0] = px;
    status.pose.position[1] = py;
    status.pose.position[2] = pz;
    status.pose.quaternion[0] = qx;
    status.pose.quaternion[1] = qy;
    status.pose.quaternion[2] = qz;
    status.pose.quaternion[3] = qw;
    status.pose.velocity[0] = vx;
    status.pose.velocity[1] = vy;
    status.pose.velocity[2] = vz;
    status.pose.angular_velocity[0] = avx;
    status.pose.angular_velocity[1] = avy;
    status.pose.angular_velocity[2] = avz;
  }
  if (receiver_state == 3)
    status.state = SvrtLinkState::ReceiverError;
  else if (receiver_state == 0)
    status.state = SvrtLinkState::Starting;
  else
    status.state = SvrtLinkState::Ready;
  return true;
}
