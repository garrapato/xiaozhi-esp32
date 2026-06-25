#!/usr/bin/env python3
"""
Copy board-local emote assets into the external assets build directory.
"""

import argparse
import os
import shutil


IGNORED_NAMES = {".DS_Store"}
IGNORED_PREFIXES = ("._",)


def ignore_metadata(directory, names):
    del directory
    return [
        name for name in names
        if name in IGNORED_NAMES or name.startswith(IGNORED_PREFIXES)
    ]


def main():
    parser = argparse.ArgumentParser(description="Stage board-local emote assets")
    parser.add_argument("--source", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--stamp", required=True)
    args = parser.parse_args()

    os.makedirs(args.output, exist_ok=True)
    shutil.copytree(args.source, args.output, dirs_exist_ok=True, ignore=ignore_metadata)

    os.makedirs(os.path.dirname(os.path.abspath(args.stamp)), exist_ok=True)
    with open(args.stamp, "a", encoding="utf-8"):
        os.utime(args.stamp, None)


if __name__ == "__main__":
    main()
