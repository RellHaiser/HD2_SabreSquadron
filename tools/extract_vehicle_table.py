"""Extracts a car's parameter block from a mission, as a .tbl for the "vehicle" console commands
(src/game/vehicle_spawn.cpp reads tables\\vehicles\\<name>.tbl in the game's folder).

python tools/extract_vehicle_table.py <actors.bin or scene2.bin> <the car's frame name> <out.tbl>

Without <out.tbl>: lists the cars of the file, with their masses. The block is the file's "COMP" run-length
coding undone (UnpackVehicleBlob in src/game/vehicle_params.cpp).
"""
import re
import struct
import sys

MAGIC = b"COMP"
HEADER = 13
BLOCK_SIZE = 0x124F


def unpack(data, at):
    size, end = struct.unpack_from("<II", data, at + 4)
    if size != BLOCK_SIZE or at + end > len(data):
        return None
    escape = data[at + 12]
    packed = data[at:at + end]
    block = bytearray()
    i = HEADER
    while i < end and len(block) < size:
        if packed[i] == escape:
            block += bytes([packed[i + 1]]) * packed[i + 2]
            i += 3
        else:
            block.append(packed[i])
            i += 1
    return bytes(block)


def cars(data):
    """(the frame's name, the block) of each car: the name is the last text before the block."""
    at = data.find(MAGIC)
    while at >= 0:
        block = unpack(data, at)
        if block:
            names = re.findall(rb"[A-Za-z0-9_ .\-]{3,}", data[max(0, at - 200):at])
            yield names[-1].decode() if names else "?", block
        at = data.find(MAGIC, at + 4)


def main():
    if len(sys.argv) not in (2, 3, 4):
        sys.exit(__doc__)
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    if len(sys.argv) == 2:
        for name, block in cars(data):
            print(f"{name:32} {struct.unpack_from('<f', block)[0]:8.0f} kg")
        return
    wanted = sys.argv[2].lower()
    for name, block in cars(data):
        if name.lower() == wanted:
            if len(sys.argv) == 4:
                with open(sys.argv[3], "wb") as f:
                    f.write(block)
            print(f"{name}: {struct.unpack_from('<f', block)[0]:.0f} kg")
            return
    sys.exit(f"no car named {sys.argv[2]}")


if __name__ == "__main__":
    main()
