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

## Cấu trúc JSON

### 1. Dữ liệu cảm biến (`irrigation/esp32_6relay/sensors`)

ESP32 gửi dữ liệu cảm biến môi trường lên server qua topic này. Tần suất mặc định: mỗi 5 giây.

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "timestamp": 1683123456,
  "temperature": 28.5,
  "humidity": 65.2,
  "heat_index": 30.1
}
```

| Trường | Kiểu | Mô tả |
|--------|------|-------|
| `api_key` | string | API key xác thực |
| `timestamp` | number | Thời gian unix timestamp |
| `temperature` | float | Nhiệt độ (°C) |
| `humidity` | float | Độ ẩm không khí (%) |
| `heat_index` | float | Chỉ số nhiệt (°C) |

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

### 7. Vô hiệu hóa tất cả các lịch tưới tạm thời

```json
{
  "api_key": "8a679613-019f-4b88-9068-da10f09dcdd2",
  "tasks": [
    {
      "id": 1,
      "active": false,
      "days": [1, 3, 5],
      "time": "10:32",
      "duration": 15,
      "zones": [1, 2],
      "priority": 5
    },
    {
      "id": 2,
      "active": false,
      "days": [2, 4, 6],
      "time": "06:00",
      "duration": 10,
      "zones": [3, 4],
      "priority": 3
    }
  ]
}
```

## Hướng dẫn tích hợp

### 1. Kết nối MQTT

```javascript
// Ví dụ sử dụng thư viện MQTT.js
const mqtt = require('mqtt');
const client = mqtt.connect('mqtt://karis.cloud:1883');

client.on('connect', () => {
  console.log('Connected to MQTT broker');
  
  // Subscribe to status topics
  client.subscribe('irrigation/esp32_6relay/status');
  client.subscribe('irrigation/esp32_6relay/sensors');
  client.subscribe('irrigation/esp32_6relay/schedule/status');
});

client.on('message', (topic, message) => {
  const data = JSON.parse(message.toString());
  console.log(`Received message on ${topic}:`, data);
});
```

### 2. Gửi lệnh điều khiển relay

```javascript
const controlRelay = (relayId, state, duration = 0) => {
  const payload = {
    api_key: "8a679613-019f-4b88-9068-da10f09dcdd2",
    relays: [
      {
        id: relayId,
        state: state,
        duration: duration
      }
    ]
  };
  
  client.publish('irrigation/esp32_6relay/control', JSON.stringify(payload));
};

// Ví dụ: Bật relay 1 trong 15 phút
controlRelay(1, true, 15);
```

### 3. Lập lịch tưới

```javascript
const createSchedule = (taskId, days, time, duration, zones, priority = 5) => {
  const payload = {
    api_key: "8a679613-019f-4b88-9068-da10f09dcdd2",
    tasks: [
      {
        id: taskId,
        active: true,
        days: days,
        time: time,
        duration: duration,
        zones: zones,
        priority: priority
      }
    ]
  };
  
  client.publish('irrigation/esp32_6relay/schedule', JSON.stringify(payload));
};

// Ví dụ: Tưới vùng 1, 2 vào 6:30 sáng các ngày thứ 2, 4, 6 trong 10 phút
createSchedule(3, [1, 3, 5], "06:30", 10, [1, 2]);
```

### 4. Cập nhật giá trị cảm biến

```javascript
const updateSoilMoisture = (zone, value) => {
  const payload = {
    api_key: "8a679613-019f-4b88-9068-da10f09dcdd2",
    soil_moisture: {
      zone: zone,
      value: value
    }
  };
  
  client.publish('irrigation/esp32_6relay/environment', JSON.stringify(payload));
};

// Ví dụ: Cập nhật độ ẩm đất vùng 1 là 25%
updateSoilMoisture(1, 25);
```

### 5. Cập nhật trạng thái mưa

```javascript
const updateRainStatus = (isRaining) => {
  const payload = {
    api_key: "8a679613-019f-4b88-9068-da10f09dcdd2",
    rain: isRaining
  };
  
  client.publish('irrigation/esp32_6relay/environment', JSON.stringify(payload));
};

// Ví dụ: Báo đang có mưa
updateRainStatus(true);
```

## Giới hạn và lưu ý quan trọng

1. **API Key**: Mọi giao tiếp với ESP32 đều yêu cầu API key chính xác trong payload JSON
2. **Ngày trong tuần**: Sử dụng định dạng: 1=Thứ 2, 2=Thứ 3, ..., 7=Chủ nhật. Trong code ESP32, ngày được lưu ở dạng bitmap: bit 0 = CN, bit 1-6 = T2-T7
3. **ID relay/vùng tưới**: Đều bắt đầu từ 1 (không phải từ 0), trên thiết bị ánh xạ đến chỉ số 0-5 trong mã nguồn
4. **Thời gian**: Sử dụng định dạng 24 giờ ("HH:MM")
5. **Cơ chế ưu tiên**:
   - Lịch có ưu tiên cao hơn (priority cao hơn) sẽ ngắt lịch có ưu tiên thấp hơn
   - Lịch bị ngắt sẽ chuyển sang trạng thái "completed" và tính thời gian chạy kế tiếp
6. **Phụ thuộc Internet**: ESP32 sử dụng NTP để đồng bộ thời gian, cần kết nối internet để thực hiện lập lịch chính xác
7. **Điều kiện cảm biến**:
   - Tất cả điều kiện được bật phải thỏa mãn để lịch tưới chạy
   - Kiểm tra điều kiện xảy ra ngay khi đến giờ bắt đầu lịch
8. **Dung lượng payload**: Không vượt quá 2048 bytes cho một message
9. **Tần suất báo cáo**:
   - Cảm biến: mỗi 5 giây
   - Trạng thái relay và lịch: mỗi 10 giây
10. **Xử lý lỗi**: ESP32 không gửi thông báo lỗi qua MQTT, chỉ ghi log qua Serial

## Tính năng nâng cao

### 1. Cơ chế gọi lại (retry)
Khi lịch tưới bị ngắt do lịch khác có ưu tiên cao hơn, lịch bị ngắt sẽ tự động được lên lịch lại cho lần chạy tiếp theo dựa trên cấu hình ngày trong tuần.

### 2. Chồng lịch (schedule stacking)
Nếu có nhiều lịch tưới cho cùng một thời điểm, hệ thống sẽ:
- Ưu tiên chạy lịch có mức priority cao nhất
- Nếu các lịch có zones khác nhau (không xung đột), chúng có thể chạy song song

### 3. Kiểm soát thủ công ưu tiên
Lệnh điều khiển relay thủ công luôn có mức ưu tiên cao nhất, sẽ ghi đè lên mọi lịch tưới đang chạy.

### 4. Đồng bộ hóa và bảo vệ tài nguyên
Mã nguồn sử dụng mutex và các kỹ thuật đồng bộ hóa khác để đảm bảo tính nhất quán và ngăn ngừa xung đột khi truy cập dữ liệu chia sẻ.

Tài liệu này cung cấp thông tin toàn diện để tích hợp và phát triển webapp điều khiển cho hệ thống tưới tự động ESP32-S3 6-Relay.