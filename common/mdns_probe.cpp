#include "mdns_discovery.h"
#include <cstdio>
int main(){std::string host;uint16_t port=0;if(!stearlight_mdns_discover(host,port)){std::fputs("no Stearlight mDNS service found\n",stderr);return 1;}std::printf("%s:%u\n",host.c_str(),port);return 0;}
