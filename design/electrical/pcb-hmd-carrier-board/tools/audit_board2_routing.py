"""Generate deterministic Board2 copper and differential-pair audit reports."""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path
import re
import sys

import pcbnew


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BOARD = ROOT / "output" / "production-audit" / "pcb-hmd-carrier-board2-routed-candidate.kicad_pcb"
DEFAULT_OUT_CSV = ROOT / "output" / "production-audit" / "HighSpeed-Length-Audit.csv"
DEFAULT_OUT_MD = ROOT / "output" / "production-audit" / "ROUTING_AUDIT.md"


def mate_name(name: str):
    if name.endswith("_P"):
        return name[:-2] + "_N"
    if name.endswith("_N"):
        return name[:-2] + "_P"
    return None


def is_high_speed(name: str):
    upper = name.upper()
    return any(token in upper for token in ("HDMI_", "DSI", "MIPI", "PCIE_", "USB3_", "USB_C_RX", "USB_C_TX"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("board", nargs="?", type=Path, default=DEFAULT_BOARD)
    parser.add_argument("--tag", help="Append a candidate tag to report filenames")
    args = parser.parse_args()
    board_path = args.board.resolve()
    suffix = f"-{args.tag}" if args.tag else ""
    out_csv = DEFAULT_OUT_CSV.with_name(f"HighSpeed-Length-Audit{suffix}.csv")
    out_md = DEFAULT_OUT_MD.with_name(f"ROUTING_AUDIT{suffix}.md")
    board = pcbnew.LoadBoard(str(board_path))
    board.BuildConnectivity()
    connectivity = board.GetConnectivity()
    connectivity.RecalculateRatsnest()
    lengths = defaultdict(float)
    vias = defaultdict(int)
    widths = defaultdict(list)
    layers = defaultdict(set)
    expected_high_speed = set()
    pads_by_net = defaultdict(list)
    for footprint in board.GetFootprints():
        for pad in footprint.Pads():
            name = str(pad.GetNetname())
            if pad.GetNetCode():
                pads_by_net[name].append(pad)
            if is_high_speed(name) and mate_name(name):
                expected_high_speed.add(name)
    segment_count = 0
    for item in board.GetTracks():
        name = str(item.GetNetname())
        if isinstance(item, pcbnew.PCB_VIA):
            vias[name] += 1
            continue
        segment_count += 1
        lengths[name] += pcbnew.ToMM(item.GetLength())
        widths[name].append(pcbnew.ToMM(item.GetWidth()))
        layers[name].add(board.GetLayerName(item.GetLayer()))

    complete_nets = set()
    for name, pads in pads_by_net.items():
        if len(pads) < 2:
            continue
        connected = {
            item.m_Uuid.AsString()
            for item in connectivity.GetConnectedItems(pads[0])
            if isinstance(item, pcbnew.PAD)
        }
        if all(pad.m_Uuid.AsString() in connected for pad in pads):
            complete_nets.add(name)

    rows = []
    seen = set()
    for name in sorted(expected_high_speed):
        if name in seen:
            continue
        mate = mate_name(name)
        if not mate or mate not in expected_high_speed:
            continue
        p_name = name[:-2] + "_P"
        n_name = name[:-2] + "_N"
        seen.update((p_name, n_name))
        p_len = lengths.get(p_name, 0.0)
        n_len = lengths.get(n_name, 0.0)
        rows.append((p_name[:-2], p_len, n_len, abs(p_len - n_len), vias[p_name], vias[n_name],
                     "/".join(sorted(layers[p_name] | layers[n_name])),
                     p_name in complete_nets and n_name in complete_nets))

    out_csv.parent.mkdir(parents=True, exist_ok=True)
    with out_csv.open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.writer(handle)
        writer.writerow(["Pair", "P_length_mm", "N_length_mm", "Mismatch_mm", "P_vias", "N_vias", "Layers", "Electrically_complete"])
        for row in rows:
            writer.writerow([row[0], f"{row[1]:.4f}", f"{row[2]:.4f}", f"{row[3]:.4f}", row[4], row[5], row[6], "YES" if row[7] else "NO"])

    copper_rows = [row for row in rows if row[1] > 0 and row[2] > 0]
    complete_rows = [row for row in rows if row[7]]
    worst = max((row[3] for row in complete_rows), default=0.0)
    zero_pairs = [row[0] for row in rows if row[1] == 0 or row[2] == 0]
    width_violations = []
    for name, values in widths.items():
        if not values:
            continue
        required = 1.0 if name in {"+5V_SYS", "BAT+", "BATP", "VBAT_SYS", "VBUS_CHARGE", "VBUS_USB_HOST", "LED_BOOST"} else 0.5 if name.startswith("+") or name in {"LCD_VSP", "LCD_VSN"} else 0.0
        if required and min(values) + 1e-6 < required:
            width_violations.append((name, min(values), required))

    # KiCad's text DRC report caps the displayed unconnected-item count.  Count
    # independent physical pad groups directly so routing progress remains
    # measurable above that reporting cap.
    open_edges = 0
    incomplete_net_edges = {}
    for name, pads in pads_by_net.items():
        components = set()
        for pad in pads:
            connected = tuple(sorted(
                item.m_Uuid.AsString()
                for item in connectivity.GetConnectedItems(pad)
                if isinstance(item, pcbnew.PAD)
            ))
            components.add(connected or (pad.m_Uuid.AsString(),))
        if len(components) > 1:
            incomplete_net_edges[name] = len(components) - 1
            open_edges += incomplete_net_edges[name]

    incomplete_nets = len(incomplete_net_edges)
    microsd_nets = (
        "SD_DAT0", "SD_DAT1", "SD_DAT2", "SD_DAT3", "SD_CLK", "SD_CMD",
        "SD_PWR", "SD_PWR_ON", "SD_DETECT_A", "SD_DETECT_B",
    )
    microsd_status = [
        (name, "INCOMPLETE" if name in incomplete_net_edges else "COMPLETE")
        for name in microsd_nets
        if name in pads_by_net
    ]

    out_md.write_text(
        "# Board2 routing audit\n\n"
        f"- Source: `{board_path.name}`\n"
        f"- Track segments: **{segment_count}**\n"
        f"- Vias: **{sum(vias.values())}**\n"
        f"- Physical open edges: **{open_edges}**\n"
        f"- Incomplete nets: **{incomplete_nets}**\n"
        f"- High-speed differential pairs measured: **{len(rows)}**\n"
        f"- High-speed pairs with copper on both sides: **{len(copper_rows)}/{len(rows)}**\n"
        f"- Electrically complete high-speed pairs: **{len(complete_rows)}/{len(rows)}**\n"
        f"- Worst P/N copper-length mismatch: **{worst:.4f} mm**\n"
        f"- High-speed pairs missing one side: **{', '.join(zero_pairs) if zero_pairs else 'none'}**\n"
        f"- Power nets below their preliminary class width: **{len(width_violations)}**\n\n"
        "The mismatch result is a geometric audit, not a field-solver result. Final\n"
        "impedance and delay require JLCPCB stackup confirmation and coupon review.\n\n"
        "## microSD routing status\n\n"
        + "".join(f"- {name}: **{status}**\n" for name, status in microsd_status)
        + "\n"
        + "## Incomplete nets\n\n"
        + "".join(
            f"- {name}: {incomplete_net_edges[name]} open edge(s)\n"
            for name in sorted(incomplete_net_edges)
        )
        + "\n"
        + ("## Width findings\n\n" + "".join(
            f"- {name}: {actual:.3f} mm minimum, {required:.3f} mm required\n"
            for name, actual, required in width_violations
        ) if width_violations else ""),
        encoding="utf-8",
    )
    print(f"Audited {segment_count} segments, {sum(vias.values())} vias, {len(rows)} differential pairs")


if __name__ == "__main__":
    main()
