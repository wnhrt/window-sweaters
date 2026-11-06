#!/usr/bin/env python3
"""Compare the Cairo port's collection plates with the macOS renderer's.

    python3 tests/compare.py ../docs/collection build/plates [--montage out.png 1,3,7]

Prints the mean absolute difference (0-255, per channel) in three text-free
regions of each plate: the window band, the 4x enlarged strip and the corners.
Exits non-zero if any region differs by more than --limit (default 6).
"""
import argparse
import sys

import numpy as np
from PIL import Image

REGIONS = {"band": (180, 210, 1360, 540), "strip": (76, 602, 1356, 682), "corners": (116, 780, 1324, 940)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("reference")
    ap.add_argument("candidate")
    ap.add_argument("--limit", type=float, default=6.0)
    ap.add_argument("--montage", nargs=2, metavar=("OUT", "PLATES"))
    args = ap.parse_args()

    worst = 0.0
    for i in range(1, 38):
        a = np.asarray(Image.open(f"{args.reference}/app-{i:02d}.png").convert("RGB")).astype(int)
        b = np.asarray(Image.open(f"{args.candidate}/app-{i:02d}.png").convert("RGB")).astype(int)
        diffs = []
        for x0, y0, x1, y1 in REGIONS.values():
            diffs.append(float(np.abs(a[y0:y1, x0:x1] - b[y0:y1, x0:x1]).mean()))
        worst = max(worst, *diffs)
        print(f"app-{i:02d}  " + "  ".join(f"{n} {d:5.2f}" for n, d in zip(REGIONS, diffs)))
    print(f"worst region: {worst:.2f} (limit {args.limit})")

    if args.montage:
        out, plates = args.montage
        rows = []
        for i in (int(p) for p in plates.split(",")):
            a = Image.open(f"{args.reference}/app-{i:02d}.png").convert("RGB")
            b = Image.open(f"{args.candidate}/app-{i:02d}.png").convert("RGB")
            row = Image.new("RGB", (a.width * 2, a.height), "white")
            row.paste(a, (0, 0))
            row.paste(b, (a.width, 0))
            rows.append(row)
        m = Image.new("RGB", (rows[0].width, sum(r.height for r in rows)))
        y = 0
        for r in rows:
            m.paste(r, (0, y))
            y += r.height
        m.save(out)
    return 0 if worst <= args.limit else 1


if __name__ == "__main__":
    sys.exit(main())
