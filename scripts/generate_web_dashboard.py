#!/usr/bin/env python3
# Copyright (c) 2026 Alan Mimms / ESPirate
# SPDX-License-Identifier: Apache-2.0

"""
generate_web_dashboard.py

Converts a clean, human-editable HTML file (e.g. web/index.html) into
a C header file (web_dashboard.h) containing an exact byte array representation.
Invoked automatically by CMake during the build process whenever web/index.html changes.
"""

import os
import sys

def generate_header(html_path: str, header_path: str) -> None:
    if not os.path.isfile(html_path):
        print(f"[generate_web_dashboard] Error: HTML file not found: {html_path}", file=sys.stderr)
        sys.exit(1)

    with open(html_path, "rb") as f:
        data = f.read()

    size = len(data)

    lines = []
    lines.append("/*")
    lines.append(" * Copyright (c) 2026 Alan Mimms / ESPirate")
    lines.append(" * SPDX-License-Identifier: Apache-2.0")
    lines.append(" *")
    lines.append(" * AUTOMATICALLY GENERATED FILE -- DO NOT EDIT DIRECTLY!")
    lines.append(f" * Source file : {html_path}")
    lines.append(f" * Payload size: {size} bytes")
    lines.append(" * To modify the dashboard, edit web/index.html and rebuild.")
    lines.append(" */")
    lines.append("")
    lines.append("#ifndef ESPIRATE_WEB_DASHBOARD_H_")
    lines.append("#define ESPIRATE_WEB_DASHBOARD_H_")
    lines.append("")
    lines.append("#include <stddef.h>")
    lines.append("")
    lines.append("static const char ESPIRATE_DASHBOARD_HTML[] = {")

    for i in range(0, size, 16):
        chunk = data[i:i + 16]
        hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"    {hex_str},")

    lines.append("    0x00")
    lines.append("};")
    lines.append("")
    lines.append("#define ESPIRATE_DASHBOARD_HTML_LEN (sizeof(ESPIRATE_DASHBOARD_HTML) - 1)")
    lines.append("")
    lines.append("#endif /* ESPIRATE_WEB_DASHBOARD_H_ */")
    lines.append("")

    new_content = "\n".join(lines)

    # If the destination file already has identical content, do not touch it.
    # This prevents triggering unnecessary recompilations in Ninja.
    if os.path.isfile(header_path):
        with open(header_path, "r", encoding="utf-8", errors="replace") as f:
            old_content = f.read()
        if old_content == new_content:
            print(f"[generate_web_dashboard] {header_path} is already up to date ({size} bytes).")
            return

    os.makedirs(os.path.dirname(os.path.abspath(header_path)), exist_ok=True)
    with open(header_path, "w", encoding="utf-8") as f:
        f.write(new_content)

    print(f"[generate_web_dashboard] Generated {header_path} from {html_path} ({size} bytes).")

def main():
    if len(sys.argv) >= 3:
        html_file = sys.argv[1]
        header_file = sys.argv[2]
    elif len(sys.argv) == 2:
        html_file = sys.argv[1]
        header_file = "build/include/web_dashboard.h"
    else:
        html_file = "web/index.html"
        header_file = "build/include/web_dashboard.h"

    generate_header(html_file, header_file)

if __name__ == "__main__":
    main()
