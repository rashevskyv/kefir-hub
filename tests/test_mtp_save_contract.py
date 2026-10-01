#!/usr/bin/env python3
"""
Compiler-free source contract and synthetic behavioral regression test for MTP save layout.

NOTE ON SCOPE AND COVERAGE:
This test executes static source contracts and a Python behavioral model of the
allocation, naming algorithms, and fail-closed read-only proxy enforcement.
It validates algorithmic invariance, collision resolution, boundary conditions,
mutation rejection, and C++ source patterns. It does NOT execute the C++ binary,
libnx/libhaze runtime, Windows MTP stack, or target Nintendo Switch hardware.
"""

import itertools
import json
import os
import re
import sys

def check(condition, msg):
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)


# ==============================================================================
# Behavioral Model (Algorithmic Specification & Synthetic Proxy Model)
# ==============================================================================

from contract_fixtures.mtp_save_scenarios import test_behavioral_model

if __name__ == "__main__":
    test_behavioral_model()
    print("ALL MTP SAVE CONTRACT TESTS PASSED.")
