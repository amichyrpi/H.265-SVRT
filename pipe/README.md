# SVRT pipe

Like Vanilla's pipe layer, this target isolates privileged/platform networking from media handling. For SVRT it owns the low-latency TCP listener and MPEG-TS demuxer. Wii U WPA association, DHCP and custom 802.11 behavior from Vanilla are intentionally not included because SVRT uses an ordinary IP network.
