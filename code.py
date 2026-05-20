import time
import board
import busio
import neopixel
import analogio
import adafruit_vl53l0x
import adafruit_tca9548a

# --- LEDs ---
# pin_leds_left = board.D6
pin_leds_right = board.D8
num_leds = 1
# leds_left = neopixel.NeoPixel(
#     pin_leds_left,
#     num_leds,
#     auto_write=False,
#     pixel_order=neopixel.RGB
# )
leds_right = neopixel.NeoPixel(
    pin_leds_right,
    num_leds,
    auto_write=False,
    pixel_order=neopixel.RGB
)

# --- Brightness potentiometer ---
brightness_pot = analogio.AnalogIn(board.A28)

# --- TCA9548A multiplexer + ToF sensors ---
i2c = busio.I2C(board.SCL, board.SDA)
tca = adafruit_tca9548a.TCA9548A(i2c)

# Left side sensors — disabled
# tof_left           = adafruit_vl53l0x.VL53L0X(tca[0])
# tof_left_back      = adafruit_vl53l0x.VL53L0X(tca[1])
# tof_left_back_back = adafruit_vl53l0x.VL53L0X(tca[2])
# tof_left_front     = adafruit_vl53l0x.VL53L0X(tca[3])

# Right side sensors — only keep right_back
# tof_right_back_back = adafruit_vl53l0x.VL53L0X(tca[4])
tof_right_back      = adafruit_vl53l0x.VL53L0X(tca[5])
# tof_right_front    = adafruit_vl53l0x.VL53L0X(tca[6])
# tof_right          = adafruit_vl53l0x.VL53L0X(tca[7])

# --- Config ---
MIN_MM       = 50
MAX_MM       = 950
DETECT_MM    = 800
LED_OFF      = (0, 0, 0)

# --- Approach confirmation ---
CONFIRM_NEEDED = 3    # consecutive readings needed to confirm approach
DELTA_THRESH   = 20   # mm change needed to count as movement (filters noise)

confirm_count      = 0
confirmed_approach = False
prev_dist_mm       = -1

def read_sensor(sensor):
    try:
        return sensor.range
    except Exception:
        return MAX_MM

def get_brightness():
    return brightness_pot.value / 65535

def distance_to_color(mm, brightness):
    if mm >= DETECT_MM:
        return LED_OFF
    mm = max(MIN_MM, min(DETECT_MM, mm))
    t = (mm - MIN_MM) / (DETECT_MM - MIN_MM)
    if t <= 0.5:
        red   = 255
        green = int(t / 0.5 * 255)
    else:
        red   = int((1 - (t - 0.5) / 0.5) * 255)
        green = 255
    red   = int(red   * brightness)
    green = int(green * brightness)
    return (green, red, 0)

while True:
    try:
        brightness = get_brightness()
        right_mm   = read_sensor(tof_right_back)

        # ── Direction detection ──────────────────────────────
        if prev_dist_mm >= 0:
            delta = prev_dist_mm - right_mm  # positive = getting closer

            if delta > DELTA_THRESH:
                # Getting closer
                confirm_count = min(confirm_count + 1, CONFIRM_NEEDED)
            elif delta < -DELTA_THRESH:
                # Getting further away — reset immediately
                confirm_count      = 0
                confirmed_approach = False

            if confirm_count >= CONFIRM_NEEDED:
                confirmed_approach = True

        prev_dist_mm = right_mm

        # ── LED output ───────────────────────────────────────
        if confirmed_approach:
            leds_right.fill(distance_to_color(right_mm, brightness))
        else:
            leds_right.fill(LED_OFF)

        leds_right.show()

        # Always send distance to bridge regardless
        print(f"{right_mm}")

    except Exception as e:
        print(f"Error: {e}")
        time.sleep(0.5)

    time.sleep(0.05)  # small delay to avoid flooding serial
