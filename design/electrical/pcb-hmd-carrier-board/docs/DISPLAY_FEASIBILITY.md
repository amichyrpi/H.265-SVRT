# Stearlight display subsystem — Rev A

Status: **placed and electrically netted; not yet routable/fabricable**
Review date: 2026-08-14

## Architecture

Each eye uses an independent, uncompressed path:

```text
CM4 HDMI0 -> TC358870XBG LEFT  -> dual 4-lane DSI -> VS025ZSM LEFT
CM4 HDMI1 -> TC358870XBG RIGHT -> dual 4-lane DSI -> VS025ZSM RIGHT
```

The panel is BOE `VS025ZSM-NV0-69P0`, 1440 x 1600, NT57860, with two
four-lane MIPI D-PHY ports. The motherboard connector is the exact Kyocera AVX
Series 5863 receptacle `245863050104829+`; it mates with the panel-side
`145863050024829+`. The footprint follows Kyocera drawing BJS5863008 and uses
the manufacturer's STEP model.

The bridge is `TC358870XBG(NOK)`. It accepts HDMI up to 297 MHz and exposes two
four-lane, 1 Gbit/s/lane DSI transmitters. It has no scaler. CM4 therefore must
produce the panel raster and the bridge must be configured for the panel's
dual-link split.

## Video modes and current evidence

- Rev A electrical target: 1440 x 1600 RGB888 at 60 Hz per eye.
- Bring-up target after 60 Hz validation: 1440 x 1600 at 90 Hz.
- A 30 fps stream is repeated into a 60 Hz scan; a 45 fps stream is repeated
  into a 90 Hz scan. The panel is not intentionally scanned at 30/45 Hz.
- The public preliminary BOE document explicitly lists 90 Hz. It does not expose
  the complete NT57860 initialization/timing pages, so 60 Hz is an engineering
  bring-up target rather than a verified panel mode.
- No DSC is present in this architecture.

## Exact connector mapping

| Pins | Function |
| --- | --- |
| 1,5,6,9,10,15,16,21,22,27,28,33,34,39,40,43,46,50 | GND |
| 2,4 | `DISPLAY_1V8` |
| 3 | `LCD_VSP` (+5.5 V) |
| 7 | `LCD_VSN` (-5.5 V) |
| 8 | `S1_TEST`, NC by default plus test point |
| 11–20,24,26 | DSI-A D0/D1/D2/D3/CLK P/N |
| 23,25,29–32,35–38 | DSI-B D0/D1/D2/D3/CLK P/N |
| 41 | common `LED_BOOST` anode |
| 42 | independent active-low panel reset |
| 44 | panel TE output |
| 45,47,49 | three WLED cathodes per panel |
| 48 | panel LEDPWM; isolated by DNP option plus test point |

This mapping is based on the exact-model public page supplied for the project.
It must be checked against a complete BOE-controlled drawing before ordering PCBs.
The public BOE preliminary PDF available locally only contains pages 1–10.

## Power and control

Four shared `TLV62569DBVR` bucks generate:

- `DISPLAY_3V3` — 453 kΩ / 100 kΩ feedback, calculated 3.319 V;
- `DISPLAY_1V8` — 200 kΩ / 100 kΩ, 1.800 V;
- `DISPLAY_1V2` — 100 kΩ / 100 kΩ, 1.200 V;
- `DISPLAY_1V15` — 91 kΩ / 100 kΩ, 1.146 V.

Toshiba specifies both `VDDC11` and `VDD11_HDMI` at 1.15 V nominal. There is
no separate 1.10 V regulator. Seven ferrite-filtered local domains are provided
per bridge for 3.3 V HDMI, 3.3 V I/O, 1.8 V I/O, 1.15 V core, 1.15 V HDMI and
the two 1.2 V MIPI PHY supplies.

One `TPS65132B5YFFR` generates +5.5 V and -5.5 V for both panels. One
`MP3387AGRT-P` drives all six WLED strings. `R_ISET=37.4 kΩ` gives approximately
33.2 mA/string from the documented 1.24 V / R relationship. Backlight is disabled
until bridge and panel initialization completes.

CM4 I2C is translated from 3.3 V to 1.8 V with `PCA9306DCTR`. A 1.8 V
`TCA9539PWR` supplies enough safe panel/bridge reset, interrupt, TE and power-enable
GPIOs. This avoids directly applying 3.3 V CM4 GPIO levels to 1.8 V controls.
`BL_PWM` uses CM4 GPIO12; the provisional fan PWM assignment must move before the
fan design is finalized.

## Placement/routing status

The two bridges, exact display connectors, oscillators, local filtering, all
power converters, backlight, level translation, GPIO expansion, passives and
bring-up pads are placed in `pcb-hmd-carrier-board2.kicad_pcb`. Existing manually
placed carrier components were not moved.

HDMI and MIPI nets are intentionally unrouted. Their track width/gap cannot be
defined until a real JLCPCB six-layer stackup is selected and 100 Ω differential
geometry is calculated. No manufacturing output may be generated from this
placement milestone.

## Bring-up checklist

1. Validate 3.3, 1.8, 1.2 and 1.15 V rails with bridges/panels disconnected.
2. Validate TPS65132 sequencing and +5.5/-5.5 V polarity under dummy loads.
3. Keep `BL_EN=0`; verify both 48 MHz oscillators and both bridge I2C addresses.
4. Confirm bridge reset/interrupt control through the TCA9539.
5. Obtain/derive the NT57860 DCS initialization sequence without inventing
   undocumented register writes.
6. Validate DSI-A/DSI-B ordering using a split-color test pattern.
7. Validate 1440 x 1600 at 60 Hz, porches and TE behavior.
8. Enable backlight at low duty cycle; verify 33 mA/string and OVP margin.
9. Only then test the documented 90 Hz panel scan and 45-to-90 frame repeat.

## Sources

- Toshiba `TC358870XBG` official data sheet:
  <https://toshiba.semicon-storage.com/info/TC358870XBG_datasheet_en_20171025.pdf?did=28743&prodName=TC358870XBG>
- Kyocera `245863050104829+` product/drawing/CAD:
  <https://ele.kyocera.com/en/product/connector/board_to_board_connectors/5863/245863050104829/>
- TI `TLV62569`: <https://www.ti.com/product/TLV62569>
- TI `TPS65132`: <https://www.ti.com/product/TPS65132>
- MPS `MP3387A`: <https://www.monolithicpower.com/en/products/power-management/display-power-and-control/backlight-drivers-wled/mp3387a.html>
- Local BOE preliminary specification:
  `reference/Display_Final/BOE_VS025ZSM_preliminary_pages1-10.pdf`
