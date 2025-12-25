#!/usr/bin/env python3

import argparse
import subprocess as sp
import os
import sys

from pathlib import Path

def parse_arguments() -> argparse.Namespace:

    parser = argparse.ArgumentParser(
                 epilog=__doc__,
                 formatter_class=argparse.RawDescriptionHelpFormatter)

    parser.add_argument('kernel', help='kernel binary location', type=Path)
    parser.add_argument('section_size', help='size of the code section reserved for the kernel symbol table', type=int)

    return parser.parse_args()


def encode(n: int):
    return n.to_bytes(4, "little")


def extract_symbol_table(kernel: Path) -> Path:
    """ Write out the kernel's symbol table into a file.
        @argument kernel Path to the kernel's binary code
        @return The path to the symbol table file
    """

    print(f"Extracting symbol table from {kernel}")

    # nm -na kernel.bin | grep ' [tT] ' | grep -v '.L[0-9]*' > kernel.map
    symbols = sp.check_output(["nm", "-n", str(kernel)])
    if not symbols:
        print(f"ERROR: Unable to dump symbols from {kernel}", file=sys.stderr)
        sys.exit(1)

    # isolate function symbols
    symbols = symbols.decode().strip().split("\n")
    function_symbols = [
        symbol.split()[::2]
        for symbol in symbols
        if symbol.split()[1] in ["t", "T"] or symbol.split()[2] in ["_kernel_code_start", "_kernel_code_end"] # not optimal but meh
    ]

    function_symbols.pop(0) # remove _kernel_start
    function_symbols.insert(1, function_symbols.pop(0)) # place _kernel_code_start first

    # Write to file
    map_file = kernel.parent / f"{kernel.stem}.map"
    with open(map_file, "wb") as file:
        file.write(encode(len(function_symbols)))
        for symbol in function_symbols:
            address = int(symbol[0], base=16)
            name = symbol[1] + chr(0)
            file.write(encode(len(name) + 2 * 4))   # Total size of the symbol in bytes
            file.write(encode(address))             # Address of the symbol
            file.write(name.encode("utf-8"))        # Name of the symbol (null-terminated)

    print(f"Saved {len(function_symbols)} kernel function symbols inside {map_file}")

    return map_file


def generate_binary_files(kernel: Path, symbol_table: Path):
    """ Split the kernel binary file into two:

        * <kernel>: Stripped kernel code
        * <kernel>.sym: Contains debug symbols used by GDB

        The symbols contained inside the symbol table are also inserted
        inside the code ELF file's appropriate section.
    """

    print("Generating debug and stripped binary files")
    sp.check_call(["objcopy", "--only-keep-debug", str(kernel), str(kernel.with_suffix(".sym"))])
    sp.check_call(["objcopy", "--strip-debug", str(kernel)])
    sp.check_call(["objcopy", "--update-section", f".kernel_symbols={symbol_table}", str(kernel)])
    sp.check_call(["objcopy", f"--add-gnu-debuglink={kernel.with_suffix('.sym')}", str(kernel)])


def main():

    args = parse_arguments()

    if not args.kernel.exists():
        print(f"ERROR: No such file or directory '{args.kernel}'", file=sys.stderr)
        sys.exit(1)

    symbol_table = extract_symbol_table(args.kernel)
    size = os.stat(symbol_table).st_size
    if size > args.section_size:
        print(f"ERROR: Symbol table is too big ({size}B > {args.section_size}B)", file=sys.stderr)
        sys.exit(1)

    generate_binary_files(args.kernel, symbol_table)

if __name__ == "__main__":
    sys.exit(main())
