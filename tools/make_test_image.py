"""Build the relocated memory image the test harness runs against.

The harness needs the Steam gta-sa.exe loaded at a fixed base (0x30000000), with relocations
applied, as Windows would load it. Game files are not part of this repository: point this
script at your own copy.

    pip install pefile
    python tools/make_test_image.py "C:/Program Files (x86)/Steam/steamapps/common/Grand Theft Auto San Andreas/gta-sa.exe" build/steam_reloc.bin
"""
import hashlib
import os
import sys

import pefile

HARNESS_BASE = 0x30000000
TESTED_MD5 = "5bfd4dd83989a8264de4b8e771f237fd"


def md5_of(path):
    with open(path, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def relocated_image(path, base):
    pe = pefile.PE(path)
    pe.relocate_image(base)
    return pe.get_memory_mapped_image(ImageBase=base)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    exe, out = sys.argv[1], sys.argv[2]

    digest = md5_of(exe)
    if digest != TESTED_MD5:
        print(f"warning: {exe} has md5 {digest}; addresses were mapped for {TESTED_MD5}")

    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "wb") as f:
        f.write(relocated_image(exe, HARNESS_BASE))
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
