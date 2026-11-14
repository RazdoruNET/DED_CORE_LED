#ifndef NET_SETUP_PORTAL_H
#define NET_SETUP_PORTAL_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Портал настройки модуля.
//
// Поднимается, когда модуль ещё не знает ядро или потерял его: точка доступа
// DED_CORE_LED, страница с состоянием, ручной ввод параметров ядра, запуск
// сопряжения, проверка светодиодов и обновление прошивки. В рабочем режиме
// портала нет.
class SetupPortal {
public:
  static SetupPortal &instance();

  void begin();
  void loop();
  void stop();

  bool active() const { return active_; }

  bool takePairingRequest();
  void setPairingStatus(const String &text);

private:
  SetupPortal();

  void registerRoutes();
  String buildPage() const;
  String buildStatusJson() const;

  void lockStatus() const;
  void unlockStatus() const;
  String statusSnapshot() const;

  // Маршруты настроек не отвечают, когда портал закрыт: сервер остаётся
  // слушать и после остановки точки доступа. Исключение — /update: его
  // регистрирует ElegantOTA, он защищён паролем и доступен из сети ядра.
  bool allowRequest(AsyncWebServerRequest *request) const;

  AsyncWebServer server_{80};

  bool started_ = false;
  bool active_ = false;

  bool pairingRequested_ = false;

  // Статус пишет loop(), а читает задача ESPAsyncWebServer.
  String pairingStatus_;
  mutable SemaphoreHandle_t statusMutex_ = nullptr;

  unsigned long restartAt_ = 0;
  unsigned long stopAt_ = 0;

  // Момент, когда связь с ядром появилась: от него отсчитывается
  // PORTAL_CLOSE_AFTER_LINK_MS.
  unsigned long linkOnlineAt_ = 0;
  bool linkWasOnline_ = false;
};

#endif // NET_SETUP_PORTAL_H
