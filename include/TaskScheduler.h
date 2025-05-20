#ifndef TASK_SCHEDULER_H
#define TASK_SCHEDULER_H

#include <Arduino.h>
#include <vector>
#include <algorithm>
#include <ArduinoJson.h>
#include <time.h>
#include <bitset>
#include "RelayManager.h"
#include "EnvironmentManager.h"

// Trạng thái của lịch tưới
enum TaskState {
    IDLE,       // Chưa đến giờ chạy
    RUNNING,    // Đang chạy
    COMPLETED,  // Đã hoàn thành
    PAUSED,     // Was running, but preempted by a higher priority task
    WAITING     // Scheduled time met, but couldn't run (conflict, conditions), will retry
};

// Cấu trúc điều kiện cảm biến
struct SensorCondition {
    bool enabled;                // Có kích hoạt điều kiện này không
    
    // Điều kiện nhiệt độ
    bool temperature_check;      // Có kiểm tra nhiệt độ không
    float min_temperature;       // Ngưỡng nhiệt độ tối thiểu (°C)
    float max_temperature;       // Ngưỡng nhiệt độ tối đa (°C)
    
    // Điều kiện độ ẩm không khí
    bool humidity_check;         // Có kiểm tra độ ẩm không khí không
    float min_humidity;          // Ngưỡng độ ẩm tối thiểu (%)
    float max_humidity;          // Ngưỡng độ ẩm tối đa (%)
    
    // Điều kiện độ ẩm đất
    bool soil_moisture_check;    // Có kiểm tra độ ẩm đất không
    float min_soil_moisture;     // Ngưỡng độ ẩm đất tối thiểu (%)
    
    // Điều kiện mưa
    bool rain_check;             // Có kiểm tra mưa không
    bool skip_when_raining;      // Bỏ qua nếu đang mưa
    
    // Điều kiện ánh sáng
    bool light_check;            // Có kiểm tra ánh sáng không
    int min_light;               // Ngưỡng ánh sáng tối thiểu (lux)
    int max_light;               // Ngưỡng ánh sáng tối đa (lux)
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
    time_t start_time;          // Thời gian bắt đầu thực tế (cho lần chạy hoặc resume gần nhất)
    time_t next_run;            // Thời gian chạy kế tiếp
    
    // Điều kiện cảm biến
    SensorCondition sensor_condition;

    // Fields for preemption and pause/resume
    bool preemptable;                       // Có thể bị tạm dừng bởi task có độ ưu tiên cao hơn không?
    unsigned long remaining_duration_on_pause_ms; // Thời gian còn lại (ms) nếu bị tạm dừng
    bool is_resuming_from_pause;            // Đánh dấu nếu lần chạy hiện tại là resume

    // Constructor to initialize new fields
    IrrigationTask() : state(IDLE), start_time(0), next_run(0),
                       preemptable(true), remaining_duration_on_pause_ms(0), is_resuming_from_pause(false) {}
};

class TaskScheduler {
public:
    TaskScheduler(RelayManager& relayManager, EnvironmentManager& envManager);
    
    // Phương thức cơ bản
    void begin();
    bool addOrUpdateTask(const IrrigationTask& task);
    bool deleteTask(int taskId);
    String getTasksJson(const char* apiKey);
    bool processCommand(const char* json);
    bool processDeleteCommand(const char* json);
    
    // Cập nhật hệ thống
    void update();
    
    // Lấy thời điểm sớm nhất cần kiểm tra lịch
    time_t getEarliestNextCheckTime() const;
    
    // Kiểm tra xem lịch trình có thay đổi không và reset cờ
    bool hasScheduleStatusChangedAndReset();
    
private:
    RelayManager& _relayManager;
    EnvironmentManager& _envManager;
    std::vector<IrrigationTask> _tasks;      // Danh sách lịch
    std::bitset<6> _activeZonesBits;         // Các vùng đang hoạt động (bit 0-5 đại diện zone 1-6)
    SemaphoreHandle_t _mutex;
    unsigned long _lastCheckTime;            // Thời điểm kiểm tra gần nhất
    time_t _earliestNextCheckTime;           // Thời điểm sớm nhất cần kiểm tra lại lịch
    bool _scheduleStatusChanged;             // Cờ đánh dấu thay đổi lịch trình
    
    // Helper methods for starting, stopping, pausing, resuming tasks
    void startTask(IrrigationTask& task);
    void stopTask(IrrigationTask& task); // Called when task completes or is forcefully stopped
    void pauseTask(IrrigationTask& task);
    void resumeTask(IrrigationTask& task);
    IrrigationTask* getTaskUsingZone(uint8_t zoneId, int excludeTaskId = -1); // Finds a RUNNING or PAUSED task in a zone
    
    // Phương thức đơn giản
    void checkTasks();                       // Kiểm tra lịch đến giờ
    bool isHigherPriority(int checkingTaskId, uint8_t conflictingZoneId);       // Kiểm tra độ ưu tiên
    bool isZoneBusy(uint8_t zoneId);         // Kiểm tra vùng có đang chạy
    time_t calculateNextRunTime(IrrigationTask& task, bool isRescheduleAfterSkip = false); // Tính giờ chạy kế tiếp
    bool checkSensorConditions(const IrrigationTask& task); // Kiểm tra điều kiện cảm biến
    uint8_t daysArrayToBitmap(JsonArray daysArray); // Chuyển mảng ngày sang bitmap
    JsonArray bitmapToDaysArray(JsonDocument& doc, uint8_t daysBitmap); // Chuyển bitmap sang mảng ngày
    void recomputeEarliestNextCheckTime();    // Tính toán lại thời điểm sớm nhất cần kiểm tra
    
    // Xử lý JSON
    void parseSensorCondition(JsonObject& jsonCondition, SensorCondition& condition);
    void addSensorConditionToJson(JsonDocument& doc, JsonObject& taskObj, const SensorCondition& condition);
};

#endif // TASK_SCHEDULER_H 