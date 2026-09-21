#!/usr/bin/env python3
# Copyright (c) 2026 Alan Mimms / ESPirate
# SPDX-License-Identifier: Apache-2.0

"""
generate_favicon_header.py

Converts web/favicon.png into a C header file (favicon_image.h) containing
an exact byte array representation. Invoked automatically by CMake during
the build process whenever web/favicon.png changes.
"""

import os
import sys

def generate_header(image_path: str, header_path: str) -> None:
    if not os.path.isfile(image_path):
        print(f"[generate_favicon_header] Error: Favicon file not found: {image_path}", file=sys.stderr)
        sys.exit(1)

    with open(image_path, "rb") as f:
        data = f.read()

    size = len(data)

    lines = []
    lines.append("/*")
    lines.append(" * Copyright (c) 2026 Alan Mimms / ESPirate")
    lines.append(" * SPDX-License-Identifier: Apache-2.0")
    lines.append(" *")
    lines.append(" * AUTOMATICALLY GENERATED FILE -- DO NOT EDIT DIRECTLY!")
    lines.append(f" * Source file : {image_path}")
    lines.append(f" * Payload size: {size} bytes")
    lines.append(" * To modify favicon, edit web/favicon.png and rebuild.")
    lines.append(" */")
    lines.append("")
    lines.append("#ifndef ESPIRATE_FAVICON_IMAGE_H_")
    lines.append("#define ESPIRATE_FAVICON_IMAGE_H_")
    lines.append("")
    lines.append("#include <stddef.h>")
    lines.append("")
    lines.append("static const unsigned char ESPIRATE_FAVICON_PNG[] = {")

    for i in range(0, size, 16):
        chunk = data[i:i + 16]
        hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"    {hex_str},")

    lines.append("};")
    lines.append("")
    lines.append(f"#define ESPIRATE_FAVICON_PNG_LEN {size}")
    lines.append("")
    lines.append("#endif /* ESPIRATE_FAVICON_IMAGE_H_ */")
    lines.append("")

    new_content = "\n".join(lines)

    # If destination file already has identical content, do not touch it.
    if os.path.isfile(header_path):
        with open(header_path, "r", encoding="utf-8", errors="ignore") as f:
            existing = f.read()
        if existing == new_content:
            return

    os.makedirs(os.path.dirname(os.path.abspath(header_path)), exist_ok=True)
    with open(header_path, "w", encoding="utf-8") as f:
        f.write(new_content)

    print(f"[generate_favicon_header] Wrote {size} bytes to {header_path}")

def main() -> None:
    if len(sys.argv) < 3:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        project_dir = os.path.dirname(script_dir)
        image_path = os.path.join(project_dir, "web", "favicon.png")
        header_path = os.path.join(project_dir, "build", "include", "favicon_image.h")
    else:
        image_path = sys.argv[1]
        header_path = sys.argv[2]

    generate_header(image_path, header_path)

if __name__ == "__main__":
    main()
