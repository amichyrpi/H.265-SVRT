"""Make board2 legend manufacturable without moving components.

Dense reference-designator text is moved to the corresponding fabrication layer,
where it remains available for assembly drawings.  Human-facing silkscreen labels
are kept and raised to JLCPCB's 0.8 mm absolute text-height baseline.
"""

from __future__ import annotations

import re
from pathlib import Path
from shutil import copy2

import pcbnew


ROOT = Path(__file__).resolve().parents[1]
BOARD_PATH = ROOT / "pcb-hmd-carrier-board2.kicad_pcb"
BACKUP = ROOT / "output/review/pcb-hmd-carrier-board2-before-legend-normalization.kicad_pcb"
REFERENCE = re.compile(r"^(?:R|C|U|J|D|Q|L|Y|TP|FB|MK)\d+[A-Z]?$", re.IGNORECASE)


def main() -> None:
    board = pcbnew.LoadBoard(str(BOARD_PATH))
    BACKUP.parent.mkdir(parents=True, exist_ok=True)
    if not BACKUP.exists():
        copy2(BOARD_PATH, BACKUP)

    moved = 0
    resized = 0
    for drawing in board.GetDrawings():
        if not isinstance(drawing, pcbnew.PCB_TEXT):
            continue
        if drawing.GetLayer() not in (pcbnew.F_SilkS, pcbnew.B_SilkS):
            continue
        if REFERENCE.fullmatch(str(drawing.GetText()).strip()):
            drawing.SetLayer(pcbnew.F_Fab if drawing.GetLayer() == pcbnew.F_SilkS else pcbnew.B_Fab)
            moved += 1
            continue
        size = drawing.GetTextSize()
        if pcbnew.ToMM(size.y) < 0.8:
            drawing.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(0.8), pcbnew.FromMM(0.8)))
            drawing.SetTextThickness(pcbnew.FromMM(0.15))
            resized += 1

    hidden_unannotated = 0
    for footprint in board.GetFootprints():
        if str(footprint.GetReference()) == "REF**":
            footprint.Reference().SetVisible(False)
            hidden_unannotated += 1

    pcbnew.SaveBoard(str(BOARD_PATH), board)
    print(f"Moved {moved} reference labels to Fab, resized {resized} user labels, "
          f"hid {hidden_unannotated} unannotated mechanical references")


if __name__ == "__main__":
    main()
