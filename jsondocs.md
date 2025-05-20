# Tài liệu API & MQTT - Hệ thống tưới tự động ESP32-S3 6-Relay

## Tổng quan hệ thống

Hệ thống tưới tự động ESP32-S3 6-Relay là một giải pháp IoT thông minh để điều khiển tưới tiêu tự động dựa trên lịch trình và điều kiện môi trường. Hệ thống sử dụng kiến trúc lõi kép của ESP32-S3 để quản lý đồng thời các tác vụ khác nhau và giao tiếp thông qua MQTT để cung cấp khả năng điều khiển từ xa.

### Chức năng chính:

1. **Điều khiển relay tưới**: Bật/tắt 6 relay (vùng tưới) độc lập
2. **Lập lịch tưới**: Tự động hóa tưới theo ngày trong tuần và thời gian
3. **Giám sát môi trường**: Thu thập và phản ứng với dữ liệu từ nhiều loại cảm biến
4. **Ưu tiên lịch tưới**: Xử lý xung đột giữa các lịch với cơ chế ưu tiên
5. **Điều kiện cảm biến**: Tưới chỉ khi thỏa mãn các điều kiện môi trường
6. **Báo cáo trạng thái**: Cập nhật liên tục về trạng thái hoạt động

### Ứng dụng:

- Hệ thống tưới tự động cho nhà kính
- Tưới vườn thông minh
- Tưới nông nghiệp quy mô nhỏ và vừa
- Hệ thống thủy canh
- Tưới cảnh quan và công viên
- Tưới nhà kính cho nghiên cứu nông nghiệp

## MQTT Topics

Hệ thống sử dụng các MQTT topic sau để giao tiếp:

| Topic | Hướng | Mô tả |
|-------|-------|-------|
| `irrigation/esp32_6relay/sensors` | Publish | ESP32 gửi dữ liệu từ cảm biến |
| `irrigation/esp32_6relay/control` | Subscribe | ESP32 nhận lệnh điều khiển relay |
| `irrigation/esp32_6relay/status` | Publish | ESP32 báo cáo trạng thái relay |
| `irrigation/esp32_6relay/schedule` | Subscribe | ESP32 nhận lệnh lập lịch tưới |
| `irrigation/esp32_6relay/schedule/status` | Publish | ESP32 báo cáo trạng thái lịch tưới |
| `irrigation/esp32_6relay/environment` | Subscribe | ESP32 nhận cập nhật điều kiện môi trường |
| `irrigation/esp32_6relay/logs` | Publish | ESP32 gửi các tin nhắn log qua MQTT để giám sát từ xa |
| `irrigation/esp32_6relay/logconfig` | Subscribe | ESP32 nhận lệnh để thay đổi mức log cho Serial hoặc MQTT tại thời gian chạy |

## Cấu trúc JSON

### 1. Dữ liệu cảm biến (`irrigation/esp32_6relay/sensors`)

ESP32 gửi dữ liệu cảm biến môi trường lên server qua topic này. Tần suất mặc định: mỗi 5 giây.

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "timestamp": 1683123456,
  "device_info": {
    "name": "esp32_6relay",
    "type": "DHT21_SoilMoisture",
    "firmware": "1.0.1"
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
  },
  "soil_moisture": {
    "value": 29.0,
    "unit": "percent",
    "sensor_type": "capacitive_soil_moisture"
  }
}
```

| Trường | Kiểu | Mô tả |
|--------|------|-------|
| `api_key` | string | API key xác thực |
| `timestamp` | number | Thời gian unix timestamp |
| `device_info` | object | Thông tin về thiết bị và firmware |
| `device_info.name` | string | Tên định danh thiết bị (ví dụ: "esp32_6relay") |
| `device_info.type` | string | Loại cảm biến/module (ví dụ: "DHT21_SoilMoisture") |
| `device_info.firmware` | string | Phiên bản firmware (ví dụ: "1.0.1") |
| `temperature` | object | Dữ liệu nhiệt độ |
| `temperature.value` | float | Giá trị nhiệt độ |
| `temperature.unit` | string | Đơn vị nhiệt độ (ví dụ: "celsius") |
| `temperature.sensor_type` | string | Loại cảm biến nhiệt độ |
| `humidity` | object | Dữ liệu độ ẩm không khí |
| `humidity.value` | float | Giá trị độ ẩm |
| `humidity.unit` | string | Đơn vị độ ẩm (ví dụ: "percent") |
| `humidity.sensor_type` | string | Loại cảm biến độ ẩm |
| `heat_index` | object | Dữ liệu chỉ số nhiệt |
| `heat_index.value` | float | Giá trị chỉ số nhiệt |
| `heat_index.unit` | string | Đơn vị chỉ số nhiệt (ví dụ: "celsius") |
| `heat_index.sensor_type` | string | Loại cảm biến chỉ số nhiệt |
| `soil_moisture` | object | Dữ liệu độ ẩm đất |
| `soil_moisture.value` | float | Giá trị độ ẩm đất |
| `soil_moisture.unit` | string | Đơn vị độ ẩm đất (ví dụ: "percent") |
| `soil_moisture.sensor_type` | string | Loại cảm biến độ ẩm đất (ví dụ: "capacitive_soil_moisture") |

### 2. Điều khiển relay (`irrigation/esp32_6relay/control`)

Gửi lệnh điều khiển relay đến ESP32. Không yêu cầu API key khi subscribe.

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "relays": [
    {
      "id": 1,
      "state": true,
      "duration": 15
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

| Trường | Kiểu | Mô tả |
|--------|------|-------|
| `api_key` | string | API key xác thực |
| `relays` | array | Mảng các relay cần điều khiển |
| `relays[].id` | number | ID của relay (1-6) |
| `relays[].state` | boolean | Trạng thái (true = bật, false = tắt) |
| `relays[].duration` | number | Thời gian bật (phút), tùy chọn |

#### Trường hợp sử dụng đặc biệt - Điều khiển nhiều relay cùng lúc

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "relays": [
    {
      "id": 1,
      "state": true,
      "duration": 15
    },
    {
      "id": 2,
      "state": true,
      "duration": 15
    },
    {
      "id": 3,
      "state": true,
      "duration": 15
    }
  ]
}
```

#### Trường hợp tắt tất cả relay

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
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

ESP32 báo cáo trạng thái của tất cả relay. Tần suất mặc định: mỗi 10 giây.

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "timestamp": 1683123456,
  "relays": [
    {
      "id": 1,
      "state": true,
      "remaining_time": 600
    },
    {
      "id": 2,
      "state": true,
      "remaining_time": 0
    },
    {
      "id": 3,
      "state": false,
      "remaining_time": 0
    },
    {
      "id": 4,
      "state": false,
      "remaining_time": 0
    },
    {
      "id": 5,
      "state": false,
      "remaining_time": 0
    },
    {
      "id": 6,
      "state": false,
      "remaining_time": 0
    }
  ]
}
```

| Trường | Kiểu | Mô tả |
|--------|------|-------|
| `api_key` | string | API key xác thực |
| `timestamp` | number | Thời gian unix timestamp |
| `relays` | array | Mảng tất cả relay |
| `relays[].id` | number | ID của relay (1-6) |
| `relays[].state` | boolean | Trạng thái relay (true = bật, false = tắt) |
| `relays[].remaining_time` | number | Thời gian còn lại (giây), 0 nếu không có hẹn giờ |

### 4. Lập lịch tưới (`irrigation/esp32_6relay/schedule`)

Gửi lệnh lập lịch tưới đến ESP32.

#### 4.1. Thêm/Cập nhật lịch

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "tasks": [
    {
      "id": 1,
      "active": true,
      "days": [1, 3, 5],
      "time": "10:32",
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

| Trường | Kiểu | Mô tả |
|--------|------|-------|
| `api_key` | string | API key xác thực |
| `tasks` | array | Mảng các lịch tưới |
| `tasks[].id` | number | ID của lịch tưới |
| `tasks[].active` | boolean | Trạng thái kích hoạt |
| `tasks[].days` | array | Các ngày trong tuần (1=T2, 2=T3, ..., 7=CN) |
| `tasks[].time` | string | Thời gian bắt đầu (HH:MM) |
| `tasks[].duration` | number | Thời lượng tưới (phút) |
| `tasks[].zones` | array | Mảng các vùng tưới (1-6) |
| `tasks[].priority` | number | Mức ưu tiên (1-10, cao hơn = quan trọng hơn) |
| `tasks[].sensor_condition` | object | Điều kiện cảm biến (tùy chọn) |

#### 4.2. Xóa lịch tưới

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "delete_tasks": [1, 2]
}
```

| Trường | Kiểu | Mô tả |
|--------|------|-------|
| `api_key` | string | API key xác thực |
| `delete_tasks` | array | Mảng ID lịch cần xóa |

#### 4.3. Thêm nhiều lịch tưới cùng lúc

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "tasks": [
    {
      "id": 1,
      "active": true,
      "days": [1, 3, 5],
      "time": "06:00",
      "duration": 15,
      "zones": [1, 2],
      "priority": 5
    },
    {
      "id": 2,
      "active": true,
      "days": [2, 4, 6],
      "time": "18:00",
      "duration": 10,
      "zones": [3, 4],
      "priority": 3
    }
  ]
}
```

#### 4.4. Vô hiệu hóa lịch tưới (không xóa)

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
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

ESP32 báo cáo trạng thái của tất cả lịch tưới. Tần suất mặc định: mỗi 10 giây.

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "timestamp": 1683123456,
  "tasks": [
    {
      "id": 1,
      "active": true,
      "days": [1, 3, 5],
      "time": "10:32",
      "duration": 15,
      "zones": [1, 2],
      "priority": 5,
      "state": "idle",
      "next_run": "2023-05-03 10:32:00",
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
    },
    {
      "id": 2,
      "active": true,
      "days": [2, 4, 6],
      "time": "06:00",
      "duration": 10,
      "zones": [3, 4],
      "priority": 3,
      "state": "running"
    }
  ]
}
```

| Trường | Kiểu | Mô tả |
|--------|------|-------|
| `api_key` | string | API key xác thực |
| `timestamp` | number | Thời gian unix timestamp |
| `tasks` | array | Mảng tất cả lịch tưới |
| `tasks[].state` | string | Trạng thái ("idle", "running", "completed") |
| `tasks[].next_run` | string | Thời gian chạy kế tiếp (yyyy-MM-dd HH:mm:ss) |
| (và tất cả các trường khác giống như trong `schedule` topic) |

### 6. Điều khiển môi trường (`irrigation/esp32_6relay/environment`)

Gửi cập nhật giá trị cảm biến môi trường thủ công đến ESP32.

#### 6.1. Cập nhật độ ẩm đất

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "soil_moisture": {
    "zone": 1,
    "value": 25
  }
}
```

#### 6.2. Cập nhật trạng thái mưa

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "rain": true
}
```

#### 6.3. Cập nhật độ sáng

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "light": 5000
}
```

#### 6.4. Cập nhật nhiều cảm biến cùng lúc

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "soil_moisture": {
    "zone": 2,
    "value": 20
  },
  "rain": false,
  "light": 10000
}
```

| Trường | Kiểu | Mô tả |
|--------|------|-------|
| `api_key` | string | API key xác thực |
| `soil_moisture` | object | Thông tin độ ẩm đất (tùy chọn) |
| `soil_moisture.zone` | number | Vùng đo (1-6) |
| `soil_moisture.value` | number | Giá trị độ ẩm đất (%) |
| `rain` | boolean | Trạng thái mưa (true = đang mưa) (tùy chọn) |
| `light` | number | Cường độ ánh sáng (lux) (tùy chọn) |

## Chi tiết về điều kiện cảm biến

Cấu trúc chi tiết về `sensor_condition` trong lịch tưới:

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

| Điều kiện | Mô tả |
|-----------|-------|
| `temperature` | Tưới chỉ khi nhiệt độ nằm trong khoảng `min` đến `max` °C |
| `humidity` | Tưới chỉ khi độ ẩm không khí nằm trong khoảng `min` đến `max` % |
| `soil_moisture` | Tưới chỉ khi độ ẩm đất thấp hơn ngưỡng `min` % |
| `rain` | Nếu `skip_when_raining` = true, sẽ không tưới khi đang mưa |
| `light` | Tưới chỉ khi độ sáng nằm trong khoảng `min` đến `max` lux |

Mỗi điều kiện có thể được bật/tắt độc lập bằng cách đặt `enabled` thành `true`/`false`.

### Cách xử lý trường hợp nhiều điều kiện

Khi có nhiều điều kiện được bật, tất cả các điều kiện phải được thỏa mãn để lịch tưới được kích hoạt. Ví dụ:

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
1. Nhiệt độ trong khoảng 20-38°C, VÀ
2. Độ ẩm đất dưới 30%

### Trường hợp đặc biệt: Kiểm tra mưa

Tham số `skip_when_raining` quyết định liệu lịch có bị bỏ qua khi đang mưa hay không:

```json
"rain": {
  "enabled": true,
  "skip_when_raining": true
}
```

Với cấu hình trên, lịch sẽ bị bỏ qua nếu cảm biến phát hiện mưa.

## Mô tả về mã lỗi

Hệ thống không trả về mã lỗi cụ thể qua MQTT, nhưng sẽ ghi log các thông báo lỗi qua Serial port:

| Lỗi | Mô tả |
|-----|-------|
| "JSON parsing failed" | Lỗi phân tích cú pháp JSON |
| "Missing required fields in task" | Thiếu trường bắt buộc trong lịch tưới |
| "Task ID not found" | Không tìm thấy ID lịch tưới |
| "ERROR: Could not get time from NTP" | Lỗi đồng bộ thời gian |
| "ERROR: No network connection, cannot send data" | Mất kết nối mạng |
| "ERROR: Failed to read from sensors" | Lỗi đọc cảm biến |
| "Task X cannot start, lower priority than running tasks" | Lịch không thể chạy do ưu tiên thấp hơn |
| "Preempted task X due to higher priority task" | Lịch bị ngắt do lịch ưu tiên cao hơn |
| "Task X skipped due to temperature out of range" | Lịch bị bỏ qua do nhiệt độ không thỏa mãn |
| "Task X skipped due to humidity out of range" | Lịch bị bỏ qua do độ ẩm không thỏa mãn |
| "Task X skipped due to soil moisture above threshold" | Lịch bị bỏ qua do độ ẩm đất đủ cao |
| "Task X skipped due to rain" | Lịch bị bỏ qua do đang mưa |
| "Task X skipped due to light level out of range" | Lịch bị bỏ qua do độ sáng không thỏa mãn |

## Ứng dụng mẫu

### 1. Lập lịch tưới đơn giản

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "tasks": [
    {
      "id": 2,
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
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "tasks": [
    {
      "id": 1,
      "active": true,
      "days": [1, 3, 5],
      "time": "10:32",
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

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "relays": [
    {
      "id": 1,
      "state": true,
      "duration": 30
    }
  ]
}
```

### 4. Lịch tưới hàng ngày

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "tasks": [
    {
      "id": 3,
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

### 5. Lịch tưới cuối tuần với độ ưu tiên cao

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "tasks": [
    {
      "id": 4,
      "active": true,
      "days": [6, 7],
      "time": "08:00",
      "duration": 20,
      "zones": [1, 2],
      "priority": 8
    }
  ]
}
```

### 6. Cập nhật cảm biến độ ẩm đất cho nhiều vùng

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "soil_moisture": {
    "zone": 1,
    "value": 25
  }
}
```

Sau đó gửi tiếp:

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "soil_moisture": {
    "zone": 2,
    "value": 30
  }
}
```

### 7. Tin nhắn Log (`irrigation/esp32_6relay/logs`)

ESP32 gửi các tin nhắn log qua MQTT để giám sát từ xa. Các tin nhắn này có thể là log hệ thống chuẩn hoặc log hiệu suất.

#### 7.1. Định dạng JSON Log chuẩn

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "timestamp": 1683123457,
  "level_num": 4,
  "level_str": "INFO",
  "tag": "Setup",
  "message": "System setup sequence started.",
  "core_id": 0,
  "free_heap": 258852
}
```

| Trường      | Kiểu   | Mô tả                                                                 |
|-------------|--------|-----------------------------------------------------------------------|
| `api_key`   | string | API key xác thực                                                      |
| `timestamp` | number | Unix timestamp hoặc milliseconds từ khi khởi động                     |
| `level_num` | number | Mã số mức log (1:CRITICAL, 2:ERROR, 3:WARNING, 4:INFO, 5:DEBUG)     |
| `level_str` | string | Tên mức log ("CRITICAL", "ERROR", "WARNING", "INFO", "DEBUG")         |
| `tag`       | string | Thẻ định danh module/component (ví dụ: "Setup", "Core0", "RelayMgr") |
| `message`   | string | Nội dung tin nhắn log                                                 |
| `core_id`   | number | ID của CPU core ESP32 đã tạo log (0 hoặc 1)                           |
| `free_heap` | number | Bộ nhớ heap còn trống (bytes) tại thời điểm log                        |

#### 7.2. Định dạng JSON Log hiệu suất

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "timestamp": 1683123458,
  "level_num": 4,
  "level_str": "INFO",
  "tag": "Core0",
  "type": "performance",
  "event_name": "SensorReadOperation",
  "duration_ms": 13,
  "success": true,
  "details": "Optional details about the event",
  "core_id": 0,
  "free_heap": 260852
}
```

| Trường        | Kiểu    | Mô tả                                                                |
|---------------|---------|----------------------------------------------------------------------|
| `api_key`     | string  | API key xác thực                                                     |
| `timestamp`   | number  | Unix timestamp hoặc milliseconds từ khi khởi động                    |
| `level_num`   | number  | Mã số mức log (thường là 4 cho INFO đối với log hiệu suất)           |
| `level_str`   | string  | Tên mức log (thường là "INFO" đối với log hiệu suất)                  |
| `tag`         | string  | Thẻ định danh module/component                                       |
| `type`        | string  | Loại log, luôn là "performance" cho log hiệu suất                    |
| `event_name`  | string  | Tên của hoạt động được đo lường (ví dụ: "SensorReadOperation")        |
| `duration_ms` | number  | Thời gian thực hiện hoạt động (tính bằng mili giây)                   |
| `success`     | boolean | Cho biết hoạt động có thành công hay không (true/false)              |
| `details`     | string  | Thông tin chi tiết tùy chọn về sự kiện (có thể không có mặt)         |
| `core_id`     | number  | ID của CPU core ESP32 đã tạo log (0 hoặc 1)                          |
| `free_heap`   | number  | Bộ nhớ heap còn trống (bytes) tại thời điểm log                       |

### 8. Cấu hình Mức Log (`irrigation/esp32_6relay/logconfig`)

Gửi lệnh đến ESP32 để thay đổi mức log cho Serial hoặc MQTT tại thời gian chạy.

```json
{
  "target": "mqtt",
  "level": "DEBUG"
}
```

| Trường   | Kiểu   | Mô tả                                                                      |
|----------|--------|----------------------------------------------------------------------------|
| `target` | string | Đích cấu hình: "serial" (cho cổng Serial) hoặc "mqtt" (cho MQTT logs)       |
| `level`  | string | Mức log mong muốn: "NONE", "CRITICAL", "ERROR", "WARNING", "INFO", "DEBUG" |

Tài liệu này cung cấp thông tin toàn diện để tích hợp và phát triển webapp điều khiển cho hệ thống tưới tự động ESP32-S3 6-Relay.