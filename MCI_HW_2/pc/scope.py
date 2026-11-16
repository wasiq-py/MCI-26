"""scope.py - PC side of the oscilloscope.

What it does:
  1. Reads bytes from the board over the COM port (or makes fake ones for testing).
  2. Turns the bytes back into samples (numbers 0..4095) and then into volts.
  3. Draws the last 20 ms of the signal live.

How to run:
  python scope.py COM7       <- real board (use your own COM port)
  python scope.py fake       <- fake 500 Hz sine wave, no board needed
  python scope.py selftest   <- checks the decoder, prints OK

Wire format (must match the board):
  Sample = 2 bytes:  byte1 = 1xxxxxxx (bit 7 SET, upper 6 bits)
                     byte2 = 00xxxxxx (bit 7 clear, lower 6 bits)
  Status = 3 bytes:  0xC0 or 0xC1, then upper 7 bits, then lower 7 bits
                     0xC0 = samples counted last second (should be 10000)
                     0xC1 = total samples lost
  Only the FIRST byte of a frame has bit 7 set. That is how we find the
  start of a frame again after a cable problem.
"""
import sys
import time
import math
import threading
from collections import deque

import numpy as np
import serial                      # pyserial
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

BAUD = 230400          # must match the board
SAMPLE_RATE = 10000    # samples per second
VREF = 3.3             # ADC reference voltage
MAX_CODE = 4095        # 12-bit ADC: codes go from 0 to 4095
WINDOW_MS = 20         # how much time the screen shows
HISTORY = SAMPLE_RATE  # keep the last 1 second of samples
MEASURE_SAMPLES = 2000 # measure over the last 0.2 s (20 cycles even at 100 Hz)


# ---------------------------------------------------------------- encoding
# (Used by the fake source and the self-test. The real board does this in C.)

def encode_sample(code):
    """Turn a number 0..4095 into the 2 bytes sent on the wire."""
    byte1 = 0x80 | (code >> 6)     # bit 7 set + upper 6 bits
    byte2 = code & 0x3F            # lower 6 bits
    return bytes([byte1, byte2])


def encode_status(kind, value):
    """kind is 0xC0 or 0xC1, value is 0..16383."""
    return bytes([kind, (value >> 7) & 0x7F, value & 0x7F])


# ---------------------------------------------------------------- decoding

class Decoder:
    """Takes raw bytes in, keeps the decoded samples and the link status."""

    def __init__(self):
        self.lock = threading.Lock()        # reader thread and plot both use this object
        self.samples = deque(maxlen=HISTORY)  # raw codes 0..4095, newest at the end
        self.frame = []                     # bytes of the frame we are building
        self.needed = 0                     # how many bytes that frame needs
        self.samples_per_sec = None         # last 0xC0 value from the board
        self.lost = None                    # last 0xC1 value from the board
        self.stray_bytes = 0                # bytes thrown away while finding sync
        self.connected = False

    def feed(self, data):
        """Give the decoder a chunk of bytes from the port."""
        with self.lock:
            for b in data:
                self._one_byte(b)

    def reset_frame(self):
        """Forget a half-built frame (used after a reconnect)."""
        with self.lock:
            self.frame = []

    def _one_byte(self, b):
        if b & 0x80:
            # Bit 7 is set: this byte STARTS a new frame.
            if self.frame:
                self.stray_bytes += len(self.frame)   # the old frame never finished
            if 0x80 <= b <= 0xBF:
                self.frame = [b]
                self.needed = 2                       # sample frame
            elif b == 0xC0 or b == 0xC1:
                self.frame = [b]
                self.needed = 3                       # status frame
            else:
                self.frame = []
                self.stray_bytes += 1                 # not a valid start byte
        else:
            # Bit 7 is clear: this byte belongs INSIDE a frame.
            if not self.frame:
                self.stray_bytes += 1                 # nothing to attach it to, drop it
                return
            self.frame.append(b)

        if self.frame and len(self.frame) == self.needed:
            self._frame_done()

    def _frame_done(self):
        f = self.frame
        self.frame = []
        if len(f) == 2:                               # a sample
            code = ((f[0] & 0x3F) << 6) | f[1]
            self.samples.append(code)
        elif f[0] == 0xC0:                            # samples counted last second
            self.samples_per_sec = (f[1] << 7) | f[2]
        else:                                         # 0xC1: samples lost
            self.lost = (f[1] << 7) | f[2]

    def get_volts(self, count):
        """Return the newest 'count' samples converted to volts."""
        with self.lock:
            codes = list(self.samples)[-count:]
        return np.array(codes) * VREF / MAX_CODE


# ---------------------------------------------------------------- data sources

def reader(port, decoder, stop):
    """Reads the COM port. If the port disappears, keeps trying to reopen it."""
    ser = None
    while not stop.is_set():
        try:
            if ser is None:
                ser = serial.Serial(port, BAUD, timeout=0.05)
                decoder.reset_frame()
                decoder.connected = True
            data = ser.read(2048)           # waits up to 50 ms for bytes
            if data:
                decoder.feed(data)
        except (serial.SerialException, OSError):
            decoder.connected = False       # cable pulled or port busy
            if ser is not None:
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
            time.sleep(0.5)                 # wait, then try again
    if ser is not None:
        ser.close()


def fake_source(decoder, stop, hz=500):
    """Pretends to be the board: a sine wave plus status frames.
    Every 2 seconds it deletes one byte, to test that we recover."""
    decoder.connected = True
    n = 0                                   # sample counter
    chunks = 0
    while not stop.is_set():
        out = bytearray()
        for _ in range(500):                # 500 samples = 50 ms of signal
            volts = 1.65 + 1.5 * math.sin(2 * math.pi * hz * n / SAMPLE_RATE)
            out += encode_sample(round(volts * MAX_CODE / VREF))
            n += 1
            if n % SAMPLE_RATE == 0:        # once per second
                out += encode_status(0xC0, SAMPLE_RATE)
                out += encode_status(0xC1, 0)
        chunks += 1
        if chunks % 40 == 0:
            del out[7]                      # simulate a lost byte
        decoder.feed(bytes(out))
        time.sleep(0.05)


# ---------------------------------------------------------------- measurements

def measure(volts):
    """Works out the numbers shown under the plot from recent samples (in volts).
    Returns None if there are too few samples."""
    if len(volts) < 100:
        return None

    vmax = float(volts.max())          # highest voltage
    vmin = float(volts.min())          # lowest voltage
    vpp = vmax - vmin                  # peak-to-peak
    avg = float(volts.mean())          # average (the DC level)

    # Frequency: count how often the wave goes UP through the middle level.
    freq = None
    if vpp > 0.1:                      # a flat line has no frequency
        mid = (vmax + vmin) / 2        # the middle level
        gap = vpp * 0.05               # must first dip 5 % below the middle (ignores small noise)
        armed = False                  # True once the wave has been low enough
        crossings = []                 # where (in sample numbers) each upward crossing happened
        for i in range(1, len(volts)):
            if volts[i] < mid - gap:
                armed = True
            elif armed and volts[i - 1] < mid <= volts[i]:
                # Crossing happened between sample i-1 and i. Estimate the exact spot.
                crossings.append(i - 1 + (mid - volts[i - 1]) / (volts[i] - volts[i - 1]))
                armed = False
        if len(crossings) >= 2:
            # (number of full cycles) / (time they took), time counted in samples
            cycles = len(crossings) - 1
            samples_taken = crossings[-1] - crossings[0]
            freq = SAMPLE_RATE * cycles / samples_taken

    period_ms = 1000 / freq if freq else None
    return {"vmax": vmax, "vmin": vmin, "vpp": vpp, "avg": avg,
            "freq": freq, "period_ms": period_ms}


def readout_text(m):
    """Turns the measurement numbers into the text shown under the plot."""
    if m is None:
        return "waiting for data..."
    freq = f"{m['freq']:.1f} Hz" if m["freq"] else "--"
    period = f"{m['period_ms']:.3f} ms" if m["period_ms"] else "--"
    return (f"Vmax {m['vmax']:.2f} V   Vmin {m['vmin']:.2f} V   Vpp {m['vpp']:.2f} V   "
            f"Avg {m['avg']:.2f} V\nFreq {freq}   Period {period}")


# ---------------------------------------------------------------- plotting

def run_plot(decoder, title):
    count = WINDOW_MS * SAMPLE_RATE // 1000          # samples on screen
    t_ms = np.arange(count) * 1000 / SAMPLE_RATE     # x axis in milliseconds

    fig, ax = plt.subplots()
    fig.subplots_adjust(bottom=0.22)               # leave room for the readouts
    line, = ax.plot(t_ms, np.zeros(count))
    ax.set_ylim(0, VREF)
    ax.set_xlim(0, WINDOW_MS)
    ax.set_xlabel("time (ms)")
    ax.set_ylabel("voltage (V)")
    ax.set_title(title)
    ax.grid(True)
    info = ax.text(0.01, 0.97, "", transform=ax.transAxes, va="top")
    readout = fig.text(0.02, 0.02, "", va="bottom", family="monospace", fontsize=9)

    def update(_frame):
        recent = decoder.get_volts(MEASURE_SAMPLES)    # longer stretch used for measuring
        volts = recent[-count:]                       # the last 20 ms are drawn
        line.set_data(t_ms[:len(volts)], volts)
        readout.set_text(readout_text(measure(recent)))
        link = "connected" if decoder.connected else "NO LINK"
        info.set_text(f"{link} | samples/s: {decoder.samples_per_sec} | "
                      f"lost: {decoder.lost} | stray bytes: {decoder.stray_bytes}")
        return line, info, readout

    anim = FuncAnimation(fig, update, interval=50, blit=False, cache_frame_data=False)
    plt.show()
    return anim


# ---------------------------------------------------------------- self-test

def self_test():
    """Feeds known bytes (with garbage mixed in) and checks what comes out."""
    d = Decoder()
    stream = (b"\x05\x3F"                      # 2 stray bytes (bit 7 clear)
              + encode_sample(0)
              + encode_sample(4095)
              + b"\x85"                        # a frame that starts and never finishes
              + encode_sample(2048)
              + encode_status(0xC0, 10000)
              + encode_status(0xC1, 300)
              + encode_sample(1))
    d.feed(stream)
    assert list(d.samples) == [0, 4095, 2048, 1], list(d.samples)
    assert d.samples_per_sec == 10000
    assert d.lost == 300
    assert d.stray_bytes == 3, d.stray_bytes
    # Measurements: sine waves at the 3 frequencies we need (1.65 V +/- 1.5 V)
    t = np.arange(MEASURE_SAMPLES)
    for hz in (100, 500, 1000):
        m = measure(1.65 + 1.5 * np.sin(2 * np.pi * hz * t / SAMPLE_RATE))
        assert abs(m["freq"] - hz) < hz * 0.01, (hz, m["freq"])
        # At 1 kHz there are only 10 samples per cycle, so a sample can miss the exact
        # peak and Vpp reads a little low (about 5 %). That is a sampling limit, not a bug.
        assert 2.8 < m["vpp"] < 3.01 and abs(m["avg"] - 1.65) < 0.02, (hz, m)
    print("OK")


# ---------------------------------------------------------------- main

def main():
    if len(sys.argv) < 2:
        print("usage: python scope.py COM7 | fake | selftest")
        return
    arg = sys.argv[1]
    if arg == "selftest":
        self_test()
        return

    decoder = Decoder()
    stop = threading.Event()
    if arg == "fake":
        worker = threading.Thread(target=fake_source, args=(decoder, stop), daemon=True)
    else:
        worker = threading.Thread(target=reader, args=(arg, decoder, stop), daemon=True)
    worker.start()

    run_plot(decoder, f"Scope - {arg}")   # blocks until the window is closed
    stop.set()


if __name__ == "__main__":
    main()
