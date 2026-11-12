#ifndef APP_TASKS_H
#define APP_TASKS_H

#include <Arduino.h>

#include "app/LedPanel.h"
#include "config/CONFIG.h"

// Индикация живёт отдельной задачей: смена карты не должна ждать разбора
// кадров телеметрии и переподключения WiFi. Задача на ядре 0 вместе с
// WiFi-стеком, loop() — на ядре 1.
inline void TaskLedCode(void *parameter) {
  (void) parameter;

  for (;;) {
    LedPanel::instance().update();
    vTaskDelay(pdMS_TO_TICKS(CONTROL_TICK_MS));
  }
}

#endif // APP_TASKS_H
