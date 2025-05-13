#ifndef RELAY_MANAGER_H
#define RELAY_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>

// Struct để lưu trạng thái relay
struct RelayStatus {
    bool state;                  // Trạng thái hiện tại (true = bật, false = tắt)
    unsigned long endTime;       // Thời điểm kết thúc (0 = không có thời gian)
};

class RelayManager {
public:
    RelayManager();
    
    // Khởi tạo relays
    void begin(const int* relayPins, int numRelays);
    
    // Điều khiển relay
    void setRelay(int relayIndex, bool state, unsigned long duration = 0);
    
    // Bật relay
    void turnOn(int relayIndex, unsigned long duration = 0);
    
    // Tắt relay
    void turnOff(int relayIndex);
    
    // Lấy trạng thái relay
    bool getState(int relayIndex);
    
    // Lấy thời gian còn lại
    unsigned long getRemainingTime(int relayIndex);
    
    // Xử lý các relay có timer
    void update();
    
    // Tạo payload JSON cho trạng thái relay
    String getStatusJson(const char* apiKey);
    
    // Xử lý JSON lệnh điều khiển
    bool processCommand(const char* json);
    
private:
    const int* _relayPins;        // Con trỏ đến mảng chân GPIO relay
    int _numRelays;               // Số lượng relay
    RelayStatus* _relayStatus;    // Mảng trạng thái relay
    SemaphoreHandle_t _mutex;     // Mutex để bảo vệ truy cập
};

#endif // RELAY_MANAGER_H 