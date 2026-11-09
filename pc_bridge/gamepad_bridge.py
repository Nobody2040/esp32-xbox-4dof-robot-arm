import os
import sys
import time
import pygame
import serial
import serial.tools.list_ports

BAUD_RATE = 115200

def find_esp32_port():
    """Auto-detects the ESP32 serial port or uses ESP32_PORT environment variable."""
    env_port = os.environ.get("ESP32_PORT")
    if env_port:
        return env_port

    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        desc = (p.description or "").lower()
        if any(chip in desc for chip in ["cp210", "ch340", "usb serial", "uart", "esp32"]):
            return p.device

    if ports:
        return ports[0].device
    return "COM3"

def main():
    pygame.init()
    pygame.joystick.init()

    if pygame.joystick.get_count() == 0:
        print("Error: No controller detected. Plug in your 2.4GHz USB dongle and turn on the gamepad.")
        sys.exit(1)

    joy = pygame.joystick.Joystick(0)
    joy.init()
    print(f"Connected to Gamepad: {joy.get_name()}")

    serial_port = find_esp32_port()
    try:
        ser = serial.Serial(serial_port, BAUD_RATE, timeout=0.05)
        time.sleep(2.0)
        ser.reset_input_buffer()
        print(f"Connected to ESP32 on {serial_port}")
    except Exception as exc:
        print(f"Failed to open {serial_port}. Ensure Arduino Serial Monitor is closed.\nDetails: {exc}")
        sys.exit(1)

    print("\nSingle-Motor-At-A-Time Controls:")
    print(" - Left Stick Left/Right  : Base     (GPIO 13)")
    print(" - Left Stick Up/Down     : Shoulder (GPIO 26)")
    print(" - Right Stick Up/Down    : Elbow    (GPIO 14)")
    print(" - Right Stick Left/Right : Gripper  (GPIO 27)")
    print(" - 'X' Button             : Sequential Center Reset")
    print("-" * 70)

    last_packet = ""

    try:
        while True:
            pygame.event.pump()

            lx = int(joy.get_axis(0) * 255)
            ly = int(joy.get_axis(1) * 255)
            rx = int(joy.get_axis(2) * 255)
            ry = int(joy.get_axis(3) * 255)

            if abs(lx) < 25: lx = 0
            if abs(ly) < 25: ly = 0
            if abs(rx) < 25: rx = 0
            if abs(ry) < 25: ry = 0

            btn_x = joy.get_button(2)
            packet = f"{lx},{ly},{rx},{ry},{btn_x}\n"

            if any([lx, ly, rx, ry, btn_x]) or packet != last_packet:
                ser.write(packet.encode("utf-8"))
                last_packet = packet

            while ser.in_waiting > 0:
                reply = ser.readline().decode("utf-8", errors="ignore").strip()
                if reply:
                    print(reply)

            time.sleep(0.02)

    except KeyboardInterrupt:
        print("\nClosing serial connection...")
        ser.close()
        pygame.quit()

if __name__ == "__main__":
    main()
