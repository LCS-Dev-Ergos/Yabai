#!/usr/bin/env python3
"""Compare presented bar/Desktop frames; restore focus unless interrupted by input.

This is an opt-in live diagnostic, not a CI test or a general visual-quality
score. A large luminance excursion catches the lcs.21 bar disappearance; a
pass alone does not establish correct focus or smooth motion. --static-desktop
adds a bounded visibility check for bright icons on matching empty Desktops.
"""

import argparse
import ctypes
import json
import math
from pathlib import Path
import selectors
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=int)
    parser.add_argument("destination", type=int)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--yabai", default="/run/current-system/sw/bin/yabai")
    parser.add_argument("--effects", nargs="+", choices=("crossfade", "fade", "subtle", "none", "snapshot"), default=["crossfade"])
    parser.add_argument("--snapshot-probe", type=Path, help="Opt-in helper compiled from snapshot_probe.m")
    parser.add_argument("--snapshot-warm", action="store_true", help="Measure three preparations before the helper's switch")
    parser.add_argument("--static-desktop", action="store_true", help="Empty-to-empty Desktop/icon regression; requires --images and Pillow")
    parser.add_argument("--duration", type=float, default=0.25)
    parser.add_argument("--images", action="store_true", help="Save PNGs for visual inspection; affects capture performance")
    args = parser.parse_args()
    if not math.isfinite(args.duration) or not 0 <= args.duration <= 1:
        parser.error("duration must be finite and in [0, 1]")
    if "snapshot" in args.effects and (not args.snapshot_probe or not args.duration):
        parser.error("snapshot requires --snapshot-probe and a positive duration")
    if args.static_desktop and not args.images:
        parser.error("--static-desktop requires --images")

    cg = ctypes.CDLL("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics")
    since = cg.CGEventSourceSecondsSinceLastEventType
    since.argtypes = [ctypes.c_int32, ctypes.c_uint32]
    since.restype = ctypes.c_double

    def idle():
        return min(since(1, event) for event in (10, 12, 1, 3, 25))

    def command(*tokens):
        return subprocess.check_output([args.yabai, "-m", *map(str, tokens)], text=True, timeout=5)

    def query(*tokens):
        return json.loads(command("query", *tokens))

    spaces = {space["index"]: space for space in query("--spaces")}
    source = spaces.get(args.source)
    destination = spaces.get(args.destination)
    if not source or not destination or args.source == args.destination:
        parser.error("choose two existing, distinct Desktops")
    if source["display"] != destination["display"] or source["is-native-fullscreen"] or destination["is-native-fullscreen"]:
        parser.error("choose two ordinary Desktops on the same display")
    if args.static_desktop and (source["windows"] or destination["windows"]):
        parser.error("--static-desktop requires two empty Desktops with matching wallpaper/icons")
    display = next(item["id"] for item in query("--displays") if item["index"] == source["display"])

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

    result_code = 0
    process = None
    effect_process = None
    interrupted = False
    try:
        for effect in args.effects:
            check_input()
            command("space", "--navigate", "focus", args.source, 1, 0)
            pause(0.8)
            folder = args.output.resolve() / f"{args.source}-{args.destination}-{effect}-{time.time_ns()}"
            folder.mkdir(parents=True)
            capture = [str(args.capture.resolve()), str(display), "1.5"]
            if args.images:
                capture.append(str(folder))
            process = subprocess.Popen(capture, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

            with selectors.DefaultSelector() as selector:
                selector.register(process.stderr, selectors.EVENT_READ)
                deadline = time.monotonic() + 12
                while not selector.select(timeout=0.1):
                    check_input()
                    if time.monotonic() > deadline:
                        raise TimeoutError("capture startup")
                message = process.stderr.readline()
            if not message.startswith("capturing"):
                raise RuntimeError(f"capture failed: {message.strip()}")

            pause(0.15)
            request = time.clock_gettime(time.CLOCK_UPTIME_RAW) * 1000
            alpha = {"crossfade": "crossfade", "fade": "0.7", "subtle": "0.95", "none": "1", "snapshot": "1"}[effect]
            duration = 0 if effect == "none" else args.duration
            if effect == "snapshot":
                probe = [str(args.snapshot_probe.resolve()), str(display), str(args.destination), str(duration)]
                if args.snapshot_warm:
                    probe.append("--warm")
                effect_process = subprocess.Popen(probe)
                deadline = time.monotonic() + 6
                while effect_process.poll() is None:
                    pause(0.02)
                    if time.monotonic() > deadline:
                        raise TimeoutError("snapshot probe completion")
                if effect_process.returncode:
                    raise RuntimeError("snapshot probe did not animate (capture unavailable, timeout or switch failed)")
                effect_process = None
            else:
                command("space", "--navigate", "focus", args.destination, alpha, duration)
            deadline = time.monotonic() + 5
            while process.poll() is None:
                pause(0.02)
                if time.monotonic() > deadline:
                    raise TimeoutError("capture completion")
            output, error = process.communicate()
            if process.returncode:
                raise RuntimeError(f"capture failed: {error.strip()}")
            process = None
            check_input()

            rows = [[float(value) for value in line.split()] for line in output.splitlines() if len(line.split()) == 11]
            before = [row for row in rows if row[0] <= request]
            after = [row for row in rows if request < row[0] <= request + 1200]
            if not before or not after:
                raise RuntimeError("capture has no baseline or post-switch frames")
            settled = query("--spaces", "--space")["index"]
            if settled != args.destination:
                raise RuntimeError(f"destination mismatch: expected {args.destination}, observed {settled}")

            low = min(before[-1][7], after[-1][7])
            high = max(before[-1][7], after[-1][7])
            excursion = max(max(low - row[7], row[7] - high, 0) for row in after)
            result = dict(effect=effect, duration=duration, pair=[args.source, args.destination], display=display,
                          request_ms=request, bar_start=before[-1][7], bar_end=after[-1][7],
                          bar_excursion=round(excursion, 2), verdict="FAIL" if excursion > 3 else "PASS",
                          frames=len(rows), images=args.images, directory=str(folder))
            if args.static_desktop:
                from PIL import Image
                files = sorted(folder.glob("*.png"))
                baseline = Image.open(files[0]).convert("RGB")
                # Stable bright Desktop content, below the bar and away from
                # the screen edges. Alignment errors also fail this check.
                pixels = baseline.load()
                mask = [(x, y) for y in range(40, baseline.height - 30)
                        for x in range(30, baseline.width - 30) if min(pixels[x, y]) > 190]
                if len(mask) < 100:
                    raise RuntimeError("not enough bright Desktop content for an icon check")
                icon_values = []
                for file in files:
                    with Image.open(file) as png:
                        rgb = png.convert("RGB")
                        icon_values.append(sum(sum(rgb.getpixel(xy)) for xy in mask) / (3 * len(mask)))
                icon_loss = max(icon_values[0] - value for value in icon_values)
                full_excursion = max(abs(row[2] - before[-1][2]) for row in after)
                result.update(icon_start=icon_values[0], icon_loss=icon_loss, desktop_excursion=full_excursion)
                if icon_loss > 5 or full_excursion > 3:
                    result["verdict"] = "FAIL"
            (folder / "frames.txt").write_text(output)
            (folder / "result.json").write_text(json.dumps(result, indent=2) + "\n")
            print(json.dumps(result), flush=True)
            result_code = max(result_code, int(result["verdict"] == "FAIL"))
    except InterruptedError as error:
        print(f"ABORT: {error}", file=sys.stderr, flush=True)
        interrupted = True
        result_code = 2
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"ABORT: {error}", file=sys.stderr, flush=True)
        result_code = 2
    finally:
        if effect_process is not None:
            if effect_process.poll() is None:
                effect_process.kill()
            effect_process.wait()
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
                    if query("--windows", "--window").get("id") != window:
                        raise RuntimeError("starting window did not regain focus")
                print(f"RESTORED: space={original} window={window}", flush=True)
            except (OSError, RuntimeError, subprocess.SubprocessError) as error:
                print(f"RESTORATION FAILED: {error}", file=sys.stderr, flush=True)
                result_code = 2

    return result_code


if __name__ == "__main__":
    sys.exit(main())
