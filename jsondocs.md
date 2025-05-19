# Tài liệu API & MQTT - Hệ thống tưới tự động ESP32-S3 6-Relay

## Tổng quan hệ thống

Hệ thống tưới tự động ESP32-S3 6-Relay là một giải pháp IoT thông minh để điều khiển tưới tiêu tự động dựa trên lịch trình và điều kiện môi trường. Hệ thống sử dụng kiến trúc lõi kép của ESP32-S3 (Core 0 cho mạng, cảm biến, lịch trình; Core 1 cho điều khiển relay) để quản lý đồng thời các tác vụ khác nhau. Giao tiếp được thực hiện thông qua MQTT để cung cấp khả năng điều khiển từ xa.

Cấu hình WiFi, MQTT và API Key được lưu trữ trong bộ nhớ NVS (Non-Volatile Storage) bằng thư viện `Preferences`. Nếu không tìm thấy cấu hình hợp lệ hoặc kết nối thất bại sau nhiều lần thử, hệ thống sẽ tự động khởi chạy Web Configuration Portal để người dùng có thể cài đặt ban đầu.

### Chức năng chính:

1.  **Điều khiển relay tưới**: Bật/tắt 6 relay (vùng tưới) độc lập.
2.  **Lập lịch tưới**: Tự động hóa tưới theo ngày trong tuần, thời gian, và các điều kiện phức tạp.
3.  **Giám sát môi trường**: Thu thập và phản ứng với dữ liệu từ cảm biến DHT21 (nhiệt độ, độ ẩm, chỉ số nhiệt) và cho phép cập nhật thủ công các giá trị môi trường khác.
4.  **Ưu tiên lịch tưới**: Xử lý xung đột giữa các lịch với cơ chế ưu tiên.
5.  **Điều kiện cảm biến**: Tưới chỉ khi thỏa mãn các điều kiện môi trường đã định cấu hình.
6.  **Báo cáo trạng thái**: Cập nhật liên tục về trạng thái hoạt động của relay và lịch tưới.
7.  **Web Configuration Portal**: Giao diện web để dễ dàng cấu hình WiFi, MQTT server, port và API key.
8.  **Logging từ xa**: Gửi log hệ thống qua MQTT và cho phép cấu hình mức độ log từ xa.

### Ứng dụng:

- Hệ thống tưới tự động cho nhà kính
- Tưới vườn thông minh
- Tưới nông nghiệp quy mô nhỏ và vừa
- Hệ thống thủy canh
- Tưới cảnh quan và công viên
- Tưới nhà kính cho nghiên cứu nông nghiệp

## Web Configuration Portal

Khi ESP32 không thể kết nối vào mạng WiFi đã cấu hình (hoặc chưa có cấu hình), nó sẽ khởi động một Access Point (AP) để người dùng có thể kết nối và cấu hình thiết bị.

-   **AP SSID**: `ESP32-Config`
-   **AP Password**: `password123` (có thể để trống nếu được cấu hình trong mã nguồn)
-   **Địa chỉ IP của Portal**: Thông thường là `192.168.4.1` (kết nối vào AP `ESP32-Config` và truy cập địa chỉ này từ trình duyệt web)
-   **Mật khẩu Admin**: `admin123` (cần thiết để lưu các thay đổi cấu hình)

### Chức năng của Portal:

-   Quét và hiển thị danh sách các mạng WiFi khả dụng.
-   Cho phép người dùng chọn mạng WiFi và nhập mật khẩu.
-   Cấu hình địa chỉ MQTT server, port và API Key.
-   Hiển thị thông tin hệ thống cơ bản.
-   Lưu trữ cấu hình vào NVS của ESP32.

## MQTT Topics

Hệ thống sử dụng các MQTT topic sau để giao tiếp:

| Topic                                   | Hướng     | Mô tả                                                                 |
| --------------------------------------- | --------- | --------------------------------------------------------------------- |
| `irrigation/esp32_6relay/sensors`       | Publish   | ESP32 gửi dữ liệu từ cảm biến (DHT21).                                |
| `irrigation/esp32_6relay/control`       | Subscribe | ESP32 nhận lệnh điều khiển relay.                                     |
| `irrigation/esp32_6relay/status`        | Publish   | ESP32 báo cáo trạng thái relay.                                        |
| `irrigation/esp32_6relay/schedule`      | Subscribe | ESP32 nhận lệnh lập lịch tưới (thêm, cập nhật, xóa).                  |
| `irrigation/esp32_6relay/schedule/status` | Publish   | ESP32 báo cáo trạng thái lịch tưới.                                    |
| `irrigation/esp32_6relay/environment`   | Subscribe | ESP32 nhận cập nhật giá trị cảm biến môi trường thủ công.             |
| `irrigation/esp32_6relay/logs`          | Publish   | ESP32 gửi log hệ thống và log hiệu năng.                             |
| `irrigation/esp32_6relay/logconfig`     | Subscribe | ESP32 nhận lệnh cấu hình mức độ log cho Serial và MQTT.                |

## Cấu trúc JSON

### 1. Dữ liệu cảm biến (`irrigation/esp32_6relay/sensors`)

ESP32 gửi dữ liệu cảm biến môi trường (từ DHT21) lên server qua topic này. Tần suất mặc định: mỗi 30 giây.

```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY",
  "timestamp": 1683123456,
  "device_info": {
    "name": "esp32_6relay",
    "type": "DHT21",
    "firmware": "1.0.0" 
  },
  "temperature": {
    "value": 28.5,
    "unit": "celsius",
    "sensor_type": "temperature"
  },
  "humidity": {
    "value": 65.2,
    "unit": "percent",
    "sensor_type": "humidity"
  },
  "heat_index": {
    "value": 30.1,
    "unit": "celsius",
    "sensor_type": "heat_index"
  }
}
```

| Trường                      | Kiểu    | Mô tả                                                              |
| --------------------------- | ------- | ------------------------------------------------------------------ |
| `api_key`                   | string  | API key đã cấu hình trên thiết bị (lấy từ NVS/Config Portal).     |
| `timestamp`                 | number  | Thời gian unix timestamp của lần đọc.                               |
| `device_info`               | object  | Thông tin về thiết bị.                                             |
| `device_info.name`          | string  | Tên thiết bị (ví dụ: "esp32_6relay").                               |
| `device_info.type`          | string  | Loại cảm biến chính (ví dụ: "DHT21").                               |
| `device_info.firmware`      | string  | Phiên bản firmware (ví dụ: "1.0.0").                                |
| `temperature`               | object  | Thông tin nhiệt độ.                                                |
| `temperature.value`         | float   | Giá trị nhiệt độ.                                                  |
| `temperature.unit`          | string  | Đơn vị nhiệt độ (ví dụ: "celsius").                                |
| `temperature.sensor_type`   | string  | Loại cảm biến (ví dụ: "temperature").                              |
| `humidity`                  | object  | Thông tin độ ẩm.                                                   |
| `humidity.value`            | float   | Giá trị độ ẩm.                                                     |
| `humidity.unit`             | string  | Đơn vị độ ẩm (ví dụ: "percent").                                   |
| `humidity.sensor_type`      | string  | Loại cảm biến (ví dụ: "humidity").                                 |
| `heat_index`                | object  | Thông tin chỉ số nhiệt.                                             |
| `heat_index.value`          | float   | Giá trị chỉ số nhiệt.                                               |
| `heat_index.unit`           | string  | Đơn vị chỉ số nhiệt (ví dụ: "celsius").                             |
| `heat_index.sensor_type`    | string  | Loại cảm biến (ví dụ: "heat_index").                               |

### 2. Điều khiển relay (`irrigation/esp32_6relay/control`)

Gửi lệnh điều khiển relay đến ESP32.

```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY", 
  "relays": [
    {
      "id": 1,
      "state": true,
      "duration": 900 
    },
    {
      "id": 2,
      "state": true
    },
    {
      "id": 5,
      "state": false
    }
  ]
}
```

| Trường              | Kiểu    | Mô tả                                                                 |
| ------------------- | ------- | --------------------------------------------------------------------- |
| `api_key`           | string  | API key (nên khớp với API key đã cấu hình trên ESP32 để xác thực).   |
| `relays`            | array   | Mảng các relay cần điều khiển.                                         |
| `relays[].id`        | number  | ID của relay (1-6).                                                   |
| `relays[].state`     | boolean | Trạng thái mong muốn (true = bật, false = tắt).                       |
| `relays[].duration`  | number  | Thời gian bật relay (tính bằng **giây**), tùy chọn. Nếu bỏ qua hoặc 0, relay sẽ bật vô thời hạn (cho đến khi có lệnh tắt). |

**Lưu ý về `duration`**: Thiết bị sẽ tự động chuyển đổi giá trị `duration` (giây) sang mili giây.

#### Trường hợp sử dụng đặc biệt - Điều khiển nhiều relay cùng lúc

```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY",
  "relays": [
    {
      "id": 1,
      "state": true,
      "duration": 900 
    },
    {
      "id": 2,
      "state": true,
      "duration": 900 
    },
    {
      "id": 3,
      "state": true,
      "duration": 900 
    }
  ]
}
```

#### Trường hợp tắt tất cả relay

```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY",
  "relays": [
    {
      "id": 1,
      "state": false
    },
    {
      "id": 2,
      "state": false
    },
    {
      "id": 3,
      "state": false
    },
    {
      "id": 4,
      "state": false
    },
    {
      "id": 5,
      "state": false
    },
    {
      "id": 6,
      "state": false
    }
  ]
}
```

### 3. Trạng thái relay (`irrigation/esp32_6relay/status`)

ESP32 báo cáo trạng thái của tất cả relay. Tần suất: Khi có thay đổi trạng thái relay, hoặc mỗi 5 phút (báo cáo cưỡng bức).

```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY",
  "timestamp": 1683123456,
  "relays": [
    {
      "id": 1,
      "state": true,
      "remaining": 599870 
    },
    {
      "id": 2,
      "state": true,
      "remaining": 0
    },
    {
      "id": 3,
      "state": false,
      "remaining": 0
    }
    // ... các relay khác
  ]
}
```

| Trường             | Kiểu    | Mô tả                                                                 |
| ------------------ | ------- | --------------------------------------------------------------------- |
| `api_key`          | string  | API key đã cấu hình trên thiết bị.                                     |
| `timestamp`        | number  | Thời gian unix timestamp của báo cáo.                                   |
| `relays`           | array   | Mảng trạng thái của tất cả relay.                                      |
| `relays[].id`       | number  | ID của relay (1-6).                                                   |
| `relays[].state`    | boolean | Trạng thái hiện tại của relay (true = bật, false = tắt).                |
| `relays[].remaining`| number  | Thời gian còn lại (tính bằng **mili giây**) mà relay sẽ tiếp tục bật. 0 nếu không có hẹn giờ hoặc relay đang tắt. |

### 4. Lập lịch tưới (`irrigation/esp32_6relay/schedule`)

Gửi lệnh lập lịch tưới (thêm mới, cập nhật hoặc xóa) đến ESP32.

#### 4.1. Thêm/Cập nhật lịch

Để thêm mới hoặc cập nhật một lịch, gửi một mảng các đối tượng `task`. Nếu `id` của task đã tồn tại, nó sẽ được cập nhật. Nếu không, một task mới sẽ được tạo.

```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY",
  "tasks": [
    {
      "id": 1,
      "active": true,
      "days": [1, 3, 5], 
      "time": "10:30",
      "duration": 15,    
      "zones": [1, 2],
      "priority": 5,
      "sensor_condition": {
        "enabled": true,
        "temperature": {
          "enabled": true,
          "min": 20,
          "max": 38
        },
        "humidity": {
          "enabled": true,
          "min": 40,
          "max": 80
        },
        "soil_moisture": {
          "enabled": true,
          "min": 30 
        },
        "rain": {
          "enabled": true,
          "skip_when_raining": true
        },
        "light": {
          "enabled": false 
        }
      }
    }
  ]
}
```

| Trường                       | Kiểu    | Mô tả                                                                                                                                  |
| ---------------------------- | ------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| `api_key`                    | string  | API key.                                                                                                                               |
| `tasks`                      | array   | Mảng các lịch tưới.                                                                                                                    |
| `tasks[].id`                 | number  | ID duy nhất của lịch tưới (do người dùng định nghĩa).                                                                                     |
| `tasks[].active`             | boolean | Trạng thái kích hoạt của lịch (true = hoạt động, false = vô hiệu hóa).                                                                    |
| `tasks[].days`               | array   | Mảng các ngày trong tuần lịch sẽ chạy (1=Thứ 2, 2=Thứ 3, ..., 7=Chủ nhật).                                                               |
| `tasks[].time`               | string  | Thời gian bắt đầu tưới (định dạng "HH:MM", 24 giờ).                                                                                      |
| `tasks[].duration`           | number  | Thời lượng tưới (tính bằng **phút**).                                                                                                  |
| `tasks[].zones`              | array   | Mảng các ID vùng tưới (relay) sẽ được kích hoạt bởi lịch này (ví dụ: `[1, 3]` nghĩa là relay 1 và relay 3).                             |
| `tasks[].priority`           | number  | Mức ưu tiên của lịch (1-10, số cao hơn có ưu tiên cao hơn). Lịch có ưu tiên cao hơn sẽ ngắt lịch ưu tiên thấp hơn nếu có xung đột vùng. |
| `tasks[].sensor_condition`   | object  | (Tùy chọn) Đối tượng chứa các điều kiện cảm biến để lịch chạy. Xem chi tiết ở mục "Chi tiết về điều kiện cảm biến".                   |

**Lưu ý về `days`**: Bên trong ESP32, mảng `days` này được chuyển đổi thành một bitmap, với bit 0 đại diện cho Chủ nhật, bit 1 cho Thứ 2, ..., bit 6 cho Thứ 7.

#### 4.2. Xóa lịch tưới

Để xóa một hoặc nhiều lịch tưới, gửi một mảng các ID lịch cần xóa.

```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY",
  "delete_tasks": [1, 2] 
}
```

| Trường             | Kiểu    | Mô tả                                 |
| ------------------ | ------- | ------------------------------------- |
| `api_key`          | string  | API key.                              |
| `delete_tasks`     | array   | Mảng các `id` của lịch tưới cần xóa. |

#### 4.3. Thêm nhiều lịch tưới cùng lúc

Gửi một mảng `tasks` như trong mục 4.1.

#### 4.4. Vô hiệu hóa lịch tưới (không xóa)

Cập nhật một lịch hiện có bằng cách đặt trường `active` thành `false`.

```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY",
  "tasks": [
    {
      "id": 1,
      "active": false,
      "days": [1, 3, 5], 
      "time": "06:00",
      "duration": 15,
      "zones": [1, 2],
      "priority": 5
    }
  ]
}
```

### 5. Trạng thái lịch tưới (`irrigation/esp32_6relay/schedule/status`)

ESP32 báo cáo trạng thái của tất cả các lịch tưới đã cấu hình. Tần suất: Khi có thay đổi trạng thái lịch, hoặc mỗi 5 phút (báo cáo cưỡng bức).

```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY",
  "timestamp": 1683123456,
  "tasks": [
    {
      "id": 1,
      "active": true,
      "days": [1, 3, 5],
      "time": "10:30",
      "duration": 15,
      "zones": [1, 2],
      "priority": 5,
      "state": "idle", 
      "next_run": "2023-05-03 10:30:00", 
      "sensor_condition": {
        "enabled": true,
        "temperature": {"enabled": true, "min": 20, "max": 38},
        "humidity": {"enabled": true, "min": 40, "max": 80},
        "soil_moisture": {"enabled": true, "min": 30},
        "rain": {"enabled": true, "skip_when_raining": true},
        "light": {"enabled": false}
      }
    },
    {
      "id": 2,
      "active": true,
      "days": [2, 4, 6],
      "time": "18:00",
      "duration": 10,
      "zones": [3, 4],
      "priority": 3,
      "state": "running" 
    }
    // ... các lịch khác
  ]
}
```

| Trường              | Kiểu    | Mô tả                                                                  |
| ------------------- | ------- | ---------------------------------------------------------------------- |
| `api_key`           | string  | API key đã cấu hình trên thiết bị.                                      |
| `timestamp`         | number  | Thời gian unix timestamp của báo cáo.                                    |
| `tasks`             | array   | Mảng chứa trạng thái của tất cả các lịch tưới.                          |
| `tasks[].id`        | number  | ID của lịch.                                                           |
| `tasks[].active`    | boolean | Lịch có đang được kích hoạt không.                                      |
| `tasks[].days`      | array   | Ngày chạy trong tuần.                                                  |
| `tasks[].time`      | string  | Thời gian bắt đầu.                                                      |
| `tasks[].duration`  | number  | Thời lượng tưới (phút).                                                 |
| `tasks[].zones`     | array   | Các vùng tưới.                                                         |
| `tasks[].priority`  | number  | Độ ưu tiên.                                                            |
| `tasks[].state`     | string  | Trạng thái hiện tại của lịch ("idle", "running", "completed").         |
| `tasks[].next_run`  | string  | (Tùy chọn) Thời gian dự kiến chạy tiếp theo (định dạng "yyyy-MM-dd HH:mm:ss"). Có thể không tồn tại nếu lịch không active hoặc không có lần chạy hợp lệ tiếp theo. |
| `tasks[].sensor_condition` | object | (Tùy chọn) Cấu hình điều kiện cảm biến của lịch.                   |

### 6. Điều khiển môi trường (`irrigation/esp32_6relay/environment`)

Gửi cập nhật giá trị cảm biến môi trường thủ công đến ESP32. Điều này hữu ích cho các cảm biến không được kết nối trực tiếp hoặc để mô phỏng điều kiện.

#### 6.1. Cập nhật độ ẩm đất

```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY",
  "soil_moisture": {
    "zone": 1, 
    "value": 25 
  }
}
```

#### 6.2. Cập nhật trạng thái mưa

```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY",
  "rain": true 
}
```

#### 6.3. Cập nhật độ sáng

```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY",
  "light": 5000 
}
```

#### 6.4. Cập nhật nhiều cảm biến cùng lúc

```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY",
  "soil_moisture": {
    "zone": 2,
    "value": 20
  },
  "rain": false,
  "light": 10000
}
```

| Trường                      | Kiểu    | Mô tả                                                                     |
| --------------------------- | ------- | ------------------------------------------------------------------------- |
| `api_key`                   | string  | API key.                                                                 |
| `soil_moisture`             | object  | (Tùy chọn) Thông tin độ ẩm đất.                                             |
| `soil_moisture.zone`        | number  | ID vùng mà giá trị độ ẩm đất này áp dụng cho (1-6).                         |
| `soil_moisture.value`       | number  | Giá trị độ ẩm đất (%).                                                      |
| `rain`                      | boolean | (Tùy chọn) Trạng thái mưa (true = đang mưa, false = không mưa).             |
| `light`                     | number  | (Tùy chọn) Cường độ ánh sáng (lux).                                         |

### 7. Log hệ thống (`irrigation/esp32_6relay/logs`)

ESP32 gửi các thông điệp log qua topic này. Điều này bao gồm log hoạt động chung và log hiệu năng. Log được gửi nếu mức độ của thông điệp log (`level_num`) nhỏ hơn hoặc bằng mức độ log đã cấu hình cho MQTT (xem topic `logconfig`).

#### 7.1. Log thông thường

Đây là các log ghi lại sự kiện, trạng thái hoặc lỗi trong quá trình hoạt động của hệ thống.

```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY",
  "timestamp": 1683123456,
  "level_num": 4,
  "level_str": "INFO",
  "tag": "RelayMgr",
  "message": "Relay 1 turned ON for 600 seconds",
  "core_id": 0,
  "free_heap": 150000
}
```

#### 7.2. Log hiệu năng (Performance)

Đây là các log chuyên biệt để theo dõi thời gian thực thi và kết quả của các tác vụ quan trọng.

```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY",
  "timestamp": 1683123458,
  "level_num": 4,
  "level_str": "INFO",
  "tag": "Core0",
  "type": "performance",
  "event_name": "MQTTSensorDataPublish",
  "duration_ms": 150,
  "success": true,
  "details": "Payload size: 200 bytes",
  "core_id": 0,
  "free_heap": 149500
}
```

| Trường             | Kiểu    | Mô tả chi tiết                                                                                                                               |
| ------------------ | ------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| `api_key`          | string  | API key đã cấu hình trên thiết bị. Hữu ích để xác định nguồn log nếu nhiều thiết bị cùng gửi log về một topic.                                  |
| `timestamp`        | number  | Thời gian ghi log. Là Unix timestamp (số giây từ 1/1/1970) nếu NTP đã đồng bộ. Nếu NTP chưa đồng bộ, đây là `millis()` (số mili giây từ khi ESP32 khởi động). |
| `level_num`        | number  | Mã số của mức độ log. Xem bảng mô tả chi tiết các mức log ở dưới.                                                                            |
| `level_str`        | string  | Tên của mức độ log dưới dạng chuỗi (ví dụ: "INFO", "ERROR"). Xem bảng mô tả chi tiết các mức log ở dưới.                                          |
| `tag`              | string  | Tag (thẻ) giúp xác định module hoặc thành phần nào của hệ thống đã tạo ra log này. Ví dụ: "NetMgr" (NetworkManager), "RelayMgr" (RelayManager), "TaskSched" (TaskScheduler), "SensorMgr", "EnvMgr", "Core0", "Core1", "Main". Giúp lọc log dễ dàng hơn. |
| `message`          | string  | (Chỉ có trong log thông thường) Nội dung chính của thông điệp log.                                                                           |
| `core_id`          | number  | ID của lõi CPU đã tạo ra log này (0 hoặc 1 trên ESP32-S3). Hữu ích cho việc gỡ lỗi các vấn đề liên quan đến xử lý đa lõi.                    |
| `free_heap`        | number  | Dung lượng bộ nhớ heap (RAM) còn trống tại thời điểm ghi log, tính bằng bytes. Rất quan trọng để theo dõi rò rỉ bộ nhớ hoặc các tình huống bộ nhớ thấp. |
| `type`             | string  | (Chỉ có trong log hiệu năng) Xác định loại log, ví dụ: "performance".                                                                       |
| `event_name`       | string  | (Chỉ có trong log hiệu năng) Tên của sự kiện hoặc tác vụ cụ thể đang được đo lường hiệu năng (ví dụ: "SensorReadOperation", "MQTTSensorDataPublish"). |
| `duration_ms`      | number  | (Chỉ có trong log hiệu năng) Thời gian thực thi của `event_name`, tính bằng mili giây.                                                      |
| `success`          | boolean | (Chỉ có trong log hiệu năng) Cho biết `event_name` có hoàn thành thành công hay không (`true` hoặc `false`).                                     |
| `details`          | string  | (Tùy chọn, chỉ có trong log hiệu năng) Cung cấp thêm thông tin hoặc ngữ cảnh chi tiết về `event_name`.                                        |

**Mô tả chi tiết các mức độ Log (`level_num` / `level_str`):**

| `level_num` | `level_str` | Mô tả và ví dụ điển hình                                                                                                                              |
| ----------- | ----------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0           | `NONE`      | (Không dùng để gửi log) Đặc biệt, dùng để tắt hoàn toàn việc ghi log cho một target (Serial hoặc MQTT).                                                 |
| 1           | `CRITICAL`  | Các lỗi nghiêm trọng khiến hệ thống có thể không hoạt động đúng hoặc ngừng hoạt động. Ví dụ: Lỗi khởi tạo NVS, lỗi mount SPIFFS, lỗi nghiêm trọng khi khởi tạo các module cốt lõi. |
| 2           | `ERROR`     | Các lỗi xảy ra trong quá trình hoạt động, khiến một chức năng cụ thể không thực hiện được. Ví dụ: Không đọc được cảm biến, không gửi được MQTT, lỗi phân tích JSON. |
| 3           | `WARNING`   | Các tình huống không mong muốn hoặc tiềm ẩn vấn đề, nhưng hệ thống vẫn có thể tiếp tục hoạt động. Ví dụ: Mất kết nối WiFi/MQTT và đang thử kết nối lại, nhận được dữ liệu không hợp lệ, một lịch tưới bị bỏ qua do không đủ điều kiện nhưng không phải lỗi hệ thống. |
| 4           | `INFO`      | Các thông điệp thông tin về trạng thái hoạt động bình thường của hệ thống, các bước chính. Ví dụ: Kết nối WiFi/MQTT thành công, một lịch tưới bắt đầu/kết thúc, thay đổi cấu hình. |
| 5           | `DEBUG`     | Các thông tin rất chi tiết, thường chỉ dùng cho mục đích gỡ lỗi. Ví dụ: Giá trị của các biến, các bước nhỏ trong một hàm, dữ liệu MQTT nhận được. Mức này tạo ra rất nhiều log. |

### 8. Cấu hình Log (`irrigation/esp32_6relay/logconfig`)

Topic này cho phép người dùng thay đổi động mức độ chi tiết của log được gửi qua Serial (cổng nối tiếp cục bộ) hoặc qua MQTT, mà không cần phải biên dịch lại và nạp lại firmware cho ESP32. Điều này rất hữu ích cho việc gỡ lỗi từ xa hoặc giám sát hoạt động của thiết bị.

**Payload ví dụ:**

Cấu hình log Serial ở mức DEBUG (rất chi tiết):
```json
{
  "target": "serial",
  "level": "DEBUG"
}
```
Cấu hình log MQTT ở mức WARNING (chỉ gửi các cảnh báo, lỗi và thông điệp nghiêm trọng):
```json
{
  "target": "mqtt",
  "level": "WARNING"
}
```

| Trường    | Kiểu   | Mô tả chi tiết                                                                                                                                  |
| --------- | ------ | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `target`  | string | Chỉ định kênh log nào sẽ được cấu hình. Có hai giá trị hợp lệ: <br> - `"serial"`: Áp dụng cho log xuất ra cổng Serial của ESP32 (thường dùng để gỡ lỗi trực tiếp). <br> - `"mqtt"`: Áp dụng cho log được gửi qua topic `irrigation/esp32_6relay/logs`. |
| `level`   | string | Chỉ định mức độ log mới cho `target` đã chọn. Các giá trị hợp lệ tương ứng với `level_str` trong bảng mô tả mức độ log ở trên: <br> - `"NONE"` <br> - `"CRITICAL"` <br> - `"ERROR"` <br> - `"WARNING"` <br> - `"INFO"` <br> - `"DEBUG"` <br> Việc thay đổi `level` sẽ ảnh hưởng đến khối lượng và loại thông tin log bạn nhận được. Ví dụ, đặt `level` thành "DEBUG" sẽ rất chi tiết, trong khi "ERROR" sẽ chỉ hiển thị các vấn đề nghiêm trọng. |

**Cách sử dụng:**

Để gỡ lỗi một vấn đề từ xa, bạn có thể gửi một lệnh đến topic `irrigation/esp32_6relay/logconfig` để tạm thời tăng mức log MQTT lên "DEBUG". Sau khi đã thu thập đủ thông tin, bạn nên gửi một lệnh khác để giảm mức log xuống "INFO" hoặc "WARNING" nhằm giảm tải cho mạng MQTT và tiết kiệm tài nguyên xử lý trên ESP32.

## Chi tiết về điều kiện cảm biến

Cấu trúc chi tiết về `sensor_condition` trong lịch tưới (`irrigation/esp32_6relay/schedule`):

```json
"sensor_condition": {
  "enabled": true,      
  "temperature": {
    "enabled": true,    
    "min": 20,          
    "max": 38           
  },
  "humidity": {
    "enabled": true,
    "min": 40,
    "max": 80
  },
  "soil_moisture": {
    "enabled": true,
    "min": 30           
  },
  "rain": {
    "enabled": true,
    "skip_when_raining": true 
  },
  "light": {
    "enabled": true,
    "min": 5000,       
    "max": 50000        
  }
}
```

| Điều kiện (`sensor_condition.<key>`) | Trường con (`enabled`, `min`, `max`, `skip_when_raining`) | Mô tả                                                                                                                                                                       |
| ------------------------------------ | -------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `enabled` (cấp cao nhất)             | -                                                        | `true` để kích hoạt kiểm tra tất cả các điều kiện cảm biến con. Nếu `false`, tất cả điều kiện con sẽ bị bỏ qua.                                                              |
| `temperature`                        | `enabled`, `min`, `max`                                  | Nếu `enabled` là `true`, lịch chỉ chạy khi nhiệt độ môi trường (từ DHT21) nằm trong khoảng `min` đến `max` °C.                                                            |
| `humidity`                           | `enabled`, `min`, `max`                                  | Nếu `enabled` là `true`, lịch chỉ chạy khi độ ẩm không khí (từ DHT21) nằm trong khoảng `min` đến `max` %.                                                                 |
| `soil_moisture`                      | `enabled`, `min`                                         | Nếu `enabled` là `true`, lịch chỉ chạy khi độ ẩm đất (cập nhật qua topic `environment` cho vùng tương ứng) thấp hơn ngưỡng `min` %.                                          |
| `rain`                               | `enabled`, `skip_when_raining`                           | Nếu `enabled` là `true` và `skip_when_raining` là `true`, lịch sẽ không chạy nếu hệ thống phát hiện đang mưa (cập nhật qua topic `environment`).                               |
| `light`                              | `enabled`, `min`, `max`                                  | Nếu `enabled` là `true`, lịch chỉ chạy khi cường độ ánh sáng (cập nhật qua topic `environment`) nằm trong khoảng `min` đến `max` lux.                                    |

Mỗi điều kiện con (temperature, humidity, ...) có thể được bật/tắt độc lập bằng cách đặt trường `enabled` của chính nó thành `true`/`false`.

### Cách xử lý trường hợp nhiều điều kiện

Khi `sensor_condition.enabled` là `true` và có nhiều điều kiện con (ví dụ: `temperature`, `soil_moisture`) cũng được `enabled`, **tất cả** các điều kiện con đó phải được thỏa mãn đồng thời để lịch tưới được kích hoạt.

Ví dụ:
```json
"sensor_condition": {
  "enabled": true,
  "temperature": {
    "enabled": true,
    "min": 20,
    "max": 38
  },
  "soil_moisture": {
    "enabled": true,
    "min": 30
  }
}
```
Trong ví dụ trên, lịch tưới chỉ chạy khi:
1.  Nhiệt độ trong khoảng 20-38°C, **VÀ**
2.  Độ ẩm đất (cho vùng tương ứng) dưới 30%.

## Mô tả về mã lỗi (Log qua Serial)

Hệ thống không trả về mã lỗi cụ thể qua MQTT như một phản hồi trực tiếp cho lệnh, nhưng sẽ ghi log các thông báo lỗi và cảnh báo qua Serial port (và qua MQTT nếu được cấu hình). Các thông báo này thường bao gồm `core_id` và `free_heap`.

| Lỗi / Thông báo (Ví dụ)                                    | Mô tả                                                                   |
| ---------------------------------------------------------- | ----------------------------------------------------------------------- |
| "JSON parsing failed"                                      | Lỗi phân tích cú pháp JSON từ payload MQTT.                             |
| "Missing required fields in task"                          | Thiếu trường bắt buộc trong JSON của lịch tưới.                         |
| "Task ID not found"                                        | Không tìm thấy ID lịch tưới khi cố gắng xóa hoặc cập nhật.               |
| "NVM: Failed to initialize Preferences."                   | Lỗi khởi tạo NVS để đọc/ghi cấu hình.                                  |
| "SPIFFS Mount Failed."                                     | Lỗi khởi tạo hệ thống file SPIFFS (dùng cho web portal).                |
| "ERROR: Could not get time from NTP" / "NTP: forceUpdate() failed..." | Lỗi đồng bộ thời gian với NTP server.                                  |
| "ERROR: No network connection, cannot send data"           | Mất kết nối WiFi/MQTT, không thể gửi dữ liệu.                           |
| "ERROR: Failed to read from sensors"                       | Lỗi đọc dữ liệu từ cảm biến DHT21.                                       |
| "Task X cannot start, lower priority than running tasks"   | Lịch X không thể bắt đầu do có lịch khác ưu tiên cao hơn đang chạy.      |
| "Preempted task X due to higher priority task Y"           | Lịch X bị ngắt bởi lịch Y có ưu tiên cao hơn.                            |
| "Task X skipped due to temperature out of range"           | Lịch X bị bỏ qua do điều kiện nhiệt độ không thỏa mãn.                   |
| "Task X skipped due to humidity out of range"              | Lịch X bị bỏ qua do điều kiện độ ẩm không khí không thỏa mãn.            |
| "Task X skipped due to soil moisture above threshold"      | Lịch X bị bỏ qua do điều kiện độ ẩm đất không thỏa mãn (đất còn đủ ẩm). |
| "Task X skipped due to rain"                               | Lịch X bị bỏ qua do đang mưa (nếu `skip_when_raining` là true).          |
| "Task X skipped due to light level out of range"           | Lịch X bị bỏ qua do điều kiện ánh sáng không thỏa mãn.                   |
| "Max WiFi retry attempts reached. Requesting Config Portal." | Đã thử kết nối WiFi nhiều lần thất bại, sẽ khởi động Config Portal.       |
| "MQTT: Connection failed, rc=X. Will retry."               | Kết nối MQTT thất bại với mã trả về X, sẽ thử lại.                       |

## Ứng dụng mẫu

*(Các ví dụ JSON ở đây nên sử dụng `YOUR_CONFIGURED_API_KEY` cho `api_key` và cập nhật các trường giá trị cho phù hợp với mô tả mới, ví dụ `duration` trong relay control tính bằng giây, `remaining` trong status tính bằng ms.)*

### 1. Lập lịch tưới đơn giản

```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY",
  "tasks": [
    {
      "id": 101,
      "active": true,
      "days": [2, 4, 6], 
      "time": "06:00",
      "duration": 10,    
      "zones": [3, 4],
      "priority": 3
    }
  ]
}
```

### 2. Lập lịch tưới với điều kiện cảm biến

```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY",
  "tasks": [
    {
      "id": 102,
      "active": true,
      "days": [1, 3, 5],
      "time": "10:30",
      "duration": 15,
      "zones": [1, 2],
      "priority": 5,
      "sensor_condition": {
        "enabled": true,
        "temperature": {
          "enabled": true,
          "min": 20,
          "max": 38
        },
        "soil_moisture": {
          "enabled": true,
          "min": 30 
        },
        "rain": {
          "enabled": true,
          "skip_when_raining": true
        }
      }
    }
  ]
}
```

### 3. Điều khiển relay thủ công

Bật relay 1 trong 30 giây (30s).
```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY",
  "relays": [
    {
      "id": 1,
      "state": true,
      "duration": 30 
    }
  ]
}
```

### 4. Lịch tưới hàng ngày cho tất cả các vùng

```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY",
  "tasks": [
    {
      "id": 103,
      "active": true,
      "days": [1, 2, 3, 4, 5, 6, 7], 
      "time": "07:00",
      "duration": 5, 
      "zones": [1, 2, 3, 4, 5, 6],
      "priority": 10 
    }
  ]
}
```

### 5. Cập nhật cảm biến độ ẩm đất và trạng thái mưa

```json
{
  "api_key": "YOUR_CONFIGURED_API_KEY",
  "soil_moisture": {
    "zone": 1,
    "value": 22
  },
  "rain": true
}
```

### 6. Cấu hình mức Log

```json
{
  "target": "serial",
  "level": "DEBUG"
}
```
(Gửi riêng)
```json
{
  "target": "mqtt",
  "level": "WARNING"
}
```

## Hướng dẫn tích hợp (JavaScript MQTT.js)

### 1. Kết nối MQTT

```javascript
// Ví dụ sử dụng thư viện MQTT.js
const mqtt = require('mqtt');
// Thay 'YOUR_MQTT_BROKER_URL' bằng địa chỉ broker của bạn, ví dụ: 'mqtt://karis.cloud:1883'
const client = mqtt.connect('YOUR_MQTT_BROKER_URL'); 

const API_KEY = "YOUR_CONFIGURED_API_KEY"; // Nên lấy từ cấu hình an toàn

client.on('connect', () => {
  console.log('Connected to MQTT broker');
  
  // Subscribe to status and data topics
  client.subscribe('irrigation/esp32_6relay/status');
  client.subscribe('irrigation/esp32_6relay/sensors');
  client.subscribe('irrigation/esp32_6relay/schedule/status');
  client.subscribe('irrigation/esp32_6relay/logs'); // Thêm topic logs
});

client.on('message', (topic, message) => {
  try {
    const data = JSON.parse(message.toString());
    console.log(`Received message on ${topic}:`, data);
    // Xử lý dữ liệu nhận được tại đây
  } catch (e) {
    console.error(`Error parsing JSON from topic ${topic}:`, message.toString(), e);
  }
});
```

### 2. Gửi lệnh điều khiển relay

```javascript
const controlRelay = (relayId, state, durationInSeconds = 0) => {
  const payload = {
    api_key: API_KEY,
    relays: [
      {
        id: relayId,
        state: state,
      }
    ]
  };
  if (durationInSeconds > 0 && state === true) {
    payload.relays[0].duration = durationInSeconds;
  }
  
  client.publish('irrigation/esp32_6relay/control', JSON.stringify(payload));
  console.log('Sent relay control command:', payload);
};

// Ví dụ: Bật relay 1 trong 15 phút (900 giây)
// controlRelay(1, true, 900);
// Ví dụ: Tắt relay 2
// controlRelay(2, false);
```

### 3. Lập lịch tưới

```javascript
const createTask = (taskDetails) => { // taskDetails là một object như trong ví dụ JSON
  const payload = {
    api_key: API_KEY,
    tasks: [taskDetails]
  };
  client.publish('irrigation/esp32_6relay/schedule', JSON.stringify(payload));
  console.log('Sent create/update task command:', payload);
};

// Ví dụ: Tưới vùng 1, 2 vào 6:30 sáng các ngày thứ 2, 4, 6 trong 10 phút
/*
createTask({
  id: 201,
  active: true,
  days: [1, 3, 5], // Thứ 2, Thứ 4, Thứ 6
  time: "06:30",
  duration: 10, // 10 phút
  zones: [1, 2],
  priority: 5
});
*/
```

### 4. Xóa lịch tưới

```javascript
const deleteTask = (taskIdsArray) => {
  const payload = {
    api_key: API_KEY,
    delete_tasks: taskIdsArray
  };
  client.publish('irrigation/esp32_6relay/schedule', JSON.stringify(payload));
  console.log('Sent delete task command:', payload);
};

// Ví dụ: Xóa lịch có ID 201 và 202
// deleteTask([201, 202]);
```

### 5. Cập nhật giá trị cảm biến thủ công

```javascript
const updateEnvironmentData = (envData) => { // envData là object như { soil_moisture: { zone: 1, value: 25 }, rain: true }
  const payload = {
    api_key: API_KEY,
    ...envData
  };
  client.publish('irrigation/esp32_6relay/environment', JSON.stringify(payload));
  console.log('Sent environment update:', payload);
};

// Ví dụ: Cập nhật độ ẩm đất vùng 1 là 25% và báo đang mưa
/*
updateEnvironmentData({
  soil_moisture: { zone: 1, value: 25 },
  rain: true
});
*/
```

### 6. Cấu hình mức Log

```javascript
const configureLogLevel = (target, level) => {
  const payload = {
    // api_key: API_KEY, // Topic này không yêu cầu api_key trong payload
    target: target, // "serial" hoặc "mqtt"
    level: level    // "NONE", "CRITICAL", "ERROR", "WARNING", "INFO", "DEBUG"
  };
  client.publish('irrigation/esp32_6relay/logconfig', JSON.stringify(payload));
  console.log('Sent log configuration command:', payload);
};

// Ví dụ: Đặt mức log MQTT thành INFO
// configureLogLevel("mqtt", "INFO");
```

## Giới hạn và lưu ý quan trọng

1.  **API Key**:
    *   Được cấu hình trên ESP32 thông qua Web Configuration Portal và lưu trong NVS.
    *   Nên được bao gồm trong payload JSON của các lệnh gửi đến ESP32 (`control`, `schedule`, `environment`) để xác thực (mặc dù việc xác thực này có thể chưa được triển khai chặt chẽ trong tất cả các module phía ESP32 cho mọi lệnh).
    *   Sẽ được ESP32 tự động thêm vào các message mà nó publish (`sensors`, `status`, `schedule/status`, `logs`).
2.  **Ngày trong tuần (cho lịch tưới)**:
    *   API JSON sử dụng: 1=Thứ 2, 2=Thứ 3, ..., 7=Chủ nhật.
    *   Lưu trữ nội bộ trên ESP32: Dưới dạng bitmap (bit 0 = Chủ nhật, bit 1 = Thứ 2, ..., bit 6 = Thứ 7).
3.  **ID relay/vùng tưới**: Luôn bắt đầu từ 1 (ví dụ: 1 đến 6), không phải từ 0. Trong mã nguồn ESP32, chúng thường được ánh xạ sang chỉ số mảng 0-5.
4.  **Thời gian (cho lịch tưới)**: Sử dụng định dạng 24 giờ ("HH:MM").
5.  **Cơ chế ưu tiên lịch tưới**:
    *   Lịch có `priority` cao hơn sẽ ngắt (preempt) lịch có `priority` thấp hơn nếu chúng xung đột về vùng tưới.
    *   Lịch bị ngắt sẽ chuyển sang trạng thái "completed" và tính toán thời gian chạy kế tiếp.
6.  **Phụ thuộc Internet/Mạng nội bộ**:
    *   ESP32 sử dụng NTP (Network Time Protocol) để đồng bộ thời gian thực, cần kết nối internet (hoặc NTP server nội bộ) để lập lịch chính xác.
    *   Kết nối MQTT server là cần thiết cho việc điều khiển và giám sát từ xa.
7.  **Điều kiện cảm biến**:
    *   Tất cả các điều kiện con được `enabled` trong mục `sensor_condition` phải được thỏa mãn đồng thời để lịch tưới được kích hoạt.
    *   Việc kiểm tra điều kiện cảm biến xảy ra ngay tại thời điểm bắt đầu theo lịch của một task.
8.  **Dung lượng payload MQTT**: Giới hạn kích thước của một message MQTT thường là 256KB theo chuẩn, nhưng PubSubClient trên ESP32 có buffer mặc định nhỏ hơn (có thể cấu hình, ví dụ 1024 bytes trong dự án này). Tránh gửi payload quá lớn.
9.  **Tần suất báo cáo (Publish từ ESP32)**:
    *   Dữ liệu cảm biến (`/sensors`): Mỗi 30 giây.
    *   Trạng thái relay (`/status`): Khi có thay đổi, và báo cáo cưỡng bức mỗi 5 phút.
    *   Trạng thái lịch (`/schedule/status`): Khi có thay đổi, và báo cáo cưỡng bức mỗi 5 phút.
    *   Logs (`/logs`): Khi có sự kiện log tương ứng với mức độ đã cấu hình.
10. **Xử lý lỗi**: ESP32 chủ yếu ghi log lỗi qua Serial và MQTT (nếu được cấu hình). Không có cơ chế phản hồi lỗi trực tiếp qua MQTT cho các lệnh không hợp lệ, ngoại trừ việc không thực hiện hành động đó.
11. **Web Configuration Portal**: Là phương thức chính để cài đặt WiFi, MQTT server, port, và API key ban đầu hoặc khi có sự cố kết nối.

## Tính năng nâng cao

### 1. Cơ chế gọi lại (Retry)
- **WiFi/MQTT**: `NetworkManager` có cơ chế tự động thử kết nối lại WiFi và MQTT server với các khoảng thời gian tăng dần nếu kết nối bị mất hoặc thất bại ban đầu. Sau một số lần thử thất bại, Config Portal có thể được kích hoạt.
- **NTP Sync**: `NetworkManager` thử đồng bộ NTP với nhiều server dự phòng và thử lại theo chu kỳ.
- **Lịch bị ngắt**: Khi lịch tưới bị ngắt do lịch khác có ưu tiên cao hơn, lịch bị ngắt sẽ tự động được tính toán lại cho lần chạy tiếp theo dựa trên cấu hình ngày trong tuần.

### 2. Chồng lịch (Schedule Stacking)
Nếu có nhiều lịch tưới được cấu hình cho cùng một thời điểm:
- Hệ thống sẽ ưu tiên chạy lịch có mức `priority` cao nhất.
- Nếu các lịch có các `zones` (vùng tưới) hoàn toàn khác nhau (không có vùng nào chung), chúng có thể chạy song song miễn là không vi phạm giới hạn số relay có thể bật cùng lúc của phần cứng (trong trường hợp này là 6).

### 3. Kiểm soát thủ công ưu tiên
Lệnh điều khiển relay thủ công (qua topic `irrigation/esp32_6relay/control`) thường có mức ưu tiên cao nhất và sẽ ghi đè lên mọi lịch tưới đang chạy trên các relay bị ảnh hưởng. Lịch đang chạy trên relay đó sẽ bị dừng (coi như completed).

### 4. Đồng bộ hóa và bảo vệ tài nguyên
Mã nguồn sử dụng mutex (Semaphore) của FreeRTOS và các kỹ thuật đồng bộ hóa khác để đảm bảo tính nhất quán của dữ liệu và ngăn ngừa xung đột khi nhiều tác vụ (tasks) truy cập vào các tài nguyên chia sẻ như trạng thái relay, danh sách lịch, dữ liệu cảm biến.

Tài liệu này cung cấp thông tin toàn diện để tích hợp và phát triển ứng dụng điều khiển cho hệ thống tưới tự động ESP32-S3 6-Relay.