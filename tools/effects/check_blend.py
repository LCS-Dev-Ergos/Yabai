#!/usr/bin/env python3
"""Check recorded crossfade progress between two static, distinct Desktops.

Use frame_capture without PNG output. This detects late images and abrupt
switches that the independent bar-visibility check cannot detect.
"""

import argparse
import json
from pathlib import Path


def analyze(rows, request_ms):
    complete = [row for row in rows if len(row) == 11]
    before = [row for row in complete if row[0] <= request_ms]
    after = [row for row in complete if request_ms < row[0] <= request_ms + 1200]
    if not before or not after:
        raise ValueError("missing baseline or post-request frames")
    source = before[-1][4:7]
    target = after[-1][4:7]
    delta = [b - a for a, b in zip(source, target)]
    length = sum(value * value for value in delta)
    if length < 400:
        raise ValueError("choose static Desktops with more distinct central content")
    progress = [sum((value - start) * direction for value, start, direction in
                    zip(row[4:7], source, delta)) / length for row in after]
    peak = 0.0
    regression = 0.0
    for value in progress:
        regression = max(regression, peak - value)
        peak = max(peak, value)
    excursion = max(0.0, max(max(-value, value - 1) for value in progress))
    intermediate = sum(.05 < value < .95 for value in progress)
    return dict(backtrack=round(regression, 3), excursion=round(excursion, 3),
                intermediate_frames=intermediate,
                verdict="PASS" if regression <= .08 and excursion <= .08 and intermediate >= 4 else "FAIL")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    metadata = json.loads((args.directory / "result.json").read_text())
    rows = [[float(value) for value in line.split()]
            for line in (args.directory / "frames.txt").read_text().splitlines()]
    result = analyze(rows, metadata["request_ms"])
    (args.directory / "blend.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))
    return int(result["verdict"] != "PASS")


if __name__ == "__main__":
    raise SystemExit(main())
