"""Restore the authoritative Board2 CM4 Lite microSD circuit in-place.

The source is the preserved feature-complete Board2 snapshot.  Footprint reference,
position, side, orientation, fields, pad geometry, model and net names are retained.
The primary Rev A BOM targets CM4 Lite and fits these parts.  The same physical
PCB supports an eMMC variant by marking the six references DNP.
"""

from pathlib import Path
from shutil import copy2

import pcbnew


ROOT = Path(__file__).resolve().parents[1]
BOARD_PATH = ROOT / "pcb-hmd-carrier-board2.kicad_pcb"
BASELINE = ROOT / "variants/cm4-lite/pcb-hmd-carrier-board2-cm4-lite.kicad_pcb"
BACKUP = ROOT / "output/review/pcb-hmd-carrier-board2-pre-microsd-restoration.kicad_pcb"
MICROSD_REFS = ("J301", "U301", "C301", "R301", "R302", "R303")

# The preserved PCB carried the correct physical Molex outline but its pad-net
# assignments had been corrupted by an earlier back-side flip.  These mappings
# are the manufacturer pin numbers, not a placement redesign.
AUTHORITATIVE_PAD_NETS = {
    "J301": {
        "1": "SD_DAT2", "2": "SD_DAT3", "3": "SD_CMD", "4": "SD_PWR",
        "5": "SD_CLK", "6": "GND", "7": "SD_DAT0", "8": "SD_DAT1",
        "9": "SD_DETECT_A", "10": "SD_DETECT_B", "11": "GND",
    },
    # RT9742GGJ5: 1=VOUT, 2=GND, 3=FLG, 4=EN, 5=VIN.
    "U301": {
        "1": "SD_PWR", "2": "GND", "3": "SD_DETECT_B",
        "4": "SD_PWR_ON", "5": "+3V3",
    },
    "C301": {"1": "SD_PWR", "2": "GND"},
    "R301": {"1": "+3V3", "2": "SD_PWR_ON"},
    "R302": {"1": "SD_DETECT_A", "2": "GND"},
    "R303": {"1": "SD_DETECT_B", "2": "GND"},
}


def by_reference(board: pcbnew.BOARD) -> dict[str, pcbnew.FOOTPRINT]:
    return {
        str(footprint.GetReference()): footprint
        for footprint in board.GetFootprints()
        if str(footprint.GetReference()) != "REF**"
    }


def ensure_net(board: pcbnew.BOARD, name: str) -> pcbnew.NETINFO_ITEM:
    nets = board.GetNetsByName()
    if name in nets:
        return nets[name]
    net = pcbnew.NETINFO_ITEM(board, name)
    board.Add(net)
    return net


def main() -> None:
    if not BASELINE.exists():
        raise RuntimeError(f"Missing preserved Board2 baseline: {BASELINE}")

    board = pcbnew.LoadBoard(str(BOARD_PATH))
    baseline = pcbnew.LoadBoard(str(BASELINE))
    current = by_reference(board)
    source = by_reference(baseline)

    BACKUP.parent.mkdir(parents=True, exist_ok=True)
    if not BACKUP.exists():
        copy2(BOARD_PATH, BACKUP)

    restored: list[str] = []
    for reference in MICROSD_REFS:
        if reference in current:
            continue
        if reference not in source:
            raise RuntimeError(f"{reference} is absent from preserved baseline")

        source_fp = source[reference]
        clone = pcbnew.FOOTPRINT(board)
        clone.CopyFrom(source_fp)
        board.Add(clone)

        source_pads = list(source_fp.Pads())
        clone_pads = list(clone.Pads())
        if len(source_pads) != len(clone_pads):
            raise RuntimeError(f"Pad count changed while cloning {reference}")
        for source_pad, clone_pad in zip(source_pads, clone_pads):
            net_name = str(source_pad.GetNetname())
            if net_name:
                clone_pad.SetNet(ensure_net(board, net_name))

        clone.SetDNP(reference in {"R302", "R303"})
        restored.append(reference)

    # Apply the verified electrical mapping both to newly restored parts and to
    # an already-restored board.  Multiple shell pads numbered 11 intentionally
    # share GND.
    current = by_reference(board)
    # Retain the authoritative pre-cleanup Board2 location with the minimum
    # manufacturability correction: 1 mm left prevents the opposite-side J8
    # through-hole shell tabs from entering the microSD body courtyard.
    j301 = current["J301"]
    j301.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(208.0), pcbnew.FromMM(128.0)))
    for reference, mapping in AUTHORITATIVE_PAD_NETS.items():
        footprint = current[reference]
        footprint.SetDNP(reference in {"R302", "R303"})
        for pad in footprint.Pads():
            number = str(pad.GetNumber())
            if number in mapping:
                pad.SetNet(ensure_net(board, mapping[number]))

    module = current["Module1"]
    module_mapping = {
        "57": "SD_CLK", "61": "SD_DAT3", "62": "SD_CMD", "63": "SD_DAT0",
        "67": "SD_DAT1", "69": "SD_DAT2", "75": "SD_PWR_ON",
    }
    for pad in module.Pads():
        number = str(pad.GetNumber())
        if number in module_mapping:
            pad.SetNet(ensure_net(board, module_mapping[number]))

    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(BOARD_PATH), board)
    print("Restored: " + (", ".join(restored) if restored else "already present"))


if __name__ == "__main__":
    main()
