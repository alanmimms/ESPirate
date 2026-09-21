#!/usr/bin/env python3
# Copyright (c) 2026 Alan Mimms / ESPirate
# SPDX-License-Identifier: Apache-2.0

"""
generate_doc_header.py

Converts doc/howto.md into a C header file (howto_doc.h) containing
an exact byte array representation. Invoked automatically by CMake during
the build process whenever doc/howto.md changes.
"""

import os
import sys

def generate_header(doc_path: str, header_path: str) -> None:
    if not os.path.isfile(doc_path):
        print(f"[generate_doc_header] Error: Document file not found: {doc_path}", file=sys.stderr)
        sys.exit(1)

    with open(doc_path, "rb") as f:
        data = f.read()

    size = len(data)

    lines = []
    lines.append("/*")
    lines.append(" * Copyright (c) 2026 Alan Mimms / ESPirate")
    lines.append(" * SPDX-License-Identifier: Apache-2.0")
    lines.append(" *")
    lines.append(" * AUTOMATICALLY GENERATED FILE -- DO NOT EDIT DIRECTLY!")
    lines.append(f" * Source file : {doc_path}")
    lines.append(f" * Payload size: {size} bytes")
    lines.append(" * To modify documentation, edit doc/howto.md and rebuild.")
    lines.append(" */")
    lines.append("")
    lines.append("#ifndef ESPIRATE_HOWTO_DOC_H_")
    lines.append("#define ESPIRATE_HOWTO_DOC_H_")
    lines.append("")
    lines.append("#include <stddef.h>")
    lines.append("")
    lines.append("static const char ESPIRATE_HOWTO_MD[] = {")

    for i in range(0, size, 16):
        chunk = data[i:i + 16]
        hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"    {hex_str},")

    lines.append("    0x00")
    lines.append("};")
    lines.append("")
    lines.append("#define ESPIRATE_HOWTO_MD_LEN (sizeof(ESPIRATE_HOWTO_MD) - 1)")
    lines.append("")
    lines.append("#endif /* ESPIRATE_HOWTO_DOC_H_ */")
    lines.append("")

    new_content = "\n".join(lines)

    # If the destination file already has identical content, do not touch it.
    if os.path.isfile(header_path):
        with open(header_path, "r", encoding="utf-8", errors="replace") as f:
            old_content = f.read()
        if old_content == new_content:
            print(f"[generate_doc_header] {header_path} is already up to date ({size} bytes).")
            return

    os.makedirs(os.path.dirname(os.path.abspath(header_path)), exist_ok=True)
    with open(header_path, "w", encoding="utf-8") as f:
        f.write(new_content)

    print(f"[generate_doc_header] Generated {header_path} from {doc_path} ({size} bytes).")

def main():
    if len(sys.argv) >= 3:
        doc_file = sys.argv[1]
        header_file = sys.argv[2]
    elif len(sys.argv) == 2:
        doc_file = sys.argv[1]
        header_file = "build/include/howto_doc.h"
    else:
        doc_file = "doc/howto.md"
        header_file = "build/include/howto_doc.h"

    generate_header(doc_file, header_file)

if __name__ == "__main__":
    main()
