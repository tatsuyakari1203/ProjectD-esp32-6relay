import csv
import datetime
import random
import time
import json
import paho.mqtt.client as mqtt

BROKER = "karis.cloud"
PORT = 1883
DEVICE_ID = "esp32_6relay"
TOPIC_CONTROL = f"irrigation/{DEVICE_ID}/control"
TOPIC_STATUS = f"irrigation/{DEVICE_ID}/status"
API_KEY = "8a679613-019f-4b88-9068-da10f09dcdd2"

NUM_TESTS = 10
WAIT_BETWEEN = 3
RELAY_IDS = [1, 2, 3, 4, 5, 6]

latency_records = []
start_times = {}
waiting_for = set()

def get_time_period():
    hour = datetime.datetime.now().hour
    if 5 <= hour < 12:
        return "morning"
    elif 12 <= hour < 18:
        return "afternoon"
    else:
        return "evening"

def on_connect(client, userdata, flags, rc):
    client.subscribe(TOPIC_STATUS)

def on_message(client, userdata, msg):
    global waiting_for
    payload = json.loads(msg.payload.decode())
    timestamp = datetime.datetime.now().isoformat()

    for relay in payload.get("relays", []):
        rid = relay["id"]
        if rid in waiting_for and relay["state"] == expected_states[rid]:
            latency = time.time() - start_times[rid]
            latency_ms = latency * 1000
            record = {
                "relay_id": rid,
                "state": expected_states[rid],
                "latency_ms": round(latency_ms, 2),
                "timestamp": timestamp,
                "period": get_time_period()
            }
            latency_records.append(record)
            waiting_for.remove(rid)

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect(BROKER, PORT, 60)
client.loop_start()

for i in range(NUM_TESTS):
    expected_states = {}
    relays_payload = []

    print(f"\n🔁 Test {i+1}/{NUM_TESTS}")
    for rid in RELAY_IDS:
        state = bool(random.getrandbits(1))
        expected_states[rid] = state
        relays_payload.append({"id": rid, "state": state})
        start_times[rid] = time.time()

    waiting_for = set(RELAY_IDS)
    payload = {
        "api_key": API_KEY,
        "relays": relays_payload
    }
    client.publish(TOPIC_CONTROL, json.dumps(payload))

    timeout = time.time() + 5
    while waiting_for and time.time() < timeout:
        time.sleep(1)

    for rid in list(waiting_for):
        record = {
            "relay_id": rid,
            "state": expected_states[rid],
            "latency_ms": None,
            "timestamp": datetime.datetime.now().isoformat(),
            "period": get_time_period()
        }
        latency_records.append(record)
        waiting_for.remove(rid)

    time.sleep(WAIT_BETWEEN)

client.loop_stop()

with open("latency_log.csv", mode="w", newline="") as file:
    writer = csv.DictWriter(file, fieldnames=["timestamp", "period", "relay_id", "state", "latency_ms"])
    writer.writeheader()
    for record in latency_records:
        writer.writerow(record)

print("✅ Đã lưu file CSV: latency_log.csv")
