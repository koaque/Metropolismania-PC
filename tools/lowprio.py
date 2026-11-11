"""Runs a command at below normal priority, with everything it starts (Windows passes a below-normal class on to child
processes), so heavy work outside pipeline.py (a compile check, a headless test run from a copied runner) leaves the
machine responsive for other work. pipeline.py lowers its own compile and test steps the same way.

usage: python tools/lowprio.py COMMAND [ARGS...]
"""
import ctypes
import os
import subprocess
import sys


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    if os.name == "nt":
        kernel32 = ctypes.windll.kernel32
        kernel32.GetCurrentProcess.restype = ctypes.c_void_p
        kernel32.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        kernel32.SetPriorityClass(kernel32.GetCurrentProcess(), 0x00004000)  # BELOW_NORMAL_PRIORITY_CLASS
    else:
        os.nice(10)
    return subprocess.call(sys.argv[1:])


if __name__ == "__main__":
    sys.exit(main())
