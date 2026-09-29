#!/usr/bin/env python3
"""Bounded, non-interactive rectangle/buffer feasibility campaign.

The capture probe samples both live client and WindowServer during each hold.
This runner also records WindowServer's top MEM before launch and after exit.
It does not change Spaces, launch Yabai, or create an overlay.
It does not monitor user Space navigation or daemon restarts; its output is
exploratory unless an independent continuous desktop guard is in place.
"""

import datetime as dt
import hashlib
import json
import pathlib
import re
import subprocess
import sys
import time


ORDER = ("rect", "filter", "filter", "rect")


def top_windowserver():
    ps = subprocess.run(
        ["/bin/ps", "-A", "-o", "pid=,comm="],
        capture_output=True, text=True, timeout=5, check=True,
    )
    for line in ps.stdout.splitlines():
        match = re.match(r"\s*(\d+)\s+(.*WindowServer)\s*$", line)
        if not match:
            continue
        pid = int(match.group(1))
        top = subprocess.run(
            ["/usr/bin/top", "-l", "1", "-pid", str(pid),
             "-stats", "pid,command,mem"],
            capture_output=True, text=True, timeout=8, check=True,
        )
        for row in top.stdout.splitlines():
            fields = row.split()
            if len(fields) >= 3 and fields[0] == str(pid):
                amount = re.fullmatch(r"([0-9.]+)([KMG])", fields[2])
                if amount:
                    scale = {"K": 1 / 1024, "M": 1, "G": 1024}[amount.group(2)]
                    return {"pid": pid, "top_mem_mib": float(amount.group(1)) * scale}
    return {"pid": None, "top_mem_mib": None}


def utc_now():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--smoke":
        binary = pathlib.Path(sys.argv[2]).resolve()
        child = subprocess.run([str(binary), "memory-smoke", "3"],
                               stdin=subprocess.DEVNULL,
                               capture_output=True, text=True, timeout=10)
        rows = [json.loads(line) for line in child.stdout.splitlines() if line]
        points = [row for row in rows if row.get("mode") == "memory"]
        valid = child.returncode == 0 and [row["point"] for row in points] == [
            "before_load", "hold_0", "hold_end"] and all(
                row["client_footprint_mib"] > 0 and
                row["windowserver_top_mem_mib"] > 0 and
                row["pid"] > 0 for row in points)
        print(json.dumps({"smoke_pass": valid, "points": points,
                          "stderr": child.stderr.strip()}))
        return 0 if valid else 1
    if len(sys.argv) != 4:
        print("usage: run_headless_ab.py --smoke PROBE_BINARY | PROBE_BINARY DISPLAY_ID OUTPUT_DIR", file=sys.stderr)
        return 64
    binary = pathlib.Path(sys.argv[1]).resolve()
    display = str(int(sys.argv[2]))
    output = pathlib.Path(sys.argv[3]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    print("warning: no continuous Space/input/daemon guard; do not treat timings or WindowServer MEM as controlled acceptance", flush=True)
    binary_sha = hashlib.sha256(binary.read_bytes()).hexdigest()
    manifest = {"binary": str(binary), "binary_sha256": binary_sha,
                "display": int(display), "count_per_process": 12,
                "hold_seconds": 60, "order": ORDER, "runs": []}
    manifest_path = output / "manifest.json"
    for index, path in enumerate(ORDER, 1):
        if index > 1:
            time.sleep(5)
        before = top_windowserver()
        command = [str(binary), "capture", display, "12", "60", f"--path={path}"]
        log_path = output / f"{index:02d}-{path}.jsonl"
        print(f"run {index}/4 {path}: begin; WS={before['top_mem_mib']} MiB", flush=True)
        start = utc_now()
        with log_path.open("w", encoding="utf-8") as log:
            child = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                                     stdout=log, stderr=subprocess.PIPE,
                                     text=True)
            try:
                stderr = child.communicate(timeout=180)[1]
            except subprocess.TimeoutExpired:
                child.terminate()
                try:
                    stderr = child.communicate(timeout=5)[1]
                except subprocess.TimeoutExpired:
                    child.kill()
                    stderr = child.communicate()[1]
                stderr += "\nrunner timeout after 180s"
        end = utc_now()
        after = top_windowserver()
        rows = [json.loads(line) for line in log_path.read_text().splitlines() if line]
        captures = [row for row in rows if row.get("mode") == "capture"]
        holds = [row for row in rows if row.get("mode") == "memory"
                 and row.get("point", "").startswith("hold_")]
        result = {"index": index, "path": path, "command": command,
                  "client_pid": child.pid, "start_utc": start, "end_utc": end,
                  "log": str(log_path), "exit_code": child.returncode,
                  "stderr": stderr.strip(), "captures": len(captures),
                  "valid_captures": sum(row.get("valid") is True for row in captures),
                  "hold_points": [row.get("point") for row in holds],
                  "windowserver_before": before, "windowserver_postexit": after}
        manifest["runs"].append(result)
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
        print(f"run {index}/4 {path}: exit={child.returncode}, "
              f"captures={len(captures)}, hold={result['hold_points']}, "
              f"WS postexit={after['top_mem_mib']} MiB", flush=True)
        if child.returncode or len(captures) != 12 or len(holds) < 4:
            print("campaign stopped at failed run; inspect manifest and raw log", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
