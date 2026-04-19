"""
PlatformIO pre-build script: inject git commit hash and build timestamp.
Adds three defines:
  BUILD_GIT_HASH  - short commit hash (e.g. "a1b2c3d"), or "unknown"
  BUILD_DATE      - date string (e.g. "2026-04-19")
  BUILD_TIME      - time string (e.g. "14:32:01")
"""

import subprocess
import sys
from datetime import datetime, timezone

Import("env")

def get_git_hash(project_dir):
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            text=True, stderr=subprocess.PIPE, cwd=project_dir
        ).strip()
    except Exception:
        return "unknown"

project_dir = env.subst("$PROJECT_DIR")
git_hash = get_git_hash(project_dir)
now = datetime.now(timezone.utc)
build_date = now.strftime("%Y-%m-%d")
build_time = now.strftime("%H:%M:%S")

env.Append(CPPDEFINES=[
    ("BUILD_GIT_HASH", f'\\"{git_hash}\\"'),
    ("BUILD_DATE",     f'\\"{build_date}\\"'),
    ("BUILD_TIME",     f'\\"{build_time}\\"'),
])

print(f"build_info.py: hash={git_hash} date={build_date} time={build_time}")
