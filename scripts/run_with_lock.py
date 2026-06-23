#!/usr/bin/env python3
"""
Run a Python script under an advisory file lock.
"""

import argparse
import fcntl
import os
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description="Run a Python script under a file lock")
    parser.add_argument("--lock", required=True, help="Path to lock file")
    parser.add_argument("--script", required=True, help="Python script to run")
    parser.add_argument("script_args", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    script_args = args.script_args
    if script_args and script_args[0] == "--":
        script_args = script_args[1:]

    os.makedirs(os.path.dirname(os.path.abspath(args.lock)), exist_ok=True)
    with open(args.lock, "w", encoding="utf-8") as lock_file:
        fcntl.flock(lock_file, fcntl.LOCK_EX)
        return subprocess.run([sys.executable, args.script] + script_args).returncode


if __name__ == "__main__":
    sys.exit(main())
