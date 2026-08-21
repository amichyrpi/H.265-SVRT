#include "network.h"
#include "mdns_discovery.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdio>
#include <string>

namespace {
using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;

struct ResolveContext {
  OVERLAPPED overlapped{};
  HANDLE event = nullptr;
  HANDLE cancellation = nullptr;
  PADDRINFOEXW addresses = nullptr;
  std::atomic<long> references{2};
};

void release_resolve_context(ResolveContext *context) {
  if (context->references.fetch_sub(1) != 1) return;
  if (context->addresses) FreeAddrInfoExW(context->addresses);
  CloseHandle(context->event);
  delete context;
}

void CALLBACK resolve_complete(DWORD, DWORD, LPWSAOVERLAPPED overlapped) {
  auto *context = reinterpret_cast<ResolveContext *>(overlapped);
  SetEvent(context->event);
  release_resolve_context(context);
}

std::wstring widen(const std::string &value) {
  const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
  if (size <= 0) return {};
  std::wstring result(size, L'\0');
  if (!MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size))
    return {};
  result.resize(size - 1);
  return result;
}

bool resolve_until(const std::string &host, const char *service,
                   Deadline deadline, PADDRINFOEXW *addresses) {
  const std::wstring wide_host = widen(host);
  const std::wstring wide_service = widen(service);
  if (wide_host.empty() || wide_service.empty()) return false;
  auto *context = new ResolveContext;
  context->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!context->event) { delete context; return false; }
  context->overlapped.hEvent = context->event;
  ADDRINFOEXW hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  const int result = GetAddrInfoExW(wide_host.c_str(), wide_service.c_str(), NS_ALL,
                                    nullptr, &hints, &context->addresses, nullptr,
                                    &context->overlapped, resolve_complete,
                                    &context->cancellation);
  const bool asynchronous = result == WSA_IO_PENDING;
  if (asynchronous) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::max(Clock::duration::zero(), deadline - Clock::now()));
    if (WaitForSingleObject(context->event,
                            static_cast<DWORD>(std::min<int64_t>(remaining.count(), INFINITE - 1))) != WAIT_OBJECT_0) {
      if (context->cancellation) GetAddrInfoExCancel(&context->cancellation);
      release_resolve_context(context);
      return false;
    }
  }
  const bool success = result == 0 || asynchronous;
  if (success) {
    *addresses = context->addresses;
    context->addresses = nullptr;
  }
  release_resolve_context(context);
  if (!asynchronous) release_resolve_context(context);
  return success && *addresses;
}

bool connect_until(SOCKET socket, const sockaddr *address, int length,
                   Deadline deadline) {
  u_long nonblocking = 1;
  if (ioctlsocket(socket, FIONBIO, &nonblocking)) return false;
  int result = connect(socket, address, length);
  if (result == SOCKET_ERROR) {
    const int error = WSAGetLastError();
    if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS) return false;
    fd_set writes;
    FD_ZERO(&writes); FD_SET(socket, &writes);
    const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
        std::max(Clock::duration::zero(), deadline - Clock::now()));
    timeval timeout{static_cast<long>(remaining.count() / 1000000),
                     static_cast<long>(remaining.count() % 1000000)};
    if (select(0, nullptr, &writes, nullptr, &timeout) <= 0) return false;
    int socket_error = 0; int size = sizeof(socket_error);
    if (getsockopt(socket, SOL_SOCKET, SO_ERROR,
                   reinterpret_cast<char *>(&socket_error), &size) || socket_error)
      return false;
  }
  nonblocking = 0;
  if (ioctlsocket(socket, FIONBIO, &nonblocking)) return false;
  return true;
}
}  // namespace

bool svrt_request(const std::string &host, const std::string &request,
                  std::string &response) {
  response.clear();
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data)) return false;
  constexpr size_t kMaxResponse = 4096;
  const Deadline deadline = Clock::now() + std::chrono::seconds(3);
  std::string target=host;uint16_t discovered_port=9945;
  if(target.empty()||target=="auto"){
    if(!stearlight_mdns_discover(target,discovered_port)){WSACleanup();return false;}
  }
  char service[16];std::snprintf(service,sizeof(service),"%u",discovered_port);
  PADDRINFOEXW addresses = nullptr;
  SOCKET socket = INVALID_SOCKET;
  bool ok = resolve_until(target, service, deadline, &addresses);
  if (ok) {
    for (PADDRINFOEXW it = addresses; it && Clock::now() < deadline;
         it = it->ai_next) {
      socket = WSASocketW(it->ai_family, it->ai_socktype, it->ai_protocol,
                          nullptr, 0, 0);
      if (socket != INVALID_SOCKET &&
          connect_until(socket, it->ai_addr, static_cast<int>(it->ai_addrlen),
                        deadline))
        break;
      if (socket != INVALID_SOCKET) closesocket(socket);
      socket = INVALID_SOCKET;
    }
  }
  if (addresses) FreeAddrInfoExW(addresses);
  if (socket == INVALID_SOCKET) ok = false;
  if (ok) {
    DWORD timeout = 2000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
    size_t sent_total = 0;
    while (sent_total < request.size()) {
      const int sent = send(socket, request.data() + sent_total,
                            static_cast<int>(std::min<size_t>(request.size() - sent_total, INT_MAX)), 0);
      if (sent <= 0) { ok = false; break; }
      sent_total += static_cast<size_t>(sent);
    }
    while (ok && response.size() < kMaxResponse) {
      char buffer[512];
      const int received = recv(socket, buffer, sizeof(buffer), 0);
      if (received <= 0) { ok = false; break; }
      if (response.size() + static_cast<size_t>(received) > kMaxResponse) {
        ok = false;
        break;
      }
      response.append(buffer, static_cast<size_t>(received));
      if (response.find('\n') != std::string::npos) break;
    }
    if (response.empty() || response.find('\n') == std::string::npos) ok = false;
  }
  if (socket != INVALID_SOCKET) closesocket(socket);
  WSACleanup();
  return ok;
}
