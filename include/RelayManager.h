#ifndef RELAY_MANAGER_H
#define RELAY_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/queue.h"

// Định nghĩa kiểu dữ liệu cho sự kiện relay timer
typedef struct {
    int relayIndex;
} RelayTimerEvent_t;

// Struct để lưu trạng thái relay
struct RelayStatus {
    bool state;                  // Trạng thái hiện tại (true = bật, false = tắt)
    TimerHandle_t timerHandle;   // Handle cho software timer của relay này
};

class RelayManager {
public:
    RelayManager();
    
    // Khởi tạo relays và queue
    void begin(const int* relayPins, int numRelays, QueueHandle_t relayEventQueue);
    
    // Điều khiển relay
    void setRelay(int relayIndex, bool state, unsigned long duration = 0);
    
    // Bật relay
    void turnOn(int relayIndex, unsigned long duration = 0);
    
    // Tắt relay
    void turnOff(int relayIndex);
    
    // Lấy trạng thái relay
    bool getState(int relayIndex);
    
    // Lấy thời gian còn lại (sẽ phức tạp hơn với software timer, có thể cần API của FreeRTOS timer)
    // unsigned long getRemainingTime(int relayIndex); // Cân nhắc loại bỏ hoặc triển khai lại
    
    // Xử lý các relay có timer - Sẽ được thay thế bằng logic trong Core1TaskCode dựa trên queue
    // void update(); // Hàm này có thể không cần thiết nữa cho việc quản lý timer
    
    // Tạo payload JSON cho trạng thái relay
    String getStatusJson(const char* apiKey);
    
    // Xử lý JSON lệnh điều khiển
    bool processCommand(const char* json);
    
    // Kiểm tra xem trạng thái có thay đổi không và reset cờ
    bool hasStatusChangedAndReset();

    // Hàm callback cho software timer (phải là static hoặc global nếu không dùng mẹo)
    static void relayTimerCallback(TimerHandle_t xTimer);
    
private:
    const int* _relayPins;        // Con trỏ đến mảng chân GPIO relay
    int _numRelays;               // Số lượng relay
    RelayStatus* _relayStatus;    // Mảng trạng thái relay (giờ chứa cả timer handle)
    SemaphoreHandle_t _mutex;     // Mutex để bảo vệ truy cập _relayStatus và _statusChanged
    bool _statusChanged;          // Cờ đánh dấu thay đổi trạng thái

    QueueHandle_t _relayEventQueue; // Hàng đợi để nhận sự kiện từ timer callbacks
};

#endif // RELAY_MANAGER_H 