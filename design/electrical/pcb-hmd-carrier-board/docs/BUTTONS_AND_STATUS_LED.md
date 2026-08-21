# Button flex and status LED

## Carrier connector

`J11` is the exact carrier-side Hirose `BM28B0.6-20DS/2-0.35V(51)` mate for
the button flex's `BM28B0.6-20DP/2-0.35V(51)`. It uses the manufacturer STEP
model and an odd/even pin-numbered footprint checked against the official
Hirose drawing.

| J11 pin | Flex signal | Carrier destination |
|---:|---|---|
| 1 | `PWR_KEY_N` | 1 kOhm series resistor to CM4 GPIO26, pad 24 |
| 9 | `VOL_UP_N` | 1 kOhm series resistor to CM4 GPIO24, pad 45 |
| 17 | `VOL_DOWN_N` | 1 kOhm series resistor to CM4 GPIO17, pad 50 |
| 21 | `GND` | CM4 ground, pad 101 |
| 2-8, 10-16, 18-20, 22 | Reserved | No connection in Rev A |

All three switches are normally open and active low. Configure the three CM4
GPIOs with pull-ups and debounce them in software. The 1 kOhm series resistors
limit transient and accidental-contention current.

`PWR_KEY_N` is presently a CM4 software input. A true wake from battery ship
mode still requires the final charger/power-stage implementation to connect the
raw button to the charger `QON` or the selected system load-switch wake input.
That power stage is not present on the manually arranged lower carrier yet, so
this PCB must not be represented as providing hard-off wake by GPIO alone.

## RGB powered-state indicator

`D11` is TOGIALED `TJ-S3227SW1TCGLCCYRGB-A5` (`C601674`), a common-anode
3.2 x 2.7 x 1.1 mm RGB LED. Pin 1 is connected to `+3V3`; the cathodes are
active low:

| Color | Series resistor | CM4 signal | CM4 pad |
|---|---:|---|---:|
| Red | 1 kOhm | `nPI_LED_PWR` | 95 |
| Green | 330 Ohm | GPIO13 | 28 |
| Blue | 330 Ohm | GPIO16 | 29 |

Red follows the CM4's native active-low power indicator output, so the LED has
a hardware powered-state indication during boot. Later software can drive the
green and blue cathodes high for off or low/PWM for color mixing. GPIO13 and
GPIO16 must be configured high before enabling PWM to avoid an unintended
startup color.

The resistor values keep indicator current low: approximately 1.5 mA maximum
per channel using the datasheet's minimum forward voltages.

## Validation

- The production-baseline audit found no position, orientation or side changes to
  retained carrier footprints. No component was removed; the six CM4 Lite microSD
  references are restored on the main PCB.
- J11 and flex J1 agree on contacts 1, 9, 17 and 21.
- Current carrier geometry DRC is zero violations; 499 unrouted items remain for the
  full board. The button/LED routes contain no known short or geometry violation.
- The standalone button flex passes ERC and DRC with zero violations and zero
  unrouted items.
