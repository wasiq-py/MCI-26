"""scope.py - PC side of the oscilloscope (HW2, Part B + PC side).

What it does:
  1. Reads bytes from the board over the COM port (or makes fake ones for testing).
  2. Turns the bytes back into samples (numbers 0..4095) and then into volts.
  3. Draws the signal live, measures it, and prints the board's status once per second.

How to run (from the folder this file is in):
  python scope.py COM7                <- real board (use your own COM port)
  python scope.py fake                <- fake 500 Hz sine, no board needed
  python scope.py fake square 100     <- fake wave: sine | square | ramp | triangle, any Hz
  python scope.py selftest            <- checks the decoder and the maths, prints OK

Buttons in the window (keyboard key in brackets):
  Pause   (space)  freeze the picture / resume
  Save    (c)      save the samples on screen to scope_<date>_<time>.csv
  Trigger (t)      ON = the wave stands still (starts each picture at a rising edge)
  AC/DC   (a)      AC = remove the average, so the wave is drawn around 0 V
  Marks   (m)      put a dot on every sample (shows the 10 kS/s sample points)
  FFT     (f)      show the frequency spectrum under the wave
  Time    (right)  change the time window: 5 / 10 / 20 / 50 / 100 ms
  Volts   (up)     change the vertical zoom: 3.3 / 2 / 1 / 0.5 V full screen

Wire format (must match protocol.h on the board):
  Sample = 2 bytes:  byte1 = 1 0 s11..s6   (bit 7 SET, upper 6 bits of the sample)
                     byte2 = 0 0 s5..s0    (bit 7 clear, lower 6 bits)
  Status = 3 bytes:  0xC0 or 0xC1, then upper 7 bits, then lower 7 bits of the value
                     0xC0 = samples taken in the last second (should be 10000)
                     0xC1 = total samples lost so far (should stay 0)
  Only the FIRST byte of a frame has bit 7 set. That is how we find the start of a
  frame again after a cable problem (Part B task 3).
"""
import os
import sys
import time
import math
import threading
from collections import deque

import numpy as np
import serial                      # pyserial
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from matplotlib.widgets import Button

BAUD = 230400            # must match uart_init(230400) on the board
SAMPLE_RATE = 10000      # samples per second (TIM6 runs at 10 kHz)
VREF = 3.3               # ADC reference voltage
MAX_CODE = 4095          # 12-bit ADC: codes go from 0 to 4095
HISTORY = SAMPLE_RATE    # keep the last 1 second of samples
MEASURE_SAMPLES = 2000   # measure over the last 0.2 s (20 cycles even at 100 Hz)
DRAW_SAMPLES = 3000      # the trigger searches this many newest samples for an edge
TIME_WINDOWS_MS = [5, 10, 20, 50, 100]     # choices for the Time button
VOLT_SPANS = [3.3, 2.0, 1.0, 0.5]          # choices for the Volts button (full screen height)


# ---------------------------------------------------------------- encoding
# (Used by the fake source and the self-test. The real board does this in C, protocol.h.)

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

    def __init__(self, print_status=True):
        self.lock = threading.Lock()          # reader thread and plot both use this object
        self.samples = deque(maxlen=HISTORY)  # raw codes 0..4095, newest at the end
        self.frame = []                       # bytes of the frame we are building
        self.needed = 0                       # how many bytes that frame needs
        self.samples_per_sec = None           # last 0xC0 value from the board
        self.lost = None                      # last 0xC1 value from the board
        self.stray_bytes = 0                  # bytes thrown away while finding sync
        self.connected = False
        self.print_status = print_status      # print each status report to the terminal

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
        elif f[0] == 0xC0:                            # samples taken last second
            self.samples_per_sec = (f[1] << 7) | f[2]
        else:                                         # 0xC1: samples lost (sent right after 0xC0)
            self.lost = (f[1] << 7) | f[2]
            if self.print_status:                     # one terminal line per second = the screenshot
                print(time.strftime("%H:%M:%S"),
                      f"| samples last second: {self.samples_per_sec}"
                      f" | lost total: {self.lost}"
                      f" | stray bytes: {self.stray_bytes}", flush=True)

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
                print(time.strftime("%H:%M:%S"), "| port", port, "open", flush=True)
            data = ser.read(2048)           # waits up to 50 ms for bytes
            if data:
                decoder.feed(data)
        except (serial.SerialException, OSError):
            if decoder.connected:
                print(time.strftime("%H:%M:%S"), "| link lost, retrying...", flush=True)
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


def fake_value(shape, hz, n):
    """Volts of a fake test wave at sample number n. Swings 0.15 V .. 3.15 V."""
    phase = (hz * n / SAMPLE_RATE) % 1.0      # 0.0 .. 1.0 through one cycle
    if shape == "square":
        x = 1.0 if phase < 0.5 else -1.0
    elif shape == "ramp":                     # sawtooth: slowly up, then drops
        x = 2 * phase - 1
    elif shape == "triangle":
        x = 4 * phase - 1 if phase < 0.5 else 3 - 4 * phase
    else:                                     # sine
        x = math.sin(2 * math.pi * phase)
    return 1.65 + 1.5 * x


def fake_source(decoder, stop, shape="sine", hz=500):
    """Pretends to be the board: a test wave plus status frames.
    Every 2 seconds it deletes one byte, to show that we recover."""
    decoder.connected = True
    n = 0                                   # sample counter
    chunks = 0
    while not stop.is_set():
        out = bytearray()
        for _ in range(500):                # 500 samples = 50 ms of signal
            volts = fake_value(shape, hz, n)
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

def rising_edges(volts):
    """Sample positions where the wave goes UP through its middle level.
    The wave must first dip 5 % of Vpp below the middle, so noise does not count twice."""
    vmax, vmin = volts.max(), volts.min()
    vpp = vmax - vmin
    if vpp < 0.1:                      # a flat line has no edges
        return []
    mid = (vmax + vmin) / 2
    gap = vpp * 0.05
    armed = False                      # True once the wave has been low enough
    edges = []
    for i in range(1, len(volts)):
        if volts[i] < mid - gap:
            armed = True
        elif armed and volts[i - 1] < mid <= volts[i]:
            # Crossing is between sample i-1 and i. Estimate the exact spot.
            edges.append(i - 1 + (mid - volts[i - 1]) / (volts[i] - volts[i - 1]))
            armed = False
    return edges


def measure(volts):
    """Works out the numbers shown under the plot. Returns None if too few samples."""
    if len(volts) < 100:
        return None
    vmax = float(volts.max())          # highest voltage
    vmin = float(volts.min())          # lowest voltage
    vpp = vmax - vmin                  # peak-to-peak
    avg = float(volts.mean())          # average (the DC level)

    # Frequency = (number of full cycles) / (time they took). Time is counted in samples.
    freq = None
    edges = rising_edges(volts)
    if len(edges) >= 2:
        freq = SAMPLE_RATE * (len(edges) - 1) / (edges[-1] - edges[0])

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
            f"Avg {m['avg']:.2f} V   Freq {freq}   Period {period}")


def triggered_window(volts, count):
    """Picks 'count' samples that start at a rising edge, so the picture stands still.
    Uses the newest edge that still has 'count' samples after it.
    Returns (samples, True) or, if there is no usable edge, (newest samples, False)."""
    edges = rising_edges(volts)
    for e in reversed(edges):
        start = int(math.ceil(e))
        if start + count <= len(volts):
            return volts[start:start + count], True
    return volts[-count:], False


def spectrum(volts):
    """FFT of the samples. Returns (frequencies in Hz, amplitude in volts)."""
    n = len(volts)
    window = np.hanning(n)                          # smooths the ends so the peaks are clean
    amps = np.abs(np.fft.rfft((volts - volts.mean()) * window)) * 2 / window.sum()
    freqs = np.fft.rfftfreq(n, 1 / SAMPLE_RATE)     # 0 .. 5000 Hz (half the sample rate)
    return freqs, amps


# ---------------------------------------------------------------- plotting

def save_csv(volts, folder="."):
    """Saves samples to a CSV file (columns: time_ms, volts). Returns the file name."""
    name = os.path.join(folder, time.strftime("scope_%Y%m%d_%H%M%S.csv"))
    t_ms = np.arange(len(volts)) * 1000 / SAMPLE_RATE
    np.savetxt(name, np.column_stack([t_ms, volts]), delimiter=",",
               header="time_ms,volts", comments="", fmt="%.4f")
    return name


def run_plot(decoder, title):
    fig = plt.figure(figsize=(11, 7))
    ax = fig.add_axes([0.08, 0.47, 0.88, 0.47])      # the wave
    ax_fft = fig.add_axes([0.08, 0.17, 0.88, 0.15])  # the spectrum (hidden until FFT is on)
    ax_fft.set_visible(False)

    line, = ax.plot([], [], lw=1.2)
    ax.set_xlabel("time (ms)")
    ax.set_ylabel("voltage (V)")
    ax.set_title(title)
    ax.grid(True)
    info = ax.text(0.01, 0.97, "", transform=ax.transAxes, va="top", fontsize=9,
                   bbox=dict(facecolor="white", alpha=0.7, edgecolor="none"))
    fft_line, = ax_fft.plot([], [], lw=1)
    ax_fft.set_xlim(0, SAMPLE_RATE / 2)
    ax_fft.set_xlabel("frequency (Hz)")
    ax_fft.set_ylabel("V")
    ax_fft.grid(True)
    fft_peak = ax_fft.text(0.99, 0.9, "", transform=ax_fft.transAxes, ha="right", va="top", fontsize=9)
    readout = fig.text(0.08, 0.385, "", va="top", family="monospace", fontsize=9)

    # What the window remembers between updates.
    state = {"paused": False,    # True = picture is frozen
             "trigger": True,    # True = start each picture at a rising edge
             "ac": False,        # True = AC coupling (average removed)
             "marks": False,     # True = dot on each sample
             "fft": False,       # True = show the spectrum
             "time_i": 2,        # index into TIME_WINDOWS_MS (20 ms)
             "volt_i": 0,        # index into VOLT_SPANS (3.3 V)
             "shown": None,      # samples on screen right now (what Save writes)
             "message": ""}      # short note, e.g. "saved ..."

    def set_axes():
        """Applies the time window, volts zoom and AC/DC choice to the axes."""
        ax.set_xlim(0, TIME_WINDOWS_MS[state["time_i"]])
        span = VOLT_SPANS[state["volt_i"]]
        if state["ac"]:
            ax.set_ylim(-span / 2, span / 2)            # AC: centred on 0 V
        elif span >= VREF:
            ax.set_ylim(0, VREF)                        # DC full scale: 0 .. 3.3 V
        else:
            centre = 1.65                               # DC zoomed: centred on the signal
            m = measure(decoder.get_volts(MEASURE_SAMPLES))
            if m:
                centre = m["avg"]
            ax.set_ylim(centre - span / 2, centre + span / 2)
        ax.set_ylabel("voltage (V, AC)" if state["ac"] else "voltage (V)")
        fig.canvas.draw_idle()

    # --- button actions (each one flips a setting and updates the label) ---
    def toggle_pause(_e=None):
        state["paused"] = not state["paused"]
        state["message"] = "PAUSED" if state["paused"] else ""
        buttons["pause"].label.set_text("Resume" if state["paused"] else "Pause")
        fig.canvas.draw_idle()

    def do_save(_e=None):
        if state["shown"] is None or len(state["shown"]) == 0:
            state["message"] = "nothing to save yet"
        else:
            state["message"] = "saved " + os.path.basename(save_csv(state["shown"]))
        fig.canvas.draw_idle()

    def toggle_trigger(_e=None):
        state["trigger"] = not state["trigger"]
        buttons["trig"].label.set_text("Trig ON" if state["trigger"] else "Trig OFF")
        fig.canvas.draw_idle()

    def toggle_ac(_e=None):
        state["ac"] = not state["ac"]
        buttons["ac"].label.set_text("AC" if state["ac"] else "DC")
        set_axes()

    def toggle_marks(_e=None):
        state["marks"] = not state["marks"]
        line.set_marker("o" if state["marks"] else "")
        line.set_markersize(3)
        fig.canvas.draw_idle()

    def toggle_fft(_e=None):
        state["fft"] = not state["fft"]
        ax_fft.set_visible(state["fft"])
        fig.canvas.draw_idle()

    def next_time(_e=None):
        state["time_i"] = (state["time_i"] + 1) % len(TIME_WINDOWS_MS)
        buttons["time"].label.set_text(f"{TIME_WINDOWS_MS[state['time_i']]} ms")
        set_axes()

    def next_volts(_e=None):
        state["volt_i"] = (state["volt_i"] + 1) % len(VOLT_SPANS)
        buttons["volts"].label.set_text(f"{VOLT_SPANS[state['volt_i']]} V")
        set_axes()

    # --- one row of buttons along the bottom ---
    specs = [("pause", "Pause", toggle_pause), ("save", "Save CSV", do_save),
             ("trig", "Trig ON", toggle_trigger), ("ac", "DC", toggle_ac),
             ("marks", "Marks", toggle_marks), ("fft", "FFT", toggle_fft),
             ("time", "20 ms", next_time), ("volts", "3.3 V", next_volts)]
    buttons = {}
    for i, (key, label, action) in enumerate(specs):
        b = Button(fig.add_axes([0.08 + i * 0.111, 0.03, 0.10, 0.06]), label)
        b.on_clicked(action)
        buttons[key] = b

    keys = {" ": toggle_pause, "c": do_save, "t": toggle_trigger, "a": toggle_ac,
            "m": toggle_marks, "f": toggle_fft, "right": next_time, "up": next_volts}
    fig.canvas.mpl_connect("key_press_event",
                           lambda e: keys[e.key]() if e.key in keys else None)

    set_axes()

    def update(_frame):
        if not state["paused"]:
            count = TIME_WINDOWS_MS[state["time_i"]] * SAMPLE_RATE // 1000   # samples on screen
            recent = decoder.get_volts(DRAW_SAMPLES)

            if len(recent) > 0:
                if state["trigger"]:
                    shown, locked = triggered_window(recent, count)
                else:
                    shown, locked = recent[-count:], False
                state["shown"] = shown

                drawn = shown - shown.mean() if state["ac"] else shown   # AC removes the average
                t_ms = np.arange(len(drawn)) * 1000 / SAMPLE_RATE
                line.set_data(t_ms, drawn)

                m = measure(recent[-MEASURE_SAMPLES:])
                text = readout_text(m)
                if state["trigger"]:
                    text += "   [trig: " + ("locked" if locked else "searching") + "]"
                readout.set_text(text)

                if state["fft"] and len(recent) >= MEASURE_SAMPLES:
                    freqs, amps = spectrum(recent[-MEASURE_SAMPLES:])
                    fft_line.set_data(freqs, amps)
                    ax_fft.set_ylim(0, max(0.05, amps.max() * 1.1))
                    fft_peak.set_text(f"FFT peak: {freqs[amps.argmax()]:.0f} Hz")

        link = "connected" if decoder.connected else "NO LINK"
        text = (f"{link} | samples/s: {decoder.samples_per_sec} | "
                f"lost: {decoder.lost} | stray bytes: {decoder.stray_bytes}")
        if state["message"]:
            text += "\n" + state["message"]
        info.set_text(text)
        return line, info, readout, fft_line

    anim = FuncAnimation(fig, update, interval=50, blit=False, cache_frame_data=False)
    plt.show()
    return anim


# ---------------------------------------------------------------- self-test

def self_test():
    """Feeds known bytes (with garbage mixed in) and checks what comes out."""
    d = Decoder(print_status=False)
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

    # Measurements on all 4 shapes at the 3 required frequencies.
    n = np.arange(MEASURE_SAMPLES)
    for shape in ("sine", "square", "ramp", "triangle"):
        for hz in (100, 500, 1000):
            v = np.array([fake_value(shape, hz, i) for i in n])
            m = measure(v)
            assert abs(m["freq"] - hz) < hz * 0.01, (shape, hz, m["freq"])
            # At 1 kHz there are only 10 samples per cycle, so a sample can miss the exact
            # peak and Vpp reads a little low. That is a sampling limit, not a bug.
            assert 2.7 < m["vpp"] <= 3.0001, (shape, hz, m["vpp"])

    # Trigger: the triggered picture must start near the middle level, going up.
    v = 1.65 + 1.5 * np.sin(2 * np.pi * 500 * n / SAMPLE_RATE + 1.0)
    shown, locked = triggered_window(v, 200)
    assert locked and len(shown) == 200 and abs(shown[0] - 1.65) < 0.5 and shown[1] > shown[0]

    # FFT: the biggest peak of a 500 Hz sine is at 500 Hz, about 1.5 V tall.
    freqs, amps = spectrum(v)
    assert abs(freqs[amps.argmax()] - 500) <= 5 and abs(amps.max() - 1.5) < 0.1

    # Saving a CSV: write 3 samples and read the file back.
    import tempfile
    with tempfile.TemporaryDirectory() as folder:
        name = save_csv(np.array([0.0, 1.65, 3.3]), folder)
        lines = open(name).read().split()
        assert lines == ["time_ms,volts", "0.0000,0.0000", "0.1000,1.6500", "0.2000,3.3000"], lines
    print("OK")


# ---------------------------------------------------------------- main

def main():
    if len(sys.argv) < 2:
        print("usage: python scope.py COM7 | fake [sine|square|ramp|triangle] [Hz] | selftest")
        return
    arg = sys.argv[1]
    if arg == "selftest":
        self_test()
        return

    decoder = Decoder()
    stop = threading.Event()
    if arg == "fake":
        shape = sys.argv[2] if len(sys.argv) > 2 else "sine"
        hz = float(sys.argv[3]) if len(sys.argv) > 3 else 500
        worker = threading.Thread(target=fake_source, args=(decoder, stop, shape, hz), daemon=True)
        title = f"Scope - fake {shape} {hz:g} Hz"
    else:
        worker = threading.Thread(target=reader, args=(arg, decoder, stop), daemon=True)
        title = f"Scope - {arg}"
    worker.start()

    run_plot(decoder, title)   # blocks until the window is closed
    stop.set()


if __name__ == "__main__":
    main()
