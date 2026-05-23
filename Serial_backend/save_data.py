import serial
import sqlite3
from datetime import datetime
import time
from serial.tools import list_ports

BAUD_RATE = 9600

def find_arduino_port():
    ports = list(list_ports.comports())

    print("Available ports:")
    for port in ports:
        print(f"- {port.device}: {port.description}")

    for port in ports:
        description = port.description.lower()
        if "ch340" in description or "usb-serial" in description or "arduino" in description:
            return port.device

    return None


SERIAL_PORT = find_arduino_port()

if SERIAL_PORT is None:
    print("No Arduino/CH340 port found.")
    print("Check USB cable, Arduino connection, and Device Manager.")
    exit()

print(f"Using port: {SERIAL_PORT}")

conn = sqlite3.connect("smart_room.db")
cursor = conn.cursor()

cursor.execute("""
CREATE TABLE IF NOT EXISTS smart_room_data (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp TEXT,
    people_count INTEGER,
    temperature REAL,
    humidity REAL,
    heater_state TEXT,
    door_state TEXT
)
""")

conn.commit()

try:
    arduino = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    time.sleep(2)

    print("Connected to Arduino. Waiting for DATA lines...")

    while True:
        line = arduino.readline().decode("utf-8", errors="ignore").strip()

        if line:
            print("Received:", line)

        if line.startswith("DATA,"):
            parts = line.split(",")

            if len(parts) == 6:
                people_count = int(parts[1])
                temperature = float(parts[2])
                humidity = float(parts[3])
                heater_state = parts[4]
                door_state = parts[5]

                timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

                cursor.execute("""
                INSERT INTO smart_room_data
                (timestamp, people_count, temperature, humidity, heater_state, door_state)
                VALUES (?, ?, ?, ?, ?, ?)
                """, (
                    timestamp,
                    people_count,
                    temperature,
                    humidity,
                    heater_state,
                    door_state
                ))

                conn.commit()

                print(
                    f"Saved | {timestamp} | People: {people_count} | "
                    f"T: {temperature} C | H: {humidity}% | "
                    f"Heater: {heater_state} | Door: {door_state}"
                )

except serial.SerialException as e:
    print("Serial port error:")
    print(e)
    print()
    print("Fix:")
    print("- Close Arduino Serial Monitor")
    print("- Close Serial Plotter")
    print("- Close any other program using the COM port")
    print("- Unplug/replug Arduino")
    print("- Run again")

except KeyboardInterrupt:
    print("Stopped by user.")

finally:
    try:
        arduino.close()
    except:
        pass

    conn.close()