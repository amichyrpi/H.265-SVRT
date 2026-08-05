#include "network.h"
#include <winsock2.h>
#include <ws2tcpip.h>

bool svrt_request(const std::string &host, const std::string &request, std::string &response) {
  response.clear(); WSADATA data{}; if (WSAStartup(MAKEWORD(2,2), &data)) return false;
  addrinfo hints{}; hints.ai_socktype=SOCK_STREAM; char port[]="9945"; addrinfo *list=nullptr;
  if (getaddrinfo(host.c_str(),port,&hints,&list)) { WSACleanup(); return false; }
  SOCKET socket=INVALID_SOCKET;
  for(auto *it=list;it;it=it->ai_next) { socket=::socket(it->ai_family,it->ai_socktype,it->ai_protocol); if(socket!=INVALID_SOCKET && !connect(socket,it->ai_addr,(int)it->ai_addrlen)) break; if(socket!=INVALID_SOCKET) closesocket(socket); socket=INVALID_SOCKET; }
  freeaddrinfo(list); if(socket==INVALID_SOCKET){WSACleanup();return false;}
  DWORD timeout=2000; setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,(const char*)&timeout,sizeof(timeout));
  bool ok=send(socket,request.data(),(int)request.size(),0)==(int)request.size(); char buffer[256]{}; int n=ok?recv(socket,buffer,sizeof(buffer)-1,0):-1;
  if(n>0) response.assign(buffer,n); else ok=false; closesocket(socket); WSACleanup(); return ok;
}
