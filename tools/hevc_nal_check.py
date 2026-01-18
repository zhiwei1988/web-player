#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys
from collections import Counter

def find_start_codes(data):
    # 返回所有 start code 的起始位置
    positions = []
    i = 0
    n = len(data)
    while i + 3 < n:
        if data[i] == 0 and data[i+1] == 0:
            if data[i+2] == 1:
                positions.append(i)
                i += 3
                continue
            if i + 3 < n and data[i+2] == 0 and data[i+3] == 1:
                positions.append(i)
                i += 4
                continue
        i += 1
    return positions

def split_nalus(data):
    starts = find_start_codes(data)
    if not starts:
        return []
    nalus = []
    for idx, start in enumerate(starts):
        # start code length
        if data[start:start+4] == b'\x00\x00\x00\x01':
            sc_len = 4
        else:
            sc_len = 3
        nal_start = start + sc_len
        nal_end = starts[idx + 1] if idx + 1 < len(starts) else len(data)
        if nal_start < nal_end:
            nalus.append((nal_start, nal_end))
    return nalus

def nal_type_h265(nalu):
    # H.265 NAL header: 2 bytes
    if len(nalu) < 2:
        return None
    return (nalu[0] >> 1) & 0x3f

def main(path):
    with open(path, 'rb') as f:
        data = f.read()

    nal_ranges = split_nalus(data)
    if not nal_ranges:
        print('No Annex-B start codes found.')
        return

    counter = Counter()
    positions = {
        32: [],  # VPS
        33: [],  # SPS
        34: [],  # PPS
        35: []   # AUD
    }

    for idx, (start, end) in enumerate(nal_ranges):
        nalu = data[start:end]
        nal_type = nal_type_h265(nalu)
        if nal_type is None:
            continue
        counter[nal_type] += 1
        if nal_type in positions:
            positions[nal_type].append(idx)

    print('Total NAL units:', len(nal_ranges))
    print('Top NAL types:')
    for nal_type, count in counter.most_common(15):
        print(f'  type {nal_type:2d}: {count}')

    print('\nPositions of VPS/SPS/PPS/AUD (by NAL index):')
    for t, name in [(32, 'VPS'), (33, 'SPS'), (34, 'PPS'), (35, 'AUD')]:
        pos = positions[t]
        preview = pos[:10]
        suffix = ' ...' if len(pos) > 10 else ''
        print(f'  {name}({t}): count={len(pos)}, first={preview}{suffix}')

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print('Usage: python3 hevc_nal_check.py <path_to_h265>')
        sys.exit(1)
    main(sys.argv[1])