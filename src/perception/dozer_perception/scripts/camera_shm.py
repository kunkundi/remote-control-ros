"""Read versioned, bounded I420 snapshots from the edge capture process."""

from dataclasses import dataclass
import fcntl
import os
import re
import struct
import time

import cv2
import numpy as np


HEADER = struct.Struct('<8sIIQQQIIII32s')
CAMERA_NAMES = ('front_left', 'front_right', 'front', 'rear', 'left', 'right')


@dataclass(frozen=True)
class CameraFrame:
    sequence: int
    timestamp_ns: int
    frame_id: str
    image: np.ndarray


class CameraShmReader:
    def __init__(self, name, max_age_sec=0.2):
        if not re.fullmatch(r'[A-Za-z0-9_]+', name):
            raise ValueError('Invalid shared camera name')
        if not 0 < max_age_sec <= 5:
            raise ValueError('Camera max age must be in (0, 5] seconds')
        self.path = '/dev/shm/' + name
        self.max_age_ns = int(max_age_sec * 1_000_000_000)
        self.last_key = None
        self.last_timing_ms = (0.0, 0.0)

    def read(self):
        self.last_timing_ms = (0.0, 0.0)
        started = time.perf_counter()
        try:
            descriptor = os.open(self.path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
        except FileNotFoundError:
            return None
        try:
            try:
                fcntl.flock(descriptor, fcntl.LOCK_SH | fcntl.LOCK_NB)
            except BlockingIOError:
                return None
            metadata = os.fstat(descriptor)
            header = os.pread(descriptor, HEADER.size, 0)
            if len(header) != HEADER.size:
                raise ValueError('Truncated shared camera header')
            if header == bytes(HEADER.size):
                return None
            (magic, version, header_size, sequence, timestamp_ns, published_ns,
             width, height, payload_size, reserved, frame_id) = HEADER.unpack(header)
            if magic != b'DZRI420\0' or version != 1 or header_size != HEADER.size or reserved:
                raise ValueError('Unsupported shared camera protocol')
            if (not 0 < width <= 4096 or not 0 < height <= 2160 or width % 2 or height % 2
                    or payload_size != width * height * 3 // 2
                    or metadata.st_size < HEADER.size + payload_size
                    or not sequence or not timestamp_ns):
                raise ValueError('Invalid shared camera dimensions or metadata')
            age_ns = time.monotonic_ns() - published_ns
            key = (metadata.st_ino, sequence, timestamp_ns)
            if age_ns < 0 or age_ns > self.max_age_ns or key == self.last_key:
                return None
            payload = os.pread(descriptor, payload_size, HEADER.size)
            if len(payload) != payload_size:
                raise ValueError('Truncated shared camera payload')
        finally:
            os.close(descriptor)
        read_ms = (time.perf_counter() - started) * 1000.0
        camera_name = frame_id.split(b'\0', 1)[0].decode('ascii')
        if not re.fullmatch(r'[A-Za-z0-9_]+', camera_name):
            raise ValueError('Invalid shared camera frame ID')
        planes = np.frombuffer(payload, dtype=np.uint8).reshape(height * 3 // 2, width)
        image = cv2.cvtColor(planes, cv2.COLOR_YUV2BGR_I420)
        convert_ms = (time.perf_counter() - started) * 1000.0 - read_ms
        self.last_timing_ms = (read_ms, convert_ms)
        self.last_key = key
        return CameraFrame(sequence, timestamp_ns, camera_name, image)
