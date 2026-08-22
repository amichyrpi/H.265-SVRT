#include "receiver_link.h"
#include <stearlight_protocol.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <windows.h>
#include <random>

namespace {
using Clock = std::chrono::steady_clock;

enum class SteamAuthorization { Unknown, Authorized, Revoked };

uint64_t reverse_bytes(uint64_t value) {
  uint64_t result = 0;
  for (unsigned i = 0; i < 8; ++i) {
    result = (result << 8) | (value & 0xffu);
    value >>= 8;
  }
  return result;
}

SteamAuthorization steam_authorization(uint64_t device_id) {
  if (!device_id) return SteamAuthorization::Unknown;
  char steam_path[MAX_PATH * 4]{};
  DWORD bytes = sizeof(steam_path);
  if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath",
                   RRF_RT_REG_SZ, nullptr, steam_path, &bytes) != ERROR_SUCCESS)
    return SteamAuthorization::Unknown;
  for (char *p = steam_path; *p; ++p) if (*p == '/') *p = '\\';
  char pattern[MAX_PATH * 4]{};
  std::snprintf(pattern, sizeof(pattern), "%s\\userdata\\*", steam_path);
  char raw_id[32]{}, wire_id[32]{};
  std::snprintf(raw_id, sizeof(raw_id), "\"%016llx\"",
                static_cast<unsigned long long>(device_id));
  std::snprintf(wire_id, sizeof(wire_id), "\"%016llx\"",
                static_cast<unsigned long long>(reverse_bytes(device_id)));
  DWORD active_user = 0, active_user_size = sizeof(active_user);
  if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "ActiveUser",
                   RRF_RT_REG_DWORD, nullptr, &active_user,
                   &active_user_size) == ERROR_SUCCESS && active_user) {
    char path[MAX_PATH * 4]{};
    std::snprintf(path, sizeof(path), "%s\\userdata\\%lu\\config\\localconfig.vdf",
                  steam_path, static_cast<unsigned long>(active_user));
    std::ifstream file(path, std::ios::binary);
    if (!file) return SteamAuthorization::Unknown;
    std::string contents((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return contents.find(raw_id) != std::string::npos ||
                   contents.find(wire_id) != std::string::npos
               ? SteamAuthorization::Authorized
               : SteamAuthorization::Revoked;
  }
  WIN32_FIND_DATAA entry{};
  HANDLE find = FindFirstFileA(pattern, &entry);
  if (find == INVALID_HANDLE_VALUE) return SteamAuthorization::Unknown;
  bool read_config = false, found = false;
  do {
    if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        entry.cFileName[0] == '.') continue;
    char path[MAX_PATH * 4]{};
    std::snprintf(path, sizeof(path), "%s\\userdata\\%s\\config\\localconfig.vdf",
                  steam_path, entry.cFileName);
    std::ifstream file(path, std::ios::binary);
    if (!file) continue;
    std::string contents((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    read_config = true;
    if (contents.find(raw_id) != std::string::npos ||
        contents.find(wire_id) != std::string::npos) {
      found = true;
      break;
    }
  } while (FindNextFileA(find, &entry));
  FindClose(find);
  if (found) return SteamAuthorization::Authorized;
  return read_config ? SteamAuthorization::Revoked : SteamAuthorization::Unknown;
}

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
  /* Authorization is part of this control reply. Poll it frequently enough
     that a revoked or restarting headset cannot receive a visible burst of
     frames before the driver observes UNAUTHORIZED. */
  poll_ms_ = std::clamp(poll_ms, 100u, 1000u);
  // A pose arrives in the status reply, so it cannot be fresher than the
  // health-poll cadence. Keep it valid across ordinary scheduling jitter and
  // one delayed poll; disconnect handling still uses consecutive failures.
  // Retain a valid sample through an isolated Wi-Fi scheduling spike.  New
  // samples still replace it at tracking rate; this only controls when a
  // real sustained outage becomes lost tracking.
  pose_freshness_ms_ = 3000;
  latency_warning_ms_ = std::max(1u, latency_warning_ms);
  state_ = static_cast<int>(SvrtLinkState::Starting);
  steam_device_id_ = 0;
  steam_authorized_ = true;
  last_steam_auth_check_ms_ = 0;
  std::random_device random;session_id_=static_cast<uint32_t>(random())^static_cast<uint32_t>(GetTickCount64());if(!session_id_)session_id_=1;
  running_ = true;
  tracking_thread_ = std::thread(&SvrtReceiverLink::TrackingRun, this);
  thread_ = std::thread(&SvrtReceiverLink::Run, this);
  return true;
}

void SvrtReceiverLink::Stop() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
  if (tracking_thread_.joinable()) tracking_thread_.join();
  tracking_port_=0;
  state_ = static_cast<int>(SvrtLinkState::Searching);
}

void SvrtReceiverLink::TrackingRun(){
  WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return;
  SOCKET socket_fd=socket(AF_INET6,SOCK_DGRAM,IPPROTO_UDP);if(socket_fd==INVALID_SOCKET){WSACleanup();return;}
  DWORD off=0;setsockopt(socket_fd,IPPROTO_IPV6,IPV6_V6ONLY,reinterpret_cast<const char*>(&off),sizeof(off));
  sockaddr_in6 bind_address{};bind_address.sin6_family=AF_INET6;bind_address.sin6_addr=in6addr_any;
  if(bind(socket_fd,reinterpret_cast<const sockaddr*>(&bind_address),sizeof(bind_address))){closesocket(socket_fd);WSACleanup();return;}
  int address_size=sizeof(bind_address);if(getsockname(socket_fd,reinterpret_cast<sockaddr*>(&bind_address),&address_size)){closesocket(socket_fd);WSACleanup();return;}
  tracking_port_=ntohs(bind_address.sin6_port);DWORD timeout=100;setsockopt(socket_fd,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
  sockaddr_in6 receiver{};receiver.sin6_family=AF_INET6;receiver.sin6_port=htons(9947);sockaddr_in numeric4{};if(InetPtonA(AF_INET,host_.c_str(),&numeric4.sin_addr)==1){receiver.sin6_addr.u.Byte[10]=0xff;receiver.sin6_addr.u.Byte[11]=0xff;std::memcpy(&receiver.sin6_addr.u.Byte[12],&numeric4.sin_addr,4);}else{addrinfo hints{};hints.ai_family=AF_INET6;hints.ai_socktype=SOCK_DGRAM;hints.ai_flags=AI_V4MAPPED;addrinfo *resolved=nullptr;if(!getaddrinfo(host_.c_str(),"9947",&hints,&resolved)&&resolved){receiver=*reinterpret_cast<sockaddr_in6*>(resolved->ai_addr);freeaddrinfo(resolved);}}
  uint64_t last_hello=0;
  while(running_){const uint64_t now_ms=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());if(!last_hello||now_ms-last_hello>1000){char hello[64];int length=std::snprintf(hello,sizeof(hello),"STEARLIGHT_TRACK %u",session_id_);sendto(socket_fd,hello,length,0,reinterpret_cast<const sockaddr*>(&receiver),sizeof(receiver));last_hello=now_ms;}stearlight_pose_packet packet;int received=recv(socket_fd,reinterpret_cast<char*>(&packet),sizeof(packet),0);if(received!=sizeof(packet))continue;stearlight_pose_info info;if(stearlight_pose_decode(&info,&packet,sizeof(packet))||info.session_id!=session_id_)continue;
    SvrtPose pose;pose.valid=(info.flags&1)!=0;pose.connected=(info.flags&2)!=0;pose.result=static_cast<int>(info.result);pose.sequence=info.sequence;const int64_t adjusted=static_cast<int64_t>(info.timestamp_us)-clock_offset_us_.load();pose.timestamp_us=adjusted>0?static_cast<uint64_t>(adjusted):info.timestamp_us;unsigned n=0;for(unsigned i=0;i<3;i++)pose.position[i]=info.values[n++];for(unsigned i=0;i<4;i++)pose.quaternion[i]=info.values[n++];for(unsigned i=0;i<3;i++)pose.velocity[i]=info.values[n++];for(unsigned i=0;i<3;i++)pose.angular_velocity[i]=info.values[n++];
    if(pose.connected&&pose.valid){std::lock_guard<std::mutex> lock(pose_mutex_);pose_=pose;pose_received_ms_=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());}
  }
  closesocket(socket_fd);WSACleanup();
}

SvrtLinkStatus SvrtReceiverLink::GetStatus() const {
  SvrtLinkStatus status{static_cast<SvrtLinkState>(state_.load()),
                        latency_ms_.load(), decoded_.load(), presented_.load(),
                        dropped_.load(), bytes_.load(),invalid_packets_.load(),fec_recovered_.load(),network_dropped_.load()};
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
  unsigned missed_polls = 0;
  const unsigned missed_poll_limit =
      std::max(12u, (3000u + poll_ms_ - 1u) / poll_ms_);
  bool have_last_good = false;
  while (running_) {
    SvrtLinkStatus status;
    const uint64_t previous_dropped = dropped_.load();
    const auto nonce = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now().time_since_epoch()).count());
    const bool poll_succeeded = Poll(nonce, status);
    if (!poll_succeeded) {
      // A single lost health probe is normal while the Pi is decoding or
      // accepting a new stream. Do not withdraw the HMD pose for one missed
      // TCP request: SteamVR interprets that as a physical unplug and clears
      // the scene/mirror. Require several consecutive failures before going
      // offline, while still reporting a real shutdown promptly.  The health
      // request is a new TCP connection each time, so allow roughly 600 ms
      // for DNS, Wi-Fi scheduling, or one delayed Pi response.
      ++missed_polls;
      if (have_last_good && missed_polls < missed_poll_limit) {
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
    invalid_packets_=status.invalid_packets;fec_recovered_=status.fec_recovered;network_dropped_=status.network_dropped;
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

  const uint16_t tracking_port=tracking_port_.load();if(!tracking_port){closesocket(socket);return false;}
  const uint64_t t1=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count());
  char request[160];
  const uint64_t known_device_id = steam_device_id_.load();
  int request_size = known_device_id
      ? std::snprintf(request, sizeof(request), "SVRT/3 CONNECT %u %u %llu %016llx %u\n",
                      session_id_, tracking_port,
                      static_cast<unsigned long long>(t1),
                      static_cast<unsigned long long>(known_device_id),
                      steam_authorized_.load() ? 1u : 0u)
      : std::snprintf(request, sizeof(request), "SVRT/2 CONNECT %u %u %llu\n",
                      session_id_, tracking_port,
                      static_cast<unsigned long long>(t1));
  bool ok = send_all(socket, request, static_cast<size_t>(request_size));
  char response[768]{};
  const bool received = ok && receive_line(socket, response, sizeof(response));
  closesocket(socket);
  if (!received) return false;

  unsigned long long decoded = 0, presented = 0, dropped = 0,
                     bytes = 0, invalid=0,recovered=0,network_dropped=0,reply_t1=0,t2=0,t3=0,
                     reply_device_id=0;
  unsigned reply_session=0;
  int receiver_state = 0;
  const int fields = std::sscanf(
      response,
      "SVRT/2 ACCEPT %u %llu %llu %llu %d %llu %llu %llu %llu %llu %llu %llu %llx",
      &reply_session,&reply_t1,&t2,&t3,&receiver_state,&decoded,&presented,&dropped,&bytes,&invalid,&recovered,&network_dropped,&reply_device_id);
  if((fields!=12&&fields!=13)||reply_session!=session_id_||reply_t1!=t1)
    return false;
  if (fields == 13 && reply_device_id) steam_device_id_ = reply_device_id;
  const uint64_t now_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          Clock::now().time_since_epoch()).count());
  if (steam_device_id_.load() &&
      (!last_steam_auth_check_ms_ || now_ms - last_steam_auth_check_ms_ >= 500)) {
    last_steam_auth_check_ms_ = now_ms;
    const SteamAuthorization auth = steam_authorization(steam_device_id_.load());
    if (auth == SteamAuthorization::Authorized) steam_authorized_ = true;
    else if (auth == SteamAuthorization::Revoked) steam_authorized_ = false;
  }
  const uint64_t t4=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count());
  clock_offset_us_=static_cast<int64_t>((static_cast<int64_t>(t2)-static_cast<int64_t>(t1)+static_cast<int64_t>(t3)-static_cast<int64_t>(t4))/2);
  status.latency_ms = static_cast<unsigned>(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started)
          .count());
  status.decoded = decoded;
  status.presented = presented;
  status.dropped = dropped;
  status.bytes = bytes;
  status.invalid_packets=invalid;status.fec_recovered=recovered;status.network_dropped=network_dropped;
  if (!steam_authorized_.load() || receiver_state == 4)
    status.state = SvrtLinkState::Searching;
  else if (receiver_state == 3)
    status.state = SvrtLinkState::ReceiverError;
  else if (receiver_state == 0)
    status.state = SvrtLinkState::Starting;
  else
    status.state = SvrtLinkState::Ready;
  return true;
}
