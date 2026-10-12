#!/usr/bin/env python3
"""生成 SR73F 模拟报文（candump -l 格式），用于无硬件时验证驱动与下游模块。

用法：
  gen_sr73f_candump.py -o sim.log --sensor-ids 0,1,2,3 --objects 20 --seconds 5
  ros2 run dozer_radar_sr73f radar_sr73f_node --ros-args -p replay_file:=sim.log ...
也可以用 can-utils 的 canplayer 回放到真实/虚拟 CAN 接口。
"""

import argparse
import math


def set_motorola(data, start_bit, length, value):
    """按 DBC 约定（Motorola，start_bit 为最高位）写入信号。"""
    pos = start_bit
    for i in range(length - 1, -1, -1):
        byte, bit = divmod(pos, 8)
        if (value >> i) & 1:
            data[byte] |= 1 << bit
        else:
            data[byte] &= ~(1 << bit) & 0xFF
        pos = (byte + 1) * 8 + 7 if bit == 0 else pos - 1


def raw(value, res, offset, length):
    r = int(round((value - offset) / res))
    return max(0, min(r, (1 << length) - 1))


def frame(can_id, data):
    return '%03X#%s' % (can_id, ''.join('%02X' % b for b in data))


def object_frame(sid, obj_id, dist_long, dist_lat, vrel_long, vrel_lat, rcs):
    d = [0] * 8
    set_motorola(d, 7, 8, obj_id)
    set_motorola(d, 15, 13, raw(dist_long, 0.2, -500.0, 13))
    set_motorola(d, 18, 11, raw(dist_lat, 0.2, -204.6, 11))
    set_motorola(d, 39, 10, raw(vrel_long, 0.25, -128.0, 10))
    set_motorola(d, 45, 9, raw(vrel_lat, 0.25, -64.0, 9))
    set_motorola(d, 63, 8, raw(rcs, 0.5, -64.0, 8))
    return frame(0x60B + sid * 0x10, d)


def status_frame(sid, nof, meas):
    d = [0] * 8
    set_motorola(d, 7, 8, nof)
    set_motorola(d, 15, 16, meas & 0xFFFF)
    return frame(0x60A + sid * 0x10, d)


def radar_state_frame(sid):
    d = [0] * 8
    set_motorola(d, 6, 1, 1)
    set_motorola(d, 15, 10, 80)   # 160 m
    set_motorola(d, 34, 3, sid)
    set_motorola(d, 38, 3, 1)     # 按距离排序
    set_motorola(d, 43, 2, 1)     # Objects
    return frame(0x201 + sid * 0x10, d)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-o', '--output', required=True)
    ap.add_argument('--sensor-ids', default='0,1')
    ap.add_argument('--objects', type=int, default=10, help='每台雷达每周期目标数')
    ap.add_argument('--seconds', type=float, default=5.0)
    ap.add_argument('--interface', default='can0')
    ap.add_argument('--start', type=float, default=1700000000.0)
    args = ap.parse_args()

    sids = [int(s) for s in args.sensor_ids.split(',') if s != '']
    cycle = 0.033
    lines = []
    t = args.start
    n_cycles = int(args.seconds / cycle)
    for c in range(n_cycles):
        for k, sid in enumerate(sids):
            ts = t + k * 0.002
            lines.append((ts, status_frame(sid, args.objects, c)))
            for i in range(args.objects):
                ph = c * cycle + i + sid
                lines.append((ts + 0.0002 * (i + 1), object_frame(
                    sid, (i + 7 * sid) & 0xFF,
                    dist_long=4.0 + 1.5 * i + math.sin(ph),
                    dist_lat=3.0 * math.cos(0.3 * ph),      # 雷达原始坐标，右正
                    vrel_long=-1.0 + 0.5 * math.sin(ph),
                    vrel_lat=0.5 * math.cos(ph),
                    rcs=5.0 + i % 10)))
            if c % 30 == 0:
                lines.append((ts + 0.02, radar_state_frame(sid)))
                lines.append((ts + 0.021, frame(0x700 + sid * 0x10, [1, 0, 21, 0, 0, 0, 0, 0])))
        t += cycle
    lines.sort(key=lambda x: x[0])
    with open(args.output, 'w') as f:
        for ts, fr in lines:
            f.write('(%.6f) %s %s\n' % (ts, args.interface, fr))
    print('写入 %d 帧到 %s（%d 台雷达，%.1f 秒）' % (len(lines), args.output, len(sids), args.seconds))


if __name__ == '__main__':
    main()
