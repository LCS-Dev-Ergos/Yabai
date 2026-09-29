#!/usr/bin/env python3
"""Compile each daemon area header alone to catch missing dependencies."""

from pathlib import Path
import os
import subprocess
import sys


root = Path(__file__).resolve().parents[1]
headers = [
    'sa/sa.h', 'displays/display.h', 'displays/display_manager.h',
    'applications/application.h', 'applications/process_manager.h',
    'spaces/space.h', 'spaces/view.h', 'spaces/space_manager.h',
    'windows/rule.h', 'windows/window.h', 'windows/window_manager.h',
    'events/event_loop.h', 'events/event_signal.h', 'events/mission_control.h',
    'events/mouse_handler.h', 'events/workspace.h', 'ipc/message.h',
    'effects/display.h', 'effects/window_fade.h', 'effects/snapshot.h',
    'navigation/topology.h', 'navigation/admission.h',
    'navigation/schedule.h', 'navigation/step.h',
    'navigation/activation.h', 'navigation/command.h', 'hooks.h',
]
environment = os.environ.copy()
temporary = root / 'build/header-tmp'
temporary.mkdir(parents=True, exist_ok=True)
environment['TMPDIR'] = str(temporary)
compiler = environment.get('CLANG', '/usr/bin/clang')
failed = 0
for header in headers:
    result = subprocess.run(
        [compiler, '-x', 'objective-c', '-std=c11', '-fsyntax-only',
         '-include', str(root / 'src' / header), '/dev/null'],
        cwd=root, env=environment, capture_output=True, text=True,
    )
    if result.returncode:
        failed += 1
        print(f'{header}:\n{result.stderr}', file=sys.stderr)

print(f'{len(headers) - failed}/{len(headers)} standalone area headers pass')
sys.exit(bool(failed))
