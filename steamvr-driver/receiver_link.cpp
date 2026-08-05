#include "receiver_link.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

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

bool resolve_with_timeout(const std::string &host, const char *service,
                          const std::atomic<bool> &running,
                          PADDRINFOEXA *addresses) {
  ADDRINFOEXA hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  // The utility stores a numeric address when pairing.  Mark it as numeric so
  // Winsock never sends this health poll through the asynchronous name
  // resolver (which can remain pending even though the Pi is reachable).
  IN_ADDR ipv4{};
  IN6_ADDR ipv6{};
  if (InetPtonA(AF_INET, host.c_str(), &ipv4) == 1 ||
      InetPtonA(AF_INET6, host.c_str(), &ipv6) == 1)
    hints.ai_flags |= AI_NUMERICHOST;
  OVERLAPPED overlapped{};
  overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!overlapped.hEvent) return false;
  HANDLE cancellation = nullptr;
  int result = GetAddrInfoExA(host.c_str(), service, NS_ALL, nullptr, &hints,
                              addresses, nullptr, &overlapped, nullptr,
                              &cancellation);
  if (result == WSA_IO_PENDING) {
    DWORD elapsed = 0;
    while (running && elapsed < 750) {
      if (WaitForSingleObject(overlapped.hEvent, 50) == WAIT_OBJECT_0) break;
      elapsed += 50;
    }
    if (WaitForSingleObject(overlapped.hEvent, 0) != WAIT_OBJECT_0) {
      if (cancellation) GetAddrInfoExCancel(&cancellation);
      // The OVERLAPPED structure and event must remain alive until Winsock
      // signals completion. Returning after an arbitrary timeout leaves the
      // resolver writing into dead stack memory during driver teardown.
      WaitForSingleObject(overlapped.hEvent, INFINITE);
    }
    result = GetAddrInfoExOverlappedResult(&overlapped);
  }
  CloseHandle(overlapped.hEvent);
  if (result != 0 && *addresses) {
    FreeAddrInfoExA(*addresses);
    *addresses = nullptr;
  }
  return result == 0 && *addresses;
}
}  // namespace

SvrtReceiverLink::~SvrtReceiverLink() { Stop(); }

bool SvrtReceiverLink::Start(std::string host, uint16_t port, unsigned poll_ms,
                             unsigned latency_warning_ms) {
  Stop();
  host_ = std::move(host);
  port_ = port ? port : 9945;
  poll_ms_ = std::max(250u, poll_ms);
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
  return {static_cast<SvrtLinkState>(state_.load()), latency_ms_.load(),
          decoded_.load(), presented_.load(), dropped_.load(), bytes_.load()};
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
  unsigned missed_polls = 0;
  bool have_last_good = false;
  while (running_) {
    SvrtLinkStatus status;
    const uint64_t previous_dropped = dropped_.load();
    const auto nonce = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now().time_since_epoch()).count());
    if (!Poll(nonce, status)) {
      // A single lost health probe is normal while the Pi is decoding or
      // accepting a new stream. Do not withdraw the HMD pose for one missed
      // TCP request: SteamVR interprets that as a physical unplug and clears
      // the scene/mirror. Require several consecutive failures before going
      // offline, while still reporting a real shutdown promptly.
      ++missed_polls;
      if (have_last_good && missed_polls < 4) {
        status = last_good;
        status.state = SvrtLinkState::Degraded;
      } else {
        status.state = SvrtLinkState::Searching;
      }
    } else if (status.state == SvrtLinkState::Ready &&
               status.dropped > previous_dropped) {
      status.state = SvrtLinkState::Degraded;
      missed_polls = 0;
      last_good = status;
      have_last_good = true;
    } else {
      missed_polls = 0;
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
    PADDRINFOEXA addresses = nullptr;
    if (!resolve_with_timeout(host_, service, running_, &addresses)) return false;
    for (PADDRINFOEXA it = addresses; it; it = it->ai_next) {
      socket = WSASocketW(it->ai_family, it->ai_socktype, it->ai_protocol,
                          nullptr, 0, 0);
      if (socket != INVALID_SOCKET &&
          connect_with_timeout(socket, it->ai_addr, static_cast<int>(it->ai_addrlen)))
        break;
      if (socket != INVALID_SOCKET) closesocket(socket);
      socket = INVALID_SOCKET;
    }
    FreeAddrInfoExA(addresses);
  }
  if (socket == INVALID_SOCKET) return false;

  char request[64];
  int request_size = std::snprintf(request, sizeof(request), "SVRT/1 PING %llu\n",
                                   static_cast<unsigned long long>(nonce));
  bool ok = send(socket, request, request_size, 0) == request_size;
  char response[256]{};
  int received = ok ? recv(socket, response, sizeof(response) - 1, 0) : -1;
  closesocket(socket);
  if (received <= 0) return false;

  unsigned long long reply_nonce = 0, decoded = 0, presented = 0, dropped = 0,
                     bytes = 0;
  int receiver_state = 0;
  if (std::sscanf(response, "SVRT/1 STATUS %llu %d %llu %llu %llu %llu",
                  &reply_nonce, &receiver_state, &decoded, &presented, &dropped,
                  &bytes) != 6 || reply_nonce != nonce)
    return false;
  status.latency_ms = static_cast<unsigned>(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started)
          .count());
  status.decoded = decoded;
  status.presented = presented;
  status.dropped = dropped;
  status.bytes = bytes;
  if (receiver_state == 3)
    status.state = SvrtLinkState::ReceiverError;
  else if (receiver_state == 0)
    status.state = SvrtLinkState::Starting;
  else if (status.latency_ms > latency_warning_ms_)
    status.state = SvrtLinkState::Degraded;
  else
    status.state = SvrtLinkState::Ready;
  return true;
}
