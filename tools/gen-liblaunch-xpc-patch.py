#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PATCHDIR = ROOT / 'mk' / 'patches' / 'launchd'
PATCH_PATH = PATCHDIR / '0006-liblaunch-xpc-bridge.patch'
