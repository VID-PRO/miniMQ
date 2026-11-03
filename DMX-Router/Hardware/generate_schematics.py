#!/usr/bin/env python3
# ------------------------------------------------------------------
#  DMX-Router 4 -> 16  --  schematic generator
#
#  Writes the four SVG sheets into this directory:
#      00-system-overview.svg
#      01-input-board.svg
#      02-output-board.svg
#      03-controller-board.svg
#
#  Usage:
#      python generate_schematics.py
#
#  The sheets are drawn by sheet_*.py; shared symbols live in
#  schematic_lib.py.  No external dependencies.
# ------------------------------------------------------------------

import os

from sheet_overview import sheet_overview
from sheet_input import sheet_input
from sheet_output import sheet_output
from sheet_controller import sheet_controller


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    jobs = [
        ("00-system-overview.svg", sheet_overview),
        ("01-input-board.svg", sheet_input),
        ("02-output-board.svg", sheet_output),
        ("03-controller-board.svg", sheet_controller),
    ]
    for name, fn in jobs:
        path = os.path.join(here, name)
        fn(path)
        print(f"wrote {name}")


if __name__ == "__main__":
    main()
