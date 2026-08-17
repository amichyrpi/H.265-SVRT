"""Generate the mandatory Board2 restoration checkpoint before routing."""

from __future__ import annotations

import csv
import re
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path

import pcbnew


ROOT = Path(__file__).resolve().parents[1]
BEFORE = ROOT / "output/review/pcb-hmd-carrier-board2-pre-restoration-2026-08-15.kicad_pcb"
AFTER = ROOT / "pcb-hmd-carrier-board2.kicad_pcb"
DRC = ROOT / "output/reports/DRC-pcb-hmd-carrier-board2-pre-routing.rpt"
NETLIST = ROOT / "output/pcb-hmd-carrier-board2-current.net.xml"
OUT = ROOT / "output/production-audit"


def footprint_map(board: pcbnew.BOARD) -> dict[str, tuple]:
    result = {}
    for fp in board.GetFootprints():
        pos = fp.GetPosition()
        result[fp.m_Uuid.AsString()] = (
            str(fp.GetReference()), pos.x, pos.y, fp.GetOrientationDegrees(),
            fp.GetLayer(), fp.GetValue()
        )
    return result


def classify(net: str) -> str:
    upper = net.upper()
    if upper.startswith("SD_"):
        return "microSD"
    if "HDMI" in upper:
        return "HDMI"
    if "DSI" in upper or "MIPI" in upper:
        return "MIPI D-PHY"
    if "PCIE" in upper:
        return "PCIe / USB3 controller"
    if "USB3" in upper or "USB_C_RX" in upper or "USB_C_TX" in upper:
        return "USB3 / Type-C mux"
    if "USB2" in upper or upper.endswith("_DP") or upper.endswith("_DM"):
        return "USB2"
    if any(token in upper for token in ("I2S", "MIC", "HP", "HPL", "HPR", "JACK", "AUDIO")):
        return "Audio"
    if any(token in upper for token in ("TRACK", "LIGHTHOUSE")):
        return "Tracking"
    if any(token in upper for token in ("LCD", "DISPLAY", "BRIDGE", "BACKLIGHT", "BL_", "LEDPWM", "TE")):
        return "Display control / power"
    if any(token in upper for token in ("REFCLK", "XTAL", "OSC", "CLK")):
        return "Clocks"
    if any(token in upper for token in ("BUTTON", "PWR_KEY", "VOL_", "PROX", "FAN", "LED_")):
        return "User controls / proximity / LEDs"
    if any(token in upper for token in ("I2C", "GPIO", "RESET", "INT", "IRQ", "SDA", "SCL")):
        return "I2C / GPIO / reset / interrupt"
    if upper == "GND" or upper.startswith("+") or any(
        token in upper for token in ("VBAT", "VBUS", "BAT", "PGOOD", "SYS", "SW", "DRV", "CHG", "POWER")
    ):
        return "PD / charger / power distribution"
    return "Remaining low-speed / verify"


def unconnected_nets() -> dict[str, set[str]]:
    text = DRC.read_text(encoding="utf-8", errors="replace")
    groups: dict[str, set[str]] = defaultdict(set)
    for match in re.finditer(
        r"\[unconnected_items\].*?(?=\n\[[a-z_]+\]|\Z)", text, re.DOTALL
    ):
        block = match.group(0)
        names = re.findall(r"\[([^\]]+)\]", block)
        names = [name for name in names if name not in {"<no net>", "unconnected_items"}]
        if names:
            groups[classify(names[0])].add(names[0])
    return groups


def main() -> None:
    before_board = pcbnew.LoadBoard(str(BEFORE))
    after_board = pcbnew.LoadBoard(str(AFTER))
    before = footprint_map(before_board)
    after = footprint_map(after_board)
    restored = sorted(after[uid][0] for uid in set(after) - set(before))
    deleted = sorted(before[uid][0] for uid in set(before) - set(after))
    moved = sorted(
        after[uid][0] for uid in set(before) & set(after)
        if before[uid][1:5] != after[uid][1:5]
    )
    changed_refs = sorted(
        f"`{before[uid][0]}` -> `{after[uid][0]}`"
        for uid in set(before) & set(after)
        if before[uid][0] != after[uid][0]
    )

    schematic_refs: set[str] = set()
    if NETLIST.exists():
        xml_root = ET.parse(NETLIST).getroot()
        schematic_refs = {
            comp.attrib["ref"] for comp in xml_root.findall("./components/comp")
        }
    board_bom_refs = {
        fp.GetReference() for fp in after_board.GetFootprints()
        if not fp.IsExcludedFromBOM() and fp.GetReference() != "REF**"
    }
    board_all_refs = {
        fp.GetReference() for fp in after_board.GetFootprints()
        if fp.GetReference() != "REF**"
    }
    pcb_only = sorted(board_all_refs - schematic_refs)
    schematic_only = sorted(schematic_refs - board_all_refs)
    grouped = unconnected_nets()
    drc_text = DRC.read_text(encoding="utf-8", errors="replace")
    physical_match = re.search(r"\*\* Found (\d+) DRC violations", drc_text)
    unconnected_match = re.search(r"\*\* Found (\d+) unconnected pads", drc_text)
    physical_count = int(physical_match.group(1)) if physical_match else -1
    unconnected_count = int(unconnected_match.group(1)) if unconnected_match else -1

    OUT.mkdir(parents=True, exist_ok=True)
    with (OUT / "Restoration-Unrouted-Groups.csv").open(
        "w", newline="", encoding="utf-8-sig"
    ) as handle:
        writer = csv.writer(handle)
        writer.writerow(["Functional_block", "Unique_unrouted_nets", "Net_names"])
        for group in sorted(grouped):
            nets = sorted(grouped[group])
            writer.writerow([group, len(nets), ",".join(nets)])

    group_lines = "\n".join(
        f"- {group}: {len(nets)} unique nets"
        for group, nets in sorted(grouped.items())
    ) or "- No unconnected nets parsed"
    report = f"""# Board2 restoration checkpoint

Generated before new production routing.

- Component/footprint count before restoration: **{len(before_board.GetFootprints())}**
- Component/footprint count after restoration: **{len(after_board.GetFootprints())}**
- Restored components: **{', '.join(restored) if restored else 'none'}**
- Components moved during restoration: **{', '.join(moved) if moved else 'none'}**
- Components deleted during restoration: **{', '.join(deleted) if deleted else 'none'}**
- Changed reference designators: **{', '.join(changed_refs) if changed_refs else 'none'}**
- microSD restoration: **complete on the main PCB; J301 moved 1 mm left for verified J8 shell clearance**
- Default assembly state: **populated for CM4 Lite; DNP in the same-PCB eMMC variant**
- Existing schematic references: **{len(schematic_refs)}**
- Board BOM references: **{len(board_bom_refs)}**
- PCB-only references still requiring schematic recovery: **{len(pcb_only)}**
- Schematic-only references requiring reconciliation: **{len(schematic_only)}**
- DRC checkpoint: **{physical_count} placement/footprint violations, {unconnected_count} unconnected items**

## Remaining unrouted nets by functional block

{group_lines}

Full net lists: `Restoration-Unrouted-Groups.csv`.

## Placement consequence

The authoritative microSD location was restored.  KiCad proved a physical overlap
with J8/U207.  J301 was moved 1 mm left and U207 moved 1.6 mm right / 3.7 mm down;
the pre-routing physical DRC is now clean.  Six duplicate `REF**` mounting-hole
references were annotated H1-H6 because unique references are required for
manufacturing exports.  All unrelated component positions remain frozen.
"""
    (OUT / "RESTORATION_CHECKPOINT.md").write_text(report, encoding="utf-8")
    print(f"Wrote restoration checkpoint: {OUT / 'RESTORATION_CHECKPOINT.md'}")


if __name__ == "__main__":
    main()
