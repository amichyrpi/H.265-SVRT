# Stearlight HMD Carrier — Rev A design plan

## Scope of this milestone

Rev A establishes the complete electrical architecture, manufacturer part choices,
power tree, interface ownership, approved 145.70 mm x 49.82 mm mechanical envelope, and component
placement.  The supplied Raspberry Pi CM4 IO design is the source of truth for the
Compute Module connectors, power pins, boot control, USB 2.0 and PCIe signals.

The board is **not a fabrication release** until the following external information
is supplied and the corresponding review gates are closed:

1. Controlled BOE display initialization/timing data and first-hardware validation.
2. Final Lighthouse tracking-board pinout for `J7`.
3. Renesas UPD720202 hardware user manual/reference schematic.  The public product
   page does not expose the pin-level hardware manual without Renesas access.
4. Final enclosure CAD, connector insertion envelopes and CM4 heatsink/fan geometry.
5. JLCPCB review/tuning of the selected `JLC06161H-3313` stack geometry and coupons.
6. Replace recovered passive/`VERIFY` pin types with reviewed vendor symbols as
   controlled source documents become available. Reference, pad/net and
   PCB/schematic parity reconciliation is complete for all 312 components.

No TBD interface is silently guessed.  The PCB reserves physical routing corridors
for both display links and the tracking connector pins remain explicitly reserved.

## Implementation order

1. Preserve and audit official/reference projects.
2. Establish hierarchical schematic and global net naming.
3. Implement protected 2S input, USB-PD sink and BQ25798 charger.
4. Reuse CM4 connector and boot/USB2/PCIe circuitry.
5. Implement PCIe USB 3 host, Type-C DFP, mux and VBUS switch.
6. Implement I2S codec, two analog differential MEMS microphones and TRRS jack.
7. Implement proximity, fan, battery and tracking interfaces.
8. Place the mechanical connectors, CM4 and functional blocks.
9. Close ERC; resolve all non-TBD errors.
10. Select the JLCPCB stack, route power and high-speed nets, then close DRC.

## Placement zones

- Front-left edge: physical USB-C charging/service connector and 3.5 mm jack.
- Front-centre edge: two microphone acoustic ports, separated symmetrically.
- Front-right edge: USB-C SuperSpeed host receptacle.
- Right edge: protected battery connector.
- Centre/back: CM4, with its radio/antenna keepout facing a board edge.
- Back-left: 100-pin tracking-board mezzanine connector.
- Power conversion: right of the CM4, away from microphones and codec.
- Audio: left-front, isolated from switch nodes and USB 3 pairs.
- USB 3: shortest practical corridor from CM4 PCIe to the right-edge receptacle.
