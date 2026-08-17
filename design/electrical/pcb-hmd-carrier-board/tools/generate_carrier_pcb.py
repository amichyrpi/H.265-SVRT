"""Generate the mechanical/component-placement baseline for Stearlight Rev A.

Run with KiCad's bundled Python, not the system Python:
  "C:/Program Files/KiCad/10.0/bin/python.exe" tools/generate_carrier_pcb.py

The script is deliberately deterministic so placement can be reviewed and regenerated.
It does not route PCIe/USB3 before the production impedance stack is selected.
"""

from pathlib import Path
import xml.etree.ElementTree as ET
import pcbnew


ROOT = Path(__file__).resolve().parents[1]
KICAD = Path(r"C:/Program Files/KiCad/10.0/share/kicad/footprints")
OUT = ROOT / "pcb-hmd-carrier-board.kicad_pcb"
NETLIST = ROOT / "output/pcb-hmd-carrier-board.net.xml"
LOCAL_KICAD_MODELS = ROOT / "libraries/3dmodels/KiCad"


def mm(x: float) -> int:
    return pcbnew.FromMM(x)


def v(x: float, y: float) -> pcbnew.VECTOR2I:
    return pcbnew.VECTOR2I(mm(x), mm(y))


def add_line(board, x1, y1, x2, y2, layer, width=0.15):
    item = pcbnew.PCB_SHAPE(board)
    item.SetShape(pcbnew.SHAPE_T_SEGMENT)
    item.SetStart(v(x1, y1))
    item.SetEnd(v(x2, y2))
    item.SetLayer(layer)
    item.SetWidth(mm(width))
    board.Add(item)
    return item


def add_rect(board, x1, y1, x2, y2, layer, width=0.15):
    add_line(board, x1, y1, x2, y1, layer, width)
    add_line(board, x2, y1, x2, y2, layer, width)
    add_line(board, x2, y2, x1, y2, layer, width)
    add_line(board, x1, y2, x1, y1, layer, width)


def add_text(board, text, x, y, layer=pcbnew.F_SilkS, size=1.1, bold=False, angle=0):
    item = pcbnew.PCB_TEXT(board)
    item.SetText(text)
    item.SetPosition(v(x, y))
    item.SetLayer(layer)
    item.SetTextSize(v(size, size))
    item.SetTextThickness(mm(0.18 if bold else 0.13))
    item.SetTextAngle(pcbnew.EDA_ANGLE(angle, pcbnew.DEGREES_T))
    item.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_CENTER)
    item.SetVertJustify(pcbnew.GR_TEXT_V_ALIGN_CENTER)
    item.SetBold(bold)
    if layer == pcbnew.B_SilkS:
        item.SetMirrored(True)
    board.Add(item)
    return item


def lib_fp(lib: str, name: str) -> pcbnew.FOOTPRINT:
    fp = pcbnew.FootprintLoad(str(KICAD / f"{lib}.pretty"), name)
    if fp is None:
        raise RuntimeError(f"Unable to load {lib}:{name}")
    return fp


def local_fp(name: str) -> pcbnew.FOOTPRINT:
    fp = pcbnew.FootprintLoad(str(ROOT / "libraries/footprints/Stearlight.pretty"), name)
    if fp is None:
        raise RuntimeError(f"Unable to load local footprint {name}")
    return fp


def replace_model_paths(fp, filename):
    models = fp.Models()
    for index in range(len(models)):
        models[index].m_Filename = filename


def localize_available_models(fp):
    """Point standard-library models at the self-contained project copies."""
    prefix = "${KICAD10_3DMODEL_DIR}/"
    models = fp.Models()
    for index in range(len(models)):
        filename = models[index].m_Filename
        if not filename.startswith(prefix):
            continue
        relative = filename[len(prefix):]
        if (LOCAL_KICAD_MODELS / relative).exists():
            models[index].m_Filename = "${KIPRJMOD}/libraries/3dmodels/KiCad/" + relative


def attach_model(fp, project_relative, offset=None, rotation=None, scale=None):
    model = pcbnew.FP_3DMODEL()
    model.m_Filename = "${KIPRJMOD}/" + project_relative.replace("\\", "/")
    if offset is not None:
        model.m_Offset = pcbnew.VECTOR3D(*offset)
    if rotation is not None:
        model.m_Rotation = pcbnew.VECTOR3D(*rotation)
    if scale is not None:
        model.m_Scale = pcbnew.VECTOR3D(*scale)
    fp.Add3DModel(model)


def use_project_model(fp, project_relative, offset=None, rotation=None, scale=None):
    """Replace inherited package CAD with one verified project-local model."""
    fp.Models().clear()
    attach_model(fp, project_relative, offset, rotation, scale)
    return fp


def set_fine_pitch_clearance(fp, clearance_mm=0.15):
    """Apply the connector maker's fine-pitch land-pattern clearance locally."""
    for pad in fp.Pads():
        pad.SetLocalClearance(mm(clearance_mm))
    return fp


def validate_model_coverage(board):
    """Reject unresolved 3D references and all former generated box models."""
    modeled = 0
    intentionally_unmodeled = []
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        if ref.startswith(("H", "FID", "TP")) or ref == "J6":
            continue
        localize_available_models(fp)
        valid_models = 0
        for model in fp.Models():
            if "/generated/" in model.m_Filename.replace("\\", "/"):
                raise RuntimeError(f"{ref} still uses forbidden generated box {model.m_Filename}")
            prefix = "${KIPRJMOD}/"
            if model.m_Filename.startswith(prefix):
                path = ROOT / model.m_Filename[len(prefix):]
                if not path.exists():
                    raise RuntimeError(f"{ref} references missing 3D model {path}")
                valid_models += 1
        if valid_models:
            modeled += 1
        else:
            intentionally_unmodeled.append(ref)
    print(f"3D coverage: {modeled} footprints with real/library models")
    if intentionally_unmodeled:
        print("No model attached (never substituted by a box): " + ", ".join(intentionally_unmodeled))


def place(board, fp, ref, value, x, y, angle=0, back=False):
    localize_available_models(fp)
    fp.SetReference(ref)
    fp.SetValue(value)
    fp.SetPosition(v(x, y))
    fp.SetOrientationDegrees(angle)
    board.Add(fp)
    if back:
        fp.Flip(fp.GetPosition(), False)
    fp.Reference().SetVisible(True)
    fp.Reference().SetLayer(pcbnew.B_Fab if back else pcbnew.F_Fab)
    fp.Reference().SetTextSize(v(0.80, 0.80))
    fp.Reference().SetTextThickness(mm(0.12))
    fp.Value().SetVisible(False)
    return fp


def assign_schematic_nets(board):
    """Apply the ERC-validated schematic netlist to the placed footprints."""
    if not NETLIST.exists():
        raise RuntimeError(
            f"Missing {NETLIST}. Export the XML netlist from the schematic before PCB generation."
        )
    root = ET.parse(NETLIST).getroot()
    missing = []
    assigned_pads = 0
    for net_node in root.findall("./nets/net"):
        name = net_node.get("name")
        net = pcbnew.NETINFO_ITEM(board, name)
        board.Add(net)
        for node in net_node.findall("node"):
            ref = node.get("ref")
            pin = node.get("pin")
            if ref.startswith("#FLG"):
                continue
            footprint = board.FindFootprintByReference(ref)
            if footprint is None:
                missing.append(f"{ref}.{pin} (footprint missing)")
                continue
            pads = [pad for pad in footprint.Pads() if pad.GetNumber() == pin]
            if not pads:
                missing.append(f"{ref}.{pin} (pad missing)")
                continue
            for pad in pads:
                pad.SetNet(net)
                assigned_pads += 1
    if missing:
        raise RuntimeError("Netlist/PCB mismatch:\n  " + "\n  ".join(sorted(set(missing))))
    print(f"Assigned {assigned_pads} pads from {NETLIST.name}")


def system_fp(lib, name, board, ref, value, x, y, angle=0, back=False):
    return place(board, lib_fp(lib, name), ref, value, x, y, angle, back)


def passive(board, kind, ref, value, x, y, angle=0, back=False):
    if kind == "R":
        lib, name = "Resistor_SMD", "R_0402_1005Metric"
    elif kind == "C":
        lib, name = "Capacitor_SMD", "C_0402_1005Metric"
    elif kind == "C0603":
        lib, name = "Capacitor_SMD", "C_0603_1608Metric"
    elif kind == "L":
        lib, name = "Inductor_SMD", "L_1210_3225Metric_Pad1.42x2.65mm_HandSolder"
    else:
        raise ValueError(kind)
    if kind in ("R", "C", "C0603"):
        angle = 0
    return system_fp(lib, name, board, ref, value, x, y, angle, back)


def build():
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(6)
    board.SetLayerName(pcbnew.F_Cu, "F.Cu")
    board.SetLayerName(pcbnew.In1_Cu, "GND1")
    board.SetLayerName(pcbnew.In2_Cu, "PWR_SIG1")
    board.SetLayerName(pcbnew.In3_Cu, "PWR_SIG2")
    board.SetLayerName(pcbnew.In4_Cu, "GND2")
    board.SetLayerName(pcbnew.B_Cu, "B.Cu")

    # 140 x 50 mm maximum envelope with 3 mm chamfers.  The earlier service
    # notch crossed the official CM4 connector pad field, so nose/shell cutouts
    # remain deferred until the display and enclosure geometry are frozen.
    outline = [
        (23, 20), (157, 20), (160, 23), (160, 67), (157, 70),
        (23, 70), (20, 67), (20, 23), (23, 20),
    ]
    for a, b in zip(outline, outline[1:]):
        add_line(board, *a, *b, pcbnew.Edge_Cuts, 0.10)

    # Board and assembly markings.
    add_text(board, "STE ARLIGHT HMD CARRIER", 90, 28.0, pcbnew.F_SilkS, 1.25, True)
    add_text(board, "REV A  |  6 LAYER  |  2S DEFAULT", 90, 30.0, pcbnew.F_SilkS, 0.80)
    add_text(board, "FRONT / FACE SIDE", 90, 68.5, pcbnew.F_SilkS, 0.8)
    add_text(board, "BACK / MODULE SIDE", 90, 21.7, pcbnew.B_SilkS, 0.8)

    # Four chassis mounting holes from the mechanical sketch.
    for n, (x, y) in enumerate([(26, 26), (154, 26), (58, 66), (136, 66)], 1):
        system_fp("MountingHole", "MountingHole_2.5mm_Pad_Via", board, f"H{n}", "M2.5", x, y)

    # CM4 reference footprint combines both 100-pin Hirose carrier connectors.
    cm4 = pcbnew.FootprintLoad(
        str(ROOT / "cm4-io/CM4IO.pretty"), "Raspberry-Pi-4-Compute-Module"
    )
    if cm4 is None:
        raise RuntimeError("Official CM4 footprint failed to load")
    # Preserve the official model offsets/rotations but use the manufacturer STEP
    # copied into this project instead of the CM4IO project's broken relative paths.
    replace_model_paths(cm4, "${KIPRJMOD}/libraries/3dmodels/DF40/DF40C-100DS.stp")
    # The official footprint origin is offset.  At this position its rotated
    # 55 x 40 mm module body is centred at board coordinate (90, 45).
    place(board, cm4, "Module1", "CM4 2GB/16GB/Wireless", 66.0, 61.5, 90, back=True)

    # Tracking-board mezzanine connector specified by the mechanical design.
    # Keep its 21.52 mm long axis horizontal on the rear face; the official STEP
    # uses its long axis as Z, hence the 90-degree model-axis correction.
    tracking = lib_fp(
        "Connector_Hirose_DF40",
        "Hirose_DF40C-100DP-0.4V_2x50-1MP_P0.4mm",
    )
    use_project_model(
        tracking,
        "libraries/3dmodels/exact/Hirose/DF40C-100DP/DF40C-100DP.stp",
        rotation=(0, 90, 0),
    )
    set_fine_pitch_clearance(tracking)
    place(board, tracking, "J7", "DF40C-100DP-0.4V(51) TRACKING", 42, 43, 0, True)

    # External/mechanical connectors: left charge+audio, microphones centre, USB host right.
    charge_usb = lib_fp("Connector_USB", "USB_C_Receptacle_JAE_DX07S016JA1R1500")
    use_project_model(
        charge_usb,
        "libraries/3dmodels/exact/JAE/DX07S016JA1R1500.step",
    )
    place(board, charge_usb, "J9", "DX07S016JA1R1500 CHARGE/SERVICE", 30, 65.7, 0)
    audio_jack = local_fp("SameSky_SJ-43514-SMT-TR")
    use_project_model(
        audio_jack,
        "libraries/3dmodels/exact/SameSky/Same_Sky_SJ-43514-SMT-TR_KiCad.step",
    )
    place(board, audio_jack, "J1", "SJ-43514-SMT-TR", 29, 52, 0)
    mic1 = lib_fp("Sensor_Audio", "Infineon_PG-LLGA-5-2")
    use_project_model(mic1, "libraries/3dmodels/exact/Infineon/IM73A135V01.step")
    place(board, mic1, "MK1", "IM73A135V01", 72, 65.5, 0)
    mic2 = lib_fp("Sensor_Audio", "Infineon_PG-LLGA-5-2")
    use_project_model(mic2, "libraries/3dmodels/exact/Infineon/IM73A135V01.step")
    place(board, mic2, "MK2", "IM73A135V01", 108, 65.5, 180)
    accessory_usb = local_fp("JAE_DX07S024JJ2R1300")
    use_project_model(
        accessory_usb,
        "libraries/3dmodels/exact/JAE/DX07S024JJ2R1300.step",
    )
    # The connector mouth is the footprint's +Y edge, aligned with the board edge.
    place(board, accessory_usb, "J8", "DX07S024JJ2R1300 ACCESSORY", 149.5, 65.0, 0)
    # Compact battery-flex interface: the two BM28 power contacts carry BAT+/GND
    # (5 A rating) and signal contacts carry the pack NTC/ID.  This replaces the
    # oversized 3.96 mm-pitch JST header that dominated the HMD edge.
    battery = local_fp("Hirose_BM28B0.6-20DS_2-0.35V")
    set_fine_pitch_clearance(battery)
    place(board, battery, "J2", "BM28 BATTERY FLEX 5A + NTC/ID", 155.0, 34.0, 90)
    system_fp("Connector_JST", "JST_SH_SM06B-SRSS-TB_1x06-1MP_P1.00mm_Horizontal", board,
              "J5", "3V3 GND SDA SCL INT GPIO", 105, 24.0, 180)
    system_fp("Connector_JST", "JST_GH_SM04B-GHS-TB_1x04-1MP_P1.25mm_Horizontal", board,
              "J4", "5V GND PWM TACH", 130, 24.0, 180)

    # USB 3 host chain on the right: CM4 PCIe -> Renesas -> mux -> Type-C.
    renesas = lib_fp("Package_DFN_QFN", "QFN-48-1EP_7x7mm_P0.5mm_EP5.7x5.7mm_ThermalVias")
    use_project_model(
        renesas,
        "libraries/3dmodels/KiCad/Package_DFN_QFN.3dshapes/QFN-48-1EP_7x7mm_P0.5mm_EP5.6x5.6mm.step",
    )
    place(board, renesas, "U14", "UPD720202K8-711-BAA-A", 124, 50, 0)
    cc_controller = lib_fp("Package_DFN_QFN", "Texas_X2QFN-12_1.6x1.6mm_P0.4mm")
    use_project_model(
        cc_controller,
        "libraries/3dmodels/exact/UltraLibrarian/TUSB320IRWBR.wrl",
    )
    place(board, cc_controller, "U5", "TUSB320LAIRWBR", 134.5, 57, 0)
    system_fp("Package_DFN_QFN", "Texas_RVC0020A_WQFN-20-1EP_3x4mm_P0.5mm_EP1.6x2.6mm",
              board, "U11", "HD3SS3212IRKSR", 135, 51, 0)
    system_fp("Package_SON", "WSON-6-1EP_2x2mm_P0.65mm_EP1x1.6mm",
              board, "U16", "TPS2553-1DRVR", 137, 45, 0)
    system_fp("Crystal", "Crystal_SMD_3225-4Pin_3.2x2.5mm", board,
              "Y1", "24MHz", 115, 55, 0)
    system_fp("Package_TO_SOT_SMD", "SOT-23", board,
              "U15", "TLV803EA30DBZR", 120, 58, 0)

    # Power/charging stays right/back, well away from the microphone/audio zone.
    pd_controller = local_fp("Texas_REF0038A_WQFN-38_6x4mm_DualPad")
    use_project_model(pd_controller, "libraries/3dmodels/exact/UltraLibrarian/TPS25751DREFR.wrl")
    place(board, pd_controller, "U12", "TPS25751DREFR", 132, 29, 0, True)
    charger = lib_fp("Package_DFN_QFN", "Texas_RQM0029A_VQFN-29_4x4mm_P0.4mm")
    use_project_model(charger, "libraries/3dmodels/exact/UltraLibrarian/BQ25798RQMR.wrl")
    place(board, charger, "U6", "BQ25798RQMR", 141, 29, 0, True)
    system_buck = lib_fp(
        "Package_DFN_QFN",
        "Texas_RJE0020A_VQFN-20-1EP_3x3mm_P0.45mm_EP0.675x0.76mm_ThermalVias",
    )
    use_project_model(system_buck, "libraries/3dmodels/exact/UltraLibrarian/TPS568230RJER.wrl")
    place(board, system_buck, "U2", "TPS568230RJER", 150, 30, 0, True)
    system_fp("Package_SO", "TSSOP-8_3x3mm_P0.65mm", board,
              "U13", "CAT24C512WI-GT3", 125, 25, 0, True)
    inductor_1 = local_fp("Bourns_SRP5030CA")
    use_project_model(inductor_1, "libraries/3dmodels/exact/Bourns/SRP5030CA-1R0M.step")
    place(board, inductor_1, "L3", "SRP5030CA-1R0M 1uH 9A", 140, 39, 0, True)
    inductor_2 = local_fp("Bourns_SRP7050TA")
    use_project_model(inductor_2, "libraries/3dmodels/exact/Bourns/SRP7050TA-2R2M.step")
    place(board, inductor_2, "L2", "SRP7050TA-2R2M 2.2uH 10A", 151, 40, 0, True)

    # Low-voltage rails close to their consumers.
    low_voltage_buck = local_fp("Texas_DMQ0006A_VSON-HR-6_1.5x1.5mm")
    use_project_model(low_voltage_buck, "libraries/3dmodels/exact/UltraLibrarian/TPS62825DMQR.wrl")
    place(board, low_voltage_buck, "U1", "TPS62825DMQR", 129, 38, 0, True)
    system_fp("Inductor_SMD", "L_0402_1005Metric", board,
              "L1", "0.47uH XFL4015-471MEC", 131, 38, 90, True)
    system_fp("Package_TO_SOT_SMD", "SOT-23-5", board,
              "U10", "TLV75528PDBVR", 90, 62, 0)
    system_fp("Inductor_SMD", "L_0603_1608Metric", board,
              "FB1", "600R@100MHz ferrite", 86, 62, 0)

    # Audio island on left/front, deliberately separated from inductors and USB3.
    codec = lib_fp(
        "Package_DFN_QFN",
        "Texas_RHB0032M_VQFN-32-1EP_5x5mm_P0.5mm_EP2.1x2.1mm_ThermalVias",
    )
    use_project_model(codec, "libraries/3dmodels/exact/UltraLibrarian/TLV320AIC3101IRHBR.wrl")
    place(board, codec, "U3", "TLV320AIC3204IRHBR", 48, 56, 0)
    system_fp("Package_SON", "USON-10_2.5x1.0mm_P0.5mm", board,
              "U4", "TPD4E05U06DQAR", 39, 55, 0)

    # No permanent debug header: these are low-profile Tag-Connect pogo pads used
    # only during bring-up, with no fitted plastic body or vertical pins.
    system_fp("Connector", "Tag-Connect_TC2050-IDC-NL_2x05_P1.27mm_Vertical", board,
              "J6", "TAG-CONNECT SERVICE PADS (NO FIT)", 55, 24, 0, True)
    test_values = ["CHG_STAT", "PGOOD_5V", "BAT_ID", "+5V_SYS",
                   "+3V3", "+1V05_USB", "GND", "VBAT_SYS"]
    for i, (x, value) in enumerate(zip([64, 68, 72, 76, 100, 104, 108, 112], test_values), 1):
        system_fp("TestPoint", "TestPoint_Pad_D1.0mm", board,
                  f"TP{i}", value, x, 23.0, 0, True)

    # Decoupling/passives: deterministic placement rows around each corresponding IC.
    caps = []
    def cap_row(prefix, start, values, x0, y0, dx=1.5, back=False, kind="C"):
        for index, value in enumerate(values, start):
            caps.append(passive(board, kind, f"C{index}", value, x0 + (index-start)*dx, y0, 90, back))

    cap_row("PD", 1, ["10u", "10u", "10u", "10u", "10u", "100n", "330p", "330p"], 127, 33, 3.0, True, "C0603")
    cap_row("CHG_A", 9, ["10u", "10u", "10u", "100n", "10u", "10u"], 128, 62, 3.0, True, "C0603")
    cap_row("CHG_B", 15, ["100n", "4u7", "47n", "47n"], 128, 59, 3.0, True, "C0603")
    cap_row("SYS", 19, ["10u", "10u", "10u", "10u", "10u", "100n", "100n", "1u", "47p"], 128, 46, 3.0, True, "C0603")
    cap_row("USB_A", 28, ["100n"]*7, 119, 61, 3.0, False, "C")
    cap_row("USB_B1", 35, ["100n"]*3 + ["10u"], 119, 64, 3.0, False, "C")
    cap_row("USB_B2", 39, ["1u", "1u"], 121, 67, 3.0, False, "C")
    cap_row("AUDIO_A", 41, ["1u", "1u", "100n", "10u"], 43, 62, 3.0, False, "C")
    cap_row("AUDIO_B", 45, ["10u", "10u", "10u", "100n"], 43, 65, 3.0, False, "C")
    cap_row("MIC_L", 49, ["1u C0G", "1u C0G"], 74, 62, 3.0, False, "C")
    cap_row("MIC_R", 51, ["1u C0G", "1u C0G"], 103, 62, 3.0, False, "C")
    passive(board, "C", "C53", "100n PCIe AC", 104, 48, 90, True)
    passive(board, "C", "C54", "100n PCIe AC", 106, 48, 90, True)
    passive(board, "C", "C55", "100n PCIe AC", 116, 45, 90, False)
    passive(board, "C", "C56", "100n PCIe AC", 118, 45, 90, False)
    passive(board, "C0603", "C57", "100u low-ESR", 145, 57, 90, True)
    passive(board, "C0603", "C58", "100u low-ESR", 148, 57, 90, True)
    passive(board, "C0603", "C59", "22u 25V", 151, 57, 90, True)
    passive(board, "C0603", "C60", "22u 25V", 154, 57, 90, True)
    passive(board, "C", "C61", "100n JACK MIC", 39, 57, 90, False)
    passive(board, "C0603", "C62", "47u HP LEFT", 38, 48, 90, False)
    passive(board, "C0603", "C63", "47u HP RIGHT", 38, 51, 90, False)
    passive(board, "C0603", "C64", "4.7u 10V", 128, 56, 90, True)
    passive(board, "C0603", "C65", "10u 6.3V", 131, 56, 90, True)
    passive(board, "C0603", "C66", "10u 6.3V", 134, 56, 90, True)
    passive(board, "C", "C67", "120p C0G", 137, 56, 90, True)
    passive(board, "C", "C68", "100n USB3 RESET", 110, 58, 90, False)

    resistor_values = [
        ("R1", "220k", 131.5, 50, True), ("R2", "30k", 133.5, 50, True),
        ("R3", "5.23k ILIM", 135.5, 50, True), ("R4", "137k ILIM", 137.5, 50, True),
        ("R5", "2.2k I2C", 139.5, 50, True), ("R6", "2.2k I2C", 141.5, 50, True),
        ("R7", "10k", 143.5, 50, True), ("R8", "10k", 145.5, 50, True),
        ("R9", "30.1k TS", 147.5, 50, True), ("R10", "10k NTC", 149.5, 50, True),
        ("R11", "100R BATP", 151.5, 50, True), ("R12", "6.04k 2S", 153.5, 50, True),
        ("R13", "200k", 129, 55, False), ("R14", "200k", 131, 55, False),
        ("R15", "ILIM 1.5A TBD", 139, 48, False),
        ("R16", "I2C 2.2k", 53, 60, False), ("R17", "I2C 2.2k", 55, 60, False),
        ("R18", "MICBIAS 2.7k", 39, 59, False),
        ("R19", "75k 1V05 FB", 128, 53, True), ("R20", "100k 1V05 FB", 130, 53, True),
        ("R21", "100k 1V05 PG", 132, 53, True),
        ("R22", "10k WIFI EN", 83, 52, True), ("R23", "10k BT EN", 85, 52, True),
        ("R24", "75k MODE", 144, 53, True), ("R25", "51k MODE", 146, 53, True),
        ("R26", "10k CLKREQ", 108, 48, True), ("R27", "2.2k CHG SCL", 150, 53, True),
        ("R28", "2.2k CHG SDA", 152, 53, True),
        ("R29", "10k OCI PU", 110, 46, False), ("R30", "1.6k RREF VERIFY", 112, 46, False),
        ("R31", "10k RESET PU", 114, 46, False),
        ("R32", "10k SPI CLK PD", 110, 49, False), ("R33", "10k SPI CS PD", 112, 49, False),
        ("R34", "10k SPI MISO PU", 114, 49, False), ("R35", "10k SPI MOSI PD", 116, 49, False),
    ]
    for ref, value, x, y, back in resistor_values:
        passive(board, "R", ref, value, x, y, 0, back)

    # ESD immediately adjacent to each external connector.
    system_fp("Package_TO_SOT_SMD", "SOT-23", board, "U7", "TPD2EUSB30DRTR", 38, 62, 0)
    system_fp("Package_SON", "USON-10_2.5x1.0mm_P0.5mm", board,
              "U8", "TPD4EUSB30DQAR", 140, 58, 90)
    system_fp("Package_SON", "USON-10_2.5x1.0mm_P0.5mm", board,
              "U9", "TPD4EUSB30DQAR", 142, 61, 0)
    system_fp("Package_SON", "WSON-6-1EP_2x2mm_P0.65mm_EP1x1.6mm", board,
              "D1", "TVS2200DRVR", 38, 66, 0)

    # Placeholders are drawings, not invented electrical connectors.
    add_rect(board, 25, 25.5, 48, 37.5, pcbnew.Dwgs_User, 0.20)
    add_text(board, "DISPLAY_L ROUTING / CONNECTOR RESERVE", 36.5, 31.5, pcbnew.Dwgs_User, 0.8, False, 90)
    add_rect(board, 132, 25.5, 155, 37.5, pcbnew.Dwgs_User, 0.20)
    add_text(board, "DISPLAY_R ROUTING / CONNECTOR RESERVE", 143.5, 31.5, pcbnew.Dwgs_User, 0.8, False, 90)

    # Functional-zone outlines make first-pass placement review unambiguous.
    for x1, y1, x2, y2, label in [
        (21.5, 44, 61, 68, "AUDIO / LOW NOISE"),
        (108, 43, 158, 63, "PCIE + USB3 HOST"),
        (105, 24, 157, 43, "POWER / CHARGE (BACK)"),
    ]:
        add_rect(board, x1, y1, x2, y2, pcbnew.Dwgs_User, 0.12)
        add_text(board, label, (x1+x2)/2, y1+1.2, pcbnew.Dwgs_User, 0.65)

    add_text(board, "J7 TRACKING DF40C-100DP", 42, 46.4, pcbnew.B_SilkS, 0.80)
    add_text(board, "L3 CHARGER 1R0", 140, 34.6, pcbnew.B_SilkS, 0.80)
    add_text(board, "L2 CM4 5V 2R2", 151, 34.6, pcbnew.B_SilkS, 0.80)
    add_text(board, "J2 BATTERY FLEX", 153.0, 38.5, pcbnew.F_SilkS, 0.80)
    add_text(board, "DISPLAY INTERFACES TBD", 90, 35, pcbnew.F_SilkS, 0.8, True)
    add_text(board, "RENESAS MANUAL REVIEW GATE", 124, 44, pcbnew.F_SilkS, 0.80)

    # Three global fiducials.
    for i, (x, y) in enumerate([(35, 23), (145, 23), (120, 23)], 1):
        system_fp("Fiducial", "Fiducial_1mm_Mask2mm", board, f"FID{i}", "FIDUCIAL", x, y)

    validate_model_coverage(board)
    assign_schematic_nets(board)
    pcbnew.SaveBoard(str(OUT), board)
    print(f"Wrote {OUT}")
    print(f"Footprints: {len(list(board.GetFootprints()))}; copper layers: {board.GetCopperLayerCount()}")


if __name__ == "__main__":
    build()
