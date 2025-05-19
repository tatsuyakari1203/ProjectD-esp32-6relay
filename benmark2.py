import csv
import datetime
import random
import time
import json
import paho.mqtt.client as mqtt

# --- Cấu hình ---
BROKER = "karis.cloud"
PORT = 1883
DEVICE_ID = "esp32_6relay"
TOPIC_CONTROL = f"irrigation/{DEVICE_ID}/control"
TOPIC_STATUS = f"irrigation/{DEVICE_ID}/status" # Vẫn subscribe để theo dõi nếu có phản hồi
API_KEY = "8a679613-019f-4b88-9068-da10f09dcdd2"

NUM_COMMANDS = 50  # Số lượng lệnh sẽ gửi (tăng lên để stress test)
SEND_INTERVAL = 0.5 # Khoảng thời gian giữa các lần gửi lệnh (giây)
RELAY_IDS = [1, 2, 3, 4, 5, 6]

# Danh sách lưu trữ log các lệnh đã gửi
commands_sent_log = []

# --- Hàm Callback MQTT ---
def on_connect(client, userdata, flags, rc):
    """Callback khi client nhận được phản hồi CONNACK từ server."""
    if rc == 0:
        print("Đã kết nối tới MQTT Broker!")
        client.subscribe(TOPIC_STATUS) # Subscribe vào topic trạng thái để quan sát
        print(f"Đã subscribe vào topic: {TOPIC_STATUS}")
    else:
        print(f"Kết nối thất bại, mã lỗi: {rc}\n")

def on_message(client, userdata, msg):
    """Callback khi nhận được tin nhắn PUBLISH từ server."""
    # Hàm này giờ chỉ dùng để quan sát, không ảnh hưởng vòng lặp gửi lệnh.
    print(f"Nhận được tin nhắn trên topic {msg.topic}: {msg.payload.decode()}")
    # Bạn có thể tùy chọn ghi log các tin nhắn trạng thái nhận được vào file hoặc danh sách riêng
    # Hiện tại, chỉ in ra console.

def get_time_period():
    """Xác định khoảng thời gian trong ngày."""
    hour = datetime.datetime.now().hour
    if 5 <= hour < 12:
        return "morning"
    elif 12 <= hour < 18:
        return "afternoon"
    else:
        return "evening"

# --- Kịch bản chính ---
# Khởi tạo MQTT client
client_id = f"stress_test_sender_{random.randint(0,10000)}"
client = mqtt.Client(client_id=client_id) # Sử dụng client_id duy nhất
client.on_connect = on_connect
client.on_message = on_message

# Kết nối tới broker
try:
    client.connect(BROKER, PORT, 60)
except Exception as e:
    print(f"Lỗi kết nối tới MQTT Broker: {e}")
    exit()

# Bắt đầu vòng lặp client MQTT để xử lý lưu lượng mạng, dispatches, và callbacks
# Chạy trong một luồng riêng
client.loop_start()

# Vòng lặp chính để gửi lệnh
print(f"Bắt đầu gửi {NUM_COMMANDS} lệnh, mỗi lệnh cách nhau {SEND_INTERVAL} giây...")
for i in range(NUM_COMMANDS):
    # 1. Chuẩn bị payload lệnh
    relays_payload = []
    current_target_states = {} # Lưu trạng thái mục tiêu của lệnh này để ghi log

    print(f"\n🔁 Đang gửi Lệnh {i+1}/{NUM_COMMANDS}")

    for rid in RELAY_IDS:
        state = bool(random.getrandbits(1)) # Đặt trạng thái relay ngẫu nhiên
        current_target_states[rid] = state
        relays_payload.append({"id": rid, "state": state})

    command_payload_json = {
        "api_key": API_KEY,
        "relays": relays_payload
    }
    command_payload_str = json.dumps(command_payload_json)

    # 2. Publish lệnh
    timestamp_sent_dt = datetime.datetime.now()
    timestamp_sent_iso = timestamp_sent_dt.isoformat()

    # Gửi tin nhắn
    publish_result = client.publish(TOPIC_CONTROL, command_payload_str, qos=0) # Sử dụng QoS 0 cho tốc độ cao
    
    # (Tùy chọn) Chờ publish hoàn tất, hữu ích cho QoS > 0. Với QoS 0, nó trả về gần như ngay lập tức.
    # publish_result.wait_for_publish(timeout=1) 

    if publish_result.rc == mqtt.MQTT_ERR_SUCCESS:
        print(f"Lệnh {i+1} đã publish thành công tới {TOPIC_CONTROL} lúc {timestamp_sent_dt.strftime('%H:%M:%S.%f')[:-3]}")
    else:
        print(f"Thất bại khi publish lệnh {i+1}. Mã lỗi: {publish_result.rc}")


    # 3. Ghi log lệnh đã gửi
    command_log_entry = {
        "command_index": i + 1,
        "timestamp_sent": timestamp_sent_iso,
        "period": get_time_period(),
        "payload_sent": command_payload_str,
        "target_states_summary": str(current_target_states) # Ghi lại tóm tắt trạng thái mục tiêu
    }
    commands_sent_log.append(command_log_entry)

    # 4. Chờ khoảng thời gian SEND_INTERVAL trước khi gửi lệnh tiếp theo
    time.sleep(SEND_INTERVAL)

# Dừng vòng lặp client MQTT
client.loop_stop()
client.disconnect()
print("\nĐã ngắt kết nối khỏi MQTT Broker.")

# Ghi log các lệnh đã gửi ra file CSV
csv_file_name = "stress_test_commands_sent_log.csv"
try:
    with open(csv_file_name, mode="w", newline="", encoding="utf-8") as file:
        # Xác định fieldnames dựa trên các key trong command_log_entry
        fieldnames = ["command_index", "timestamp_sent", "period", "payload_sent", "target_states_summary"]
        writer = csv.DictWriter(file, fieldnames=fieldnames)
        writer.writeheader()
        for record in commands_sent_log:
            writer.writerow(record)
    print(f"✅ Log lệnh đã gửi được lưu vào file CSV: {csv_file_name}")
except IOError as e:
    print(f"Lỗi khi ghi file CSV: {e}")

