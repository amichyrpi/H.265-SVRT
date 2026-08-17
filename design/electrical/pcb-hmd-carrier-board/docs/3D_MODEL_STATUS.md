# 3D model status

The current board2 contains 265 footprints with one or more 3D model references.
All resolve to project-local
manufacturer, manufacturer-package, standard KiCad, or manufacturer-drawing-derived
CAD. The 40 test-point pads, the no-fit Tag-Connect service pads and six mounting
holes intentionally have no raised body. No generated
cube/package-envelope model is attached to the PCB.

## Project-local verified models

- Hirose DF40C-100DS-0.4V(51), used only by the CM4 carrier footprint:
  `libraries/3dmodels/DF40/DF40C-100DS.stp`
- Hirose DF40C-100DP-0.4V(51) tracking-board plug, using Hirose's exact official
  STEP model with its CAD axes normalized by the PCB footprint:
  `libraries/3dmodels/exact/Hirose/DF40C-100DP/DF40C-100DP.stp`
- Hirose BM28B0.6-20DS/2-0.35V battery-flex connector:
  `libraries/3dmodels/BM28/BM28-20DS.stp`
- Hirose BM28B0.6-20DS/2-0.35V button-flex mate J11 reuses the same verified
  manufacturer model: `libraries/3dmodels/BM28/BM28-20DS.stp`
- TOGIALED TJ-S3227SW1TCGLCCYRGB-A5 RGB status LED:
  `libraries/3dmodels/Button_Status/TOGIALED_TJ-S3227_RGB.step`
- The CM4 placement uses the exact two DF40 connector models only. A fake CM4 body
  is deliberately not included.
- JAE DX07S024JJ2R1300 accessory and DX07S016JA1R1500 charge/service receptacles:
  exact LCSC/JAE-linked CAD converted to STEP without changing connector geometry.
- Same Sky SJ-43514-SMT-TR jack: official Same Sky STEP, axis-normalized for KiCad.
- TPS25751DREFR, BQ25798RQMR, TPS568230RJER and TPS62825DMQR: exact TI package CAD.
- TUSB320LAIRWBR and TLV320AIC3204IRHBR: verified TI package-code CAD (RWB0012A
  and RHB0032M respectively).
- Bourns SRP5030CA-1R0M and SRP7050TA-2R2M: manufacturer-drawing-derived molded
  bodies, terminals and top markings at the exact published package dimensions.
- Infineon IM73A135: manufacturer-drawing-derived model including the microphone
  metal lid and acoustic port.
- Molex 503398-1892 microSD socket: manufacturer-drawing-derived 13.10 x 14.05 x
  1.28 mm assembly with stamped-shell openings, insulator, card mouth, contacts and
  detect spring. It is fitted on the primary CM4 Lite Board2 assembly and DNP only
  in the optional same-PCB eMMC BOM variant.
- Official KiCad package models downloaded project-locally by
  `tools/fetch_kicad_models.ps1`.

Standard KiCad models are referenced for ordinary passives and common packages when
the local KiCad 3D library provides them.

The KiCad 3D viewer/render resolves all 265 modeled footprints. The review STEP
export omits six components that currently have mesh-only WRL/GLB sources (`U1`,
`U2`, `U3`, `U5`, `U6`, `U12`), because KiCad cannot include VRML meshes in a STEP
assembly. Obtain manufacturer B-rep STEP models for those six packages before the
enclosure interference model is considered complete.

`tools/generate_verified_component_models.py` only normalizes official CAD or builds
documented manufacturer-drawing-derived models. The PCB generator rejects
any model path below `libraries/3dmodels/generated/`, preventing the former cube
fallback from returning.
