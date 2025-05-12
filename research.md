Cảm ơn bạn đã cung cấp thông tin. Mình sẽ bắt đầu tổng hợp một báo cáo chi tiết về ESP32-S3 DevKitC và board Waveshare ESP32-S3 6-Relay, bao gồm toàn bộ thông tin phần cứng, cách cài đặt RTOS và quản lý task trên 2 nhân, các tính năng digital/analog/cảm biến và ví dụ mã nguồn sử dụng PlatformIO.


# BÁO CÁO KỸ THUẬT: ESP32-S3 DevKitC và Waveshare ESP32-S3 6-Relay

## Mục lục

- [BÁO CÁO KỸ THUẬT: ESP32-S3 DevKitC và Waveshare ESP32-S3 6-Relay](#báo-cáo-kỹ-thuật-esp32-s3-devkitc-và-waveshare-esp32-s3-6-relay)
  - [Mục lục](#mục-lục)
  - [1. Thông tin phần cứng](#1-thông-tin-phần-cứng)
    - [1.1. Đặc điểm kỹ thuật ESP32-S3 DevKitC](#11-đặc-điểm-kỹ-thuật-esp32-s3-devkitc)
    - [1.2. Mô tả bo mạch Waveshare ESP32-S3 6-Relay](#12-mô-tả-bo-mạch-waveshare-esp32-s3-6-relay)
    - [1.3. Sơ đồ chân và kết nối relay](#13-sơ-đồ-chân-và-kết-nối-relay)
  - [2. Hướng dẫn thiết lập môi trường phát triển với PlatformIO](#2-hướng-dẫn-thiết-lập-môi-trường-phát-triển-với-platformio)
    - [2.1. Cài đặt PlatformIO trong VS Code](#21-cài-đặt-platformio-trong-vs-code)
    - [2.2. Cấu hình `platformio.ini` cho ESP32-S3](#22-cấu-hình-platformioini-cho-esp32-s3)
  - [3. RTOS và đa nhiệm trên ESP32](#3-rtos-và-đa-nhiệm-trên-esp32)
    - [3.1. Tổng quan FreeRTOS trên ESP32](#31-tổng-quan-freertos-trên-esp32)
    - [3.2. Chạy task song song trên hai nhân](#32-chạy-task-song-song-trên-hai-nhân)
    - [3.3. Ví dụ tạo task đa nhiệm](#33-ví-dụ-tạo-task-đa-nhiệm)
  - [4. Digital, Analog và kết nối cảm biến](#4-digital-analog-và-kết-nối-cảm-biến)
    - [4.1. Digital I/O](#41-digital-io)
    - [4.2. Sử dụng ADC](#42-sử-dụng-adc)
    - [4.3. Sử dụng PWM](#43-sử-dụng-pwm)
    - [4.4. Kết nối cảm biến cơ bản](#44-kết-nối-cảm-biến-cơ-bản)
  - [5. Ví dụ điều khiển relay bằng ESP32-S3](#5-ví-dụ-điều-khiển-relay-bằng-esp32-s3)
    - [5.1. Kích relay qua GPIO](#51-kích-relay-qua-gpio)
    - [5.2. Bật/tắt relay định kỳ hoặc theo sự kiện](#52-bậttắt-relay-định-kỳ-hoặc-theo-sự-kiện)
    - [Trả lời trực tiếp](#trả-lời-trực-tiếp)
      - [**Lập trình và cấu hình**](#lập-trình-và-cấu-hình)
      - [**Phần cứng và tính năng**](#phần-cứng-và-tính-năng)
      - [**Mã mẫu cơ bản**](#mã-mẫu-cơ-bản)
    - [Báo cáo chi tiết về ESP32-S3 6 Relay](#báo-cáo-chi-tiết-về-esp32-s3-6-relay)
      - [**Giới thiệu và tổng quan**](#giới-thiệu-và-tổng-quan)
      - [**Thông số phần cứng chi tiết**](#thông-số-phần-cứng-chi-tiết)
      - [**Cài đặt môi trường lập trình**](#cài-đặt-môi-trường-lập-trình)
      - [**Lập trình lõi kép với FreeRTOS**](#lập-trình-lõi-kép-với-freertos)
      - [**Cài đặt RTOS**](#cài-đặt-rtos)
      - [**Cấu hình các tính năng Digital, Analog, và Cảm biến**](#cấu-hình-các-tính-năng-digital-analog-và-cảm-biến)
      - [**Mở rộng và tích hợp**](#mở-rộng-và-tích-hợp)
      - [**Hỗ trợ và tài liệu**](#hỗ-trợ-và-tài-liệu)
    - [Key Citations](#key-citations)

## 1. Thông tin phần cứng

### 1.1. Đặc điểm kỹ thuật ESP32-S3 DevKitC

ESP32-S3 DevKitC là **một board phát triển** của Espressif, sử dụng module **ESP32-S3-WROOM**. Chip ESP32-S3 là SoC 2.4 GHz tích hợp Wi-Fi và Bluetooth 5 (LE), **dual-core Xtensa LX7** lên đến 240 MHz. Cụ thể:

* **CPU:** dual-core Xtensa LX7, xung nhịp tối đa 240 MHz.
* **Bộ nhớ:** 512 KB SRAM tích hợp, hỗ trợ thêm flash SPI (đặc biệt hỗ trợ flash octal nhanh) và PSRAM ngoài.
* **Kết nối không dây:** Wi-Fi 802.11 b/g/n (2.4 GHz) và Bluetooth 5 (LE, hỗ trợ long range, 2 Mbps).
* **GPIO và ngoại vi:** tổng cộng 45 chân GPIO lập trình được; hỗ trợ nhiều giao thức ngoại vi như SPI, I²S, I²C, UART, SD/MMC host, CAN (TWAI), bộ định thời (timer), PWM, ADC, RMT, DAC và chức năng cảm ứng điện dung. Trong đó có 14 chân có thể dùng làm cảm biến điện dung (touch).
* **USB OTG:** hỗ trợ USB 1.1 (full-speed) trên chân USB (có cổng USB Type-C trên board DevKitC phiên bản mới) dùng để cấp nguồn, lập trình hoặc giao tiếp USB/JTAG. Board có **USB-to-UART** tích hợp (lên tới 3 Mbps) để nạp code qua cổng USB Micro-B (hoặc USB-C tùy phiên bản).
* **Nguồn:** Board có mạch điều áp 5V→3.3V và đèn LED báo nguồn 3.3V.
* **Nút bấm:** Nút `BOOT` (tải chương trình) và `RESET` (khởi động lại) có sẵn để vào bootloader hoặc reset thiết bị.
* **Mô-đun WiFi/BT:** Thường là ESP32-S3-WROOM-1 (PCB antenna) hoặc WROOM-1U (sma antenna), cung cấp khả năng xử lý tác vụ AI (acceleration cho mạng nơ-ron) nhờ hướng dẫn vector.

Tóm lại, ESP32-S3 DevKitC là board có khả năng xử lý mạnh mẽ, nhiều kết nối ngoại vi, phù hợp cho phát triển IoT và AIoT (AIoT: AI + IoT).

### 1.2. Mô tả bo mạch Waveshare ESP32-S3 6-Relay

Waveshare ESP32-S3 6-Relay là **mô-đun công nghiệp** tích hợp sẵn **6 relay** (mỗi relay 1NO+1NC) điều khiển bằng ESP32-S3, dùng nguồn rộng (7–36V) hoặc USB Type-C 5V. Các đặc điểm chính gồm:

* **Bộ điều khiển chính:** ESP32-S3 (dual-core LX7 240 MHz, Wi-Fi/Bluetooth LE tích hợp). Cung cấp hiệu năng AI mạnh mẽ và mã hóa bảo mật.
* **Relay chất lượng cao:** 6 kênh relay đóng ngắt dòng tải nặng (công tắc SPDT), chịu tối đa 10A @ 250VAC hoặc 10A @ 30VDC mỗi kênh. Các relay được cách ly opto (PC817) để chống nhiễu, cung cấp 1 chân COM (chung), 1 chân NC (đóng khi OFF) và 1 chân NO (mở khi OFF) trên đầu cắm vít.
* **Cổng giao tiếp RS485:** onboard giao tiếp RS485 cách ly, tiện kết nối các thiết bị công nghiệp hoặc modbus ngoại vi. Kèm theo có trở điều kháng 120Ω (RS485 terminator) có thể bật/tắt bằng jumper.
* **Giao diện mở rộng:** Có **khe cắm tương thích Raspberry Pi Pico** cho các mạch mở rộng (RTC, CAN, RS232, LoRa, cảm biến, v.v.).
* **Nguồn và kết nối:** Hỗ trợ cấp nguồn 7–36V qua cổng vít hoặc 5V qua cổng USB-C (có thể sạc). Có mạch bảo vệ cách ly nguồn, cáp TVS và ống phóng sét khí giúp ổn định điện áp. LED báo trạng thái nguồn (PWR) và tín hiệu RX/TX RS485 cũng được trang bị.
* **Các thành phần bổ sung:** Buzzer tích hợp (điều khiển qua GPIO21), đèn LED RGB WS2812 (GPIO38), LED báo nguồn 3.3V. Mạch được bao trong hộp nhựa ABS có thanh bắt rail dễ lắp đặt.

&#x20;*Hình 1. Bo mạch Waveshare ESP32-S3 6-Relay (các thành phần chính được đánh số). Các relay 1–6, cổng vít kết nối tải (COM/NC/NO), cổng USB-C, cổng giao tiếp RS485, các chân GPIO ra, nút Boot/Reset, LED báo, v.v.*

Như hình trên, các relay được gắn trên khối vít phía trên (mục 8 trong ảnh), thuận tiện để đấu nối thiết bị công suất. Mỗi relay có 3 chân vít: **COM, NC, NO**. Khi relay không cấp điện (OFF), COM nối với NC; khi kích hoạt (ON), COM nối với NO. Người dùng đấu nguồn hay tải tùy ý vào COM/NO (để khi ON thì đóng mạch) hoặc COM/NC (để khi OFF đóng mạch) tùy ứng dụng. Các công tắc DIP (mục 20) cho phép cấu hình mặc định chân điều khiển của mỗi kênh.

### 1.3. Sơ đồ chân và kết nối relay

Bảng dưới đây liệt kê **các chân GPIO trên ESP32-S3** tương ứng với từng kênh relay (theo tài liệu Waveshare):

| GPIO | Hàm                      |
| ---- | ------------------------ |
| GP1  | CH1 – điều khiển Relay 1 |
| GP2  | CH2 – điều khiển Relay 2 |
| GP41 | CH3 – điều khiển Relay 3 |
| GP42 | CH4 – điều khiển Relay 4 |
| GP45 | CH5 – điều khiển Relay 5 |
| GP46 | CH6 – điều khiển Relay 6 |

. Khi viết code, ta chỉ cần điều khiển các chân này (digital HIGH/LOW) để bật/tắt relay tương ứng. Các chân còn lại như GP21 (buzzer), GP38 (LED RGB), GP17/GP18 (UART/RS485 TX/RX) cũng được ghi chú trong tài liệu.

Để **đấu nối** các thiết bị vào relay: mỗi kênh có ba đầu vít COM/NC/NO (kể từ phải sang trái tương ứng NC, COM, NO trên board). Ví dụ, để cấp nguồn cho tải khi relay ON, ta nối nguồn (hoặc tín hiệu) vào COM và tải vào NO; khi muốn tắt relay, nối vào NC. Lưu ý đảm bảo chỉ kết nối thiết bị tĩnh thích hợp (ví dụ thiết bị AC/DC chịu điện áp tương ứng) với relay theo cặp COM/NO/NC.

## 2. Hướng dẫn thiết lập môi trường phát triển với PlatformIO

### 2.1. Cài đặt PlatformIO trong VS Code

Để lập trình ESP32-S3 với PlatformIO, trước hết cần **cài đặt Visual Studio Code** (tải từ [code.visualstudio.com](https://code.visualstudio.com/)) và sau đó cài **extension PlatformIO IDE**. Cụ thể, mở VS Code, chọn **Extensions** (Ctrl+Shift+X), tìm và cài “**PlatformIO IDE**”. Sau khi cài xong, biểu tượng PlatformIO (hình ngôi nhà) sẽ hiện ra ở thanh công cụ bên trái. Nếu chưa thấy, khởi động lại VS Code. Như vậy, môi trường PlatformIO đã sẵn sàng.

### 2.2. Cấu hình `platformio.ini` cho ESP32-S3

Trong Project mới, ta cấu hình file **`platformio.ini`** để chọn đúng board ESP32-S3 DevKitC và framework (Arduino hoặc ESP-IDF). Ví dụ, để dùng Arduino framework, cấu hình mẫu có thể như sau:

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
```

Trong đó `board = esp32-s3-devkitc-1` tương ứng với ESP32-S3-DevKitC-1 (8MB flash). Bạn cũng có thể thay bằng `esp32-s3-devkitc-1-n16r8v` nếu board có 16MB flash (theo ví dụ trong cộng đồng). Đối với ESP-IDF framework, chỉ cần đổi `framework = espidf`. Các tùy chọn khác như `board_build.f_cpu` cũng có thể điều chỉnh nếu cần. Sau khi lưu `platformio.ini`, PlatformIO sẽ tự động tải về các gói cần thiết và chuẩn bị môi trường biên dịch.

## 3. RTOS và đa nhiệm trên ESP32

### 3.1. Tổng quan FreeRTOS trên ESP32

ESP32-S3 (giống các chip ESP32 khác) sử dụng hệ điều hành thời gian thực **FreeRTOS** tùy biến (được tích hợp trong ESP-IDF). Phiên bản FreeRTOS của Espressif hỗ trợ **đa nhiệm đối xứng (SMP)** cho hai nhân CPU (PRO\_CPU = Core 0, APP\_CPU = Core 1). Thông thường, các tác vụ điều khiển giao thức như Wi-Fi/Bluetooth được ghim vào Core 0 (PRO\_CPU), còn tác vụ ứng dụng chính chạy trên Core 1 (APP\_CPU). Tuy nhiên, người lập trình có thể tùy ý khởi tạo nhiệm vụ (task) trên một hoặc cả hai nhân.

Các hàm tạo task tiêu chuẩn như `xTaskCreatePinnedToCore()` cho phép chỉ định nhân xử lý. Ví dụ, để tạo hai task độc lập chạy song song, ta có thể dùng:

```cpp
TaskHandle_t Task0, Task1;

void setup() {
  Serial.begin(115200);
  // Tạo task chạy trên Core 0:
  xTaskCreatePinnedToCore(
    Task0code, "Task0", 10000, NULL, 1, &Task0, 0
  );
  // Tạo task chạy trên Core 1:
  xTaskCreatePinnedToCore(
    Task1code, "Task1", 10000, NULL, 1, &Task1, 1
  );
}
```

Trong đó `Task0code` và `Task1code` là các hàm thực thi theo vòng lặp vô hạn. `xTaskCreatePinnedToCore` là hàm của ESP-IDF cho phép chỉ định tác vụ vào một core cụ thể. Cụ thể, đối số cuối `0` hoặc `1` là ID nhân xử lý. Nếu muốn tác vụ có thể chạy trên cả hai nhân, dùng `tskNO_AFFINITY`.

### 3.2. Chạy task song song trên hai nhân

Với FreeRTOS SMP, mỗi nhân sẽ **lập lịch độc lập** các task theo mức độ ưu tiên. Trên ESP32-S3, nhân 0 (PRO\_CPU) thường được tự động dùng cho các tính năng hệ thống (WiFi/BT), còn nhân 1 (APP\_CPU) chạy ứng dụng và `loop()` của Arduino theo mặc định. Tuy nhiên, bất kỳ task nào được tạo với `xTaskCreatePinnedToCore` có thể gán cố định vào core mong muốn (hoặc để mặc định).

Để tận dụng đa nhân, ta tạo một hoặc nhiều task, mỗi task là một hàm thực hiện công việc trong vòng lặp. Ví dụ, hai hàm nhiệm vụ sau in thông tin thread và core hiện tại:

```cpp
void Task0code(void *pvParameters) {
  for (;;) {
    Serial.print("Task0 running on core ");
    Serial.println(xPortGetCoreID());
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
void Task1code(void *pvParameters) {
  for (;;) {
    Serial.print("Task1 running on core ");
    Serial.println(xPortGetCoreID());
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
```

Các task này khi được tạo (như phần trước) sẽ chạy đồng thời trên core 0 và 1. Kết quả in ra màn hình sẽ cho thấy mỗi task chạy trên core đã chọn (ví dụ “Task0 running on core 0”). Cách này cho phép thực hiện các công việc độc lập song song, như điều khiển hai động cơ, xử lý giao tiếp, thu thập dữ liệu cảm biến…

### 3.3. Ví dụ tạo task đa nhiệm

Ví dụ minh họa sử dụng FreeRTOS trong Arduino (PlatformIO): tạo hai task, một task nháy LED trên Core 0 và một task đọc cảm biến giả lập trên Core 1. Trong `setup()` ta tạo như sau:

```cpp
TaskHandle_t taskLed, taskSensor;
const int ledPin = 2;

void setup() {
  Serial.begin(115200);
  xTaskCreatePinnedToCore(ledTask, "LEDTask", 2048, (void*)&ledPin, 1, &taskLed, 0);
  xTaskCreatePinnedToCore(sensorTask, "SensorTask", 2048, NULL, 1, &taskSensor, 1);
}
void ledTask(void* param) {
  int pin = *((int*)param);
  pinMode(pin, OUTPUT);
  while (true) {
    digitalWrite(pin, HIGH);
    vTaskDelay(pdMS_TO_TICKS(500));
    digitalWrite(pin, LOW);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
void sensorTask(void* pvParameters) {
  while (true) {
    Serial.println("Sensor reading...");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
```

Trong ví dụ trên, LED ghim GP2 sẽ nhấp nháy trên core 0, trong khi “đọc cảm biến” (ở đây chỉ in thông báo) chạy trên core 1. Kết quả hai task chạy song song độc lập, minh họa cách sử dụng đa nhân của ESP32-S3.

## 4. Digital, Analog và kết nối cảm biến

### 4.1. Digital I/O

Các chân GPIO trên ESP32-S3 có thể cấu hình làm **digital input** hoặc **digital output** rất dễ dàng. Trong Arduino framework, dùng hàm `pinMode(pin, MODE)`, `digitalWrite(pin, state)` và `digitalRead(pin)` để điều khiển. Ví dụ, để bật tắt LED hoặc relay qua chân GPIO, ta viết:

```cpp
const int ledPin = 2;
void setup() {
  pinMode(ledPin, OUTPUT);
}
void loop() {
  digitalWrite(ledPin, HIGH);   // bật LED
  delay(1000);
  digitalWrite(ledPin, LOW);    // tắt LED
  delay(1000);
}
```

Hoặc để đọc công tắc, nối công tắc vào chân input có thể dùng:

```cpp
const int buttonPin = 15;
void setup() {
  pinMode(buttonPin, INPUT_PULLUP);
  Serial.begin(115200);
}
void loop() {
  if (digitalRead(buttonPin) == LOW) {
    Serial.println("Button pressed");
    delay(200); // chống nhiễu (debounce)
  }
}
```

### 4.2. Sử dụng ADC

ESP32-S3 có **bộ chuyển đổi ADC** (độ phân giải 12-bit) dùng để đọc tín hiệu analog (0–3.3V). Trong Arduino, ta dùng `analogRead(pin)` để lấy giá trị 0–4095 tương ứng điện áp đo được. Ví dụ đọc cảm biến áp LM35 hoặc biến trở:

```cpp
const int adcPin = 35; // GPIO35 (ADC1_CH6)
void setup() {
  Serial.begin(115200);
}
void loop() {
  int value = analogRead(adcPin);
  Serial.println(value);
  delay(500);
}
```

Giá trị `value` trong khoảng 0–4095 tương ứng từ 0V đến 3.3V. Lưu ý rằng các chân ADC2 (GPIO 0,2,15,13,12,14) sẽ không hoạt động khi Wi-Fi đang dùng.

### 4.3. Sử dụng PWM

ESP32-S3 hỗ trợ xuất tín hiệu PWM (điều chế xung) trên hầu hết các chân GPIO (ngoại trừ GPIO34–39). Bộ điều khiển PWM (LEDC) có **16 kênh độc lập** và độ phân giải lên tới 16-bit. Trong Arduino, ta có thể sử dụng hàm `ledcAttachPin(pin, channel)`, `ledcSetup(channel, freq, resolution)`, và `ledcWrite(channel, duty)` để phát xung PWM. Ví dụ tạo xung PWM tần số 5 kHz, độ rộng 8-bit:

```cpp
const int pwmPin = 18;
const int pwmChannel = 0;
void setup() {
  ledcSetup(pwmChannel, 5000, 8); 
  ledcAttachPin(pwmPin, pwmChannel);
}
void loop() {
  for (int duty = 0; duty <= 255; duty++) {
    ledcWrite(pwmChannel, duty); // tăng dần độ rộng xung
    delay(10);
  }
  for (int duty = 255; duty >= 0; duty--) {
    ledcWrite(pwmChannel, duty); // giảm độ rộng xung
    delay(10);
  }
}
```

Trong ví dụ, chân GP18 phát tín hiệu PWM 5 kHz với chu kỳ nhiệm vụ thay đổi dần, điều khiển độ sáng LED hoặc động cơ.

### 4.4. Kết nối cảm biến cơ bản

Để kết nối cảm biến với ESP32-S3, tuỳ loại cảm biến sẽ dùng giao diện **digital** (GPIO, I²C, SPI, UART, v.v.) hoặc **analog**. Ví dụ: cảm biến nhiệt độ LM35 (analog) nối vào chân ADC, sau đó dùng `analogRead` để lấy giá trị; cảm biến DHT11 (digital) nối vào một GPIO và sử dụng thư viện DHT hoặc đọc tín hiệu single-wire. Đối với cảm biến I²C (ví dụ MPU6050, BH1750), nối hai chân SDA/SCL và sử dụng thư viện Wire. Ta minh hoạ chung bằng ví dụ đọc cảm biến ánh sáng (LDR) qua ADC:

```cpp
const int sensorPin = 34; // ADC1_CH6
void setup() {
  Serial.begin(115200);
}
void loop() {
  int lightLevel = analogRead(sensorPin);
  Serial.print("Light level = ");
  Serial.println(lightLevel);
  delay(500);
}
```

Với cảm biến digital đơn giản (ví dụ công tắc từ/hạ tầng), ta dùng `digitalRead` trên chân đã cấu hình input như ví dụ 4.1 ở trên. Mọi giá trị thu được có thể hiển thị qua `Serial` hoặc gửi đi, tùy mục đích ứng dụng.

## 5. Ví dụ điều khiển relay bằng ESP32-S3

### 5.1. Kích relay qua GPIO

Để điều khiển relay, ta chỉ việc xuất tín hiệu HIGH/LOW tới chân GPIO tương ứng (GP1–GP46) đã nối với bộ cách ly của relay trên board. Ví dụ, để đóng relay kênh 1 (CH1, chân GP1) trong Arduino ta có thể viết:

```cpp
const int relayPin = 1; // chân GP1 tương ứng Relay 1
void setup() {
  pinMode(relayPin, OUTPUT);
}
void loop() {
  digitalWrite(relayPin, HIGH);  // Bật relay (đóng COM-NO)
  delay(1000);
  digitalWrite(relayPin, LOW);   // Tắt relay (đóng COM-NC)
  delay(1000);
}
```

Trong ví dụ này, relay sẽ đóng/mở liên tục mỗi giây. Cần chú ý một số bo mạch relay sử dụng **mức kích thích thấp (active LOW)**, tuy nhiên tài liệu Waveshare cho thấy relay điều khiển thông qua optocoupler với điện trở pull-up, nên mức HIGH kích hoạt đóng relay.

Ngoài ra, có thể điều khiển relay bằng cách nhận tín hiệu từ cảm biến hay nút bấm. Ví dụ, gắn một công tắc (hoặc cảm biến) vào chân GPIO khác và trong code kiểm tra trạng thái để bật relay:

```cpp
const int switchPin = 15; // nút nhấn
const int relayPin = 2;   // Relay CH1
void setup() {
  pinMode(switchPin, INPUT_PULLUP);
  pinMode(relayPin, OUTPUT);
}
void loop() {
  if (digitalRead(switchPin) == LOW) {
    digitalWrite(relayPin, HIGH); // Bật relay khi nút nhấn LOW
  } else {
    digitalWrite(relayPin, LOW);
  }
}
```

### 5.2. Bật/tắt relay định kỳ hoặc theo sự kiện

Để điều khiển relay định kỳ (ví dụ bật/tắt tự động sau mỗi khoảng thời gian), ta có thể sử dụng bộ đếm thời gian (timer) hoặc tạo FreeRTOS task. Ví dụ, sử dụng một task để nhấp nháy relay mỗi giây:

```cpp
void relayTask(void* pvParameter) {
  int pin = (int)pvParameter;
  pinMode(pin, OUTPUT);
  while (true) {
    digitalWrite(pin, HIGH);
    vTaskDelay(pdMS_TO_TICKS(1000));  // delay 1000 ms
    digitalWrite(pin, LOW);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void setup() {
  const int relayPin = 2;
  xTaskCreatePinnedToCore(relayTask, "RelayBlink", 2048, (void*)relayPin, 1, NULL, 0);
}
```

Task `relayTask` sẽ bật relay 1 giây, tắt 1 giây, chạy lặp mãi. Bằng cách gán task này vào một core (ví dụ Core 0), hệ thống có thể thực hiện song song với các tác vụ khác. Tương tự, có thể dùng hàm `esp_timer_create()` trong ESP-IDF để gọi callback định kỳ. Tuyệt đối tuân thủ các chú ý an toàn điện khi điều khiển relay tải lớn (dòng >10A) và cách ly nguồn cho phù hợp.

**Nguồn tham khảo:** Tài liệu chính thức ESP32-S3 DevKitC và Waveshare ESP32-S3 Relay-6CH; Hướng dẫn PlatformIO cho ESP32-S3; Tài liệu FreeRTOS của Espressif. Các ví dụ mã tham khảo Arduino/ESP-IDF.
### Trả lời trực tiếp

- Mô-đun ESP32-S3 6 Relay là thiết bị công nghiệp với 6 rơ-le, hỗ trợ WiFi, Bluetooth, RS485, phù hợp cho IoT.
- Lập trình lõi kép sử dụng FreeRTOS, dễ dàng qua Arduino IDE, với mã mẫu cho điều khiển rơ-le và cảm biến.
- Cài đặt RTOS tích hợp sẵn, không cần cấu hình thêm trong Arduino.
- Phần cứng gồm 6 rơ-le, GPIO đa năng, ADC, và giao diện mở rộng qua header 40 chân.
- Tính năng digital, analog, cảm biến được setup dễ dàng với thư viện Arduino hoặc MicroPython.

#### **Lập trình và cấu hình**
Bạn có thể lập trình ESP32-S3 6 Relay bằng Arduino IDE hoặc MicroPython. Để lập trình lõi kép, dùng FreeRTOS với hàm `xTaskCreatePinnedToCore()` để gán tác vụ cho lõi 0 hoặc 1, ví dụ nhấp nháy LED trên hai lõi. RTOS đã tích hợp sẵn, không cần cài đặt thêm. Mã mẫu cho điều khiển rơ-le và đọc cảm biến analog có sẵn, như ví dụ dưới đây.

#### **Phần cứng và tính năng**
Mô-đun có 6 rơ-le với định mức ≤10A 250V AC, hỗ trợ RS485, WiFi, Bluetooth, và header 40 chân mở rộng. Các GPIO như GP1-GP46 điều khiển rơ-le, ADC đọc tín hiệu analog, và cảm biến I2C/SPI kết nối dễ dàng. Bảo vệ cách ly quang, nguồn đảm bảo an toàn.

#### **Mã mẫu cơ bản**
Dưới đây là mã điều khiển rơ-le và đọc cảm biến analog:

- **Điều khiển rơ-le (Arduino)**:
  ```cpp
  const int relayPin = 1; // GP1 cho rơ-le CH1
  void setup() {
    pinMode(relayPin, OUTPUT);
  }
  void loop() {
    digitalWrite(relayPin, HIGH); // Bật rơ-le
    delay(1000);
    digitalWrite(relayPin, LOW); // Tắt rơ-le
    delay(1000);
  }
  ```

- **Đọc cảm biến analog (Arduino)**:
  ```cpp
  const int analogPin = 4; // GPIO4 cho ADC
  void setup() {
    Serial.begin(115200);
  }
  void loop() {
    int value = analogRead(analogPin);
    Serial.println(value);
    delay(1000);
  }
  ```

---

### Báo cáo chi tiết về ESP32-S3 6 Relay

#### **Giới thiệu và tổng quan**
Mô-đun ESP32-S3 6 Relay của Waveshare là một giải pháp công nghiệp mạnh mẽ, dựa trên vi điều khiển ESP32-S3 với lõi kép Xtensa LX7 32-bit, tốc độ tối đa 240MHz. Nó tích hợp WiFi 2.4GHz, Bluetooth LE, RS485, và giao diện Pico, phù hợp cho các ứng dụng IoT và tự động hóa. Mô-đun có 6 kênh rơ-le với định mức tiếp điểm ≤10A 250V AC hoặc ≤10A 30V DC, và được trang bị các biện pháp bảo vệ như cách ly quang, cách ly số, và cách ly nguồn, đảm bảo an toàn trong môi trường công nghiệp. Kích thước 145 × 90 × 40 mm, vỏ ABS gắn ray DIN, lý tưởng cho lắp đặt cố định.

#### **Thông số phần cứng chi tiết**
Dưới đây là bảng tổng hợp các thông số phần cứng chính:

| **Thể loại**              | **Chi tiết**                                                                 |
|---------------------------|-----------------------------------------------------------------------------|
| **Vi điều khiển**         | ESP32-S3, lõi kép Xtensa LX7, 240MHz, WiFi 2.4GHz, Bluetooth LE             |
| **Rơ-le**                | 6 kênh, ≤10A 250V AC / 30V DC, 1NO 1NC                                     |
| **Nguồn cấp**            | 7~36V DC qua terminal, hoặc 5V/1A qua USB Type-C                            |
| **Giao tiếp**            | RS485, USB Type-C, SMA nữ cho anten WiFi/Bluetooth                         |
| **Giao diện mở rộng**    | Tương thích Raspberry Pi Pico HAT, hỗ trợ RTC/CAN/RS232/LoRa/cảm biến       |
| **Chỉ báo**              | Đèn PWR, RXD, TXD, LED RGB (GPIO38), còi (GPIO21)                          |
| **Bảo vệ**               | Cách ly quang, số, nguồn, TVS, ống xả khí sứ                                |
| **Kích thước**           | 145 × 90 × 40 mm, vỏ ABS gắn ray DIN                                       |

Bảng ánh xạ GPIO cụ thể:
- GP0: Nút BOOT.
- GP21: Còi báo động.
- GP38: LED RGB.
- GP1: CH1 (Rơ-le 1), GP2: CH2, GP41: CH3, GP42: CH4, GP45: CH5, GP46: CH6.
- GP17: TXD (RS485), GP18: RXD (RS485).

#### **Cài đặt môi trường lập trình**
- **Arduino IDE**: Tải từ [Arduino]([invalid url, do not cite]), thêm hỗ trợ ESP32 qua Board Manager với URL [invalid url, do not cite]. Chọn board “ESP32S3 Dev Module”. Cài đặt thư viện như ArduinoJson (v6.21.4), PubSubClient (v2.8.0), NTPClient (v3.2.1).
- **MicroPython**: Sử dụng Thonny IDE từ [Thonny]([invalid url, do not cite]), tải firmware từ [MicroPython]([invalid url, do not cite]) và nạp vào ESP32-S3.
- **ESP-IDF**: Dành cho ứng dụng phức tạp, tham khảo [ESP-IDF]([invalid url, do not cite]) để cài đặt môi trường phát triển.

#### **Lập trình lõi kép với FreeRTOS**
ESP32-S3 có hai lõi (Core 0 và Core 1), hỗ trợ lập trình đồng thời với FreeRTOS. Trong Arduino IDE, dùng hàm `xTaskCreatePinnedToCore()` để gán tác vụ cho lõi cụ thể. Ví dụ mã nhấp nháy LED trên hai lõi:

```cpp
TaskHandle_t Task1;
TaskHandle_t Task2;

const int led_1 = 32;
const int led_2 = 25;

void setup() {
  Serial.begin(115200);
  pinMode(led_1, OUTPUT);
  pinMode(led_2, OUTPUT);
  
  xTaskCreatePinnedToCore(Task1code, "Task1", 10000, NULL, 1, &Task1, 0);
  xTaskCreatePinnedToCore(Task2code, "Task2", 10000, NULL, 1, &Task2, 1);
}

void loop() {}

void Task1code(void * parameter) {
  Serial.print("Task1 running on core ");
  Serial.println(xPortGetCoreID());
  for(;;) {
    digitalWrite(led_1, HIGH);
    delay(500);
    digitalWrite(led_1, LOW);
    delay(500);
  }
}

void Task2code(void * parameter) {
  Serial.print("Task2 running on core ");
  Serial.println(xPortGetCoreID());
  for(;;) {
    digitalWrite(led_2, HIGH);
    delay(1000);
    digitalWrite(led_2, LOW);
    delay(1000);
  }
}
```

Hướng dẫn: Kết nối LED với GPIO32 và GPIO25 qua điện trở 220 ohm, tải mã, LED nhấp nháy với tần số khác nhau trên hai lõi.

#### **Cài đặt RTOS**
FreeRTOS đã tích hợp sẵn trong Arduino core cho ESP32-S3, không cần cấu hình thêm. Trong ESP-IDF, hỗ trợ SMP (Symmetric Multiprocessing) cho lõi kép, với tùy chọn `CONFIG_FREERTOS_UNICORE` cho chế độ đơn lõi nếu cần.

#### **Cấu hình các tính năng Digital, Analog, và Cảm biến**
- **Digital**: Sử dụng `digitalWrite(pin, state)` để điều khiển rơ-le, ví dụ GP1 cho CH1. Mã mẫu đã cung cấp ở phần trên.
- **Analog**: Đọc tín hiệu qua ADC (GPIO1-10, ví dụ GPIO4), dùng `analogRead()`. Mã mẫu đọc analog cũng đã cung cấp.
- **Cảm biến**: Hỗ trợ I2C, SPI, one-wire. Ví dụ cảm biến BMP280 (I2C):

```cpp
#include <Wire.h>
#include <Adafruit_BMP280.h>

Adafruit_BMP280 bmp;

void setup() {
  Serial.begin(115200);
  if (!bmp.begin(0x76)) {
    Serial.println("Không tìm thấy cảm biến BMP280");
    while (1);
  }
}

void loop() {
  Serial.print("Nhiệt độ = ");
  Serial.print(bmp.readTemperature());
  Serial.println(" *C");
  delay(2000);
}
```

Hướng dẫn: Kết nối BMP280 với GPIO8 (SDA), GPIO9 (SCL), tải mã, nhiệt độ hiển thị mỗi 2 giây.

#### **Mở rộng và tích hợp**
- Mở rộng RS485 với Modbus RTU Relay, timer với Pico-RTC-DS3231, CAN với Pico-CAN-B, giám sát môi trường với Pico-Environment-Sensor, RS232/RS485 với Pico-2CH-RS232/RS485. Các demo có sẵn tại [Waveshare Wiki](https://www.waveshare.com/wiki/ESP32-S3-Relay-6CH).
- Tích hợp Homeassistant, tham khảo [hướng dẫn](https://www.waveshare.com/wiki/Using_Modbus_RTU_Relay_with_Homeassistant).

#### **Hỗ trợ và tài liệu**
Waveshare cung cấp các mã mẫu như `01_MAIN_WIFI_AP`, `03_MAIN_WIFI_MQTT`, và firmware mặc định. Hỗ trợ kỹ thuật qua ticket, giờ làm việc 9 AM - 6 PM GMT+8, Thứ Hai đến Thứ Sáu.

---

### Key Citations
- [Waveshare ESP32-S3-Relay-6CH Wiki](https://www.waveshare.com/wiki/ESP32-S3-Relay-6CH)
- [Arduino Official Download Page]([invalid url, do not cite])
- [Thonny IDE Download Page]([invalid url, do not cite])
- [MicroPython Official GitHub Repository]([invalid url, do not cite])
- [ESP-IDF Official Documentation]([invalid url, do not cite])