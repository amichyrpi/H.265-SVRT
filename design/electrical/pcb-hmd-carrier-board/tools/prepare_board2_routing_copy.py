"""Create a disposable Board2 copy for signal/power autorouting.

The authoritative PCB is never modified.  Board2 uses continuous GND zones on
In1.Cu and In4.Cu, so asking an autorouter to draw point-to-point tracks among
the 395 GND pads is both wasteful and electrically wrong.  This copy detaches
only the GND pads while retaining the existing GND zones/tracks as routing
obstacles.  All other nets, footprints, placements, rules, and the approved
outline are preserved exactly.
"""

from pathlib import Path
import argparse
import re

import pcbnew


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "pcb-hmd-carrier-board2.kicad_pcb"
def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source", type=Path, default=SOURCE,
        help="source KiCad board; defaults to the authoritative Board2",
    )
    parser.add_argument(
        "--output-tag",
        help="override the disposable output/session basename suffix",
    )
    parser.add_argument(
        "--signals-only", action="store_true",
        help="also detach POWER and HIGH_CURRENT pads for a signal-only pass",
    )
    parser.add_argument(
        "--high-speed-only", action="store_true",
        help="route only HDMI/MIPI, PCIe, USB3 and USB2 classes",
    )
    parser.add_argument(
        "--low-speed-only", action="store_true",
        help="route only Default/BGA escape low-speed nets",
    )
    parser.add_argument(
        "--power-only", action="store_true",
        help="route only POWER and HIGH_CURRENT net classes",
    )
    parser.add_argument(
        "--only-net-pattern",
        help=(
            "route only nets whose names match this regular expression; all "
            "other pads and their copper are detached in the disposable copy"
        ),
    )
    parser.add_argument(
        "--temporary-high-speed-clearance-mm",
        type=float,
        help=(
            "lower the high-speed net-class clearance only in this disposable "
            "routing copy so fine-pitch escape feasibility can be evaluated"
        ),
    )
    parser.add_argument(
        "--temporary-high-speed-width-mm",
        type=float,
        help=(
            "lower the high-speed track width only in this disposable routing "
            "copy to test fine-pitch breakout; final trunks still require SI widths"
        ),
    )
    parser.add_argument(
        "--keep-irrelevant-tracks-as-obstacles", action="store_true",
        help=(
            "retain existing copper from detached nets as routing obstacles; "
            "useful for small functional-block passes"
        ),
    )
    args = parser.parse_args()
    selected_modes = sum(bool(value) for value in (
        args.signals_only, args.high_speed_only, args.low_speed_only,
        args.power_only,
        args.only_net_pattern
    ))
    if selected_modes > 1:
        parser.error(
            "--signals-only, --high-speed-only, --low-speed-only, --power-only and "
            "--only-net-pattern are "
            "mutually exclusive"
        )
    only_net_re = re.compile(args.only_net_pattern) if args.only_net_pattern else None
    suffix = args.output_tag or (
        "selected-nets" if only_net_re else
        "high-speed-only" if args.high_speed_only else
        "low-speed-only" if args.low_speed_only else
        "power-only" if args.power_only else
        "signals-only" if args.signals_only else
        "routing-copy"
    )
    output = ROOT / "output" / "production-audit" / f"pcb-hmd-carrier-board2-{suffix}.kicad_pcb"
    dsn = ROOT / "output" / "production-audit" / f"pcb-hmd-carrier-board2-{suffix}.dsn"
    source = args.source.resolve()
    if not source.exists():
        raise RuntimeError(f"Routing source does not exist: {source}")
    board = pcbnew.LoadBoard(str(source))
    detached_classes = {"GND_PLANE"}
    if args.signals_only:
        detached_classes.update({"POWER", "HIGH_CURRENT"})
    high_speed_classes = {"HDMI_MIPI_100R", "USB3_90R", "PCIE_85R", "USB2_90R"}

    if (
        args.temporary_high_speed_clearance_mm is not None
        or args.temporary_high_speed_width_mm is not None
    ):
        if not (args.high_speed_only or args.only_net_pattern):
            parser.error(
                "temporary high-speed geometry options require --high-speed-only "
                "or --only-net-pattern"
            )
        netclasses = board.GetDesignSettings().m_NetSettings.GetNetclasses()
        for class_name in high_speed_classes:
            if args.temporary_high_speed_clearance_mm is not None:
                netclasses[class_name].SetClearance(
                    pcbnew.FromMM(args.temporary_high_speed_clearance_mm)
                )
            if args.temporary_high_speed_width_mm is not None:
                netclasses[class_name].SetTrackWidth(
                    pcbnew.FromMM(args.temporary_high_speed_width_mm)
                )

    detached_pads = 0
    detached_net_names = set()
    obstacle_anchor_nets = set()
    for footprint in board.GetFootprints():
        for pad in footprint.Pads():
            net_name = str(pad.GetNetname())
            net_class = str(pad.GetNetClassName())
            detach = (
                not bool(only_net_re.search(net_name))
                if only_net_re else
                net_class not in high_speed_classes
                if args.high_speed_only else
                net_class not in {"Default", "BGA_ESCAPE"}
                if args.low_speed_only else
                net_class not in {"POWER", "HIGH_CURRENT"}
                if args.power_only else
                net_class in detached_classes
            )
            if pad.GetNetCode() and detach:
                detached_net_names.add(net_name)
                # Specctra drops or warns about existing fixed copper whose net
                # has no remaining pin.  Keep one electrically inert anchor pad
                # per excluded net so that its existing copper remains a legal
                # routing obstacle, without presenting a multi-pin connection
                # for the autorouter to solve.
                if (
                    args.keep_irrelevant_tracks_as_obstacles
                    and net_name not in obstacle_anchor_nets
                ):
                    obstacle_anchor_nets.add(net_name)
                    continue
                pad.SetNetCode(0)
                detached_pads += 1

    # A specialized pass must not export existing wires whose nets have been
    # intentionally removed from that disposable DSN.  Doing so creates invalid
    # Specctra wiring records and can bias the router around irrelevant copper.
    removed_tracks = 0
    if (
        (
            args.signals_only or args.high_speed_only or args.low_speed_only
            or args.power_only or only_net_re
        )
        and not args.keep_irrelevant_tracks_as_obstacles
    ):
        for track in list(board.GetTracks()):
            if str(track.GetNetname()) in detached_net_names:
                board.Remove(track)
                removed_tracks += 1

    output.parent.mkdir(parents=True, exist_ok=True)
    pcbnew.SaveBoard(str(output), board)
    if not pcbnew.ExportSpecctraDSN(board, str(dsn)):
        raise RuntimeError(f"Failed to export routing DSN: {dsn}")
    print(
        f"Routing copy saved with {detached_pads} pads detached from "
        f"{f'all nets not matching {args.only_net_pattern!r}' if only_net_re else 'all non-high-speed classes' if args.high_speed_only else 'all non-low-speed classes' if args.low_speed_only else 'all non-power classes' if args.power_only else ', '.join(sorted(detached_classes))} "
        f"and {removed_tracks} irrelevant tracks removed: {output}"
    )
    if obstacle_anchor_nets:
        print(
            f"Retained {len(obstacle_anchor_nets)} single-pad net anchors so "
            "existing copper remains a valid fixed obstacle"
        )
    print(f"Specctra design exported: {dsn}")


if __name__ == "__main__":
    main()
