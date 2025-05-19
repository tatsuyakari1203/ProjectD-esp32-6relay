# Lập trình Đa lõi ESP32-S3 với Arduino & PlatformIO: Tóm tắt và Mã mẫu

# ESP32-S3 với hai lõi Xtensa LX7 cho phép thực thi song song, tăng hiệu suất ứng dụng. Arduino framework trên ESP32-S3 sử dụng FreeRTOS để quản lý đa nhiệm.

## I. Các Khái niệm Cốt lõi

# 1. ****Kiến trúc Lõi Kép:****

   - ****Core 0 (PRO\_CPU):**** Thường dùng cho Wi-Fi, Bluetooth và các tác vụ hệ thống cần độ trễ thấp.

   - ****Core 1 (APP\_CPU):**** Thường dùng cho logic ứng dụng chính.

   - ****Mặc định Arduino:**** Hàm `setup()` và `loop()` chạy trên ****Core 1 (APP\_CPU)****.

2. ****FreeRTOS và Tác vụ (Tasks):****

   - Mỗi công việc độc lập chạy song song được gọi là một "task".

   - Sử dụng hàm `xTaskCreatePinnedToCore()` để tạo task và ghim nó vào một lõi cụ thể.

3. ****Xác định Lõi Hiện tại:****

   - Dùng `xPortGetCoreID()` để biết mã đang chạy trên lõi nào (0 hoặc 1).

## II. Tạo và Quản lý Tác vụ

Hàm `xTaskCreatePinnedToCore()`:    BaseType_t xTaskCreatePinnedToCore(
        TaskFunction_t pvTaskCode,     // Con trỏ đến hàm thực thi của task
        const char * const pcName,       // Tên của task (để debug)
        const uint32_t usStackDepth,   // Kích thước stack (tính bằng BYTE)
        void * const pvParameters,     // Tham số truyền vào hàm task
        UBaseType_t uxPriority,        // Độ ưu tiên (0 là thấp nhất)
        TaskHandle_t * const pvCreatedTask, // Con trỏ lưu Task Handle (tùy chọn)
        const BaseType_t xCoreID         // Lõi để chạy task (0 hoặc 1)
    );* ****`usStackDepth` (Kích thước Stack):**** Rất quan trọng! Đơn vị là ****BYTES**** trên ESP-IDF/Arduino. Cần cấp đủ để tránh lỗi tràn stack (ví dụ: 2048, 4096, 8192 bytes).

* ****`uxPriority` (Độ ưu tiên):**** Tác vụ `loop()` của Arduino thường có độ ưu tiên 1.

* ****`xCoreID` (ID Lõi):**** `0` cho PRO\_CPU, `1` cho APP\_CPU.
=================================================================

## III. Phân chia Tác vụ Cơ bản

# - ****Core 0 (PRO\_CPU):**** Dành cho các tác vụ mạng (Wi-Fi, Bluetooth), các tác vụ thời gian thực, hoặc các công việc nền không nên ảnh hưởng đến logic chính.

- ****Core 1 (APP\_CPU):**** Dành cho logic ứng dụng chính (thường là trong `loop()` hoặc các task do người dùng tạo cho ứng dụng).

## IV. Giao tiếp và Đồng bộ hóa Giữa các Lõi/Task (IPC)

# Khi các task (đặc biệt trên các lõi khác nhau) cần chia sẻ dữ liệu hoặc đồng bộ hóa, cần sử dụng các cơ chế IPC của FreeRTOS để tránh lỗi (race conditions, data corruption).1) ****Hàng đợi (Queues):****

   - An toàn để gửi bản sao dữ liệu giữa các task.

   - API: `xQueueCreate()`, `xQueueSend()`, `xQueueReceive()`.

2) ****Mutexes (Mutual Exclusion):****

   - Bảo vệ tài nguyên dùng chung (ví dụ: biến toàn cục, ngoại vi). Chỉ một task được truy cập tại một thời điểm.

   - API: `xSemaphoreCreateMutex()`, `xSemaphoreTake()`, `xSemaphoreGive()`.

3) ****Semaphores Nhị phân (Binary Semaphores):****

   - Dùng để báo hiệu (signaling) giữa các task hoặc giữa ISR và task.

   - API: `xSemaphoreCreateBinary()`, `xSemaphoreTake()`, `xSemaphoreGive()`.

## V. Mã Mẫu Cơ bản

### 1. Task đơn giản trên Core 0, `loop()` trên Core 1

    #include <Arduino.h>

    TaskHandle_t myCore0TaskHandle;

    // Hàm tác vụ sẽ chạy trên Core 0
    void core0TaskCode(void *pvParameters) {
      Serial.print("Core 0 Task is running on core: ");
      Serial.println(xPortGetCoreID());

      for (;;) { // Vòng lặp vô hạn của task
        Serial.println("Hello from Core 0!");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay 1 giây
      }
    }

    void setup() {
      Serial.begin(115200);
      delay(1000); // Chờ Serial sẵn sàng

      Serial.print("setup() is running on core: ");
      Serial.println(xPortGetCoreID()); // Sẽ in ra 1

      // Tạo task và ghim vào Core 0
      xTaskCreatePinnedToCore(
          core0TaskCode,      /* Hàm thực thi */
          "Core0Task",        /* Tên task */
          4096,               /* Kích thước stack (bytes) */
          NULL,               /* Tham số */
          1,                  /* Độ ưu tiên */
          &myCore0TaskHandle, /* Task handle (tùy chọn) */
          0);                 /* Chạy trên Core 0 */

      Serial.println("Core 0 Task created.");
    }

    void loop() {
      // Hàm loop() này chạy trên Core 1
      Serial.print("loop() is running on core: ");
      Serial.println(xPortGetCoreID()); // Sẽ in ra 1
      Serial.println("Hello from Core 1 (loop)!");
      delay(2000); // Delay 2 giây
    }


### 2. Giao tiếp giữa hai Lõi dùng Queue

    #include <Arduino.h>

    QueueHandle_t dataQueue;
    TaskHandle_t senderTaskHandle;

    // Task gửi dữ liệu (chạy trên Core 0)
    void senderTask(void *pvParameters) {
      Serial.print("SenderTask (Core 0) running on core: ");
      Serial.println(xPortGetCoreID());
      int counter = 0;
      for (;;) {
        counter++;
        if (xQueueSend(dataQueue, &counter, pdMS_TO_TICKS(100)) == pdPASS) {
          Serial.printf("Core 0 sent: %d\n", counter);
        } else {
          Serial.println("Core 0: Failed to send to queue.");
        }
        vTaskDelay(pdMS_TO_TICKS(1500));
      }
    }

    void setup() {
      Serial.begin(115200);
      delay(1000);

      Serial.print("setup() running on core: ");
      Serial.println(xPortGetCoreID()); // Core 1

      // Tạo queue chứa 5 item kiểu int
      dataQueue = xQueueCreate(5, sizeof(int));

      if (dataQueue == NULL) {
        Serial.println("Error creating queue!");
        while (1);
      }
      Serial.println("Queue created.");

      xTaskCreatePinnedToCore(
          senderTask, "SenderTask", 4096, NULL, 1, &senderTaskHandle, 0); // Core 0
      Serial.println("SenderTask created on Core 0.");
    }

    void loop() { // Chạy trên Core 1
      int receivedValue;
      if (xQueueReceive(dataQueue, &receivedValue, pdMS_TO_TICKS(1000)) == pdPASS) {
        Serial.printf("Core 1 (loop) received: %d\n", receivedValue);
      } else {
        // Serial.println("Core 1 (loop): No data from queue or timeout.");
      }
      // Không cần delay ở đây nếu xQueueReceive có timeout dài
    }


### 3. Bảo vệ Biến Toàn cục dùng Mutex

    #include <Arduino.h>

    SemaphoreHandle_t sharedCounterMutex;
    volatile int sharedCounter = 0; // Biến cần bảo vệ

    TaskHandle_t taskA_Handle;
    TaskHandle_t taskB_Handle;

    // Task A trên Core 0
    void taskA(void *pvParameters) {
      Serial.print("TaskA (Core 0) running on core: ");
      Serial.println(xPortGetCoreID());
      for (;;) {
        if (xSemaphoreTake(sharedCounterMutex, portMAX_DELAY) == pdTRUE) {
          sharedCounter++;
          Serial.printf("Task A (Core 0) increments counter to: %d\n", sharedCounter);
          xSemaphoreGive(sharedCounterMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(700));
      }
    }

    // Task B trên Core 1
    void taskB(void *pvParameters) {
      Serial.print("TaskB (Core 1) running on core: ");
      Serial.println(xPortGetCoreID());
      for (;;) {
        if (xSemaphoreTake(sharedCounterMutex, portMAX_DELAY) == pdTRUE) {
          sharedCounter--; // Hoặc một thao tác khác
          Serial.printf("Task B (Core 1) modifies counter to: %d\n", sharedCounter);
          xSemaphoreGive(sharedCounterMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(1200));
      }
    }

    void setup() {
      Serial.begin(115200);
      delay(1000);

      Serial.print("setup() running on core: ");
      Serial.println(xPortGetCoreID()); // Core 1

      sharedCounterMutex = xSemaphoreCreateMutex();
      if (sharedCounterMutex == NULL) {
        Serial.println("Error creating mutex!");
        while (1);
      }
      Serial.println("Mutex created.");

      xTaskCreatePinnedToCore(taskA, "TaskA", 4096, NULL, 1, &taskA_Handle, 0); // Core 0
      xTaskCreatePinnedToCore(taskB, "TaskB", 4096, NULL, 1, &taskB_Handle, 1); // Core 1
      Serial.println("Tasks A and B created.");
    }

    void loop() {
      // loop() vẫn chạy trên Core 1, có thể để trống hoặc làm nhiệm vụ phụ.
      vTaskDelay(pdMS_TO_TICKS(5000));
    }


## VI. Các Cấu hình `platformio.ini` và `sdkconfig` Quan trọng (Tóm tắt)

- ****`platformio.ini`:****

      [env:esp32s3dev]
      platform = espressif32
      board = esp32s3devkitc_1 ; Thay bằng board của bạn
      framework = arduino
      monitor_speed = 115200
      ; build_flags = -DCONFIG_ARDUINO_LOOP_STACK_SIZE=16384 ; Tăng stack cho loop()
      ;               -DCONFIG_FREERTOS_HZ=1000             ; Đặt tick rate là 1000Hz

- ****`sdkconfig`:**** Có thể tùy chỉnh qua `pio run -t menuconfig` hoặc `build_flags`.

  - `CONFIG_ARDUINO_LOOP_STACK_SIZE`: Kích thước stack cho `loop()` (mặc định 8192 bytes).

  - `CONFIG_FREERTOS_HZ`: Tần số tick FreeRTOS (mặc định 100Hz hoặc 1000Hz).

  - `CONFIG_FREERTOS_USE_TRACE_FACILITY` & `CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS`: Bật để dùng `vTaskList()`.

  - `CONFIG_PM_ENABLE` & `CONFIG_FREERTOS_USE_TICKLESS_IDLE`: Cho quản lý năng lượng.
=====================================================================================

## VII. Lưu ý Quan trọng

# - **Quản lý Stack:** Cấp phát đủ stack cho mỗi task. Dùng `uxTaskGetStackHighWaterMark()` để kiểm tra.

- **Chọn Cơ chế Đồng bộ hóa Phù hợp:** Mutex để bảo vệ, Queue để truyền dữ liệu, Semaphore để báo hiệu.

- **Tránh Deadlock:** Khi dùng nhiều Mutex, các task phải "take" chúng theo cùng một thứ tự.

- **Gỡ lỗi (Debugging):** Có thể phức tạp. Sử dụng `Serial.print` với ID lõi và tên task. PlatformIO hỗ trợ GDB.Lập trình đa lõi trên ESP32-S3 mở ra nhiều khả năng, nhưng đòi hỏi sự cẩn trọng trong thiết kế và quản lý tài nguyên.
