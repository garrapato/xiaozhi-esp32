#!/usr/bin/env python3
"""
Convert a non-interlaced 8-bit RGB/RGBA PNG into byte-swapped RGB565.
"""

import argparse
import struct
import zlib


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def paeth(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png(path):
    with open(path, "rb") as f:
        data = f.read()

    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("not a PNG file")

    pos = len(PNG_SIGNATURE)
    width = height = bit_depth = color_type = interlace = None
    idat = bytearray()

    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        chunk_type = data[pos + 4:pos + 8]
        chunk_data = data[pos + 8:pos + 8 + length]
        pos += 12 + length

        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", chunk_data)
        elif chunk_type == b"IDAT":
            idat.extend(chunk_data)
        elif chunk_type == b"IEND":
            break

    if bit_depth != 8 or color_type not in (2, 6) or interlace != 0:
        raise ValueError("only non-interlaced 8-bit RGB/RGBA PNG files are supported")

    channels = 3 if color_type == 2 else 4
    stride = width * channels
    raw = zlib.decompress(bytes(idat))
    rows = []
    offset = 0
    previous = bytearray(stride)

    for _ in range(height):
        filter_type = raw[offset]
        offset += 1
        current = bytearray(raw[offset:offset + stride])
        offset += stride

        for i, value in enumerate(current):
            left = current[i - channels] if i >= channels else 0
            up = previous[i]
            upper_left = previous[i - channels] if i >= channels else 0

            if filter_type == 1:
                current[i] = (value + left) & 0xFF
            elif filter_type == 2:
                current[i] = (value + up) & 0xFF
            elif filter_type == 3:
                current[i] = (value + ((left + up) >> 1)) & 0xFF
            elif filter_type == 4:
                current[i] = (value + paeth(left, up, upper_left)) & 0xFF
            elif filter_type != 0:
                raise ValueError(f"unsupported PNG filter type {filter_type}")

        rows.append(current)
        previous = current

    return width, height, channels, rows


def main():
    parser = argparse.ArgumentParser(description="Convert PNG to RGB565")
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--black-border", type=int, default=0,
                        help="Force this many outer pixels to black")
    args = parser.parse_args()

    width, height, channels, rows = read_png(args.input)
    with open(args.output, "wb") as out:
        for y, row in enumerate(rows):
            for x in range(width):
                base = x * channels
                if (x < args.black_border or y < args.black_border or
                        x >= width - args.black_border or y >= height - args.black_border):
                    r = g = b = 0
                else:
                    r = row[base]
                    g = row[base + 1]
                    b = row[base + 2]
                value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                out.write(struct.pack(">H", value))

    print(f"Converted {args.input} ({width}x{height}) -> {args.output}")


if __name__ == "__main__":
    main()
