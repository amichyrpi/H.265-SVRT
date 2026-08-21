"""Fetch public manufacturer/Ultra Librarian preview meshes used for 3D QA.

The UUIDs below are fixed to the cited exact component (or, where noted, the
same manufacturer package code).  This script intentionally does not scrape a
search result or silently substitute a similarly sized package.
"""

from pathlib import Path
import re
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
DESTINATION = ROOT / "libraries/3dmodels/exact/UltraLibrarian"

# filename: (Ultra Librarian UUID, provenance note)
MODELS = {
    "BQ25798RQMR.glb": (
        "66a653bb-5b64-11eb-9033-0a34d6323d74",
        "Exact TI BQ25798RQMR / RQM0029A package",
    ),
    "TPS25751DREFR.glb": (
        "1e6cce1e-112c-11ef-bf12-024899f9dfe1",
        "Exact TI TPS25751DREFR / REF0038A package",
    ),
    "TPS568230RJER.glb": (
        "2b719482-c80b-11e9-ab3a-0a3560a4cccc",
        "Exact TI TPS568230RJER / RJE0020A package",
    ),
    "TPS62825DMQR.glb": (
        "16ada6e5-103f-11e9-ab3a-0a3560a4cccc",
        "Exact TI TPS62825DMQR / DMQ0006A package",
    ),
    "TUSB320IRWBR.glb": (
        "16bfe88d-103f-11e9-ab3a-0a3560a4cccc",
        "TI RWB0012A package also used by TUSB320LAIRWBR",
    ),
    "TLV320AIC3101IRHBR.glb": (
        "5c1f74f8-4604-11f0-b69d-024899f9dfe1",
        "TI RHB0032M package also used by TLV320AIC3204IRHBR",
    ),
}


def main() -> None:
    DESTINATION.mkdir(parents=True, exist_ok=True)
    for filename, (uuid, note) in MODELS.items():
        preview_url = f"https://3d.ultralibrarian.com/{uuid}?ac=1"
        request = urllib.request.Request(preview_url, headers={"User-Agent": "Stearlight-CAD/1.0"})
        html = urllib.request.urlopen(request, timeout=30).read().decode("utf-8")
        match = re.search(r'https://static\.ultralibrarian\.com/[^"\']+\.glb', html)
        if not match:
            raise RuntimeError(f"No GLB preview found for {filename} ({uuid})")
        output = DESTINATION / filename
        glb_request = urllib.request.Request(match.group(0), headers={"User-Agent": "Stearlight-CAD/1.0"})
        output.write_bytes(urllib.request.urlopen(glb_request, timeout=60).read())
        print(f"{filename}: {output.stat().st_size} bytes — {note}")


if __name__ == "__main__":
    main()
