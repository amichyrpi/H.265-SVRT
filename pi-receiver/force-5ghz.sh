#!/bin/sh
set -eu
if ! command -v nmcli >/dev/null 2>&1; then
    echo "Stearlight: NetworkManager is required to enforce 5 GHz" >&2
    exit 1
fi
connection=$(nmcli -t -f NAME,TYPE,DEVICE connection show --active | awk -F: '$2=="802-11-wireless" && $3!="" {print $1; exit}')
if [ -z "$connection" ]; then echo "Stearlight: no active Wi-Fi connection" >&2; exit 1; fi
nmcli connection modify "$connection" 802-11-wireless.band a 802-11-wireless.powersave 2
frequency=$(nmcli -t -f IN-USE,FREQ device wifi | awk -F: '$1=="*" {print $2; exit}' | tr -cd '0-9')
if [ "${frequency:-0}" -lt 5000 ]; then
    echo "Stearlight: reconnecting $connection on the enforced 5 GHz band" >&2
    nmcli connection down "$connection" >/dev/null
    nmcli connection up "$connection" >/dev/null
    frequency=$(nmcli -t -f IN-USE,FREQ device wifi | awk -F: '$1=="*" {print $2; exit}' | tr -cd '0-9')
fi
if [ "${frequency:-0}" -lt 5000 ]; then echo "Stearlight: refusing VR streaming outside the 5 GHz band" >&2; exit 1; fi
echo "Stearlight: Wi-Fi locked to 5 GHz (${frequency} MHz)"
