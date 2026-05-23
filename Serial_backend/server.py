import serial
import sqlite3
import time
import threading
from datetime import datetime
from flask import Flask, jsonify
from serial.tools import list_ports

BAUD_RATE = 9600
DB_NAME = "smart_room.db"

app = Flask(__name__)

latest_data = {
    "people_count": 0,
    "temperature": None,
    "humidity": None,
    "heater_state": "OFF",
    "door_state": "CLOSED",
    "room_status": "UNKNOWN",
    "timestamp": None
}


def find_arduino_port():
    ports = list(list_ports.comports())

    print("Available ports:", flush=True)
    for port in ports:
        print(f"- {port.device}: {port.description}", flush=True)

    for port in ports:
        description = port.description.lower()
        if "ch340" in description or "usb-serial" in description or "arduino" in description:
            return port.device

    return None


def init_database():
    conn = sqlite3.connect(DB_NAME)
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
    conn.close()


def save_to_database(data):
    conn = sqlite3.connect(DB_NAME)
    cursor = conn.cursor()

    cursor.execute("""
    INSERT INTO smart_room_data
    (timestamp, people_count, temperature, humidity, heater_state, door_state)
    VALUES (?, ?, ?, ?, ?, ?)
    """, (
        data["timestamp"],
        data["people_count"],
        data["temperature"],
        data["humidity"],
        data["heater_state"],
        data["door_state"]
    ))

    conn.commit()
    conn.close()


def read_latest_from_database():
    conn = sqlite3.connect(DB_NAME)
    cursor = conn.cursor()

    cursor.execute("""
    SELECT timestamp, people_count, temperature, humidity, heater_state, door_state
    FROM smart_room_data
    ORDER BY id DESC
    LIMIT 1
    """)

    row = cursor.fetchone()
    conn.close()

    if row is None:
        return latest_data

    return {
        "timestamp": row[0],
        "people_count": row[1],
        "temperature": row[2],
        "humidity": row[3],
        "heater_state": row[4],
        "door_state": row[5],
        "room_status": "FULL" if row[1] >= 4 else "AVAILABLE"
    }


def read_arduino():
    global latest_data

    serial_port = find_arduino_port()

    if serial_port is None:
        print("No Arduino port found. Check USB/COM port.", flush=True)
        return

    print(f"Connecting to Arduino on {serial_port}...", flush=True)

    try:
        arduino = serial.Serial(serial_port, BAUD_RATE, timeout=1)
        time.sleep(2)

        print("Connected to Arduino. Waiting for DATA lines...", flush=True)

        while True:
            line = arduino.readline().decode("utf-8", errors="ignore").strip()

            if line:
                print("Received:", line, flush=True)

            if line.startswith("DATA,"):
                parts = line.split(",")

                # Arduino may send either:
                # DATA,people,temperature,humidity,heater,door
                # or:
                # DATA,people,temperature,humidity,heater,door,status
                if len(parts) >= 6:
                    try:
                        room_status = parts[6] if len(parts) >= 7 else "UNKNOWN"

                        data = {
                            "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                            "people_count": int(parts[1]),
                            "temperature": float(parts[2]),
                            "humidity": float(parts[3]),
                            "heater_state": parts[4],
                            "door_state": parts[5],
                            "room_status": room_status
                        }

                        latest_data = data
                        save_to_database(data)

                        print(
                            f"Saved | {data['timestamp']} | "
                            f"People: {data['people_count']} | "
                            f"T: {data['temperature']} C | "
                            f"H: {data['humidity']}% | "
                            f"Heater: {data['heater_state']} | "
                            f"Door: {data['door_state']} | "
                            f"Status: {data['room_status']}",
                            flush=True
                        )

                    except Exception as e:
                        print("DATA parsing error:", e, flush=True)
                        print("Bad DATA line:", line, flush=True)

    except Exception as e:
        print("Arduino reading error:", e, flush=True)


@app.route("/latest", methods=["GET"])
def get_latest():
    # If Arduino has updated latest_data, return it.
    # If not, return latest saved database value.
    if latest_data["timestamp"] is not None:
        return jsonify(latest_data)

    return jsonify(read_latest_from_database())


@app.route("/history", methods=["GET"])
def get_history():
    conn = sqlite3.connect(DB_NAME)
    cursor = conn.cursor()

    cursor.execute("""
    SELECT timestamp, people_count, temperature, humidity, heater_state, door_state
    FROM smart_room_data
    ORDER BY id DESC
    LIMIT 20
    """)

    rows = cursor.fetchall()
    conn.close()

    history = []

    for row in rows:
        history.append({
            "timestamp": row[0],
            "people_count": row[1],
            "temperature": row[2],
            "humidity": row[3],
            "heater_state": row[4],
            "door_state": row[5]
        })

    return jsonify(history)


if __name__ == "__main__":
    init_database()

    arduino_thread = threading.Thread(target=read_arduino)
    arduino_thread.daemon = True
    arduino_thread.start()

    print("Starting Flask server...", flush=True)
    app.run(host="0.0.0.0", port=5000, debug=False)