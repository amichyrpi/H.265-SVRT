# Interface and GPIO assignment

| Function | CM4 signal | Carrier net | Status |
|---|---|---|---|
| Audio bit clock | GPIO18 / PCM_CLK | I2S_BCLK | Assigned |
| Audio frame clock | GPIO19 / PCM_FS | I2S_LRCLK | Assigned |
| Audio data to CM4 | GPIO20 / PCM_DIN | I2S_DIN | Assigned |
| Audio data from CM4 | GPIO21 / PCM_DOUT | I2S_DOUT | Assigned |
| Control I2C SDA | GPIO2 / SDA1 | I2C_SDA | Assigned |
| Control I2C SCL | GPIO3 / SCL1 | I2C_SCL | Assigned |
| Proximity interrupt | GPIO17 | PROX_INT | Assigned |
| Display backlight PWM | GPIO12 / PWM0 | BL_PWM | Assigned; fan PWM must move |
| Fan tachometer | GPIO6 | FAN_TACH | Provisional |
| USB service | USB2 DP/DM | USB2_SERVICE_DP/DM | Assigned |
| eMMC boot enable | nRPIBOOT | EMMC_BOOT_N | Assigned to service test/header |
| USB 3 host | PCIe Gen2 x1 | PCIE_TX/RX_P/N | Assigned |
| CM4 Lite storage | SD_CLK/CMD/DAT[0:3]/SD_PWR_ON | J301 native microSD | Present on main PCB; populate for Lite, DNP for eMMC |

## External connectors

- `J8`: JAE DX07S024JJ2R1300 24-pin horizontal USB-C receptacle, fixed DFP.
- `J9`: JAE DX07S016JA1R1500 16-pin USB-C charge/service receptacle. All
  duplicated VBUS and GND contacts are connected; CC1/CC2 and USB2 D+/D- are
  carried to the PD/service circuitry.
- `J2`: Hirose BM28B0.6-20DS/2-0.35V compact battery-flex interface. Its two
  dedicated 5 A power contacts carry BAT+ and GND; signal contacts 1 and 2 carry
  NTC and battery ID. The remaining signal contacts are reserved and explicitly NC.
- `J1`: CTIA TRRS, stereo headphones plus external microphone.
- `J5`: 3V3, GND, SDA, SCL, INT and spare GPIO to sensor flex.
- `J7`: Hirose DF40C-100DP-0.4V(51), a 100-position, 0.4 mm-pitch straight
  board-to-board plug placed with its 21.52 mm long axis horizontal. All pins remain
  reserved pending the final tracking electrical interface; no fabricated pinout is
  implied.
- `J6`: Tag-Connect TC2050-IDC-NL no-fit service pads for UART, I2C, eMMC boot
  and reset. No permanent vertical header is installed.
- `J301`: Molex `503398-1892` microSD socket following the official CM4 IO Board
  circuit. It remains on the main PCB and is selected by BOM variant: populated for
  CM4 Lite and DNP for eMMC CM4 modules.

## Display subsystem

- `J201` / `J_DISPLAY_LEFT`: KYOCERA AVX `245863050104829+`, 50 positions,
  0.35 mm pitch, connected to the left BOE `VS025ZSM-NV0-69P0` flex.
- `J202` / `J_DISPLAY_RIGHT`: identical connector for the right panel.
- CM4 HDMI0 feeds `U201` (left `TC358870XBG`); HDMI1 feeds `U202` (right).
- Each bridge exposes two four-lane MIPI DSI links to its panel connector.
- `PCA9306DCTR` translates the CM4 3.3 V I2C bus to the 1.8 V display bus.
- `TCA9539PWR` controls bridge reset/interrupt, panel reset/TE, bias enable and
  backlight enable without applying 3.3 V GPIO levels to 1.8 V devices.
- Panel `TE` pins are inputs to the host. `S1_TEST` and uncertain `LEDPWM`
  behavior remain isolated at test/DNP options for bring-up.

The HDMI and MIPI nets are assigned to dedicated preliminary controlled-impedance
classes. Their final geometry still requires JLCPCB confirmation for the selected
six-layer stack. See
[DISPLAY_FEASIBILITY.md](DISPLAY_FEASIBILITY.md).
