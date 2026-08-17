# Preliminary power budget

Rev A assumes a **protected 2S Li-ion/LiPo pack**.  The charger can be populated for
2S, 3S or 4S, but the assembled Rev A default is 2S.  Changing cell count requires
recalculating the charger straps, voltage ratings, thermal limits and pack protection.

| Rail / load | Design allocation | Notes |
|---|---:|---|
| CM4 5 V | 4.0 A peak | Includes CPU/GPU, Wi-Fi and transient margin |
| USB-C accessory VBUS | 1.5 A max | Limited by TPS2553; advertised Type-C current must match |
| Future displays | 1.0 A reserve at 5 V | Placeholder only; panel rails are TBD |
| Tracking + proximity + fan | 0.6 A at 5 V equivalent | Provisional reserve |
| USB3/audio/control losses | 0.5 A at 5 V equivalent | Includes 3V3/1V05 conversion loss |
| **5 V system design target** | **7.6 A peak** | TPS568230 is rated for 8 A; thermal proof remains required |

## Power tree

```text
USB-C charge/service receptacle
  -> TPS25751D PD sink
  -> BQ25798 buck-boost charger/power path
  -> protected 2S pack (BAT+, GND, NTC, ID)
  -> VBAT_SYS
      -> TPS568230 5 V / 8 A -> +5V_SYS -> CM4 + USB host switch + reserves
      -> CM4 3.3 V output    -> +3V3 control/audio (within CM4 output limit)
      -> 1.05 V buck         -> +1V05_USB
      -> low-noise 2.8 V LDO -> +2V8_MIC
```

The 5 V converter is strapped for forced-continuous-conduction mode at 800 kHz.
Its inductor, feedback, input/output capacitance and thermal design require a final
calculation against the selected JLCPCB stack and measured load transients before
release. Current Rev A values are:
2.2 uH, 220 k / 30 k feedback, 47–330 pF feed-forward, at least 66 uF output
capacitance and at least 40 uF effective ceramic input capacitance.

The pack must include independent cell protection/balancing.  The carrier does not
claim to replace a certified pack BMS.
