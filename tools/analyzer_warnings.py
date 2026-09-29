#!/usr/bin/env python3
"""Normalize clang analyzer warning paths across daemon translation units."""

import os
import re
import sys


warning = re.compile(r"^(.+?):[0-9]+:[0-9]+: warning: (.*)$")
for line in sys.stdin:
    match = warning.match(line)
    if match:
        path = os.path.normpath(match.group(1))
        print(f"{path}: {match.group(2)}")
