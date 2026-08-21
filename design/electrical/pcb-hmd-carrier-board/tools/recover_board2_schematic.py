"""Recover a complete hierarchical Board2 electrical capture from its PCB.

This is deliberately conservative.  Footprint pad numbers and assigned PCB nets
are copied exactly.  Unknown vendor pin electrical types are passive and carry a
VERIFY note rather than being guessed.  The result is a Board2-specific schematic
that can be incrementally replaced with manufacturer symbols without deleting or
renumbering physical components.
"""

from copy import deepcopy
import json
from pathlib import Path
import re

from kiutils.symbol import SymbolLib

import generate_carrier_schematic as g


ROOT = Path(__file__).resolve().parents[1]
AUTHORITY = ROOT / "output" / "production-audit" / "Board2-Electrical-Authority.json"
SHEETS = ROOT / "sheets" / "board2"


EXACT_BLOCK_REFS = {
    "CM4": {"Module1"},
    "Audio": {"U3", "U4", "U10", "J1", "MK1", "MK2", "FB1", "C41", "C42", "C43",
              "C44", "C45", "C46", "C47", "C48", "C49", "C50", "C51", "C52", "C61",
              "C62", "C63", "R16", "R17", "R18"},
    "USB3": {"U5", "U7", "U8", "U9", "U11", "U14", "U15", "U16", "J8", "Y1",
             "C28", "C29", "C30", "C31", "C32", "C33", "C34", "C35", "C36", "C37",
             "C38", "C39", "C40", "C53", "C54", "C55", "C56", "C68", "R13", "R14",
             "R15", "R26", "R29", "R30", "R31", "R32", "R33", "R34", "R35"},
    "Interfaces": {"J2", "J4", "J5", "J6", "J7", "J9", "J10", "J11", "D11", "U210",
                   "R73", "R74", "R75", "R76", "R77", "R78"},
}


def ref_number(reference):
    match = re.search(r"(\d+)", reference)
    return int(match.group(1)) if match else -1


def block_for(item):
    ref = item["reference"]
    for name, refs in EXACT_BLOCK_REFS.items():
        if ref in refs:
            return name
    number = ref_number(ref)
    if 200 <= number < 300:
        return "Display"
    if 300 <= number < 400:
        return "MicroSD"
    if ref.startswith(("J", "TP", "MK", "D")):
        return "Interfaces"
    power_net_tokens = ("BAT", "VBUS", "+5V", "CHG", "PPHV", "REGN", "SW_", "VCC_5V")
    nets = {pad["net"] for pad in item["pads"]}
    if any(any(token in net for token in power_net_tokens) for net in nets):
        return "Power"
    return "Support"


def recovered_symbol(item):
    pins = []
    for pad in item["pads"]:
        number = pad["number"] or "EP"
        net = pad["net"]
        name = net if net else f"UNCONNECTED_{number}"
        pins.append((number, name[:48], "passive"))
    if not pins:
        return None
    safe_ref = re.sub(r"[^A-Za-z0-9_]", "_", item["reference"])
    symbol = g.custom_symbol(f"BOARD2_{safe_ref}", pins)
    symbol.libraryNickname = "Board2_Recovered"
    return symbol


def make_block(name, items):
    sheet = g.new_sheet(
        f"BOARD2 {name.upper()}",
        "Recovered from approved PCB placement; passive pin types marked VERIFY until vendor-symbol audit",
    )
    # Two independently packed columns prevent large connectors (CM4/J7/BGAs)
    # from overlapping labels belonging to later symbols.
    x_positions = [90, 245]
    column_y = [48.0, 48.0]
    for index, item in enumerate(items):
        symbol = recovered_symbol(item)
        if symbol is None:
            sheet.texts.append(g.Text(
                f"{item['reference']} {item['value']} — mechanical/no electrical pads",
                g.Position(35, 48 + index * 4, 0), g.eff(0.8), g.uid(),
            ))
            continue
        column = 0 if column_y[0] <= column_y[1] else 1
        x = x_positions[column]
        y = column_y[column]
        rows = (len(item["pads"]) + 1) // 2
        symbol_height = 2 * max(10.16, (rows + 1) * 1.27)
        column_y[column] += symbol_height + 16.0
        nets = {}
        for pad in item["pads"]:
            net_name = pad["net"]
            if not net_name or net_name.startswith("unconnected-("):
                net_name = "#NC"
            nets[pad["number"] or "EP"] = net_name
        g.add_component(
            sheet, symbol, item["reference"], item["value"], item["footprint"],
            item["datasheet"], x, y, nets, no_connect_unmapped=True,
            in_bom=not item["excluded_bom"], on_board=True, dnp=item["dnp"],
        )
    return sheet


def main():
    data = json.loads(AUTHORITY.read_text(encoding="utf-8"))
    blocks = {name: [] for name in ("CM4", "Power", "USB3", "Audio", "Display", "Interfaces", "MicroSD", "Support")}
    for item in data["footprints"]:
        blocks[block_for(item)].append(item)
    SHEETS.mkdir(parents=True, exist_ok=True)
    sheet_defs = []
    all_symbols = []
    for name, items in blocks.items():
        items.sort(key=lambda item: item["reference"])
        sheet = make_block(name, items)
        filename = f"Board2_{name}.kicad_sch"
        sheet.to_file(SHEETS / filename)
        sheet_defs.append((f"BOARD2 {name.upper()}", f"board2/{filename}"))
        all_symbols.extend(deepcopy(symbol) for symbol in sheet.libSymbols)
    top = g.make_top(sheet_defs)
    top.titleBlock.title = "STEARLIGHT HMD CARRIER BOARD2 — REV A"
    top.titleBlock.comments[1] = "PCB-authoritative restoration; VERIFY flags preserve unknown vendor data"
    top.to_file(ROOT / "pcb-hmd-carrier-board2.kicad_sch")
    unique = {}
    for symbol in all_symbols:
        unique[symbol.entryName] = symbol
    SymbolLib(version="20211014", generator="kiutils", symbols=list(unique.values())).to_file(
        ROOT / "libraries" / "symbols" / "Board2_Recovered.kicad_sym"
    )
    summary = ROOT / "output" / "production-audit" / "SCHEMATIC_RECONCILIATION.md"
    summary.write_text(
        "# Board2 schematic reconciliation\n\n"
        f"- PCB authority footprints: **{len(data['footprints'])}**\n"
        f"- Recovered schematic components with pads: **{sum(bool(i['pads']) for i in data['footprints'])}**\n"
        f"- Mechanical/no-pad footprints documented: **{sum(not i['pads'] for i in data['footprints'])}**\n"
        "- Deleted PCB components: **0**\n"
        "- Reference designators changed: **0**\n"
        "- Net source: exact PCB pad assignments\n"
        "- Schematic/PCB parity: run after recovery; true no-connect pads carry no net\n"
        "- Electrical pin types: passive/VERIFY where an authoritative vendor symbol is not locally available\n\n"
        "## Sheets\n\n" + "".join(f"- {name}: {len(blocks[name])} footprints\n" for name in blocks),
        encoding="utf-8",
    )
    print(f"Recovered {len(data['footprints'])} PCB footprints into {len(sheet_defs)} hierarchical sheets")


if __name__ == "__main__":
    main()
