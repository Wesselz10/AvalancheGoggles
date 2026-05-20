import serial

PICO_PORT    = "COM5"
ARDUINO_PORT = "COM7"

pico    = serial.Serial(PICO_PORT,    9600, timeout=1)
arduino = serial.Serial(ARDUINO_PORT, 9600, timeout=1)

print("Bridge running. Ctrl+C to stop.")

while True:
    line = pico.readline().decode("utf-8", errors="ignore").strip()
    if "," in line:  # only forward if it looks like valid data
        arduino.write((line + "\n").encode())
        print(line)