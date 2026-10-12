#!/usr/bin/env python3
"""Works out the Etterna chart keys the tests expect.

This is a second, independent reading of Etterna's code, written from the
Etterna sources rather than from ChartKey.cpp, so the two can check each other:

  Steps::GenerateChartKey / FillStringWithBPMs  (Steps.cpp)
  NoteDataUtil::LoadFromSMNoteDataStringWithPlayer  (NoteDataUtil.cpp)
  SMLoader::ProcessBPMsAndStops, ParseBPMs  (NotesLoaderSM.cpp), positive BPMs only
  TimingData::GetBPMAtRow, NoteData::LogNonEmptyRows

Etterna's float math is done in 32 bits here too (ctypes.c_float).  Etterna is MIT licensed,
Copyright (c) 2016-2023 Etterna; the license text is in src/Wife.h.

  reference.py song.sm ...      prints "file index type difficulty key" lines
  reference.py -s song.sm N     prints the string that gets hashed for chart N
"""
import ctypes
import hashlib
import sys

TYPE_EMPTY, TYPE_TAP, TYPE_HOLD_HEAD, TYPE_HOLD_TAIL, TYPE_MINE, TYPE_LIFT, \
    TYPE_KEYSOUND, TYPE_FAKE = range(8)
MAX_NOTE_ROW = 1 << 30

TRACKS = {
    'dance-single': 4, 'dance-double': 8, 'dance-couple': 8, 'dance-solo': 6,
    'dance-threepanel': 3, 'pump-single': 5, 'pump-halfdouble': 6,
    'pump-double': 10, 'para-single': 5, 'ez2-single': 5,
}


def f32(x):
    return ctypes.c_float(x).value


def lround(x):
    # round half away from zero, like C lround
    return int(x + 0.5) if x >= 0 else -int(-x + 0.5)


def beat_to_row(beat):
    return lround(f32(beat * f32(48.0)))


def read_msd(path):
    """Tags as lists of params: '#BPMS:0=120;' -> ['BPMS', '0=120']."""
    raw = open(path, 'rb').read().decode('latin-1')
    out = []
    i = 0
    n = len(raw)
    cur = None
    param = ''
    while i < n:
        c = raw[i]
        if cur is None:
            if raw.startswith('//', i):
                while i < n and raw[i] != '\n':
                    i += 1
                continue
            if c == '#':
                cur = []
                param = ''
            i += 1
            continue
        if raw.startswith('//', i):
            while i < n and raw[i] != '\n':
                i += 1
            continue
        if c == '\\' and i + 1 < n:
            param += raw[i + 1]
            i += 2
            continue
        if c == ':':
            cur.append(param)
            param = ''
        elif c == ';':
            cur.append(param)
            out.append(cur)
            cur = None
        else:
            param += c
        i += 1
    return out


def parse_pairs(text):
    pairs = []
    for expr in text.split(','):
        vals = expr.split('=')
        if len(vals) != 2:
            continue
        try:
            beat = f32(float(vals[0]))
            value = f32(float(vals[1]))
        except ValueError:
            continue
        pairs.append((beat, value))
    return pairs


def bpm_segments(bpm_text):
    """{row: bpm} the way SMLoader::ProcessBPMsAndStops builds it, for positive BPMs."""
    pairs = [(b, v) for (b, v) in parse_pairs(bpm_text) if v != 0]
    pairs.sort(key=lambda p: p[0])  # stable
    segs = {}
    idx = 0
    bpm = 0.0
    while idx < len(pairs) and pairs[idx][0] <= 0:
        bpm = pairs[idx][1]
        idx += 1
    if bpm == 0:
        if idx == len(pairs):
            bpm = 60.0
        else:
            # Etterna skips the first change here and uses the next one's value
            idx += 1
            bpm = pairs[idx][1] if idx < len(pairs) else 60.0
    segs[beat_to_row(0)] = bpm
    for beat, value in pairs[idx:]:
        segs[beat_to_row(beat)] = value
    return segs


def bpm_at_row(segs, row):
    rows = sorted(segs)
    best = rows[0]
    for r in rows:
        if r <= row:
            best = r
    return segs[best]


def load_notes(text, tracks):
    # comments go, together with the newline that ends them
    out = ''
    i = 0
    while True:
        j = text.find('//', i)
        if j < 0:
            out += text[i:]
            break
        out += text[i:j]
        k = text.find('\n', j)
        i = len(text) if k < 0 else k + 1
    notes = [dict() for _ in range(tracks)]  # row -> [type, duration]

    def hold_at(t, row):
        for r in sorted((r for r in notes[t] if r < row), reverse=True):
            typ, dur = notes[t][r]
            if typ == TYPE_HOLD_HEAD:
                return r if dur + r >= row else None
            if typ in (TYPE_TAP, TYPE_MINE, TYPE_LIFT, TYPE_FAKE):
                return None
        return None

    m = 0
    for measure in out.split(','):
        if measure == '':
            continue
        lines = []
        for ln in measure.split('\n'):
            ln = ln.strip('\r\n\t ')
            if ln:
                lines.append(ln)
        for li, line in enumerate(lines):
            pct = f32(li / f32(len(lines)))
            beat = f32(f32(m + pct) * 4)
            row = beat_to_row(beat)
            t = 0
            p = 0
            while t < tracks and p < len(line):
                ch = line[p]
                p += 1
                if p < len(line) and line[p] == '[':
                    q = line.find(']', p)
                    p = len(line) if q < 0 else q + 1
                if ch == '1':
                    notes[t][row] = [TYPE_TAP, 0]
                elif ch in '24':
                    notes[t][row] = [TYPE_HOLD_HEAD, MAX_NOTE_ROW]
                elif ch == '3':
                    head = hold_at(t, row)
                    if head is not None:
                        notes[t][head][1] = row - head
                elif ch == 'M':
                    notes[t][row] = [TYPE_MINE, 0]
                elif ch == 'K':
                    notes[t][row] = [TYPE_KEYSOUND, 0]
                elif ch == 'L':
                    notes[t][row] = [TYPE_LIFT, 0]
                elif ch == 'F':
                    notes[t][row] = [TYPE_FAKE, 0]
                t += 1
        m += 1
    for t in range(tracks):
        for r in [r for r, (typ, dur) in notes[t].items()
                  if typ == TYPE_HOLD_HEAD and dur == MAX_NOTE_ROW]:
            del notes[t][r]
    return notes


def key_string(notes, segs):
    rows = sorted(set(r for t in notes for r in t))
    s = ''
    for r in rows:
        for t in notes:
            s += str(t[r][0] if r in t else TYPE_EMPTY)
        s += str(int(f32(bpm_at_row(segs, r) + f32(0.374643))))
    return s


def charts(path):
    tags = read_msd(path)
    bpms = ''
    for tag in tags:
        if tag[0].upper() == 'BPMS':
            bpms = tag[1]
    segs = bpm_segments(bpms)
    result = []
    for tag in tags:
        if tag[0].upper() in ('NOTES', 'NOTES2') and len(tag) >= 7:
            typ = tag[1].strip()
            tracks = TRACKS[typ]
            result.append((typ, tag[3].strip(), key_string(load_notes(tag[6], tracks), segs)))
    return result


def main():
    args = sys.argv[1:]
    if args and args[0] == '-s':
        typ, diff, s = charts(args[1])[int(args[2])]
        print(s)
        return
    for path in args:
        for i, (typ, diff, s) in enumerate(charts(path)):
            print(path, i, typ, diff, 'X' + hashlib.sha1(s.encode('latin-1')).hexdigest())


if __name__ == '__main__':
    main()
