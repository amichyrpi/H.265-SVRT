#include "audio_transport.h"

#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <openvr_driver.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
#pragma pack(push, 1)
struct AudioHeader {
  char magic[4];
  uint32_t rate;
  uint16_t channels;
  uint16_t bits;
  uint32_t format;  // 1 = PCM, 3 = IEEE float
};
#pragma pack(pop)

void log(const char *message) {
  char line[512];
  std::snprintf(line, sizeof(line), "SVRT audio: %s", message);
  vr::VRDriverLog()->Log(line);
}

SOCKET connect_audio(const std::string &host, uint16_t port) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  char service[16];
  std::snprintf(service, sizeof(service), "%u", port);
  addrinfo *addresses = nullptr;
  if (getaddrinfo(host.c_str(), service, &hints, &addresses))
    return INVALID_SOCKET;
  SOCKET result = INVALID_SOCKET;
  for (addrinfo *it = addresses; it; it = it->ai_next) {
    result = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (result != INVALID_SOCKET) {
      u_long nonblocking = 1;
      ioctlsocket(result, FIONBIO, &nonblocking);
      const int connected = connect(result, it->ai_addr,
                                    static_cast<int>(it->ai_addrlen));
      if (connected == 0 || WSAGetLastError() == WSAEWOULDBLOCK) {
        fd_set writes;
        FD_ZERO(&writes); FD_SET(result, &writes);
        timeval timeout{0, 750000};
        int error = 0; int length = sizeof(error);
        if (select(0, nullptr, &writes, nullptr, &timeout) > 0 &&
            !getsockopt(result, SOL_SOCKET, SO_ERROR,
                        reinterpret_cast<char *>(&error), &length) && !error) {
          nonblocking = 0;
          ioctlsocket(result, FIONBIO, &nonblocking);
          DWORD io_timeout = 500;
          setsockopt(result, SOL_SOCKET, SO_SNDTIMEO,
                     reinterpret_cast<const char *>(&io_timeout), sizeof(io_timeout));
          break;
        }
      }
    }
    if (result != INVALID_SOCKET) closesocket(result);
    result = INVALID_SOCKET;
  }
  freeaddrinfo(addresses);
  return result;
}

bool send_all(SOCKET socket, const void *data, size_t size) {
  const char *cursor = static_cast<const char *>(data);
  while (size) {
    const int sent = send(socket, cursor,
                          static_cast<int>(std::min<size_t>(size, INT_MAX)), 0);
    if (sent <= 0) return false;
    cursor += sent;
    size -= static_cast<size_t>(sent);
  }
  return true;
}
}  // namespace

SvrtAudioTransport::~SvrtAudioTransport() { Stop(); }

bool SvrtAudioTransport::Start(const std::string &host, uint16_t port) {
  if (running_.exchange(true)) return true;
  host_ = host;
  port_ = port;
  worker_ = std::thread(&SvrtAudioTransport::Run, this);
  return true;
}

void SvrtAudioTransport::Stop() {
  if (!running_.exchange(false)) return;
  if (worker_.joinable()) worker_.join();
}

void SvrtAudioTransport::Run() {
  WSADATA winsock{};
  if (WSAStartup(MAKEWORD(2, 2), &winsock)) return;
  const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  ComPtr<IMMDeviceEnumerator> enumerator;
  ComPtr<IMMDevice> endpoint;
  ComPtr<IAudioClient> client;
  ComPtr<IAudioCaptureClient> capture;
  WAVEFORMATEX *mix = nullptr;
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
      CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
  if (SUCCEEDED(hr))
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &endpoint);
  if (SUCCEEDED(hr)) hr = endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                              nullptr, &client);
  if (SUCCEEDED(hr)) hr = client->GetMixFormat(&mix);
  uint32_t wire_format = 0;
  if (SUCCEEDED(hr) && mix) {
    if (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) wire_format = 3;
    else if (mix->wFormatTag == WAVE_FORMAT_PCM) wire_format = 1;
    else if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
      const auto *extended = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(mix);
      if (extended->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) wire_format = 3;
      else if (extended->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) wire_format = 1;
    }
  }
  if (!wire_format || (mix->wBitsPerSample != 16 && mix->wBitsPerSample != 32))
    hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
  if (SUCCEEDED(hr))
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK, 1000000, 0, mix, nullptr);
  if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&capture));
  if (SUCCEEDED(hr)) hr = client->Start();
  if (FAILED(hr)) {
    char message[128];
    std::snprintf(message, sizeof(message),
                  "WASAPI loopback initialization failed (0x%08lx)",
                  static_cast<unsigned long>(hr));
    log(message);
  } else {
    char message[160];
    std::snprintf(message, sizeof(message),
                  "capturing Windows default output: %u Hz, %u channels, %u-bit",
                  mix->nSamplesPerSec, mix->nChannels, mix->wBitsPerSample);
    log(message);
  }
  SOCKET socket = INVALID_SOCKET;
  std::vector<uint8_t> silence;
  while (running_ && SUCCEEDED(hr)) {
    if (socket == INVALID_SOCKET) {
      socket = connect_audio(host_, port_);
      if (socket == INVALID_SOCKET) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        continue;
      }
      AudioHeader header{{'S', 'V', 'R', 'A'}, htonl(mix->nSamplesPerSec),
                         htons(mix->nChannels), htons(mix->wBitsPerSample),
                         htonl(wire_format)};
      if (!send_all(socket, &header, sizeof(header))) {
        closesocket(socket);
        socket = INVALID_SOCKET;
        continue;
      }
      log("connected to Raspberry Pi audio receiver");
    }
    UINT32 packets = 0;
    if (FAILED(capture->GetNextPacketSize(&packets))) break;
    if (!packets) {
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
      continue;
    }
    BYTE *data = nullptr;
    UINT32 frames = 0;
    DWORD flags = 0;
    if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
      break;
    const size_t bytes = static_cast<size_t>(frames) * mix->nBlockAlign;
    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
      silence.assign(bytes, 0);
      data = silence.data();
    }
    const bool sent = send_all(socket, data, bytes);
    capture->ReleaseBuffer(frames);
    if (!sent) {
      closesocket(socket);
      socket = INVALID_SOCKET;
    }
  }
  if (socket != INVALID_SOCKET) closesocket(socket);
  if (client) client->Stop();
  if (mix) CoTaskMemFree(mix);
  if (SUCCEEDED(com)) CoUninitialize();
  WSACleanup();
}
