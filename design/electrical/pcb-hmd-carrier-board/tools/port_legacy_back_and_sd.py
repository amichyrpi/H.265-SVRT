"""Port the useful legacy rear-side circuitry onto carrier board 2.

The current .kicad_pcb contains an old source board above the manually placed
target board.  This script preserves every existing target footprint, moves the
still-required rear-side blocks by reference, removes the obsolete source-board
copy, and adds the official CM4 IO Board microSD circuit as a CM4 Lite-only
assembly option.

Run with KiCad's bundled Python:
  "C:/Program Files/KiCad/10.0/bin/python.exe" tools/port_legacy_back_and_sd.py
"""

from pathlib import Path
from shutil import copy2

import pcbnew


ROOT = Path(__file__).resolve().parents[1]
BOARD_PATH = ROOT / "pcb-hmd-carrier-board2.kicad_pcb"
BACKUP = ROOT / "output/review/pcb-hmd-carrier-board2-before-legacy-port.kicad_pcb"
CM4_IO_FP = ROOT / "cm4-io/CM4IO.pretty"
KICAD_FP = Path(r"C:/Program Files/KiCad/10.0/share/kicad/footprints")


def mm(value: float) -> int:
    return pcbnew.FromMM(value)


def point(x: float, y: float) -> pcbnew.VECTOR2I:
    return pcbnew.VECTOR2I(mm(x), mm(y))


def at(fp, x: float, y: float, angle=None):
    fp.SetPosition(point(x, y))
    if angle is not None:
        fp.SetOrientationDegrees(angle)
    return fp


def load(directory: Path, name: str):
    footprint = pcbnew.FootprintLoad(str(directory), name)
    if footprint is None:
        raise RuntimeError(f"Cannot load footprint {directory.name}:{name}")
    return footprint


def get_net(board, name: str):
    nets = board.GetNetsByName()
    if name in nets:
        return nets[name]
    net = pcbnew.NETINFO_ITEM(board, name)
    board.Add(net)
    return net


def connect(footprint, mapping, nets):
    for pad in footprint.Pads():
        number = pad.GetNumber()
        if number in mapping:
            pad.SetNet(nets[mapping[number]])


def add_text(board, text, x, y, size=0.55):
    item = pcbnew.PCB_TEXT(board)
    item.SetText(text)
    item.SetPosition(point(x, y))
    item.SetLayer(pcbnew.B_SilkS)
    item.SetTextSize(point(size, size))
    item.SetTextThickness(mm(0.09))
    item.SetMirrored(True)
    board.Add(item)


def system_fp(board, lib, name, ref, value, x, y, angle=0, dnp=False):
    fp = load(KICAD_FP / f"{lib}.pretty", name)
    fp.SetReference(ref)
    fp.SetValue(value)
    fp.SetPosition(point(x, y))
    fp.SetOrientationDegrees(angle)
    board.Add(fp)
    fp.Flip(fp.GetPosition(), False)
    fp.SetDNP(dnp)
    fp.SetExcludedFromBOM(False)
    fp.Reference().SetVisible(False)
    fp.Value().SetVisible(False)
    return fp


def source_fp(board, ref):
    matches = [
        fp for fp in board.GetFootprints()
        if fp.GetReference() == ref and pcbnew.ToMM(fp.GetPosition().y) < 80
    ]
    if len(matches) != 1:
        raise RuntimeError(f"Expected one source {ref}, found {len(matches)}")
    return matches[0]


# These positions occupy only free rear-side regions.  They intentionally avoid
# the CM4 mechanical envelope and every component already placed on the target.
PLACEMENT = {
    # Mechanical/service interfaces and bring-up pads: upper-left wing.
    "J7": (112.3, 94.8, 180), "J6": (132.7, 94.8, 180),
    "TP1": (101.5, 99.8, 0), "TP2": (105.5, 99.8, 0),
    "TP3": (109.5, 99.8, 0), "TP4": (113.5, 99.8, 0),
    "TP5": (117.5, 99.8, 0), "TP6": (121.5, 99.8, 0),
    "TP7": (125.5, 99.8, 0), "TP8": (129.5, 99.8, 0),

    # Renesas USB-controller 1.05 V point-of-load supply.
    "U1": (134.0, 100.2, 180), "L1": (137.0, 100.2, 90),
    "C64": (131.0, 102.5, 0), "C65": (134.2, 102.5, 0),
    "C66": (137.4, 102.5, 0), "C67": (139.0, 104.5, 0),
    "R19": (132.0, 100.5, 0), "R20": (137.7, 98.7, 0),
    "R21": (139.0, 100.0, 0),

    # USB-PD controller and configuration EEPROM: upper-right wing.
    "U12": (201.0, 94.5, 180), "U13": (208.2, 94.5, 180),
    "C1": (198.0, 90.5, 0), "C2": (201.2, 90.5, 0),
    "C3": (204.4, 90.5, 0), "C4": (207.6, 90.5, 0),
    "C5": (210.8, 90.5, 0), "C6": (227.5, 107.5, 0),
    "C7": (198.0, 98.6, 0), "C8": (201.2, 98.6, 0),
    "C9": (204.4, 98.6, 0), "C10": (229.0, 105.0, 0),
    "C11": (232.0, 105.0, 0), "C12": (235.0, 105.0, 0),
    "R5": (212.7, 92.7, 0), "R6": (212.7, 94.0, 0),
    "R7": (212.7, 95.4, 0), "R8": (212.7, 97.0, 0),

    # System 5 V buck: lower-left wing.
    "U2": (126.8, 129.0, 180), "L2": (135.5, 125.5, 180),
    "C19": (103.2, 124.5, 0), "C20": (106.4, 124.5, 0),
    "C21": (109.6, 124.5, 0), "C22": (103.2, 127.0, 0),
    "C23": (106.4, 127.0, 0), "C25": (128.5, 124.0, 0),
    "C26": (128.5, 126.0, 0), "C27": (128.5, 133.0, 0),
    "C57": (102.5, 130.0, 0), "C58": (106.0, 130.0, 0),
    "C59": (109.5, 130.0, 0), "C60": (106.0, 133.0, 0),
    "R1": (116.0, 133.5, 0), "R2": (118.2, 133.5, 0),
    "R24": (120.4, 133.5, 0), "R25": (125.0, 134.0, 0),

    # Battery charger: lower-right wing, before the microSD socket.
    "U6": (200.5, 128.5, 180), "L3": (210.0, 128.5, 180),
    "C13": (198.5, 122.5, 0), "C14": (201.7, 122.5, 0),
    "C15": (204.9, 122.5, 0), "C16": (208.1, 122.5, 0),
    "C17": (201.5, 124.8, 0), "C18": (204.7, 124.8, 0),
    "C24": (198.5, 124.8, 0), "R3": (202.5, 132.8, 0),
    "R4": (204.7, 132.8, 0), "R9": (206.9, 132.8, 0),
    "R10": (209.1, 132.8, 0), "R11": (211.3, 132.8, 0),
    "R12": (213.5, 132.8, 0), "R27": (202.5, 134.6, 0),
    "R28": (204.7, 134.6, 0),

    # CM4 and PCIe safe-default biasing, close to target interfaces.
    "R22": (137.0, 106.0, 0), "R23": (139.0, 106.0, 0),
    "R26": (137.0, 108.0, 0),
}


def add_microsd(board):
    if any(fp.GetReference() == "J301" for fp in board.GetFootprints()):
        raise RuntimeError("J301 already exists; refusing to apply the port twice")

    net_names = ["GND", "+3V3", "SD_PWR", "SD_PWR_ON", "SD_CLK", "SD_CMD",
                 "SD_DAT0", "SD_DAT1", "SD_DAT2", "SD_DAT3", "SD_DETECT_A", "SD_DETECT_B"]
    nets = {name: get_net(board, name) for name in net_names}

    socket = load(CM4_IO_FP, "SDCARD_MOLEX_503398-1892")
    socket.SetReference("J301")
    socket.SetValue("Molex 503398-1892 (CM4 LITE ONLY)")
    socket.SetPosition(point(221.5, 96.7))
    socket.SetOrientationDegrees(0)
    socket.Models().clear()
    model = pcbnew.FP_3DMODEL()
    model.m_Filename = "${KIPRJMOD}/libraries/3dmodels/Molex/Molex_503398-1892.step"
    socket.Add3DModel(model)
    board.Add(socket)
    socket.Flip(socket.GetPosition(), False)
    socket.SetDNP(True)  # Default 16 GB eMMC CM4 build cannot expose native SD.
    socket.Reference().SetVisible(False)
    socket.Value().SetVisible(False)
    connect(socket, {
        "1": "SD_DAT2", "2": "SD_DAT3", "3": "SD_CMD", "4": "SD_PWR",
        "5": "SD_CLK", "6": "GND", "7": "SD_DAT0", "8": "SD_DAT1",
        "9": "SD_DETECT_A", "10": "SD_DETECT_B", "11": "GND",
    }, nets)

    switch = system_fp(board, "Package_TO_SOT_SMD", "SOT-23-5", "U301",
                       "RT9742GGJ5 SD POWER (CM4 LITE ONLY)", 137.5, 111.5, dnp=True)
    connect(switch, {"1": "SD_PWR", "2": "GND", "3": "SD_DETECT_B",
                     "4": "SD_PWR_ON", "5": "+3V3"}, nets)

    cap = system_fp(board, "Capacitor_SMD", "C_0603_1608Metric", "C301",
                    "10uF SD POWER (CM4 LITE ONLY)", 230.5, 107.5, dnp=True)
    connect(cap, {"1": "SD_PWR", "2": "GND"}, nets)

    pull = system_fp(board, "Resistor_SMD", "R_0402_1005Metric", "R301",
                     "12k 1% SD_PWR_ON PU (CM4 LITE ONLY)", 137.0, 114.0, dnp=True)
    connect(pull, {"1": "+3V3", "2": "SD_PWR_ON"}, nets)

    # Optional card-detect links follow the reference board's no-fit treatment.
    rdet_a = system_fp(board, "Resistor_SMD", "R_0402_1005Metric", "R302",
                       "0R SD DETECT OPTION", 139.0, 114.0, dnp=True)
    connect(rdet_a, {"1": "SD_DETECT_A", "2": "GND"}, nets)
    rdet_b = system_fp(board, "Resistor_SMD", "R_0402_1005Metric", "R303",
                       "0R SD DETECT OPTION", 137.0, 116.0, dnp=True)
    connect(rdet_b, {"1": "SD_DETECT_B", "2": "GND"}, nets)

    target_module = next(
        fp for fp in board.GetFootprints()
        if fp.GetReference() == "Module1" and pcbnew.ToMM(fp.GetPosition().y) > 80
    )
    cm4_map = {"57": "SD_CLK", "62": "SD_CMD", "61": "SD_DAT3", "63": "SD_DAT0",
               "67": "SD_DAT1", "69": "SD_DAT2", "75": "SD_PWR_ON"}
    connect(target_module, cm4_map, nets)

    add_text(board, "MICROSD - CM4 LITE ONLY / DNP FOR EMMC", 221.5, 104.8, 0.45)


def remove_old_source(board, moved_uuids):
    source_drawings = []
    for drawing in list(board.GetDrawings()):
        box = drawing.GetBoundingBox()
        centre_y = pcbnew.ToMM(box.GetY() + box.GetHeight() // 2)
        if centre_y < 80:
            source_drawings.append(drawing)

    # Preserve moved UUIDs but delete every remaining source-board footprint.
    for fp in list(board.GetFootprints()):
        if fp.m_Uuid.AsString() in moved_uuids:
            continue
        if pcbnew.ToMM(fp.GetPosition().y) < 80:
            board.Remove(fp)

    # The two layouts are vertically separated, so source geometry can be
    # identified without touching the target board at y >= 80 mm.
    for drawing in source_drawings:
        board.Remove(drawing)
    # The source layout is intentionally unrouted and has no copper zones;
    # tracks/zones therefore need no destructive coordinate filtering here.


def main():
    board = pcbnew.LoadBoard(str(BOARD_PATH))
    if any(fp.GetReference() == "J301" for fp in board.GetFootprints()):
        raise RuntimeError("Legacy port/microSD change already present")

    BACKUP.parent.mkdir(parents=True, exist_ok=True)
    if not BACKUP.exists():
        copy2(BOARD_PATH, BACKUP)

    # Correct inherited branding that was accidentally placed on B.Cu.  Keep
    # it as a non-fabrication comment because the front already carries the
    # production revision marking.
    for drawing in board.GetDrawings():
        if hasattr(drawing, "GetText") and drawing.GetText() == "Stearlight <C> 2026":
            drawing.SetLayer(pcbnew.Cmts_User)

    # The manually assembled target had two TP16 references.  Keep their
    # positions intact and give the right-hand pad a unique production ref.
    duplicate_tp16 = sorted(
        [fp for fp in board.GetFootprints() if fp.GetReference() == "TP16" and pcbnew.ToMM(fp.GetPosition().y) > 80],
        key=lambda fp: pcbnew.ToMM(fp.GetPosition().x),
    )
    if len(duplicate_tp16) == 2:
        duplicate_tp16[1].SetReference("TP23")

    moved_uuids = set()
    for ref, (x, y, angle) in PLACEMENT.items():
        fp = source_fp(board, ref)
        at(fp, x, y, angle)
        moved_uuids.add(fp.m_Uuid.AsString())

    # Deliberately not ported: source Module1 duplicates the target CM4;
    # C53/C54 duplicate target PCIe coupling capacitors C55/C56.
    add_microsd(board)
    remove_old_source(board, moved_uuids)

    add_text(board, "TRACKING", 112.3, 98.1)
    add_text(board, "SERVICE", 132.7, 98.6)
    add_text(board, "USB-PD", 203.0, 98.4)
    add_text(board, "CHARGER", 207.5, 134.7)
    add_text(board, "+5V SYSTEM", 125.0, 134.7)
    add_text(board, "USB 1V05", 136.0, 104.0)

    pcbnew.SaveBoard(str(BOARD_PATH), board)
    print(f"Moved {len(moved_uuids)} useful legacy rear footprints")
    print("Skipped duplicate Module1, C53 and C54")
    print("Added J301/U301/C301/R301-R303 as CM4 Lite-only DNP variant")
    print(BOARD_PATH)


if __name__ == "__main__":
    main()
