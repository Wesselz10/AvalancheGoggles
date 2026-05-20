import time
import board
import busio
import neopixel
import analogio
import adafruit_vl53l0x
import adafruit_tca9548a

# --- LEDs ---
pin_leds_left  = board.D6
pin_leds_right = board.D8
num_leds = 1
leds_left = neopixel.NeoPixel(
    pin_leds_left,
    num_leds,
    auto_write=False,
    pixel_order=neopixel.RGB
)
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

tof_right_back = adafruit_vl53l0x.VL53L0X(tca[5])

# --- Config ---
MIN_MM       = 50
MAX_MM       = 950
DETECT_MM    = 800
LED_OFF      = (0, 0, 0)

# --- Approach confirmation ---
CONFIRM_NEEDED = 3
DELTA_THRESH   = 20

confirm_count      = 0
confirmed_approach = False
prev_dist_mm       = -1

# --- Fade state ---
fade_brightness = 0.0
FADE_IN_STEP    = 0.08
FADE_OUT_STEP   = 0.08

def read_sensor(sensor):
    try:
        return sensor.range
    except Exception:
        return MAX_MM

def get_brightness():
    return brightness_pot.value / 65535

def distance_to_color(mm, brightness, fade):
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
    combined = brightness * fade
    red   = int(red   * combined)
    green = int(green * combined)
    return (green, red, 0)

while True:
    try:
        brightness = get_brightness()
        right_mm   = read_sensor(tof_right_back)

        # ── Direction detection ──────────────────────────────
        if prev_dist_mm >= 0:
            delta = prev_dist_mm - right_mm

            if delta > DELTA_THRESH:
                confirm_count = min(confirm_count + 1, CONFIRM_NEEDED)
            elif delta < -DELTA_THRESH:
                confirm_count      = 0
                confirmed_approach = False

            if confirm_count >= CONFIRM_NEEDED:
                confirmed_approach = True

        prev_dist_mm = right_mm

        # ── Fade in / fade out ───────────────────────────────
        if confirmed_approach:
            fade_brightness = min(1.0, fade_brightness + FADE_IN_STEP)
        else:
            fade_brightness = max(0.0, fade_brightness - FADE_OUT_STEP)

        # ── LED output — both LEDs identical ─────────────────
        if fade_brightness > 0.0:
            color = distance_to_color(right_mm, brightness, fade_brightness)
        else:
            color = LED_OFF

        leds_left.fill(color)
        leds_right.fill(color)
        leds_left.show()
        leds_right.show()

        print(f"{right_mm}")

    except Exception as e:
        print(f"Error: {e}")
        time.sleep(0.5)

    time.sleep(0.05)
