# CM4 Lite microSD production option

Board2 uses one physical PCB for both CM4 storage assemblies. The complete native
microSD circuit is present on the main `pcb-hmd-carrier-board2.kicad_pcb`:

- `J301`: Molex `503398-1892` microSD socket;
- `U301`: Richtek `RT9742GGJ5` load switch;
- `C301`: 10 uF on `SD_PWR`;
- `R301`: 12 kOhm, 1%, `SD_PWR_ON` pull-up;
- `R302` / `R303`: optional card-detect links.

The Molex pads use the manufacturer numbering: DAT2, DAT3, CMD, VDD, CLK, VSS,
DAT0 and DAT1 on pins 1 through 8, with the detect switch on pins 9/10. The
RT9742 assignment is 1=VOUT, 2=GND, 3=FLG, 4=EN and 5=VIN.

The primary Rev A assembly targets `CM4 Lite / Wireless`, fits J301/U301/C301/R301,
and connects SD_CLK/CMD/DAT[0:3]/SD_PWR_ON to the official CM4 pins. R302/R303 are
preserved as card-detect configuration links and remain DNP until the desired
detect polarity is selected. An optional CM4 eMMC assembly marks the whole block
DNP because the native SD interface is consumed internally by eMMC. No separate
carrier PCB is required.

The `variants/cm4-lite/` file is retained only as a historical restoration source;
it is not the fabrication authority. The main Board2 project, BOM variant and DNP
state are authoritative.

Sources:

- https://www.raspberrypi.com/documentation/computers/compute-module.html
- https://pip.raspberrypi.com/categories/685-whitepapers-app-notes/documents/RP-003470-WP/Configuring-the-Compute-Module-4.pdf
- https://www.molex.com/en-us/products/part-detail/5033981892
- https://www.molex.com/content/dam/molex/molex-dot-com/products/automated/en-us/salesdrawingpdf/503/503398/5033981892_sd.pdf
- https://www.richtek.com/assets/product_file/RT9742/DS9742-10.pdf
