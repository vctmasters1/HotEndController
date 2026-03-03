# ESP32 Hotend Controller — Klipper extra
#
# Bidirectional I2C communication with an ESP32-C3 independent
# hotend controller via the EBB2209 (RP2040) CAN-bus MCU.
#
# Sends:  target (set) temperature → ESP32 (register 0x01, write)
# Reads:  actual temperature       ← ESP32 (2-byte read)
#
# The ESP32-C3 reads the actual temperature itself (PT1000 ADC),
# runs its own PID heater control, and displays both temps
# on its built-in OLED.
#
# Because we can read the actual temp back, this module:
#   - Exposes hotend2 as a temperature sensor in Klipper dashboard
#   - Supports blocking M109_HOTEND2 (wait for temp)
#
# INSTALLATION
#   Copy this file to:  ~/klipper/klippy/extras/esp32_temp_display.py
#   Then add the [esp32_temp_display] section to your printer.cfg
#   (see ESP-PT1000-2.cfg).
#
# I2C PROTOCOL
#   Slave address : 0x50  (7-bit)
#   Write reg 0x01: [set_hi, set_lo] — uint16 BE, °C × 10
#                   Send 0 to turn the heater off.
#   Read (2 bytes): [act_hi, act_lo] — uint16 BE, °C × 10
#                   0xFFFF = sensor fault

import logging


KELVIN_TO_CELSIUS = -273.15


class ESP32TempDisplay:
    def __init__(self, config):
        self.printer = config.get_printer()
        self.name = config.get_name()
        self.reactor = self.printer.get_reactor()

        # ── I2C bus (via Klipper's bus module) ───────────────
        from . import bus
        self.i2c = bus.MCU_I2C_from_config(
            config, default_addr=0x50, default_speed=100000
        )

        # ── Configuration ────────────────────────────────────
        self.update_interval = config.getfloat(
            "update_interval", 1.0, minval=0.1
        )

        self.update_timer = None
        self.hotend2_target = 0.0          # independent target for hotend2
        self.hotend2_actual = 0.0          # last read actual temp
        self.hotend2_fault  = False        # sensor fault flag
        self.min_extrude_temp = config.getfloat(
            "min_extrude_temp", 170.0
        )

        # ── Register as a temperature sensor for the dashboard ──
        self.printer.register_event_handler(
            "klippy:ready", self._handle_ready
        )

        # ── GCode commands ───────────────────────────────────
        gcode = self.printer.lookup_object("gcode")
        gcode.register_command(
            "SET_HOTEND2_TEMP",
            self.cmd_SET_HOTEND2_TEMP,
            desc="Set the ESP32-controlled secondary hotend temperature",
        )
        gcode.register_command(
            "HOTEND2_OFF",
            self.cmd_HOTEND2_OFF,
            desc="Turn off the ESP32-controlled secondary hotend",
        )
        gcode.register_command(
            "WAIT_HOTEND2",
            self.cmd_WAIT_HOTEND2,
            desc="Block until hotend2 reaches target (±tolerance)",
        )

    # ── Temperature reporting for Klipper dashboard ──────────
    def get_temp(self, eventtime):
        """Return (actual, target) for the temperature display."""
        return self.hotend2_actual, self.hotend2_target

    def get_status(self, eventtime):
        return {
            'temperature': self.hotend2_actual,
            'target': self.hotend2_target,
            'fault': self.hotend2_fault,
        }

    # ── Klippy ready ─────────────────────────────────────────
    def _handle_ready(self):
        self.update_timer = self.reactor.register_timer(
            self._update_cycle, self.reactor.NOW
        )

    # ── Periodic I2C cycle: write target + read actual ───────
    def _update_cycle(self, eventtime):
        try:
            self._send_set_temp(self.hotend2_target)
            self._read_actual_temp()
        except Exception:
            logging.exception("esp32_temp_display: update error")
        return eventtime + self.update_interval

    # ── Read actual temperature from ESP32 ───────────────────
    def _read_actual_temp(self):
        try:
            params = self.i2c.i2c_read([], 2)
            response = bytearray(params['response'])
            raw = (response[0] << 8) | response[1]
            if raw == 0xFFFF:
                self.hotend2_fault = True
                logging.warning("esp32_temp_display: sensor fault (0xFFFF)")
            else:
                self.hotend2_actual = raw / 10.0
                self.hotend2_fault = False
        except Exception:
            logging.exception("esp32_temp_display: I2C read error")

    # ── GCode: SET_HOTEND2_TEMP S=<temp> ─────────────────────
    def cmd_SET_HOTEND2_TEMP(self, gcmd):
        temp = gcmd.get_float("S", None)
        if temp is None:
            gcmd.respond_info(
                "Hotend 2: actual=%.1f°C target=%.1f°C"
                % (self.hotend2_actual, self.hotend2_target)
            )
            return
        self.hotend2_target = temp
        self._send_set_temp(temp)
        gcmd.respond_info("Hotend 2 target = %.1f °C" % temp)

    # ── GCode: HOTEND2_OFF ───────────────────────────────────
    def cmd_HOTEND2_OFF(self, gcmd):
        self.hotend2_target = 0.0
        self._send_set_temp(0.0)
        gcmd.respond_info("Hotend 2 OFF")

    # ── GCode: WAIT_HOTEND2 — block until temp reached ───────
    def cmd_WAIT_HOTEND2(self, gcmd):
        tolerance = gcmd.get_float("TOLERANCE", 3.0)
        timeout   = gcmd.get_float("TIMEOUT", 600.0)     # 10 min max
        target    = self.hotend2_target
        if target <= 0.0:
            gcmd.respond_info("Hotend 2 target is 0 — nothing to wait for")
            return
        gcmd.respond_info(
            "Waiting for hotend 2 to reach %.1f°C (±%.1f°C, timeout %.0fs)"
            % (target, tolerance, timeout)
        )
        eventtime = self.reactor.monotonic()
        deadline  = eventtime + timeout
        while eventtime < deadline:
            # Trigger a fresh read
            try:
                self._read_actual_temp()
            except Exception:
                pass
            if self.hotend2_fault:
                raise gcmd.error("Hotend 2 sensor fault — aborting wait")
            if abs(self.hotend2_actual - target) <= tolerance:
                gcmd.respond_info(
                    "Hotend 2 reached %.1f°C (target %.1f°C)"
                    % (self.hotend2_actual, target)
                )
                return
            # Pause 1 second before next poll
            eventtime = self.reactor.pause(eventtime + 1.0)
        raise gcmd.error(
            "Hotend 2 timeout — actual %.1f°C, target %.1f°C after %.0fs"
            % (self.hotend2_actual, target, timeout)
        )

    # ── I2C write: register 0x01 — set temperature ───────────
    def _send_set_temp(self, temp_c):
        raw = max(0, int(temp_c * 10)) & 0xFFFF
        data = [
            0x01,                                # register
            (raw >> 8) & 0xFF, raw & 0xFF,       # uint16 BE
        ]
        self.i2c.i2c_write(data)


def load_config(config):
    return ESP32TempDisplay(config)
