"""Writes the probe script for PS2X_IOP_PROBE: commands for YYSOUND's sound function that the game does not send in the scenarios used to
check the port (spoken text, stopping a wave, tempo, the joint files, the track queries, the errors, the shutdown).

usage: python tools/iop_probe.py [OUT]      (default: work/iop_probe.txt)

Line format: milliseconds of IOP time after the driver's first real call, the command in hex, the command's data as a hex string
(the layouts are the ST_* structures of config/iop/yysound_types.h). Run it with, for example,

  python tools/pipeline.py run --timeout 90 --env PS2X_IOP_VERIFY=1 --env PS2X_IOP_PROBE=<script> \\
      --env PS2X_IOP_VERIFY_ALL=atick_resume,Load_atick_resume,soundFunc --env PS2X_IOP_COVERAGE=<dump>

The verifier then compares the original with the port on every injected command and on every tick that handles one.
The names used are the disc's own sound files (names only, no data).
"""
import os
import struct
import sys

SOUND = "cdrom0:\\DATA\\SOUND\\"


def name(path):
    return path.encode("ascii") + b";1"


def fixed(text, size):
    data = text if isinstance(text, bytes) else text.encode("ascii")
    return data[:size].ljust(size, b"\0")


def wave_open(fno, hd, bd, kind=0, ident=0x7000):
    return struct.pack("<HBB", ident, fno, kind) + fixed(name(SOUND + hd), 64) + fixed(name(SOUND + bd), 64)


def midi_open(fno, sq, hd, bd, ident=0x7100):
    return struct.pack("<HB", ident, fno) + fixed(name(SOUND + "SQ\\" + sq), 64) + fixed(name(SOUND + "HD\\" + hd), 64) + fixed(name(SOUND + "BD\\" + bd), 64)


def sq_open(fno, sq, midifno, ident=0x7200):
    return struct.pack("<HB", ident, fno) + fixed(name(SOUND + "SQ\\" + sq), 64) + struct.pack("<B", midifno)


def wave_play(track, fno, jno, program, note, loop, ident=0x7300):
    return struct.pack("<HBBhBBB", ident, track, fno, jno, program, note, loop)


def midi_play(track, fno, loop, ident=0x7400):
    return struct.pack("<HBBB", ident, track, fno, loop)


def voice_play(tracks, note, codes, tones=None, volumes=None, ident=0x7500):
    voice = bytes(codes)[:47] + b"\0"
    voice = voice.ljust(48, b"\0")
    tone = bytes(tones if tones is not None else [0x30 + (i * 7) % 0x50 for i in range(48)]).ljust(48, b"\0")
    volume = struct.pack("<48H", *(volumes if volumes is not None else [(0x1000 * (i % 3)) for i in range(48)]))
    return struct.pack("<HBBB", ident, tracks[0], tracks[1], note) + voice + tone + b"\0" + volume  # volume is a WORD array at +0x66


def lines():
    out = []

    def at(ms, command, data=b""):
        out.append("%d %X %s" % (ms, command, data.hex()))

    # wave files: a plain one for the voice and the waves, one opened as a joint file
    at(100, 0x2001, wave_open(6, "SMALLSE.HD", "SMALLSE.BD", 0, 0x7001))
    at(120, 0x2001, wave_open(7, "HD\\STDCHIME.HD", "BD\\STDCHIME.BD", 1, 0x7002))
    # a spoken text: kana, voiced and semi-voiced marks, small kana, the long vowel sign, an end mark
    at(1500, 0x2100, bytes([6, 6, 2]))
    at(1510, 0x3000, struct.pack("<H", 0x7701))
    plain = [0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4]
    at(1600, 0x2101, voice_play((13, 14), 3, plain, tones=[0x50] * 48, ident=0x7701))
    marks = [0xB6, 0xDE, 0xB7, 0xDE, 0xCA, 0xDF, 0xCB, 0xDF, 0xCA, 0xDE, 0xC4, 0xDE, 0xB1, 0xDE, 0xA6, 0xDD, 0xB0, 0xAF, 0xB1, 0xB0, 0x2D]
    at(1650, 0x2101, voice_play((12, 11), 4, marks, tones=[0x70] * 48, ident=0x7502))
    small = [0xB7, 0xAC, 0xB8, 0xAD, 0xB9, 0xAE, 0xC1, 0xAC, 0xCB, 0xAD, 0xD0, 0xAE, 0xB2, 0xA7, 0xB3, 0xA8, 0xB4, 0xA9, 0xB5, 0xAA, 0xB6, 0xAB,
             0xC3, 0xAA, 0xC4, 0xAB, 0xC2, 0xAA, 0xD6, 0xA7, 0xB0, 0xB1, 0xAF]
    at(1700, 0x2101, voice_play((10, 9), 5, small, tones=[0x20, 0x45, 0x70, 0x30, 0x65, 0x50] * 8, ident=0x7503))
    # the voice list is full (8 entries), then free again
    for i in range(7):
        at(1750 + i * 5, 0x2101, voice_play((8, 7), 1, [0xB1, 0xB2, 0xB3], ident=0x7510 + i))
    # queries
    at(3500, 0x2024, bytes([13]))
    at(3510, 0x2024, bytes([2]))
    at(3520, 0x1024, bytes([5]))
    # waves: play, change, stop
    at(3600, 0x2010, wave_play(12, 6, 0, 1, 60, 0, 0x7301))  # loop 0: plays for ever, so the later commands find it playing
    at(3700, 0x2022, struct.pack("<HBxh", 0x7302, 12, 100))
    at(3710, 0x2020, struct.pack("<HBxii", 0x7303, 12, 0x1000, 0x1200))
    at(3720, 0x2021, struct.pack("<HBxHHHH", 0x7304, 12, 0x800, 0x900, 4, 5))
    at(3730, 0x2025, struct.pack("<HBxi", 0x7305, 12, 0x2000))
    at(4300, 0x2011, struct.pack("<HB", 0x7306, 12))
    at(4310, 0x2011, struct.pack("<HB", 0x7307, 11))
    at(4400, 0x2010, wave_play(12, 6, 0, 0xFF, 60, 1, 0x7308))
    at(4410, 0x2010, wave_play(12, 6, 0, 120, 60, 1, 0x7309))
    at(4420, 0x2010, wave_play(12, 5, 0, 1, 60, 1, 0x730A))
    at(4430, 0x2010, wave_play(20, 6, 0, 1, 60, 1, 0x730B))
    # MIDI: open, a second sequence file, play, tempo, volume, fade, stop, close
    at(5000, 0x1001, midi_open(6, "SNGCHIME.SQ", "SNGCHIME.HD", "SNGCHIME.BD", 0x7401))
    at(6000, 0x2002, sq_open(5, "STDCHIME.SQ", 6, 0x7402))
    at(7000, 0x1010, midi_play(14, 6, 1, 0x7403))
    at(7500, 0x1026, struct.pack("<HBxh", 0x7404, 14, 10))
    at(7510, 0x1026, struct.pack("<HBxh", 0x7405, 14, -300))
    at(7520, 0x1020, struct.pack("<HBxi", 0x7406, 14, 0x3000))
    at(7530, 0x1021, struct.pack("<HBxHH", 0x7407, 14, 0x1000, 3))
    at(7540, 0x1025, struct.pack("<HBxi", 0x7408, 14, 0x2000))
    at(8500, 0x1011, struct.pack("<HB", 0x7409, 14))
    at(8600, 0x1003, struct.pack("<HB", 0x740A, 6))
    # joint files
    at(9000, 0x2200, struct.pack("<HBxh", 0x7601, 7, 0))
    at(9010, 0x2200, struct.pack("<HBxh", 0x7602, 7, 500))
    at(9100, 0x2201, struct.pack("<HBxh", 0x7603, 7, -1))
    at(9200, 0x2200, struct.pack("<HBxh", 0x7604, 3, 0))
    # errors: numbers over their limits
    at(9500, 0x1003, struct.pack("<HB", 0x7701, 9))
    at(9510, 0x2003, struct.pack("<HB", 0x7702, 9))
    at(9520, 0x1026, struct.pack("<HBxh", 0x7703, 20, 1))
    at(9530, 0x1020, struct.pack("<HBxi", 0x7704, 20, 1))
    at(9540, 0x1020, struct.pack("<HBxi", 0x7705, 3, 0x5000))
    at(9550, 0x2020, struct.pack("<HBxii", 0x7706, 20, 1, 1))
    at(9560, 0x2020, struct.pack("<HBxii", 0x7707, 3, 0x5000, 1))
    at(9570, 0x2022, struct.pack("<HBxh", 0x7708, 20, 1))
    at(9580, 0x2022, struct.pack("<HBxh", 0x7709, 3, 0x7000))
    at(9590, 0x2011, struct.pack("<HB", 0x770A, 20))
    at(9600, 0x1011, struct.pack("<HB", 0x770B, 20))
    at(9610, 0x1021, struct.pack("<HBxHH", 0x770C, 20, 1, 1))
    at(9620, 0x2021, struct.pack("<HBxHHHH", 0x770D, 20, 1, 1, 1, 1))
    at(9630, 0x1025, struct.pack("<HBxi", 0x770E, 20, 1))
    at(9640, 0x2025, struct.pack("<HBxi", 0x770F, 3, 0x5000))
    at(9650, 0x1010, midi_play(14, 6, 1, 0x7710))
    at(9660, 0x1010, midi_play(30, 2, 1, 0x7711))
    at(9670, 0x2010, wave_play(3, 9, 0, 1, 60, 1, 0x7712))
    at(9680, 0x9999, struct.pack("<H", 0x7713))
    at(9690, 0x3000, struct.pack("<H", 0))
    # load pause, sound enable
    at(10000, 0x3001, bytes([1]))
    at(10500, 0x3001, bytes([0]))
    at(10600, 0x2, bytes([0]))
    at(10800, 0x2, bytes([1]))
    # close the wave files, then the shutdown
    at(11000, 0x2003, struct.pack("<HB", 0x7801, 7))
    at(11010, 0x2003, struct.pack("<HB", 0x7802, 6))
    at(11500, 0x0, b"")
    at(14000, 0x2, bytes([1]))
    return out


def main():
    target = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "work", "iop_probe.txt")
    text = "\n".join(lines()) + "\n"
    with open(target, "w", newline="\n") as handle:
        handle.write("# generated by tools/iop_probe.py\n" + text)
    print("wrote %s: %d commands" % (os.path.normpath(target), len(lines())))


if __name__ == "__main__":
    main()
