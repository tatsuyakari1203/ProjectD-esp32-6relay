#ifndef TASK_SCHEDULER_H
#define TASK_SCHEDULER_H

#include <Arduino.h>
#include <vector>
#include <algorithm>
#include <ArduinoJson.h>
#include <time.h>
#include "RelayManager.h"

// Trạng thái của lịch tưới
enum TaskState {
    IDLE,       // Chưa đến giờ chạy
    RUNNING,    // Đang chạy
    COMPLETED   // Đã hoàn thành
};

// Cấu trúc lịch tưới đơn giản
struct IrrigationTask {
    int id;                     // ID của lịch
    bool active;                // Trạng thái kích hoạt
    uint8_t days;               // Các ngày trong tuần (bit 0-6 đại diện CN đến T7)
    uint8_t hour;               // Giờ bắt đầu (0-23)
    uint8_t minute;             // Phút bắt đầu (0-59)
    uint16_t duration;          // Thời lượng tưới (phút)
    std::vector<uint8_t> zones; // Các vùng tưới (tương ứng relay 1-6)
    uint8_t priority;           // Mức ưu tiên (1-10, cao hơn = quan trọng hơn)
    
    // Thông tin trạng thái cơ bản
    TaskState state;            // Trạng thái hiện tại
    time_t start_time;          // Thời gian bắt đầu thực tế
    time_t next_run;            // Thời gian chạy kế tiếp
};

class TaskScheduler {
public:
    TaskScheduler(RelayManager& relayManager);
    
    // Phương thức cơ bản
    void begin();
    bool addOrUpdateTask(const IrrigationTask& task);
    bool deleteTask(int taskId);
    String getTasksJson(const char* apiKey);
    bool processCommand(const char* json);
    bool processDeleteCommand(const char* json);
    
    // Cập nhật hệ thống
    void update();
    
private:
    RelayManager& _relayManager;
    std::vector<IrrigationTask> _tasks;      // Danh sách lịch
    std::vector<uint8_t> _activeZones;       // Các vùng đang hoạt động
    SemaphoreHandle_t _mutex;
    unsigned long _lastCheckTime;            // Thời điểm kiểm tra gần nhất
    
    // Phương thức đơn giản
    void checkTasks();                       // Kiểm tra lịch đến giờ
    void startTask(IrrigationTask& task);    // Bắt đầu lịch tưới
    void stopTask(IrrigationTask& task);     // Dừng lịch tưới
    bool isHigherPriority(int taskId);       // Kiểm tra độ ưu tiên
    bool isZoneBusy(uint8_t zoneId);         // Kiểm tra vùng có đang chạy
    time_t calculateNextRunTime(IrrigationTask& task); // Tính giờ chạy kế tiếp
    uint8_t daysArrayToBitmap(JsonArray daysArray); // Chuyển mảng ngày sang bitmap
    JsonArray bitmapToDaysArray(JsonDocument& doc, uint8_t daysBitmap); // Chuyển bitmap sang mảng ngày
};

#endif // TASK_SCHEDULER_H 