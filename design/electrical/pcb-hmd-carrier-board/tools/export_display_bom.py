"""Export the additive display-subsystem BOM and 3D-model coverage."""

import csv
from collections import defaultdict
from pathlib import Path

import pcbnew


ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / "pcb-hmd-carrier-board2.kicad_pcb"
BASELINE = ROOT / "output/review/pcb-hmd-carrier-board2-before-display.kicad_pcb"
OUTPUT = ROOT / "docs/DISPLAY_BOM.csv"
MODEL_STATUS = ROOT / "docs/DISPLAY_3D_MODEL_STATUS.md"


def uuid(fp):
    return fp.m_Uuid.AsString()


def source(value):
    table = [
        ("TC358870", "Toshiba", "C3008712", "Obsolete/legacy; prototype stock only"),
        ("245863050104829", "KYOCERA AVX", "", "Active; official drawing and STEP"),
        ("SIT8008", "SiTime", "C1507821", "Active"),
        ("TLV62569", "Texas Instruments", "C141836", "Active"),
        ("TPS65132", "Texas Instruments", "", "Active"),
        ("MP3387", "Monolithic Power Systems", "", "Active"),
        ("PCA9306", "Texas Instruments", "C123752", "Active"),
        ("TCA9539", "Texas Instruments", "", "Active"),
        ("DFLS160", "Diodes Incorporated", "", "Active"),
        ("ASPI-4030", "Abracon", "", "Verify current stock"),
        ("DFE252010", "TOKO/Murata", "", "Verify current stock"),
    ]
    for token, maker, supplier, note in table:
        if token in value:
            return maker, supplier, note
    return "Qualified generic", "JLCPCB/LCSC TBD", "Exact passive MPN to freeze before assembly"


def main():
    board = pcbnew.LoadBoard(str(BOARD))
    baseline = pcbnew.LoadBoard(str(BASELINE))
    old = {uuid(fp) for fp in baseline.GetFootprints()}
    added = [fp for fp in board.GetFootprints() if uuid(fp) not in old]
    groups = defaultdict(list)
    for fp in added:
        key = (str(fp.GetValue()), str(fp.GetFPID().GetLibItemName()))
        groups[key].append(fp)

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    with OUTPUT.open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.writer(handle)
        writer.writerow(["References", "Quantity", "Value / MPN", "Footprint", "Manufacturer", "Supplier part", "Lifecycle / assembly note", "3D model"])
        for (value, footprint), items in sorted(groups.items(), key=lambda entry: min(x.GetReference() for x in entry[1])):
            maker, supplier, note = source(value)
            refs = ",".join(sorted(str(fp.GetReference()) for fp in items))
            writer.writerow([refs, len(items), value, footprint, maker, supplier, note, "yes" if all(len(fp.Models()) for fp in items) else "test-pad/no physical model"])
        writer.writerow([
            "PANEL_L,PANEL_R", 2, "VS025ZSM-NV0-69P0", "External panel; mates with J201/J202",
            "BOE", "Display supplier TBD", "External 1440x1600 NT57860 dual-DSI panel; controlled full specification required",
            "external assembly item",
        ])

    modeled = [fp for fp in added if len(fp.Models())]
    unmodeled = [fp for fp in added if not len(fp.Models())]
    MODEL_STATUS.write_text(
        "# Display subsystem 3D model coverage\n\n"
        f"Generated footprints: **{len(added)}**  \n"
        f"With a package/connector 3D model: **{len(modeled)}**  \n"
        f"Without a 3D model: **{len(unmodeled)}**\n\n"
        "The unmodelled items are intentionally bare electrical test pads; they have no component body.\n\n"
        "## Unmodelled test pads\n\n" +
        "\n".join(f"- `{fp.GetReference()}` — {fp.GetValue()}" for fp in sorted(unmodeled, key=lambda item: item.GetReference())) +
        "\n\nThe Kyocera connector uses the official manufacturer STEP. The Toshiba and TI chip-scale models are dimensioned from their package drawings and include the actual populated ball arrays and pin-one marks.\n",
        encoding="utf-8",
    )
    print(f"Exported {len(groups)} BOM lines for {len(added)} placed display footprints")


if __name__ == "__main__":
    main()
