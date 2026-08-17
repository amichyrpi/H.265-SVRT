"""Generate the Rev A hierarchical architecture and review schematics.

The official Raspberry Pi CM4 sheets are copied without editing.  Other sheets embed
the exact KiCad library symbols where available and local pin-accurate symbols for
parts absent from KiCad 10.  Unknown external interfaces are intentionally reserved.
"""

from copy import deepcopy
from pathlib import Path
from shutil import copy2
from uuid import uuid4

from kiutils.schematic import Schematic
from kiutils.symbol import SymbolLib, Symbol, SymbolPin
from kiutils.items.syitems import SyRect
from kiutils.items.schitems import (
    SchematicSymbol, GlobalLabel, NoConnect, Text, HierarchicalSheet,
    HierarchicalSheetInstance,
)
from kiutils.items.common import (
    Position, Property, Effects, Font, Stroke, ColorRGBA, Fill, PageSettings,
    TitleBlock,
)


ROOT = Path(__file__).resolve().parents[1]
SHEETS = ROOT / "sheets"
KICAD_SYMBOLS = Path(r"C:/Program Files/KiCad/10.0/share/kicad/symbols")


def uid():
    return str(uuid4())


def eff(size=1.0, hide=False, bold=False):
    return Effects(font=Font(height=size, width=size, thickness=0.15, bold=bold), hide=hide)


_libs = {}


def source_symbol(lib, entry):
    if lib not in _libs:
        _libs[lib] = SymbolLib.from_file(KICAD_SYMBOLS / f"{lib}.kicad_sym")
    found = next((s for s in _libs[lib].symbols if s.entryName == entry), None)
    if found is None:
        raise KeyError(f"Missing KiCad symbol {lib}:{entry}")
    found = deepcopy(found)
    found.libraryNickname = lib
    normalize_symbol(found)
    return found


def normalize_symbol(sym):
    """Down-convert KiCad 10 library metadata to the 20211123 schematic grammar."""
    for index, prop in enumerate(sym.properties):
        prop.id = index
        prop.showName = False
        if prop.key == "Description":
            prop.key = "ki_description"
        if index >= 2 and prop.effects:
            prop.effects.hide = True
    for child in sym.units:
        child.libraryNickname = None


def pin_list(sym, unit=1):
    pins = list(sym.pins)
    for child in sym.units:
        if child.unitId == unit:
            pins.extend(child.pins)
    # Some symbols put pins in style units; number is the stable key.
    unique = {}
    for pin in pins:
        unique[pin.number] = pin
    return list(unique.values())


def custom_symbol(name, pins):
    """Create a pin-accurate, readable rectangular symbol.

    pins is [(number, name, electrical_type), ...].  Package/footprint geometry lives
    in the PCB library; this only describes electrical pin identity and direction.
    """
    count = len(pins)
    rows = (count + 1) // 2
    half_h = max(10.16, (rows + 1) * 1.27)
    half_w = 12.7
    body = Symbol(
        libraryNickname=None, entryName=name, unitId=0, styleId=1,
        graphicItems=[SyRect(start=Position(-half_w, -half_h), end=Position(half_w, half_h),
                             stroke=Stroke(width=0.254, type="default"), fill=Fill(type="background"))]
    )
    # kiutils' SyRect argument is lowercase fill; keep construction explicit.
    body.graphicItems[0].fill = Fill(type="background")
    unit_pins = []
    for index, (number, pin_name, etype) in enumerate(pins):
        side_left = index < rows
        row = index if side_left else index - rows
        y = half_h - 2.54 - row * 2.54
        x = -half_w - 2.54 if side_left else half_w + 2.54
        angle = 0 if side_left else 180
        unit_pins.append(SymbolPin(
            electricalType=etype, graphicalStyle="line", position=Position(x, y, angle),
            length=2.54, name=pin_name, nameEffects=eff(0.9), number=str(number),
            numberEffects=eff(0.8),
        ))
    unit = Symbol(libraryNickname=None, entryName=name, unitId=1, styleId=1, pins=unit_pins)
    root = Symbol(
        libraryNickname="Stearlight", entryName=name, inBom=True, onBoard=True,
        pinNames=True, pinNamesOffset=1.0,
        properties=[
            Property("Reference", "U", position=Position(0, half_h + 2.0, 0), effects=eff(1.27)),
            Property("Value", name, position=Position(0, -half_h - 2.0, 0), effects=eff(1.27)),
            Property("Footprint", "", position=Position(0, 0, 0), effects=eff(1.0, True)),
            Property("Datasheet", "~", position=Position(0, 0, 0), effects=eff(1.0, True)),
        ],
        units=[body, unit],
    )
    normalize_symbol(root)
    return root


TPS25751_PINS = [
    (1, "LDO_3V3", "power_out"), (2, "ADCIN1", "input"), (3, "ADCIN2", "input"),
    (4, "LDO_1V5", "power_out"), (5, "GPIO0", "bidirectional"), (6, "GPIO1", "bidirectional"),
    (7, "GPIO2", "bidirectional"), (8, "I2Ct_SDA", "bidirectional"), (9, "I2Ct_SCL", "input"),
    (10, "I2Ct_IRQ", "open_collector"), (11, "GND", "power_in"), (12, "GND", "power_in"),
    (13, "GPIO11", "bidirectional"), (14, "GND", "power_in"), (15, "DRAIN", "passive"),
    (16, "I2Cc_SDA", "bidirectional"), (17, "I2Cc_SCL", "open_collector"),
    (18, "I2Cc_IRQ", "input"), (19, "GPIO3", "bidirectional"), (20, "PPHV", "power_out"),
    (21, "PPHV", "passive"), (22, "PPHV", "passive"), (23, "VBUS_IN", "power_in"),
    (24, "VBUS_IN", "power_in"), (25, "VBUS_IN", "power_in"),
    (26, "GPIO4/USB_P/LD1", "bidirectional"), (27, "GPIO5/USB_N/LD2", "bidirectional"),
    (28, "CC1", "bidirectional"), (29, "CC2", "bidirectional"), (30, "DRAIN", "passive"),
    (31, "GND", "power_in"), (32, "VBUS", "power_out"), (33, "VBUS", "passive"),
    (34, "PP5V", "power_in"), (35, "PP5V", "power_in"), (36, "GPIO7", "bidirectional"),
    (37, "GPIO6", "bidirectional"), (38, "VIN_3V3", "power_in"),
    (39, "GND_PAD", "power_in"), (40, "DRAIN_PAD", "passive"),
]

AIC3204_NAMES = [
    "MCLK", "BCLK", "WCLK", "DIN/MFP1", "DOUT/MFP2", "IOVDD", "IOVSS",
    "SCLK/MFP3", "SCL/SS", "SDA/MOSI", "MISO/MFP4", "SPI_SELECT", "IN1_L",
    "IN1_R", "IN2_L", "IN2_R", "AVSS", "REF", "MICBIAS", "IN3_L", "IN3_R",
    "LOL", "LOR", "AVDD", "HPL", "LDOIN/HPVDD", "HPR", "DVSS", "DVDD",
    "LDO_SELECT", "RESET", "GPIO/MFP5",
]
AIC3204_TYPES = {
    1:"input", 2:"bidirectional", 3:"bidirectional", 4:"input", 5:"output",
    6:"power_in", 7:"power_in", 8:"bidirectional", 9:"input", 10:"bidirectional",
    11:"output", 12:"input", 13:"input", 14:"input", 15:"input", 16:"input",
    17:"power_in", 18:"passive", 19:"power_out", 20:"input", 21:"input",
    22:"output", 23:"output", 24:"passive", 25:"output", 26:"power_in",
    27:"output", 28:"power_in", 29:"passive", 30:"input", 31:"input",
    32:"bidirectional",
}
AIC3204_PINS = [(i + 1, n, AIC3204_TYPES[i + 1]) for i, n in enumerate(AIC3204_NAMES)]

HD3SS3212_PINS = [
    (1, "RSVD1", "passive"), (2, "OEn", "input"), (3, "A0p", "bidirectional"),
    (4, "A0n", "bidirectional"), (5, "GND", "power_in"), (6, "VCC", "power_in"),
    (7, "A1p", "bidirectional"), (8, "A1n", "bidirectional"), (9, "SEL", "input"),
    (10, "RSVD2", "passive"), (11, "GND", "power_in"), (12, "C1n", "bidirectional"),
    (13, "C1p", "bidirectional"), (14, "C0n", "bidirectional"), (15, "C0p", "bidirectional"),
    (16, "B1n", "bidirectional"), (17, "B1p", "bidirectional"), (18, "B0n", "bidirectional"),
    (19, "B0p", "bidirectional"), (20, "GND", "power_in"),
]

TPS2553_PINS = [
    (1, "OUT", "power_out"), (2, "ILIM", "passive"), (3, "FAULT_N", "open_collector"),
    (4, "EN", "input"), (5, "GND", "power_in"), (6, "IN", "power_in"),
    (7, "EP", "power_in"),
]

TPS568230_PINS = [
    (1, "BST", "passive"), (2, "VIN", "power_in"), (3, "VIN", "power_in"),
    (4, "VIN", "power_in"), (5, "VIN", "power_in"), (6, "SW", "power_out"),
    (7, "GND", "power_in"), (8, "GND", "power_in"), (9, "PGOOD", "open_collector"),
    (10, "NC", "no_connect"), (11, "SS", "input"), (12, "EN", "input"),
    (13, "AGND", "power_in"), (14, "FB", "input"), (15, "MODE", "input"),
    (16, "NC", "no_connect"), (17, "VCC", "power_out"), (18, "GND", "power_in"),
    (19, "SW", "passive"), (20, "SW", "passive"), (21, "EP", "power_in"),
]


CUSTOM = {
    "TPS25751D": custom_symbol("TPS25751D", TPS25751_PINS),
    "TLV320AIC3204": custom_symbol("TLV320AIC3204", AIC3204_PINS),
    "HD3SS3212": custom_symbol("HD3SS3212", HD3SS3212_PINS),
    "TPS2553": custom_symbol("TPS2553", TPS2553_PINS),
    "TPS568230": custom_symbol("TPS568230", TPS568230_PINS),
    "TRACKING_100": custom_symbol(
        "TRACKING_100", [(i, f"RSVD_{i:03d}", "passive") for i in range(1, 101)]
    ),
    "BM28_20P2": custom_symbol(
        "BM28_20P2",
        [(i, f"SIG_{i:02d}", "passive") for i in range(1, 21)] +
        [("P1", "POWER_1", "passive"), ("P2", "POWER_2", "passive")],
    ),
    "CAT24C512": custom_symbol("CAT24C512", [
        (1, "A0", "input"), (2, "A1", "input"), (3, "A2", "input"),
        (4, "GND", "power_in"), (5, "SDA", "bidirectional"),
        (6, "SCL", "input"), (7, "WP", "input"), (8, "VCC", "power_in"),
    ]),
    "TLV75528": custom_symbol("TLV75528", [
        (1, "IN", "power_in"), (2, "GND", "power_in"), (3, "EN", "input"),
        (4, "NC", "no_connect"), (5, "OUT", "power_out"),
    ]),
    "SJ43514": custom_symbol("SJ43514", [
        (1, "SLEEVE_MIC", "passive"), (2, "TIP_LEFT", "passive"),
        (3, "RING1_RIGHT", "passive"), (4, "RING2_GND", "passive"),
    ]),
    "CRYSTAL4": custom_symbol("CRYSTAL4", [
        (1, "X1", "passive"), (2, "GND", "power_in"),
        (3, "X2", "passive"), (4, "GND", "power_in"),
    ]),
    "TPS62825": custom_symbol("TPS62825", [
        (1, "EN", "input"), (2, "PG", "open_collector"), (3, "FB", "input"),
        (4, "GND", "power_in"), (5, "SW", "power_out"), (6, "VIN", "power_in"),
    ]),
    "PWR_FLAG_LOCAL": custom_symbol("PWR_FLAG_LOCAL", [(1, "PWR_FLAG", "power_out")]),
    "TLV803E": custom_symbol("TLV803E", [
        (1, "GND", "power_in"), (2, "RESET_N", "open_collector"),
        (3, "VDD", "power_in"),
    ]),
    "TVS2200": custom_symbol("TVS2200", [
        (1, "GND", "power_in"), (2, "GND", "passive"),
        (3, "GND", "passive"), (4, "IN", "passive"),
        (5, "IN", "passive"), (6, "IN", "passive"),
        (7, "GND_EP", "power_in"),
    ]),
}


def endpoint(origin, pin):
    # All generated symbols use orientation 0; KiCad schematic Y increases downward.
    return Position(origin.X + pin.position.X, origin.Y - pin.position.Y, 0)


def add_component(sch, sym, ref, value, footprint, datasheet, x, y, nets=None, unit=1,
                  no_connect_unmapped=False, in_bom=True, on_board=True, dnp=False):
    # KiCad's ERC connection grid is 50 mil (1.27 mm).  Generated placement is
    # snapped here so every symbol pin and label endpoint remains connectable.
    x = round(x / 1.27) * 1.27
    y = round(y / 1.27) * 1.27
    key = f"{sym.libraryNickname}:{sym.entryName}"
    if not any(f"{s.libraryNickname}:{s.entryName}" == key for s in sch.libSymbols):
        sch.libSymbols.append(deepcopy(sym))
    pins = pin_list(sym, unit)
    all_pins = {}
    for candidate in list(sym.pins) + [p for child in sym.units for p in child.pins]:
        all_pins[candidate.number] = candidate
    properties = [
        Property("Reference", ref, 0, Position(x, y - 4.0, 0), eff(1.0)),
        Property("Value", value, 1, Position(x, y + 4.0, 0), eff(1.0)),
        Property("Footprint", footprint, 2, Position(x, y, 0), eff(0.8, True)),
        Property("Datasheet", datasheet or "~", 3, Position(x, y, 0), eff(0.8, True)),
    ]
    inst = SchematicSymbol(
        libraryNickname=sym.libraryNickname, entryName=sym.entryName,
        position=Position(x, y, 0), unit=unit, inBom=in_bom, onBoard=on_board,
        dnp=dnp, uuid=uid(),
        properties=properties, pins={number: uid() for number in all_pins},
    )
    sch.schematicSymbols.append(inst)
    nets = nets or {}
    for pin in pins:
        pos = endpoint(inst.position, pin)
        net = nets.get(pin.number)
        if net == "#NC":
            sch.noConnects.append(NoConnect(position=pos, uuid=uid()))
        elif net:
            sch.globalLabels.append(GlobalLabel(
                text=net, shape="bidirectional", position=pos, effects=eff(0.75), uuid=uid()
            ))
        elif no_connect_unmapped:
            sch.noConnects.append(NoConnect(position=pos, uuid=uid()))
    return inst


def add_power_flag(sch, ref, x, y, net):
    return add_component(sch, CUSTOM["PWR_FLAG_LOCAL"], ref, "PWR_FLAG", "", "~",
                         x, y, {"1": net}, in_bom=False, on_board=False)


def add_passive(sch, kind, ref, value, x, y, net1, net2, footprint=None):
    if kind == "R":
        sym = source_symbol("Device", "R")
        footprint = footprint or "Resistor_SMD:R_0402_1005Metric"
    elif kind == "C":
        sym = source_symbol("Device", "C")
        footprint = footprint or "Capacitor_SMD:C_0402_1005Metric"
    elif kind == "L":
        sym = source_symbol("Device", "L")
        if footprint is None:
            raise ValueError(f"Inductor {ref} requires an explicitly verified footprint")
    else:
        raise ValueError(kind)
    return add_component(sch, sym, ref, value, footprint, "~", x, y,
                         {"1": net1, "2": net2})


def official_cm4_symbol():
    src = Schematic.from_file(ROOT / "cm4-io/CM4_GPIO.kicad_sch")
    return deepcopy(next(s for s in src.libSymbols if s.entryName == "ComputeModule4-CM4"))


def cm4_200_symbol():
    official = official_cm4_symbol()
    pins = []
    for unit in (1, 2):
        for pin in pin_list(official, unit):
            etype = pin.electricalType
            if pin.name in ("+3.3v_(Output)", "+1.8v_(Output)"):
                # The official CM4 symbol models every duplicated rail pin as a
                # power output, which creates false output-to-output ERC errors.
                # A single explicit PWR_FLAG models the shared module rail.
                etype = "passive"
            pins.append((int(pin.number), pin.name, etype))
    pins.sort(key=lambda item: item[0])
    return custom_symbol("CM4_200PIN", pins)


def cm4_nets(sym, unit):
    result = {}
    for pin in pin_list(sym, unit):
        name = pin.name
        if name == "GND":
            result[pin.number] = "GND"
        elif name == "+5v_(Input)":
            result[pin.number] = "+5V_SYS"
        elif name == "+3.3v_(Output)":
            result[pin.number] = "+3V3"
    if unit == 1:
        result.update({
            "25":"I2S_DOUT", "26":"I2S_LRCLK", "27":"I2S_DIN", "30":"FAN_TACH",
            "31":"FAN_PWM", "34":"AUDIO_RESET_N", "41":"PROX_GPIO", "46":"TYPEC_INT_N",
            "47":"PROX_INT", "48":"TYPEC_FLIP", "49":"I2S_BCLK", "56":"I2C_SCL",
            "51":"UART_RX", "54":"AUDIO_MCLK", "55":"UART_TX", "58":"I2C_SDA", "78":"+3V3", "89":"WIFI_ENABLE_N", "91":"BT_ENABLE_N",
            "92":"RUN_PG", "93":"EMMC_BOOT_N", "100":"CM4_EXT_RESET_N",
        })
    else:
        result.update({
            "101":"GND", "102":"PCIE_CLKREQ_N", "103":"USB2_SERVICE_DM",
            "105":"USB2_SERVICE_DP", "109":"PCIE_RST_N", "110":"PCIE_CLK_P",
            "112":"PCIE_CLK_N", "116":"PCIE_RX_P", "118":"PCIE_RX_N",
            "122":"PCIE_TX_CM4_P", "124":"PCIE_TX_CM4_N",
        })
    return result


def make_cm4(unit, title):
    s = new_sheet(title, "Official Raspberry Pi CM4 connector symbol and pin numbering")
    sym = official_cm4_symbol()
    add_component(s, sym, "Module1", "CM4 2GB/16GB/Wireless",
                  "CM4IO:Raspberry-Pi-4-Compute-Module",
                  "https://www.raspberrypi.com/documentation/computers/compute-module.html",
                  105, 92, cm4_nets(sym, unit), unit=unit)
    if unit == 2:
        s.texts.append(Text("Camera, HDMI and DSI pins are deliberately NC until exact displays are selected.",
                            Position(30, 155, 0), eff(1.0, False, True), uid()))
    return s


def make_cm4_combined():
    s = new_sheet("CM4 CONNECTORS / GPIO / HIGH-SPEED", "All 200 official Raspberry Pi CM4 pins in one carrier symbol")
    official = official_cm4_symbol()
    sym = cm4_200_symbol()
    nets = cm4_nets(official, 1)
    nets.update(cm4_nets(official, 2))
    add_component(s, sym, "Module1", "CM4 2GB/16GB/Wireless",
                  "CM4IO:Raspberry-Pi-4-Compute-Module",
                  "https://www.raspberrypi.com/documentation/computers/compute-module.html",
                  205, 148, nets, unit=1, no_connect_unmapped=True)
    add_passive(s, "R", "R22", "10k", 40, 245, "+3V3", "WIFI_ENABLE_N")
    add_passive(s, "R", "R23", "10k", 80, 245, "+3V3", "BT_ENABLE_N")
    s.texts.append(Text("Camera, HDMI and DSI pins are deliberately left unconnected until exact displays are selected.",
                        Position(55, 275, 0), eff(1.0, False, True), uid()))
    return s


def new_sheet(title, comments):
    # Start from the known-good Raspberry Pi sheet envelope.  KiCad's old schematic
    # grammar contains project bookkeeping that a completely blank kiutils object
    # does not emit, while a parsed official sheet round-trips cleanly.
    sch = Schematic.from_file(ROOT / "reference/CM4IOUSB3/PSUs.kicad_sch")
    sch.uuid = uid()
    sch.paper = PageSettings("A3")
    sch.titleBlock = TitleBlock(title=title, revision="A", company="Stearlight",
                                comments={1: comments, 2: "NOT A FABRICATION RELEASE"})
    sch.libSymbols = []
    sch.schematicSymbols = []
    sch.junctions = []
    sch.noConnects = []
    sch.busEntries = []
    sch.graphicalItems = []
    sch.shapes = []
    sch.images = []
    sch.texts = []
    sch.textBoxes = []
    sch.labels = []
    sch.globalLabels = []
    sch.hierarchicalLabels = []
    sch.sheets = []
    sch.sheetInstances = []
    sch.texts.append(Text(title, Position(55, 18, 0), eff(2.0, False, True), uid()))
    sch.texts.append(Text(comments, Position(55, 22, 0), eff(1.0), uid()))
    return sch


def make_power():
    s = new_sheet("POWER / USB-PD / BATTERY", "TPS25751 + BQ25798 EVM-derived; Rev A default protected 2S")
    add_component(s, CUSTOM["TPS25751D"], "U12", "TPS25751DREFR",
                  "Stearlight:Texas_REF0038A_WQFN-38_6x4mm_DualPad",
                  "reference/TI_USB_PD/TPS25751.pdf", 65, 65,
                  {"1":"LDO_3V3", "4":"LDO_1V5", "8":"I2Ct_SDA", "9":"I2Ct_SCL",
                   "10":"PD_IRQ_N", "11":"GND", "12":"GND", "14":"GND", "16":"CHG_SDA",
                   "17":"CHG_SCL", "18":"CHG_IRQ_N", "20":"PPHV", "21":"PPHV", "22":"PPHV",
                   "23":"VBUS_CHARGE", "24":"VBUS_CHARGE", "25":"VBUS_CHARGE",
                   "26":"USB2_SERVICE_DP", "27":"USB2_SERVICE_DM", "28":"CC1", "29":"CC2",
                   "31":"GND", "32":"VBUS_CHARGE", "33":"VBUS_CHARGE", "34":"+5V_SYS", "35":"+5V_SYS",
                   "38":"+3V3", "39":"GND", "40":"#NC",
                   "2":"LDO_1V5", "3":"LDO_1V5", "5":"#NC", "6":"#NC", "7":"#NC",
                   "13":"#NC", "15":"#NC", "19":"#NC", "30":"#NC", "36":"#NC", "37":"#NC"})
    bq = source_symbol("Battery_Management", "BQ25798")
    add_component(s, bq, "U6", "BQ25798RQMR",
                  "Package_DFN_QFN:Texas_RQM0029A_VQFN-29_4x4mm_P0.4mm",
                  "reference/TI_USB_PD/BQ25798.pdf", 130, 70,
                  {"1":"CHG_STAT", "2":"PPHV", "3":"PPHV", "4":"BST1",
                   "5":"REGN", "6":"#NC", "7":"#NC", "8":"PPHV", "9":"PPHV",
                   "10":"GND", "11":"GND", "12":"GND", "13":"GND",
                   "14":"CHG_SCL", "15":"CHG_SDA", "16":"TS", "17":"ILIM_HIZ",
                   "18":"BATP", "19":"BST2", "20":"PROG", "21":"CHG_IRQ_N",
                   "22":"BAT+", "23":"BAT+", "24":"#NC", "25":"VBAT_SYS",
                   "26":"SW2", "27":"GND", "28":"SW1", "29":"PMID"})
    buck = CUSTOM["TPS568230"]
    add_component(s, buck, "U2", "TPS568230RJER",
                  "Package_DFN_QFN:Texas_RJE0020A_VQFN-20-1EP_3x3mm_P0.45mm_EP0.675x0.76mm_ThermalVias",
                  "reference/TI_System_Power/TPS568230.pdf", 200, 65,
                  {"1":"BST_5V", "2":"VBAT_SYS", "3":"VBAT_SYS", "4":"VBAT_SYS", "5":"VBAT_SYS",
                   "6":"SW_5V", "7":"GND", "8":"GND", "9":"PGOOD_5V", "10":"#NC", "11":"#NC",
                   "12":"VBAT_SYS", "13":"GND", "14":"FB_5V", "15":"MODE_5V", "16":"#NC", "17":"VCC_5V",
                   "18":"GND", "19":"SW_5V", "20":"SW_5V", "21":"GND"})
    add_component(s, CUSTOM["TPS62825"], "U1", "TPS62825DMQR",
                  "Stearlight:Texas_DMQ0006A_VSON-HR-6_1.5x1.5mm",
                  "reference/TI_System_Power/TPS62825.pdf", 235, 95,
                  {"1":"+3V3", "2":"USB1V05_PG", "3":"USB1V05_FB", "4":"GND",
                   "5":"USB1V05_SW", "6":"+3V3"})
    add_component(s, CUSTOM["CAT24C512"], "U13", "CAT24C512WI-GT3",
                  "Package_SO:TSSOP-8_3x3mm_P0.65mm", "https://www.onsemi.com/pdf/datasheet/cat24c512-d.pdf",
                  65, 125, {"1":"GND", "2":"GND", "3":"GND", "4":"GND",
                            "5":"I2Ct_SDA", "6":"I2Ct_SCL", "7":"GND", "8":"LDO_3V3"})
    charge_usb = source_symbol("Connector", "USB_C_Receptacle_USB2.0_16P")
    add_component(s, charge_usb, "J9", "DX07S016JA1R1500 CHARGE/SERVICE",
                  "Connector_USB:USB_C_Receptacle_JAE_DX07S016JA1R1500",
                  "https://products.jae.com/gl/en/connectors/category/io/dx07-receptacle/dx07s016ja1r1500/",
                  125, 145,
                  {"A1":"GND", "A4":"VBUS_CHARGE", "A5":"CC1",
                   "A6":"USB2_SERVICE_DP", "A7":"USB2_SERVICE_DM",
                   "A9":"VBUS_CHARGE", "A12":"GND", "B1":"GND",
                   "B4":"VBUS_CHARGE", "B5":"CC2", "B6":"USB2_SERVICE_DP",
                   "B7":"USB2_SERVICE_DM", "B9":"VBUS_CHARGE", "B12":"GND",
                   "SH":"CHASSIS_GND"}, no_connect_unmapped=True)
    add_component(s, CUSTOM["TVS2200"],
                  "D1", "TVS2200DRVR",
                  "Package_SON:WSON-6-1EP_2x2mm_P0.65mm_EP1x1.6mm",
                  "https://www.ti.com/lit/ds/symlink/tvs2200.pdf", 165, 145,
                  {"1":"GND", "2":"GND", "3":"GND", "4":"VBUS_CHARGE",
                   "5":"VBUS_CHARGE", "6":"VBUS_CHARGE", "7":"GND"})

    # Values and topology follow TI USB-PD-CHG-EVM-01 and the TPS568230 5 V table.
    # J9 is the physical USB-C charge/service port; all four VBUS/GND contacts are used.
    power_caps = [
        ("C1", "10u 6.3V", "LDO_3V3", "GND"),
        ("C2", "10u 6.3V", "LDO_1V5", "GND"),
        ("C3", "10u 6.3V", "+3V3", "GND"),
        ("C4", "10u 25V", "PPHV", "GND"), ("C5", "10u 25V", "PPHV", "GND"),
        ("C6", "100n 25V", "PPHV", "GND"),
        ("C7", "330p", "CC1", "GND"), ("C8", "330p", "CC2", "GND"),
        ("C9", "10u 25V", "VBUS_CHARGE", "GND"),
        ("C10", "10u 25V", "VBUS_CHARGE", "GND"),
        ("C11", "10u 25V", "VBUS_CHARGE", "GND"),
        ("C12", "100n 25V", "VBUS_CHARGE", "GND"),
        ("C13", "10u 25V", "PMID", "GND"), ("C14", "10u 25V", "PMID", "GND"),
        ("C15", "100n 25V", "PMID", "GND"), ("C16", "4.7u 10V", "REGN", "GND"),
        ("C17", "47n 25V", "BST1", "SW1"), ("C18", "47n 25V", "BST2", "SW2"),
        ("C19", "100n 25V", "VBAT_SYS", "GND"), ("C20", "10u 25V", "VBAT_SYS", "GND"),
        ("C21", "10u 25V", "VBAT_SYS", "GND"), ("C22", "10u 25V", "VBAT_SYS", "GND"),
        ("C23", "10u 25V", "VBAT_SYS", "GND"), ("C24", "10u 25V", "BAT+", "GND"),
        ("C25", "100n 25V", "BST_5V", "SW_5V"), ("C26", "1u 10V", "VCC_5V", "GND"),
        ("C27", "47p", "FB_5V", "GND"),
        ("C57", "100u 10V low-ESR", "+5V_SYS", "GND"),
        ("C58", "100u 10V low-ESR", "+5V_SYS", "GND"),
        ("C59", "22u 25V", "VBAT_SYS", "GND"),
        ("C60", "22u 25V", "VBAT_SYS", "GND"),
    ]
    for index, (ref, value, n1, n2) in enumerate(power_caps):
        add_passive(s, "C", ref, value, 25 + (index % 9) * 27, 180 + (index // 9) * 25,
                    n1, n2, "Capacitor_SMD:C_0603_1608Metric" if "10u" in value or "22u" in value else None)
    power_resistors = [
        ("R1", "220k 1%", "+5V_SYS", "FB_5V"), ("R2", "30k 1%", "FB_5V", "GND"),
        ("R3", "5.23k 1%", "REGN", "ILIM_HIZ"), ("R4", "137k 1%", "ILIM_HIZ", "GND"),
        ("R5", "2.2k", "LDO_3V3", "I2Ct_SDA"), ("R6", "2.2k", "LDO_3V3", "I2Ct_SCL"),
        ("R7", "10k", "LDO_3V3", "PD_IRQ_N"), ("R8", "10k", "+3V3", "CHG_IRQ_N"),
        ("R9", "30.1k 1%", "TS", "GND"), ("R10", "10k NTC option", "TS", "GND"),
        ("R11", "100R 1%", "BATP", "BAT+"), ("R12", "6.04k 1% (2S)", "PROG", "GND"),
    ]
    for index, (ref, value, n1, n2) in enumerate(power_resistors):
        add_passive(s, "R", ref, value, 25 + (index % 9) * 27, 260 + (index // 9) * 20, n1, n2)
    add_passive(s, "L", "L3", "SRP5030CA-1R0M 1uH 9A", 245, 145, "SW1", "SW2",
                "Stearlight:Bourns_SRP5030CA")
    add_passive(s, "L", "L2", "SRP7050TA-2R2M 2.2uH 10A", 245, 165, "SW_5V", "+5V_SYS",
                "Stearlight:Bourns_SRP7050TA")
    add_passive(s, "L", "L1", "0.47uH XFL4015-471MEC", 220, 145,
                "USB1V05_SW", "+1V05_USB", "Inductor_SMD:L_0402_1005Metric")
    add_passive(s, "C", "C64", "4.7u 10V", 175, 280, "+3V3", "GND",
                "Capacitor_SMD:C_0603_1608Metric")
    add_passive(s, "C", "C65", "10u 6.3V", 205, 280, "+1V05_USB", "GND",
                "Capacitor_SMD:C_0603_1608Metric")
    add_passive(s, "C", "C66", "10u 6.3V", 235, 280, "+1V05_USB", "GND",
                "Capacitor_SMD:C_0603_1608Metric")
    add_passive(s, "C", "C67", "120p C0G", 265, 280, "+1V05_USB", "USB1V05_FB")
    add_passive(s, "R", "R19", "75k 1%", 175, 255, "+1V05_USB", "USB1V05_FB")
    add_passive(s, "R", "R20", "100k 1%", 205, 255, "USB1V05_FB", "GND")
    add_passive(s, "R", "R21", "100k", 235, 255, "+3V3", "USB1V05_PG")
    add_passive(s, "R", "R24", "75k 1% (FCCM 800kHz)", 255, 255, "VCC_5V", "MODE_5V")
    add_passive(s, "R", "R25", "51k 1% (FCCM 800kHz)", 255, 275, "MODE_5V", "GND")
    add_passive(s, "R", "R27", "2.2k", 145, 255, "LDO_3V3", "CHG_SCL")
    add_passive(s, "R", "R28", "2.2k", 145, 275, "LDO_3V3", "CHG_SDA")
    add_power_flag(s, "#FLG03", 65, 145, "+5V_SYS")
    add_power_flag(s, "#FLG04", 85, 145, "+1V05_USB")
    add_power_flag(s, "#FLG05", 105, 145, "+3V3")
    add_power_flag(s, "#FLG06", 105, 165, "GND")
    s.texts.append(Text("Populate PROG = 6.04 k for 2S. 3S/4S options are DNP and require design review.",
                        Position(30, 120, 0), eff(1.0, False, True), uid()))
    return s


def make_usb3():
    s = new_sheet("PCIE TO USB 3 HOST", "High-speed geometry held until JLCPCB six-layer stack selection")
    renesas = source_symbol("Interface_USB", "UPD720202K8-7x1-BAA")
    add_component(s, renesas, "U14", "UPD720202K8-711-BAA-A",
                  "Package_DFN_QFN:QFN-48-1EP_7x7mm_P0.5mm_EP5.7x5.7mm_ThermalVias",
                  "https://www.renesas.com/en/products/upd720202", 65, 75,
                  {"1":"PCIE_CLK_P", "2":"PCIE_CLK_N", "3":"+3V3",
                   "4":"PCIE_RX_USB3_P", "5":"PCIE_RX_USB3_N", "6":"+1V05_USB",
                   "7":"PCIE_TX_P", "8":"PCIE_TX_N", "9":"+1V05_USB",
                   "10":"PCIE_CLKREQ_N", "11":"USB3_POR_N", "12":"+3V3",
                   "13":"SPI_MISO", "14":"SPI_CS_N", "15":"SPI_CLK", "16":"SPI_MOSI",
                   "17":"#NC", "18":"#NC", "19":"USB_OC_N",
                   "20":"#NC", "21":"+1V05_USB", "22":"+3V3",
                   "23":"USB3_XT2", "24":"USB3_XT1", "25":"+3V3",
                   "26":"USB3_RREF", "27":"GND", "28":"USB3_TX_P", "29":"USB3_TX_N",
                   "30":"+1V05_USB", "31":"USB3_RX_P", "32":"USB3_RX_N",
                   "33":"+1V05_USB", "34":"+3V3", "35":"USB2_HOST_DP",
                   "36":"USB2_HOST_DM", "37":"#NC", "38":"#NC",
                   "39":"+1V05_USB", "40":"#NC", "41":"#NC",
                   "42":"+1V05_USB", "43":"+3V3", "44":"#NC",
                   "45":"#NC", "46":"#NC", "47":"PCIE_RST_N",
                   "48":"#NC", "49":"GND"})
    add_component(s, CUSTOM["TLV803E"], "U15", "TLV803EA30DBZR",
                  "Package_TO_SOT_SMD:SOT-23", "reference/TI_System_Power/TLV803E.pdf",
                  115, 135, {"1":"GND", "2":"USB3_POR_N", "3":"+3V3"})
    mux = CUSTOM["HD3SS3212"]
    add_component(s, mux, "U11", "HD3SS3212IRKSR",
                  "Package_DFN_QFN:Texas_RVC0020A_WQFN-20-1EP_3x4mm_P0.5mm_EP1.6x2.6mm",
                  "reference/TI_USB_C/HD3SS3212.pdf", 130, 72,
                  {"1":"#NC", "2":"GND", "3":"USB3_TX_P", "4":"USB3_TX_N", "5":"GND", "6":"+3V3",
                   "7":"USB3_RX_P", "8":"USB3_RX_N", "9":"TYPEC_FLIP", "11":"GND",
                   "12":"USB_C_RX2_N", "13":"USB_C_RX2_P", "14":"USB_C_TX2_N", "15":"USB_C_TX2_P",
                   "16":"USB_C_RX1_N", "17":"USB_C_RX1_P", "18":"USB_C_TX1_N", "19":"USB_C_TX1_P",
                   "20":"GND", "10":"#NC"})
    cc = source_symbol("Interface_USB", "TUSB320")
    add_component(s, cc, "U5", "TUSB320LAIRWBR",
                  "Package_DFN_QFN:Texas_X2QFN-12_1.6x1.6mm_P0.4mm", "reference/TI_USB_C/TUSB320LAI.pdf",
                  190, 55, {"1":"CC1", "2":"CC2", "3":"+3V3", "4":"#NC",
                              "5":"GND", "6":"TYPEC_INT_N", "7":"I2C_SDA", "8":"I2C_SCL",
                              "9":"TYPEC_ATTACHED_N", "10":"GND", "11":"GND", "12":"+3V3"})
    add_component(s, CUSTOM["TPS2553"], "U16", "TPS2553-1DRVR",
                  "Package_SON:WSON-6-1EP_2x2mm_P0.65mm_EP1x1.6mm", "reference/TI_USB_C/TPS2553.pdf",
                  190, 95, {"1":"VBUS_USB_HOST", "2":"USB_ILIM", "3":"USB_OC_N", "4":"TYPEC_ATTACHED_N",
                              "5":"GND", "6":"+5V_SYS", "7":"GND"})
    usb_c = source_symbol("Connector", "USB_C_Receptacle")
    add_component(s, usb_c, "J8", "DX07S024JJ2R1300 ACCESSORY",
                  "Stearlight:JAE_DX07S024JJ2R1300",
                  "https://www.jae.com/en/connectors/series/detail/id=64368", 245, 75,
                  {"A1":"GND", "A2":"USB_C_TX1_P", "A3":"USB_C_TX1_N",
                   "A4":"VBUS_USB_HOST", "A5":"CC1", "A6":"USB2_HOST_DP",
                   "A7":"USB2_HOST_DM", "A8":"#NC", "A9":"VBUS_USB_HOST",
                   "A10":"USB_C_RX2_N", "A11":"USB_C_RX2_P", "A12":"GND",
                   "B1":"GND", "B2":"USB_C_TX2_P", "B3":"USB_C_TX2_N",
                   "B4":"VBUS_USB_HOST", "B5":"CC2", "B6":"USB2_HOST_DP",
                   "B7":"USB2_HOST_DM", "B8":"#NC", "B9":"VBUS_USB_HOST",
                   "B10":"USB_C_RX1_N", "B11":"USB_C_RX1_P", "B12":"GND",
                   "SH":"CHASSIS_GND"})
    add_component(s, CUSTOM["CRYSTAL4"], "Y1", "24MHz 12pF",
                  "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm", "~", 65, 135,
                  {"1":"USB3_XT1", "2":"GND", "3":"USB3_XT2", "4":"GND"})
    add_component(s, source_symbol("Power_Protection", "TPD4EUSB30"), "U8",
                  "TPD4EUSB30DQAR", "Package_SON:USON-10_2.5x1.0mm_P0.5mm",
                  "https://www.ti.com/lit/ds/symlink/tpd4eusb30.pdf", 140, 145,
                  {"1":"USB_C_TX1_P", "2":"USB_C_TX1_N", "3":"GND",
                   "4":"USB_C_RX1_P", "5":"USB_C_RX1_N", "8":"GND"},
                  no_connect_unmapped=True)
    add_component(s, source_symbol("Power_Protection", "TPD4EUSB30"), "U9",
                  "TPD4EUSB30DQAR", "Package_SON:USON-10_2.5x1.0mm_P0.5mm",
                  "https://www.ti.com/lit/ds/symlink/tpd4eusb30.pdf", 190, 145,
                  {"1":"USB_C_TX2_P", "2":"USB_C_TX2_N", "3":"GND",
                   "4":"USB_C_RX2_P", "5":"USB_C_RX2_N", "8":"GND"},
                  no_connect_unmapped=True)
    add_component(s, source_symbol("Power_Protection", "TPD2EUSB30"), "U7",
                  "TPD2EUSB30DRTR", "Package_TO_SOT_SMD:SOT-23",
                  "https://www.ti.com/lit/ds/symlink/tpd2eusb30.pdf", 235, 145,
                  {"1":"USB2_HOST_DP", "2":"USB2_HOST_DM", "3":"GND"})
    usb_caps = [
        ("C28", "100n", "+1V05_USB", "GND"), ("C29", "100n", "+1V05_USB", "GND"),
        ("C30", "100n", "+1V05_USB", "GND"), ("C31", "100n", "+1V05_USB", "GND"),
        ("C32", "100n", "+1V05_USB", "GND"), ("C33", "100n", "+1V05_USB", "GND"),
        ("C34", "100n", "+3V3", "GND"), ("C35", "100n", "+3V3", "GND"),
        ("C36", "100n", "+3V3", "GND"), ("C37", "100n", "+3V3", "GND"),
        ("C38", "10u", "+3V3", "GND"), ("C39", "1u", "+1V05_USB", "GND"),
        ("C40", "1u", "+3V3", "GND"),
    ]
    for index, (ref, value, n1, n2) in enumerate(usb_caps):
        add_passive(s, "C", ref, value, 25 + (index % 8) * 30, 190 + (index // 8) * 25,
                    n1, n2, "Capacitor_SMD:C_0603_1608Metric" if value == "10u" else None)
    add_passive(s, "R", "R13", "200k", 25, 250, "+3V3", "TYPEC_INT_N")
    add_passive(s, "R", "R14", "200k", 65, 250, "+3V3", "TYPEC_ATTACHED_N")
    add_passive(s, "R", "R15", "20k 1% (1.5A limit)", 105, 250, "USB_ILIM", "GND")
    add_passive(s, "R", "R26", "10k PCIe CLKREQ pull-up", 130, 250, "+3V3", "PCIE_CLKREQ_N")
    add_passive(s, "R", "R29", "10k OCI pull-up", 130, 270, "+3V3", "USB_OC_N")
    add_passive(s, "R", "R30", "1.6k RREF - verify gated Renesas manual", 160, 270,
                "USB3_RREF", "GND")
    add_passive(s, "R", "R31", "10k reset pull-up", 190, 270, "+3V3", "USB3_POR_N")
    add_passive(s, "C", "C68", "100n", 220, 270, "+3V3", "GND")
    # The -711 firmware-download variant omits serial ROM.  These straps are
    # Renesas Figure 5-8 (TN-USB-A0004A/E), including the mandatory SPISO pull-up.
    add_passive(s, "R", "R32", "10k", 300, 190, "SPI_CLK", "GND")
    add_passive(s, "R", "R33", "10k", 300, 210, "SPI_CS_N", "GND")
    add_passive(s, "R", "R34", "10k", 300, 230, "+3V3", "SPI_MISO")
    add_passive(s, "R", "R35", "10k", 300, 250, "SPI_MOSI", "GND")
    add_passive(s, "C", "C53", "100n PCIe AC", 150, 250, "PCIE_TX_CM4_P", "PCIE_TX_P")
    add_passive(s, "C", "C54", "100n PCIe AC", 180, 250, "PCIE_TX_CM4_N", "PCIE_TX_N")
    add_passive(s, "C", "C55", "100n PCIe AC", 210, 250, "PCIE_RX_USB3_P", "PCIE_RX_P")
    add_passive(s, "C", "C56", "100n PCIe AC", 240, 250, "PCIE_RX_USB3_N", "PCIE_RX_N")
    s.texts.append(Text("REVIEW GATE: verify all Renesas straps/decoupling/sequencing against restricted hardware manual.",
                        Position(30, 130, 0), eff(1.1, False, True), uid()))
    return s


def make_audio():
    s = new_sheet("AUDIO AND MICROPHONES", "Codec reference circuit; IM73A135 microphones use dedicated 2.8 V rail")
    codec_nets = {
        "1":"AUDIO_MCLK", "2":"I2S_BCLK", "3":"I2S_LRCLK", "4":"I2S_DOUT", "5":"I2S_DIN",
        "6":"+3V3", "7":"GND", "9":"I2C_SCL", "10":"I2C_SDA", "12":"GND",
        "8":"#NC", "11":"#NC", "13":"MIC_R_N", "14":"MIC_R_P", "15":"MIC_L_P",
        "16":"MIC_L_N", "17":"GND", "18":"AUDIO_REF", "19":"MICBIAS",
        "20":"#NC", "21":"JACK_MIC_CODEC", "22":"#NC", "23":"#NC",
        "24":"AUDIO_1V8_LDO", "25":"HP_L_CODEC", "26":"+3V3_AUDIO",
        "27":"HP_R_CODEC", "28":"GND", "29":"AUDIO_1V8_LDO", "30":"+3V3",
        "31":"AUDIO_RESET_N", "32":"#NC",
    }
    add_component(s, CUSTOM["TLV320AIC3204"], "U3", "TLV320AIC3204IRHBR",
                  "Package_DFN_QFN:Texas_RHB0032M_VQFN-32-1EP_5x5mm_P0.5mm_EP2.1x2.1mm_ThermalVias",
                  "reference/TI_Audio/TLV320AIC3204.pdf", 100, 75, codec_nets)
    mic = source_symbol("Sensor_Audio", "IM73A135V01")
    for ref, x, side in [("MK1", 45, "L"), ("MK2", 170, "R")]:
        add_component(s, mic, ref, "IM73A135V01", "Sensor_Audio:Infineon_PG-LLGA-5-2",
                      "reference/Infineon_Microphone/IM73A135.pdf", x, 75,
                      {"1":f"MIC_{side}_RAW_P", "2":"+2V8_MIC", "3":f"MIC_{side}_RAW_N", "4":"GND", "5":"GND"})
    add_component(s, CUSTOM["TLV75528"], "U10", "TLV75528PDBVR",
                  "Package_TO_SOT_SMD:SOT-23-5", "https://www.ti.com/lit/ds/symlink/tlv755p.pdf",
                  215, 75, {"1":"+3V3_AUDIO", "2":"GND", "3":"+3V3_AUDIO",
                            "4":"#NC", "5":"+2V8_MIC"})
    add_component(s, CUSTOM["SJ43514"], "J1", "SJ-43514-SMT-TR",
                  "Stearlight:SameSky_SJ-43514-SMT-TR", "reference/TI_Audio/SJ-4351X-SMT.pdf",
                  230, 125, {"1":"JACK_MIC", "2":"HP_L", "3":"HP_R", "4":"GND"})
    add_component(s, source_symbol("Power_Protection", "TPD4EUSB30"), "U4",
                  "TPD4E05U06DQAR", "Package_SON:USON-10_2.5x1.0mm_P0.5mm",
                  "https://www.ti.com/lit/ds/symlink/tpd4e05u06.pdf", 185, 130,
                  {"1":"HP_L", "2":"HP_R", "3":"GND", "4":"JACK_MIC", "5":"GND", "8":"GND"},
                  no_connect_unmapped=True)
    audio_caps = [
        ("C41", "1u", "+3V3_AUDIO", "GND"), ("C42", "1u", "+2V8_MIC", "GND"),
        ("C43", "100n", "+3V3", "GND"), ("C44", "10u", "AUDIO_1V8_LDO", "GND"),
        ("C45", "10u", "+3V3_AUDIO", "GND"), ("C46", "10u", "AUDIO_REF", "GND"),
        ("C47", "10u", "MICBIAS", "GND"), ("C48", "100n", "+3V3_AUDIO", "GND"),
        ("C49", "1u film/C0G", "MIC_L_RAW_P", "MIC_L_P"),
        ("C50", "1u film/C0G", "MIC_L_RAW_N", "MIC_L_N"),
        ("C51", "1u film/C0G", "MIC_R_RAW_P", "MIC_R_P"),
        ("C52", "1u film/C0G", "MIC_R_RAW_N", "MIC_R_N"),
        ("C61", "100n", "JACK_MIC", "JACK_MIC_CODEC"),
        ("C62", "47u low-ESR", "HP_L_CODEC", "HP_L"),
        ("C63", "47u low-ESR", "HP_R_CODEC", "HP_R"),
    ]
    for index, (ref, value, n1, n2) in enumerate(audio_caps):
        add_passive(s, "C", ref, value, 25 + (index % 8) * 30, 175 + (index // 8) * 25,
                    n1, n2, "Capacitor_SMD:C_0603_1608Metric" if "10u" in value or "film" in value else None)
    add_passive(s, "R", "R16", "2.2k", 40, 240, "+3V3", "I2C_SCL")
    add_passive(s, "R", "R17", "2.2k", 80, 240, "+3V3", "I2C_SDA")
    add_passive(s, "R", "R18", "2.7k", 120, 240, "MICBIAS", "JACK_MIC")
    add_passive(s, "L", "FB1", "600R@100MHz ferrite", 160, 240, "+3V3", "+3V3_AUDIO",
                "Inductor_SMD:L_0603_1608Metric")
    add_power_flag(s, "#FLG07", 190, 240, "+3V3_AUDIO")
    s.texts.append(Text("Both acoustic ports: 0.8 mm PCB hole plus enclosure gasket/vent keepout.",
                        Position(45, 115, 0), eff(1.0, False, True), uid()))
    return s


def make_interfaces():
    s = new_sheet("MECHANICAL / RESERVED INTERFACES", "No display or Lighthouse pinout is invented")
    conn100 = CUSTOM["TRACKING_100"]
    add_component(s, conn100, "J7", "DF40C-100DP-0.4V(51) TRACKING",
                  "Connector_Hirose_DF40:Hirose_DF40C-100DP-0.4V_2x50-1MP_P0.4mm",
                  "https://www.hirose.com/product/p/CL0684-4032-1-51?lang=en", 85, 80, {},
                  no_connect_unmapped=True)
    conn6 = source_symbol("Connector_Generic", "Conn_01x06")
    add_component(s, conn6, "J5", "PROXIMITY FLEX", "Connector_JST:JST_SH_SM06B-SRSS-TB_1x06-1MP_P1.00mm_Horizontal",
                  "reference/Vishay_Proximity/VCNL36825T.pdf", 160, 55,
                  {"1":"+3V3", "2":"GND", "3":"I2C_SDA", "4":"I2C_SCL", "5":"PROX_INT", "6":"PROX_GPIO"})
    add_component(s, CUSTOM["BM28_20P2"], "J2", "BM28 BATTERY FLEX 5A + NTC/ID",
                  "Stearlight:Hirose_BM28B0.6-20DS_2-0.35V",
                  "https://www.hirose.com/en/product/p/CL0673-5040-0-53", 160, 105,
                  {"P1":"BAT+", "P2":"GND", "1":"TS", "2":"BAT_ID"},
                  no_connect_unmapped=True)
    conn4 = source_symbol("Connector_Generic", "Conn_01x04")
    add_component(s, conn4, "J4", "5V GND PWM TACH",
                  "Connector_JST:JST_GH_SM04B-GHS-TB_1x04-1MP_P1.25mm_Horizontal", "~",
                  205, 55, {"1":"+5V_SYS", "2":"GND", "3":"FAN_PWM", "4":"FAN_TACH"})
    service = source_symbol("Connector_Generic", "Conn_02x05_Odd_Even")
    add_component(s, service, "J6", "TAG-CONNECT SERVICE PADS (NO FIT)",
                  "Connector:Tag-Connect_TC2050-IDC-NL_2x05_P1.27mm_Vertical", "~",
                  205, 105, {"1":"+3V3", "2":"GND", "3":"I2C_SDA", "4":"I2C_SCL",
                                "5":"UART_TX", "6":"UART_RX", "7":"EMMC_BOOT_N",
                                "8":"CM4_EXT_RESET_N", "9":"RUN_PG", "10":"GND"},
                  in_bom=False)
    # Named service pads keep power/status nets measurable during bring-up. They
    # also make BAT_ID explicitly reserved instead of leaving a dangling label.
    test_point = source_symbol("Connector", "TestPoint")
    test_nets = [
        ("TP1", "CHG_STAT", 155, 125), ("TP2", "PGOOD_5V", 180, 125),
        ("TP3", "BAT_ID", 205, 125), ("TP4", "+5V_SYS", 155, 145),
        ("TP5", "+3V3", 180, 145), ("TP6", "+1V05_USB", 205, 145),
        ("TP7", "GND", 155, 165), ("TP8", "VBAT_SYS", 180, 165),
    ]
    for ref, net, x, y in test_nets:
        add_component(s, test_point, ref, net, "TestPoint:TestPoint_Pad_D1.0mm", "~",
                      x, y, {"1":net})
    s.texts.append(Text("DISPLAY_L — CONNECTOR, PINOUT, POWER AND SEQUENCE TBD", Position(35, 135, 0), eff(1.2, False, True), uid()))
    s.texts.append(Text("DISPLAY_R — CONNECTOR, PINOUT, POWER AND SEQUENCE TBD", Position(35, 142, 0), eff(1.2, False, True), uid()))
    s.texts.append(Text("All 100 J7 tracking pins are intentionally NC until the tracking interface is defined.",
                        Position(35, 150, 0), eff(1.0, False, True), uid()))
    return s


def write_sheet(name, sch):
    path = SHEETS / f"{name}.kicad_sch"
    sch.to_file(path)
    return path


def make_top(sheet_defs):
    top = new_sheet("STEARLIGHT HMD CARRIER — REV A", "Architecture and review hierarchy")
    top.sheetInstances = [HierarchicalSheetInstance("/", "1")]
    top.texts.append(Text("Official CM4 sheets are preserved derivatives of Raspberry Pi CM4 IO v5.",
                          Position(25, 28, 0), eff(1.0), uid()))
    x_positions = [25, 80, 135, 190]
    y_positions = [40, 85]
    for index, (name, filename) in enumerate(sheet_defs):
        x = x_positions[index % 4]
        y = y_positions[index // 4]
        sid = uid()
        sh = HierarchicalSheet(
            position=Position(x, y), width=45, height=28, uuid=sid,
            stroke=Stroke(width=0.2, type="solid", color=ColorRGBA(132, 0, 132, 1)),
            fill=ColorRGBA(255, 255, 255, 0, precision=4),
            sheetName=Property("Sheet name", name, 0, Position(x, y - 1.5, 0), eff(1.1)),
            fileName=Property("Sheet file", f"sheets/{filename}", 1, Position(x, y + 29.5, 0), eff(0.8)),
        )
        top.sheets.append(sh)
        top.sheetInstances.append(HierarchicalSheetInstance(f"/{sid}", str(index + 2)))
    return top


def main():
    SHEETS.mkdir(parents=True, exist_ok=True)
    SymbolLib(version="20211014", generator="kiutils",
              symbols=[deepcopy(s) for s in list(CUSTOM.values()) + [cm4_200_symbol()]]).to_file(
                  ROOT / "libraries/symbols/Stearlight.kicad_sym")
    # Reuse the exact official CM4 symbol/pin numbering, but only retain circuitry
    # required by this carrier (the IO board's HDMI/camera connectors do not belong here).
    write_sheet("CM4", make_cm4_combined())
    write_sheet("Power", make_power())
    write_sheet("USB3", make_usb3())
    write_sheet("Audio", make_audio())
    write_sheet("Interfaces", make_interfaces())
    top = make_top([
        ("CM4 GPIO / HIGH-SPEED", "CM4.kicad_sch"),
        ("POWER / USB-PD", "Power.kicad_sch"),
        ("PCIE / USB3", "USB3.kicad_sch"),
        ("AUDIO / MICS", "Audio.kicad_sch"),
        ("TRACKING / SENSORS / DISPLAYS", "Interfaces.kicad_sch"),
    ])
    top.to_file(ROOT / "pcb-hmd-carrier-board.kicad_sch")
    print("Generated hierarchical schematic")


if __name__ == "__main__":
    main()
