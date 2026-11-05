# GUI 確認の base-*.png と pr-*.png の画素の差を出す(#195)。標準ライブラリだけで動く。
#
#   python .claude/skills/gui-check/pixdiff.py <フォルダ>
#
# 組ごとに 1 行: <名前> diff_pixels=<n> x <min>-<max> y <min>-<max> max_channel_delta <n>(同じなら identical)。
# 大きさが違う組と、相手のない base-*.png は、理由を出して終了コード 1 にする。組が 1 つもないときも 1。
import glob
import os
import struct
import sys
import zlib


def read_png(path):
    with open(path, "rb") as file:
        data = file.read()
    position, compressed, width, height, color = 8, b"", 0, 0, 0
    while position < len(data):
        length, kind = struct.unpack(">I4s", data[position:position + 8])
        chunk = data[position + 8:position + 8 + length]
        if kind == b"IHDR":
            width, height, _, color = struct.unpack(">IIBB", chunk[:10])
        elif kind == b"IDAT":
            compressed += chunk
        position += 12 + length
    channels = {2: 3, 6: 4}[color]
    raw = zlib.decompress(compressed)
    stride = width * channels
    rows, previous = [], bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            left = row[i - channels] if i >= channels else 0
            up = previous[i]
            corner = previous[i - channels] if i >= channels else 0
            if kind == 1:
                row[i] = (row[i] + left) & 255
            elif kind == 2:
                row[i] = (row[i] + up) & 255
            elif kind == 3:
                row[i] = (row[i] + (left + up) // 2) & 255
            elif kind == 4:
                estimate = left + up - corner
                a, b, c = abs(estimate - left), abs(estimate - up), abs(estimate - corner)
                row[i] = (row[i] + (left if a <= b and a <= c else up if b <= c else corner)) & 255
        rows.append(bytes(row))
        previous = row
    return width, height, channels, rows


def describe(base_path, pr_path):
    width, height, base_channels, base = read_png(base_path)
    pr_width, pr_height, pr_channels, pr = read_png(pr_path)
    if (width, height) != (pr_width, pr_height):
        return None, f"size differs {width}x{height} vs {pr_width}x{pr_height}"
    count, xs, ys, largest = 0, [], [], 0
    for y in range(height):
        for x in range(width):
            a = base[y][x * base_channels:x * base_channels + 3]
            b = pr[y][x * pr_channels:x * pr_channels + 3]
            if a != b:
                count += 1
                xs.append(x)
                ys.append(y)
                largest = max(largest, max(abs(i - j) for i, j in zip(a, b)))
    if not count:
        return count, "identical"
    return count, f"x {min(xs)}-{max(xs)} y {min(ys)}-{max(ys)} max_channel_delta {largest}"


def main(argv):
    if len(argv) != 1 or not os.path.isdir(argv[0]):
        print("usage: pixdiff.py <フォルダ>", file=sys.stderr)
        return 2
    failed = False
    bases = sorted(glob.glob(os.path.join(argv[0], "base-*.png")))
    if not bases:
        print("base-*.png がありません")
        return 1
    for base in bases:
        name = os.path.basename(base)[len("base-"):]
        pr = os.path.join(argv[0], "pr-" + name)
        if not os.path.exists(pr):
            print(f"{name} no pr- file")
            failed = True
            continue
        count, detail = describe(base, pr)
        if count is None:
            failed = True
            print(f"{name} {detail}")
        else:
            print(f"{name} diff_pixels={count} {detail}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main(sys.argv[1:]))
