#!/usr/bin/env python3
#
# Convert the SBL config TOML (board/<board>/<board>-sbl-config.toml) into
# the JSON file consumed by the SBL CMake build.
#
# Based on board/elvees/gen_build_config.py from the ELVEES
# mcom03-buildroot-external tree, but uses the standard library tomllib
# module instead of the third-party "toml" package.
#
# Copyright (C) 2026 INMYS

import argparse
import json
import tomllib
from pathlib import Path


def generate_json(input_file, output_file=None):
    with open(input_file, "rb") as sbl_config:
        toml_data = tomllib.load(sbl_config)

    json_data = []
    for entry in toml_data["configs"]:
        fragments = entry.get("ddrinit_fragments", [])
        suffix = f"-{'-'.join(fragments)}" if fragments else ""
        ddrinit_name = f"{entry['ddrinit_defconfig']}{suffix}"

        for boot_type in entry["boot_types"]:
            if boot_type not in ("xip", "bootrom"):
                continue

            new_entry = {}
            new_entry["ddrinit_defconfig"] = entry["ddrinit_defconfig"]
            new_entry["ddrinit_name"] = ddrinit_name
            new_entry["boot_type"] = boot_type
            new_entry["uboot_dtb"] = entry["uboot_dtb"]
            new_entry["ddrinit_uboot_dtb"] = f"{ddrinit_name}-{entry['uboot_dtb']}"

            joined_fragments = ":".join(fragments)

            if boot_type == "xip":
                new_entry["ddrinit_build_fragments"] = joined_fragments
                new_entry["sbl_xip_name"] = (
                    f"sbl-xip-{new_entry['ddrinit_uboot_dtb']}"
                )
            else:  # bootrom
                prefix = f"{joined_fragments}:" if fragments else ""
                new_entry["ddrinit_name"] += "-bootrom"
                new_entry["ddrinit_build_fragments"] = f"{prefix}bootrom"

            json_data.append(new_entry)

    if not output_file:
        output_file = str(Path(input_file).with_suffix("")) + ".json"

    with open(output_file, "w") as sbl_config_json:
        json.dump(json_data, sbl_config_json, indent=4)

    return output_file


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "buildroot_target_dir",
        nargs="?",
        help="Automatically passed by Buildroot, unused",
    )
    parser.add_argument("-i", "--input", required=True,
                        help="path to the input SBL config TOML file")
    parser.add_argument("-o", "--output", default=None,
                        help="path to the output JSON file")
    args = parser.parse_args()

    output = generate_json(args.input, args.output)
    print(f"SBL config: {output}")


if __name__ == "__main__":
    main()
