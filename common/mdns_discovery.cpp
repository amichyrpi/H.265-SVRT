#include "mdns_discovery.h"
#include <winsock2.h>
#include <windows.h>
#include <windns.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <cstdio>
#include <vector>

namespace {
std::string utf8(const wchar_t *value) {
  if (!value || !*value) return {};
  const int size = WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);
  if (size <= 1) return {};
  std::string result(static_cast<size_t>(size),'\0');
  WideCharToMultiByte(CP_UTF8,0,value,-1,result.data(),size,nullptr,nullptr);
  result.pop_back();
  return result;
}
}
bool stearlight_mdns_discover(std::string &host, uint16_t &control_port) {
  host.clear(); control_port=0; PDNS_RECORDW records=nullptr;
  const DWORD options=DNS_QUERY_MULTICAST_ONLY|DNS_QUERY_BYPASS_CACHE;
  DNS_STATUS status=DnsQuery_W(L"_stearlight._tcp.local",DNS_TYPE_PTR,options,nullptr,reinterpret_cast<PDNS_RECORD*>(&records),nullptr);
  std::wstring instance;
  if(status==ERROR_SUCCESS)for(PDNS_RECORDW it=records;it;it=it->pNext)
    if(it->wType==DNS_TYPE_PTR&&it->Data.PTR.pNameHost){instance=it->Data.PTR.pNameHost;break;}
  if(records)DnsRecordListFree(records,DnsFreeRecordList);records=nullptr;
  if(!instance.empty()){
    status=DnsQuery_W(instance.c_str(),DNS_TYPE_SRV,options,nullptr,reinterpret_cast<PDNS_RECORD*>(&records),nullptr);
    if(status==ERROR_SUCCESS)for(PDNS_RECORDW it=records;it;it=it->pNext)
      if(it->wType==DNS_TYPE_SRV&&it->Data.SRV.pNameTarget){host=utf8(it->Data.SRV.pNameTarget);control_port=it->Data.SRV.wPort;break;}
    if(records)DnsRecordListFree(records,DnsFreeRecordList);
    if(!host.empty()&&control_port)return true;
  }
  /* Some Windows firewall profiles suppress multicast DNS replies. Keep the
     same zero-configuration behavior with the protocol's fixed discovery
     datagram; the source address is used, never an address from the payload. */
  WSADATA winsock{};if(WSAStartup(MAKEWORD(2,2),&winsock))return false;
  SOCKET socket_fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(socket_fd==INVALID_SOCKET){WSACleanup();return false;}
  BOOL enabled=TRUE;setsockopt(socket_fd,SOL_SOCKET,SO_BROADCAST,reinterpret_cast<const char*>(&enabled),sizeof(enabled));
  DWORD timeout=700;setsockopt(socket_fd,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
  std::vector<sockaddr_in> targets;ULONG adapter_size=16*1024;std::vector<uint8_t> adapter_data(adapter_size);auto *adapters=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(adapter_data.data());
  if(GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,adapters,&adapter_size)==ERROR_BUFFER_OVERFLOW){adapter_data.resize(adapter_size);adapters=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(adapter_data.data());}
  if(GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,adapters,&adapter_size)==NO_ERROR)for(auto *adapter=adapters;adapter;adapter=adapter->Next)if(adapter->OperStatus==IfOperStatusUp)for(auto *unicast=adapter->FirstUnicastAddress;unicast;unicast=unicast->Next)if(unicast->Address.lpSockaddr&&unicast->OnLinkPrefixLength<=32){const auto *local=reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);const uint32_t prefix=unicast->OnLinkPrefixLength?0xffffffffu<<(32-unicast->OnLinkPrefixLength):0;const uint32_t broadcast=(ntohl(local->sin_addr.s_addr)&prefix)|~prefix;sockaddr_in target{};target.sin_family=AF_INET;target.sin_port=htons(9757);target.sin_addr.s_addr=htonl(broadcast);targets.push_back(target);}
  if(targets.empty()){sockaddr_in target{};target.sin_family=AF_INET;target.sin_port=htons(9757);target.sin_addr.s_addr=INADDR_BROADCAST;targets.push_back(target);}
  const char query[]="STEARLIGHT_DISCOVERY";char reply[96]{};sockaddr_in peer{};int peer_size=sizeof(peer),size=-1;
  for(unsigned attempt=0;attempt<3&&size<=0;++attempt){for(const auto &target:targets)sendto(socket_fd,query,sizeof(query)-1,0,reinterpret_cast<const sockaddr*>(&target),sizeof(target));peer_size=sizeof(peer);size=recvfrom(socket_fd,reply,sizeof(reply)-1,0,reinterpret_cast<sockaddr*>(&peer),&peer_size);}
  unsigned control=0,video=0,audio=0,tracking=0;bool found=size>0&&std::sscanf(reply,"STEARLIGHT/2 %u %u %u %u",&control,&video,&audio,&tracking)==4&&control>0&&control<65536;
  if(found){char address[INET_ADDRSTRLEN];if(InetNtopA(AF_INET,&peer.sin_addr,address,sizeof(address))){host=address;control_port=static_cast<uint16_t>(control);}else found=false;}
  closesocket(socket_fd);WSACleanup();return found;
}
