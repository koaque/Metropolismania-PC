"""tools/movie_check_cmp.py LOG_A LOG_B: compares the [movie] picture lines (MMPC_MOVIE_CHECK=1, src/port/difftest.cpp) of two run
logs, for example one with every port off (MMPC_PORT_DISABLE=all) and one with the ports. Prints the counts, the first lines
that differ, and exits 1 when the sequences differ or either log has no picture lines."""
import re
import sys

LINE = re.compile(r"\[movie\] picture (\d+) frame (-?\d+) slot ([0-9A-F]+) blocks (\d+) image ([0-9A-F]+) subtitle (-?\d+) ([0-9A-F]+)")


def pictures(path):
    out = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for text in f:
            m = LINE.search(text)
            if m:
                out.append(m.groups()[1:])  # the picture number is the position
    return out


def main():
    a, b = pictures(sys.argv[1]), pictures(sys.argv[2])
    print(f"A: {len(a)} pictures, B: {len(b)} pictures")
    if not a or not b:
        print("no picture lines in one of the logs")
        return 1
    differ = [i for i in range(min(len(a), len(b))) if a[i] != b[i]]
    for i in differ[:5]:
        print(f"picture {i}: A frame {a[i][0]} slot {a[i][1]} blocks {a[i][2]} image {a[i][3]} subtitle {a[i][4]} {a[i][5]}")
        print(f"{'':>{len(str(i)) + 9}}B frame {b[i][0]} slot {b[i][1]} blocks {b[i][2]} image {b[i][3]} subtitle {b[i][4]} {b[i][5]}")
    distinct = len(set(p[3] for p in a))
    print(f"{len(differ)} of {min(len(a), len(b))} common pictures differ; {distinct} distinct images in A")
    return 1 if differ or len(a) != len(b) else 0


if __name__ == "__main__":
    sys.exit(main())
