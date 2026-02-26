#!/usr/bin/env python3
"""Generate seed inputs for SymCC MPI benchmark targets."""

import os
import struct
import random

SEED_DIR = os.path.join(os.path.dirname(__file__), "seeds")
os.makedirs(SEED_DIR, exist_ok=True)

random.seed(42)  # Reproducible


def write_seed(name, data):
    path = os.path.join(SEED_DIR, name)
    with open(path, "wb") as f:
        f.write(data)
    print(f"  {name}: {len(data)} bytes")


print("Generating seeds for maze (16 bytes each):")
# Direction encoding: 0=up, 1=down, 2=left, 3=right
# Seed 1: all right (baseline)
write_seed("maze_001_all_right", bytes([3] * 16))
# Seed 2: all down
write_seed("maze_002_all_down", bytes([1] * 16))
# Seed 3: down-right zigzag
write_seed("maze_003_zigzag", bytes([1, 3] * 8))
# Seed 4: random
write_seed("maze_004_random1", bytes([random.randint(0, 3) for _ in range(16)]))
write_seed("maze_005_random2", bytes([random.randint(0, 255) for _ in range(16)]))
write_seed("maze_006_random3", bytes([random.randint(0, 255) for _ in range(16)]))
# Seed 7: attempt near-solution path
write_seed("maze_007_near_sol", bytes([3, 1, 3, 1, 3, 1, 1, 3, 1, 3, 1, 3, 3, 1, 3, 1]))
# Seed 8: zeros
write_seed("maze_008_zeros", bytes(16))

print("\nGenerating seeds for parser (32 bytes each):")
# Seed 1: valid PING
buf = bytearray(32)
buf[0:4] = b"SYM\x01"
buf[4] = 1  # PING
buf[5] = 0x03  # flags: urgent + encrypted
buf[6:8] = struct.pack("<H", 4)  # length
buf[12:16] = b"test"  # payload
# compute checksum
cs = 0
for i in range(4):
    cs ^= buf[12 + i] << ((i % 4) * 8)
struct.pack_into("<I", buf, 8, cs)
write_seed("parser_001_ping", bytes(buf))

# Seed 2: valid DATA with marker
buf = bytearray(32)
buf[0:4] = b"SYM\x01"
buf[4] = 2  # DATA
buf[6:8] = struct.pack("<H", 8)
struct.pack_into("<I", buf, 12, 0xDEADBEEF)
buf[16:20] = b"\x00\x00\x00\x00"
cs = 0
for i in range(8):
    cs ^= buf[12 + i] << ((i % 4) * 8)
struct.pack_into("<I", buf, 8, cs)
write_seed("parser_002_data", bytes(buf))

# Seed 3: bad magic (to trigger first branches)
write_seed("parser_003_bad_magic", bytes(32))

# Seed 4: valid AUTH
buf = bytearray(32)
buf[0:4] = b"SYM\x01"
buf[4] = 4  # AUTH
buf[6:8] = struct.pack("<H", 8)
buf[12:20] = b"wrongpwd"
cs = 0
for i in range(8):
    cs ^= buf[12 + i] << ((i % 4) * 8)
struct.pack_into("<I", buf, 8, cs)
write_seed("parser_004_auth", bytes(buf))

# Seed 5-8: random variations
for i in range(4):
    write_seed(f"parser_00{i+5}_random", bytes(random.randint(0, 255) for _ in range(32)))

print("\nGenerating seeds for deep_branches (8 bytes each):")
write_seed("deep_001_zeros", bytes(8))
write_seed("deep_002_0x41", b"ABCDEFGH")
write_seed("deep_003_low", bytes([0x07, 0x0A, 0x1B, 0x2A, 0x41, 0x61, 0xF1, 0xFF]))
write_seed("deep_004_random1", bytes([random.randint(0, 255) for _ in range(8)]))
write_seed("deep_005_random2", bytes([random.randint(0, 255) for _ in range(8)]))
write_seed("deep_006_mid", bytes([0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0]))
write_seed("deep_007_high", bytes([0xC0, 0xD0, 0xE0, 0xF0, 0xF1, 0xF8, 0xFE, 0xFF]))
write_seed("deep_008_alpha", b"ABCDabcd")

print("\nGenerating seeds for crypto_check (16 bytes each):")
write_seed("crypto_001_zeros", bytes(16))
write_seed("crypto_002_ones", bytes([0xFF] * 16))
write_seed("crypto_003_seq", bytes(range(16)))
write_seed("crypto_004_high", bytes([0x81, 0x82, 0x83, 0x84] + [0] * 12))
write_seed("crypto_005_random1", bytes([random.randint(0, 255) for _ in range(16)]))
write_seed("crypto_006_random2", bytes([random.randint(0, 255) for _ in range(16)]))
write_seed("crypto_007_random3", bytes([random.randint(0, 255) for _ in range(16)]))
write_seed("crypto_008_alt", bytes([0x00, 0xFF] * 8))

print(f"\nAll seeds written to {SEED_DIR}")
print(f"Total: {len(os.listdir(SEED_DIR))} seed files")
