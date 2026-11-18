# Usage: brand_icon.py face.ppm out_dir   (render.sh runs it on the Polished face)
# Writes the Home Assistant integration icon: out_dir/icon.png (256 px) and icon@2x.png (512 px). It's the rendered
# face in the README's brushed bezel (montage.py --bezel), filling the square on a transparent background, as
# Home Assistant's brand images expect. Home Assistant falls back to the icon for the logo and dark variants.
import math, os, struct, sys, zlib

S = 240  # rendered face size
RING = 9  # bezel width at face scale, as in montage.py
R = S / 2 + RING
# Bezel colours from site/index.html: linear-gradient(170deg, #4a5059, #2a2e34 38%, #15171a 72%, #23262b).
STOPS = [(0, (0x4A, 0x50, 0x59)), (.38, (0x2A, 0x2E, 0x34)), (.72, (0x15, 0x17, 0x1A)), (1, (0x23, 0x26, 0x2B))]
SIN, COS = math.sin(math.radians(170)), -math.cos(math.radians(170))
SPAN = abs(2 * R * SIN) + abs(2 * R * COS)
SS = 3  # supersamples per axis, for smooth edges


def grad(t):
    t = min(1, max(0, t))
    for (a, ca), (b, cb) in zip(STOPS, STOPS[1:]):
        if t <= b:
            f = (t - a) / (b - a)
            return tuple(ca[k] + (cb[k] - ca[k]) * f for k in range(3))
    return STOPS[-1][1]


def read_ppm(path):
    data = open(path, "rb").read()
    assert data[:2] == b"P6"
    return data.split(b"\n", 3)[3]


def face_px(px, x, y):
    """Bilinear sample of the face at face coordinates (x, y), pixel centres at +0.5."""
    x, y = min(max(x - .5, 0), S - 1.001), min(max(y - .5, 0), S - 1.001)
    x0, y0 = int(x), int(y)
    fx, fy = x - x0, y - y0
    out = [0.0, 0.0, 0.0]
    for dx, dy, w in ((0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)), (0, 1, (1 - fx) * fy), (1, 1, fx * fy)):
        o = ((y0 + dy) * S + x0 + dx) * 3
        for k in range(3):
            out[k] += px[o + k] * w
    return out


def icon(px, n):
    scale = 2 * R / n  # face units per output pixel: the bezel's outer edge touches the square's edges
    img = bytearray(n * n * 4)
    for oy in range(n):
        for ox in range(n):
            acc, cover = [0.0, 0.0, 0.0], 0
            for sy in range(SS):
                for sx in range(SS):
                    x = (ox + (sx + .5) / SS) * scale - R  # relative to the centre, face units
                    y = (oy + (sy + .5) / SS) * scale - R
                    d = math.hypot(x, y)
                    if d > R:
                        continue
                    c = face_px(px, x + S / 2, y + S / 2) if d <= S / 2 else grad(.5 + (x * SIN + y * COS) / SPAN)
                    for k in range(3):
                        acc[k] += c[k]
                    cover += 1
            if cover:
                o = (oy * n + ox) * 4
                img[o:o + 4] = bytes(round(v / cover) for v in acc) + bytes([round(255 * cover / SS ** 2)])
    return bytes(img)


def write_png(path, n, rgba):
    raw = b"".join(b"\0" + rgba[y * n * 4:(y + 1) * n * 4] for y in range(n))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", n, n, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


face, out = sys.argv[1], sys.argv[2]
px = read_ppm(face)
os.makedirs(out, exist_ok=True)
for name, n in (("icon.png", 256), ("icon@2x.png", 512)):
    write_png(os.path.join(out, name), n, icon(px, n))
