#ifndef APP_STATUS_H
#define APP_STATUS_H

#include <Arduino.h>

// Состояние модуля для портала настройки.
// Пишут его задача индикации и сетевой слой, читает портал.

struct RuntimeStatus {
  bool linkOnline = false;
  bool coreProvisioned = false;

  String coreSsid;
  String coreHost;

  // Номер активной карты из кадра ядра: 0…2. -1 — данных ещё не было.
  int card = -1;

  // Индекс горящего светодиода: 0…2. -1 — все погашены (нет связи или
  // самопроверка).
  int litIndex = -1;

  unsigned long frames = 0;
  unsigned long lastFrameMs = 0;
};

extern RuntimeStatus runtime;

#endif // APP_STATUS_H
