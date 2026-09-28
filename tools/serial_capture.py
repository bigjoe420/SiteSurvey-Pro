import sys, time, serial

SECONDS = int(sys.argv[1]) if len(sys.argv) > 1 else 75
OUT = sys.argv[2] if len(sys.argv) > 2 else 'tools/scroll_verify_capture.txt'

# RTS/DTR are wired to the ESP32 auto-reset circuit (EN / BOOT). pyserial
# asserts them on open, which pulsed EN and reset the device at every
# capture start — destroying the very crash state we were trying to record.
# Deassert both immediately after open (before any read).
port = serial.Serial('COM3', 115200, timeout=1)
port.setRTS(False)
port.setDTR(False)
start = time.time()
n = 0
with open(OUT, 'w', encoding='utf-8', errors='replace') as f:
    while time.time() - start < SECONDS:
        data = port.read(4096)
        if data:
            f.write(data.decode('utf-8', errors='replace'))
            f.flush()
            n += len(data)
port.close()
print(f'captured {n} bytes in {SECONDS}s -> {OUT}')
