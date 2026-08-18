#!/usr/bin/env python3
"""Generate deterministic GIF/CUR doc-check fixtures (no external deps)."""
import struct, os

BASE = '/mnt/f/AI/Codex/Autohotkey_Linux/tests/doccheck/fixtures'

PAL4 = bytes([
    0x00, 0x00, 0x00,
    0x00, 0xFF, 0x00,
    0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF,
])

def lzw_stream(pixels, min_code=2):
    clear, end = 1 << min_code, (1 << min_code) + 1
    next_code = end + 1
    width = min_code + 1
    bits = []
    def emit(code):
        for k in range(width):
            bits.append((code >> k) & 1)
    emit(clear)
    have_prev = False
    for idx in pixels:
        emit(idx)
        if have_prev and next_code < 4096:
            next_code += 1
            if next_code == (1 << width) and width < 12:
                width += 1
        have_prev = True
    emit(end)
    stream = bytearray()
    for i in range(0, len(bits), 8):
        b = 0
        for k in range(8):
            if i + k < len(bits) and bits[i + k]:
                b |= 1 << k
        stream.append(b)
    blocks = bytearray()
    for off in range(0, len(stream), 255):
        chunk = stream[off:off + 255]
        blocks.append(len(chunk))
        blocks += chunk
    blocks.append(0)
    return bytes(blocks)

def make_gif(width, height, pixels, palette=PAL4, transparent=None):
    out = bytearray()
    out += b'GIF89a'
    out += struct.pack('<HH', width, height)
    out += bytes([0x81, 0, 0])  # GCT flag + size bits = 1 -> 2^2 = 4 entries.
    out += palette
    if transparent is not None:
        out += bytes([0x21, 0xF9, 4, 0x01, 0, 0, transparent, 0])
    out += bytes([0x2C]) + struct.pack('<HHHH', 0, 0, width, height)
    out += bytes([0x00])
    out += bytes([2])
    out += lzw_stream(pixels)
    out += bytes([0x3B])
    return bytes(out)

def test_gif():
    pixels = []
    for y in range(8):
        for x in range(8):
            if y < 2 or y >= 6 or x < 2 or x >= 6:
                pixels.append(1)
            elif x < 5 and y < 5:
                pixels.append(2)
            else:
                pixels.append(3)
    return make_gif(8, 8, pixels, transparent=1)

def test_red_gif():
    return make_gif(2, 2, [2, 2, 2, 2])

def test_cur():
    src = open(BASE + '/test.ico', 'rb').read()
    patched = bytearray(src)
    patched[2] = 2
    return bytes(patched)

if __name__ == '__main__':
    os.makedirs(BASE, exist_ok=True)
    open(BASE + '/test.gif', 'wb').write(test_gif())
    open(BASE + '/test_red.gif', 'wb').write(test_red_gif())
    open(BASE + '/test.cur', 'wb').write(test_cur())
    print('done')