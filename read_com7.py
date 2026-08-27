import serial, time, sys

PORT = "COM7"
BAUD = 115200

s = serial.Serial(PORT, BAUD, timeout=1)
# Send reset via RTS/DTR toggling equivalent (Ctrl+R in some tools)
# Actually just read for 8 seconds
s.flushInput()
t0 = time.time()
while time.time() - t0 < 8:
    data = s.read(max(s.in_waiting, 1))
    if data:
        sys.stdout.buffer.write(data)
        sys.stdout.flush()
    else:
        time.sleep(0.01)
s.close()
