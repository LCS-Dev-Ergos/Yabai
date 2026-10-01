#!/usr/bin/env python3
"""Time Desktop transitions from the request to what WindowServer presents.

Opt-in live measurement, not a CI test. It switches the visible Desktop, so
announce the run and leave the machine alone: it waits for five seconds without
keys or clicks, stops at the next one and restores the starting Desktop and
window unless input interrupted it.

Each trial starts on SOURCE, records the display with frame-capture, sends one
transition to DESTINATION and measures the central box of every presented
frame against the last frame before the request (a) and the settled
destination (b):

  first_change_ms  first frame whose colour moved at least 5% of |b - a|
                   (and 4 units) away from a: when the press visibly answers
  settled_ms       first frame from which every later frame stays within that
                   distance of b: when the transition is over

Probes also report when their work began, after process launch and AppKit
setup that the running daemon does not repeat; *_from_work_ms measure from
there and are the figures to compare with the daemon's own variants.

Variants run interleaved, rotating which goes first each round, so drift in
system load reaches all of them alike. SOURCE and DESTINATION must be ordinary
Desktops on one display whose central content differs clearly and stays
static, for example two empty Desktops with different wallpapers.

Variants:
  none                      the installed daemon, no effect
  crossfade                 the installed daemon's crossfade (--duration)
  snapshot:NAME:PATH        a helper compiled from snapshot_probe.m, for
                            example with -DSNAPSHOT_PROBE_CURVE=1 for ease-out
  veil:NAME:PEAK:IN:OUT[:start|peak[:DELAY_MS[:blur=R,tint=A]]]
                            the veil probe (--veil-probe) with these settings

Example:
  python3 tools/effects/transition_timing.py 4 5 --rounds 6 \\
      --output build/experiments/transition-timing \\
      --variant none --variant crossfade \\
      --variant snapshot:smooth:build/tools/snapshot-smooth \\
      --variant snapshot:ease:build/tools/snapshot-ease-out \\
      --variant veil:dip:0.35:60:160:start
"""

import argparse
import ctypes
import json
import math
from pathlib import Path
import selectors
import statistics
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
RECORD_SECONDS = 1.6
CENTRAL = slice(4, 7)  # frame-capture columns: central mean red, green, blue


def parse_variant(text: str, parser: argparse.ArgumentParser) -> dict:
    parts = text.split(":")
    if parts == ["none"] or parts == ["crossfade"]:
        return dict(name=parts[0], kind=parts[0])
    if parts[0] == "snapshot" and len(parts) == 3 and parts[1]:
        path = Path(parts[2])
        if not path.is_file():
            parser.error(f"snapshot helper {path} does not exist")
        return dict(name=parts[1], kind="snapshot", path=path.resolve())
    if parts[0] == "veil" and len(parts) in (5, 6, 7, 8) and parts[1]:
        try:
            peak, fade_in, fade_out = float(parts[2]), int(parts[3]), int(parts[4])
            delay = int(parts[6]) if len(parts) >= 7 else 0
            extra = dict(item.split("=", 1) for item in parts[7].split(",")) if len(parts) == 8 else {}
            blur, tint = int(extra.pop("blur", 0)), float(extra.pop("tint", 1.0))
            if extra:
                raise ValueError(extra)
        except ValueError:
            parser.error(f"invalid veil settings in {text}")
        switch_at = parts[5] if len(parts) >= 6 else "start"
        if not (
            0 < peak <= 1
            and 0 <= fade_in <= 300
            and 1 <= fade_out <= 600
            and switch_at in ("start", "peak")
            and 0 <= delay <= 100
            and 0 <= blur <= 100
            and 0 <= tint <= 1
        ):
            parser.error(f"veil settings out of range in {text}")
        return dict(
            name=parts[1],
            kind="veil",
            peak=peak,
            fade_in=fade_in,
            fade_out=fade_out,
            switch_at=switch_at,
            delay=delay,
            blur=blur,
            tint=tint,
        )
    parser.error(f"unknown variant {text}")


def distance(x, y):
    return math.sqrt(sum((p - q) ** 2 for p, q in zip(x, y)))


def analyze(rows, request_ms):
    """first_change_ms and settled_ms relative to the request; None when invalid."""
    before = [row[CENTRAL] for row in rows if row[0] <= request_ms]
    after = [(row[0], row[CENTRAL]) for row in rows if row[0] > request_ms]
    # The recorder reports only frames whose content changed, so a static
    # Desktop may contribute a single frame before the request.
    if not before or len(after) < 2:
        return None, "too few frames around the request"
    a = before[-1]
    b = after[-1][1]
    span = distance(a, b)
    if span < 30:
        return None, f"source and destination differ by only {span:.1f}"
    threshold = max(0.05 * span, 4.0)
    first_change = next(
        (t for t, colour in after if distance(colour, a) >= threshold), None
    )
    settled = None
    for i, (t, _) in enumerate(after):
        if all(distance(colour, b) <= threshold for _, colour in after[i:]):
            settled = t
            break
    if first_change is None or settled is None:
        return None, "no change or no settled destination"
    darkest = min(sum(colour) / 3 for _, colour in after)
    return dict(
        first_change_ms=round(first_change - request_ms, 1),
        settled_ms=round(settled - request_ms, 1),
        span=round(span, 1),
        darkest=round(darkest, 1),
        frames=len(rows),
    ), None


class GpuSampler(threading.Thread):
    """GPU "Device Utilization %" from IOAccelerator, every ~50 ms, for one trial.

    The recorder and the probe add their own load, alike in every variant, so
    compare variants with each other. The figure is the whole GPU's, not
    WindowServer's share.
    """

    def __init__(self):
        super().__init__(daemon=True)
        self.samples: list[int] = []
        self.running = True

    def run(self):
        while self.running:
            output = subprocess.run(["ioreg", "-r", "-d", "1", "-c", "IOAccelerator"],
                                    capture_output=True, text=True).stdout
            marker = '"Device Utilization %"='
            if marker in output:
                digits = output.split(marker, 1)[1].split(",", 1)[0].split("}", 1)[0]
                if digits.strip().isdigit():
                    self.samples.append(int(digits.strip()))
            time.sleep(0.05)

    def stop(self) -> list[int]:
        self.running = False
        self.join(timeout=1)
        return self.samples


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("source", type=int)
    parser.add_argument("destination", type=int)
    parser.add_argument("--variant", action="append", required=True)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument(
        "--duration",
        type=float,
        default=0.25,
        help="crossfade and snapshot fade, seconds",
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--capture", type=Path, default=ROOT / "build/tools/effects-frame-capture"
    )
    parser.add_argument(
        "--veil-probe", type=Path, default=ROOT / "build/tools/veil-probe"
    )
    parser.add_argument("--yabai", default="/run/current-system/sw/bin/yabai")
    args = parser.parse_args()
    variants = [parse_variant(text, parser) for text in args.variant]
    if len({v["name"] for v in variants}) != len(variants):
        parser.error("variant names must be unique")
    if not 1 <= args.rounds <= 12:
        parser.error("rounds must be in 1..12")
    if not (math.isfinite(args.duration) and 0 < args.duration <= 1):
        parser.error("duration must be in (0, 1]")
    if not args.capture.is_file():
        parser.error(f"{args.capture} is missing: build tools/effects/frame_capture.m")
    if any(v["kind"] == "veil" for v in variants) and not args.veil_probe.is_file():
        parser.error(f"{args.veil_probe} is missing: build tools/effects/veil_probe.m")

    cg = ctypes.CDLL("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics")
    since = cg.CGEventSourceSecondsSinceLastEventType
    since.argtypes = [ctypes.c_int32, ctypes.c_uint32]
    since.restype = ctypes.c_double

    def idle():
        return min(since(1, event) for event in (10, 12, 1, 3, 25))

    def command(*tokens):
        return subprocess.check_output(
            [args.yabai, "-m", *map(str, tokens)], text=True, timeout=5
        )

    def query(*tokens):
        return json.loads(command("query", *tokens))

    spaces = {space["index"]: space for space in query("--spaces")}
    source, destination = spaces.get(args.source), spaces.get(args.destination)
    if not source or not destination or args.source == args.destination:
        parser.error("choose two existing, distinct Desktops")
    if (
        source["display"] != destination["display"]
        or source["is-native-fullscreen"]
        or destination["is-native-fullscreen"]
    ):
        parser.error("choose two ordinary Desktops on the same display")
    display = next(
        item["id"] for item in query("--displays") if item["index"] == source["display"]
    )

    deadline = time.monotonic() + 30
    while idle() < 5:
        if time.monotonic() > deadline:
            print("ABORT: no five-second input-idle interval", flush=True)
            return 2
        time.sleep(0.1)

    original = query("--spaces", "--space")["index"]
    try:
        window = query("--windows", "--window").get("id")
    except subprocess.CalledProcessError:
        window = None
    started = time.monotonic()

    def check_input():
        if idle() + 0.03 < time.monotonic() - started:
            raise InterruptedError("user input")

    def pause(seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            check_input()
            time.sleep(0.02)

    def wait(process, seconds, what):
        deadline = time.monotonic() + seconds
        while process.poll() is None:
            pause(0.02)
            if time.monotonic() > deadline:
                raise TimeoutError(what)

    def launch(variant):
        if variant["kind"] == "none":
            return subprocess.Popen(
                [
                    args.yabai,
                    "-m",
                    "space",
                    "--navigate",
                    "focus",
                    str(args.destination),
                    "1",
                    "0",
                ]
            )
        if variant["kind"] == "crossfade":
            return subprocess.Popen(
                [
                    args.yabai,
                    "-m",
                    "space",
                    "--navigate",
                    "focus",
                    str(args.destination),
                    "crossfade",
                    str(args.duration),
                ]
            )
        if variant["kind"] == "snapshot":
            return subprocess.Popen(
                [
                    str(variant["path"]),
                    str(display),
                    str(args.destination),
                    str(args.duration),
                ],
                stderr=subprocess.PIPE,
                text=True,
            )
        return subprocess.Popen(
            [
                str(args.veil_probe.resolve()),
                str(display),
                str(args.destination),
                str(variant["peak"]),
                str(variant["fade_in"]),
                str(variant["fade_out"]),
                "--switch-at",
                variant["switch_at"],
                "--switch-delay",
                str(variant["delay"]),
                "--blur",
                str(variant["blur"]),
                "--tint",
                str(variant["tint"]),
            ],
            stderr=subprocess.PIPE,
            text=True,
        )

    run = args.output.resolve() / time.strftime("%Y%m%d-%H%M%S")
    run.mkdir(parents=True)
    results = []
    code = 0
    recorder = effect = None
    interrupted = False
    try:
        for round_index in range(args.rounds):
            for offset in range(len(variants)):
                variant = variants[(offset + round_index) % len(variants)]
                check_input()
                command("space", "--navigate", "focus", args.source, 1, 0)
                pause(0.8)
                recorder = subprocess.Popen(
                    [str(args.capture.resolve()), str(display), str(RECORD_SECONDS)],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
                assert recorder.stderr is not None
                with selectors.DefaultSelector() as selector:
                    selector.register(recorder.stderr, selectors.EVENT_READ)
                    deadline = time.monotonic() + 12
                    while not selector.select(timeout=0.1):
                        check_input()
                        if time.monotonic() > deadline:
                            raise TimeoutError("recorder startup")
                    message = recorder.stderr.readline()
                if not message.startswith("capturing"):
                    raise RuntimeError(f"recorder failed: {message.strip()}")
                pause(0.15)
                gpu = GpuSampler()
                gpu.start()
                request_ms = time.clock_gettime(time.CLOCK_UPTIME_RAW) * 1000
                effect = launch(variant)
                wait(effect, 6, f"{variant['name']} completion")
                effect_log = effect.stderr.read() if effect.stderr else ""
                effect_rc = effect.returncode
                effect = None
                wait(recorder, 5, "recorder completion")
                gpu_samples = gpu.stop()
                frames, error = recorder.communicate()
                if recorder.returncode:
                    raise RuntimeError(f"recorder failed: {error.strip()}")
                recorder = None
                check_input()
                rows = [
                    [float(value) for value in line.split()]
                    for line in frames.splitlines()
                    if len(line.split()) == 11
                ]
                settled_space = query("--spaces", "--space")["index"]
                metrics, invalid = analyze(rows, request_ms)
                trial: dict[str, object] = dict(
                    round=round_index,
                    variant=variant["name"],
                    kind=variant["kind"],
                    rc=effect_rc,
                    reached=settled_space == args.destination,
                    request_ms=round(request_ms, 3),
                    gpu_max=max(gpu_samples, default=-1),
                    gpu_mean=round(statistics.fmean(gpu_samples), 1) if gpu_samples else -1,
                )
                if invalid or effect_rc or not trial["reached"]:
                    trial["invalid"] = invalid or (
                        "effect failed" if effect_rc else "destination not reached"
                    )
                elif metrics is not None:
                    trial.update(metrics)
                    # Probes report when their work began, after process launch
                    # and AppKit setup that a running daemon does not repeat.
                    marker = next((line.split()[-1] for line in effect_log.splitlines()
                                   if "work_begin_uptime_ms" in line), None)
                    if marker is not None:
                        work, _ = analyze(rows, float(marker))
                        if work is not None:
                            trial["work_begin_ms"] = round(float(marker) - request_ms, 1)
                            trial["first_change_from_work_ms"] = work["first_change_ms"]
                            trial["settled_from_work_ms"] = work["settled_ms"]
                if effect_log:
                    trial["log"] = effect_log.strip().splitlines()
                stem = f"{round_index:02d}-{variant['name']}"
                (run / f"{stem}.frames.txt").write_text(frames)
                with open(run / "trials.jsonl", "a") as handle:
                    handle.write(json.dumps(trial) + "\n")
                print(
                    json.dumps({k: v for k, v in trial.items() if k != "log"}),
                    flush=True,
                )
                results.append(trial)
    except InterruptedError as error:
        print(f"ABORT: {error}", file=sys.stderr, flush=True)
        interrupted = True
        code = 2
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"ABORT: {error}", file=sys.stderr, flush=True)
        code = 2
    finally:
        for process in (effect, recorder):
            if process is not None:
                if process.poll() is None:
                    process.kill()
                process.communicate()
        if interrupted:
            print("USER INPUT: leave the user's new focus in place", flush=True)
        else:
            try:
                command("space", "--navigate", "focus", original, 1, 0)
                deadline = time.monotonic() + 5
                while query("--spaces", "--space")["index"] != original:
                    if time.monotonic() > deadline:
                        raise TimeoutError("restoring the starting Desktop")
                    time.sleep(0.05)
                if window:
                    command("window", "--focus", window)
                print(f"RESTORED: space={original} window={window}", flush=True)
            except (OSError, RuntimeError, subprocess.SubprocessError) as error:
                print(f"RESTORATION FAILED: {error}", file=sys.stderr, flush=True)
                code = 2

    summary = {}
    for variant in variants:
        valid = [
            t for t in results if t["variant"] == variant["name"] and "invalid" not in t
        ]
        entry: dict[str, object] = dict(
            valid=len(valid),
            invalid=sum(1 for t in results if t["variant"] == variant["name"])
            - len(valid),
        )
        for key in ("first_change_ms", "settled_ms", "first_change_from_work_ms", "settled_from_work_ms",
                    "gpu_max", "gpu_mean"):
            values = sorted(t[key] for t in valid if key in t)
            if values:
                entry[key] = dict(
                    median=statistics.median(values), min=values[0], max=values[-1]
                )
        summary[variant["name"]] = entry
    (run / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2), flush=True)
    return code


if __name__ == "__main__":
    sys.exit(main())
