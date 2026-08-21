"""Import a reviewed Specctra session into a candidate Board2 file.

The authoritative PCB is never overwritten by this script.  Promotion happens
only after KiCad DRC and the high-speed audit pass.
"""

import argparse
from pathlib import Path

import pcbnew


ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / "pcb-hmd-carrier-board2.kicad_pcb"
DEFAULT_SESSION = ROOT / "output" / "production-audit" / "pcb-hmd-carrier-board2.ses"
DEFAULT_CANDIDATE = ROOT / "output" / "production-audit" / "pcb-hmd-carrier-board2-routed-candidate.kicad_pcb"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--session", type=Path, default=DEFAULT_SESSION)
    parser.add_argument("--output", type=Path, default=DEFAULT_CANDIDATE)
    args = parser.parse_args()
    session = args.session.resolve()
    candidate = args.output.resolve()
    if not session.exists():
        raise RuntimeError(f"Routing session does not exist: {session}")
    board = pcbnew.LoadBoard(str(BOARD))
    if not pcbnew.ImportSpecctraSES(board, str(session)):
        raise RuntimeError("KiCad rejected the Specctra routing session")
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    candidate.parent.mkdir(parents=True, exist_ok=True)
    pcbnew.SaveBoard(str(candidate), board)
    segments = sum(
        isinstance(item, pcbnew.PCB_TRACK) and not isinstance(item, pcbnew.PCB_VIA)
        for item in board.GetTracks()
    )
    vias = sum(isinstance(item, pcbnew.PCB_VIA) for item in board.GetTracks())
    print(f"Candidate saved with {segments} track segments and {vias} vias: {candidate}")


if __name__ == "__main__":
    main()
