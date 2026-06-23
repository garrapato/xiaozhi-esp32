#!/usr/bin/env python3
"""
Build srmodels.bin from the current sdkconfig.
"""

import argparse
import os
import shutil
import sys
import tempfile

from build_default_assets import (
    get_multinet_model_paths,
    get_wakenet_model_paths,
    process_sr_models,
    read_multinet_from_sdkconfig,
    read_wake_word_type_from_sdkconfig,
    read_wakenet_from_sdkconfig,
)


def main():
    parser = argparse.ArgumentParser(description="Build srmodels.bin from sdkconfig")
    parser.add_argument("--sdkconfig", required=True, help="Path to sdkconfig")
    parser.add_argument("--esp_sr_model_path", required=True, help="Path to ESP-SR model directory")
    parser.add_argument("--output", required=True, help="Output srmodels.bin path")
    args = parser.parse_args()

    wake_word_config = read_wake_word_type_from_sdkconfig(args.sdkconfig)
    wakenet_model_names = read_wakenet_from_sdkconfig(args.sdkconfig)
    multinet_model_names = read_multinet_from_sdkconfig(args.sdkconfig)

    wakenet_model_paths = []
    multinet_model_paths = []

    if wake_word_config["use_esp_wake_word"] or wake_word_config["use_afe_wake_word"]:
        wakenet_model_paths = get_wakenet_model_paths(wakenet_model_names, args.esp_sr_model_path)

    if wake_word_config["use_custom_wake_word"]:
        if not multinet_model_names:
            print("Error: USE_CUSTOM_WAKE_WORD is enabled but no multinet models are selected in sdkconfig")
            return 1
        multinet_model_paths = get_multinet_model_paths(multinet_model_names, args.esp_sr_model_path)

    if not wakenet_model_paths and not multinet_model_paths:
        print("Error: No SR models selected for the configured wake word type")
        return 1

    output_path = os.path.abspath(args.output)
    output_dir = os.path.dirname(output_path)
    os.makedirs(output_dir, exist_ok=True)

    temp_dir = tempfile.mkdtemp(prefix=".build-", dir=output_dir)
    try:
        temp_output_dir = os.path.join(temp_dir, "out")
        os.makedirs(temp_output_dir, exist_ok=True)
        srmodels = process_sr_models(wakenet_model_paths, multinet_model_paths, temp_dir, temp_output_dir)
        if srmodels is None:
            return 1

        generated = os.path.join(temp_output_dir, "srmodels.bin")
        temp_output = f"{output_path}.tmp.{os.getpid()}"
        shutil.copy2(generated, temp_output)
        os.replace(temp_output, output_path)
    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)

    print(f"Generated sdkconfig SR models: {output_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
