"""Configure the approved carrier-board production-rule baseline.

This script only edits project settings.  It does not route or move components.
The selected dielectric build is JLCPCB JLC06161H-3313 (1.6 mm, 1 oz outer,
0.5 oz inner).  Differential widths are manufacturing-start values and remain
subject to JLCPCB impedance review/coupon tuning before fabrication release.
"""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "pcb-hmd-carrier-board2.kicad_pro"


def netclass(name: str, width: float, clearance: float, pair_width: float,
             pair_gap: float, via: float = 0.45, drill: float = 0.20,
             priority: int = 1) -> dict:
    return {
        "bus_width": 12,
        "clearance": clearance,
        "diff_pair_gap": pair_gap,
        "diff_pair_via_gap": 0.25,
        "diff_pair_width": pair_width,
        "line_style": 0,
        "microvia_diameter": 0.25,
        "microvia_drill": 0.10,
        "name": name,
        "pcb_color": "rgba(0, 0, 0, 0.000)",
        "priority": priority,
        "schematic_color": "rgba(0, 0, 0, 0.000)",
        "track_width": width,
        "tuning_profile": "",
        "via_diameter": via,
        "via_drill": drill,
        "wire_width": 6,
    }


def main() -> None:
    data = json.loads(PROJECT.read_text(encoding="utf-8"))
    data["meta"]["filename"] = PROJECT.name

    default = netclass("Default", 0.15, 0.15, 0.15, 0.15, 0.45, 0.20,
                       2_147_483_647)
    classes = [
        default,
        netclass("HDMI_MIPI_100R", 0.123, 0.20, 0.123, 0.15, priority=1),
        netclass("USB3_90R", 0.170, 0.20, 0.170, 0.15, priority=2),
        netclass("PCIE_85R", 0.200, 0.20, 0.200, 0.15, priority=3),
        netclass("USB2_90R", 0.170, 0.20, 0.170, 0.15, priority=4),
        netclass("POWER", 0.50, 0.20, 0.20, 0.20, 0.60, 0.30, 5),
        netclass("HIGH_CURRENT", 1.00, 0.20, 0.20, 0.20, 0.80, 0.40, 6),
        netclass("BGA_ESCAPE", 0.10, 0.10, 0.10, 0.10, 0.25, 0.15, 7),
        # Plane-connected GND is excluded from the DSN autorouter; L2/L5 zones
        # and deliberate stitching vias provide the return network.
        netclass("GND_PLANE", 0.25, 0.15, 0.20, 0.20, 0.45, 0.20, 8),
    ]

    hdmi = [
        f"HDMI_{eye}_{lane}_{polarity}"
        for eye in ("L", "R")
        for lane in ("CLK", "D0", "D1", "D2")
        for polarity in ("P", "N")
    ]
    mipi = [
        f"{eye}_DSI{port}_{lane}_{polarity}"
        for eye in ("L", "R")
        for port in ("A", "B")
        for lane in ("CLK", "D0", "D1", "D2", "D3")
        for polarity in ("P", "N")
    ]
    pcie = [
        f"PCIE_{lane}_{polarity}"
        for lane in ("CLK", "RX", "TX")
        for polarity in ("P", "N")
    ] + [
        f"PCIE_{lane}_USB3_{polarity}"
        for lane in ("RX", "TX")
        for polarity in ("P", "N")
    ]
    usb3 = [
        f"USB3_{lane}_{polarity}"
        for lane in ("RX", "TX")
        for polarity in ("P", "N")
    ] + [
        f"USB_C_{lane}{side}_{polarity}"
        for lane in ("RX", "TX")
        for side in (1, 2)
        for polarity in ("P", "N")
    ]
    usb2 = ["USB2_HOST_DP", "USB2_HOST_DM", "USB2_SERVICE_DP", "USB2_SERVICE_DM"]
    power = [
        "+3V3", "+3V3_AUDIO", "+2V8_MIC", "+1V05_USB",
        "DISPLAY_3V3", "DISPLAY_1V8", "DISPLAY_1V2", "DISPLAY_1V15",
        "LCD_VSP", "LCD_VSN",
    ]
    high_current = [
        "+5V_SYS", "BAT+", "BATP", "VBAT_SYS", "VBUS_CHARGE",
        "VBUS_USB_HOST", "LED_BOOST",
    ]

    patterns = []
    for class_name, names in (
        ("HDMI_MIPI_100R", hdmi + mipi),
        ("PCIE_85R", pcie),
        ("USB3_90R", usb3),
        ("USB2_90R", usb2),
        ("POWER", power),
        ("HIGH_CURRENT", high_current),
    ):
        patterns.extend({"netclass": class_name, "pattern": name} for name in names)
    patterns.append({"netclass": "GND_PLANE", "pattern": "GND"})

    data["net_settings"]["classes"] = classes
    data["net_settings"]["netclass_patterns"] = patterns
    data["board"]["design_settings"]["diff_pair_dimensions"] = [
        {"gap": 0.15, "via_gap": 0.25, "width": 0.123},
        {"gap": 0.15, "via_gap": 0.25, "width": 0.170},
        {"gap": 0.15, "via_gap": 0.25, "width": 0.200},
    ]
    data["board"]["design_settings"]["track_widths"] = [
        0.10, 0.123, 0.15, 0.17, 0.20, 0.25, 0.50, 1.00, 1.50,
    ]
    data["board"]["design_settings"]["via_dimensions"] = [
        {"diameter": 0.25, "drill": 0.15},
        {"diameter": 0.45, "drill": 0.20},
        {"diameter": 0.60, "drill": 0.30},
        {"diameter": 0.80, "drill": 0.40},
    ]

    rules = data["board"]["design_settings"]["rules"]
    rules.update({
        "min_clearance": 0.09,
        "min_copper_edge_clearance": 0.30,
        "min_hole_clearance": 0.20,
        "min_hole_to_hole": 0.20,
        "min_through_hole_diameter": 0.15,
        "min_track_width": 0.09,
        "min_via_annular_width": 0.05,
        "min_via_diameter": 0.25,
    })

    PROJECT.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print(f"Configured {PROJECT.name} with {len(classes)} net classes and "
          f"{len(patterns)} explicit assignments")


if __name__ == "__main__":
    main()
