#ifndef NET_CORE_LINK_H
#define NET_CORE_LINK_H

#include <Arduino.h>
#include <WebSocketsClient.h>

// Кадр телеметрии ядра в той части, которая нужна индикации.
struct CoreTelemetry {
  bool valid = false;

  // Активная карта, 0…2. -1 — поле в кадре не пришло.
  int card = -1;

  unsigned long frames = 0;
  unsigned long lastFrameMs = 0;
};

// WebSocket-клиент к ядру.
//
// Ядро рассылает всем клиентам кадр {"event":"message","data":{…}} и принимает
// команды. Модулю достаточно читать card и подтверждать свой токен; остальные
// события (ping/pong, углы, WiFi-состояние) его не касаются.
class CoreLink {
public:
  static CoreLink &instance();

  void begin(const String &host, uint16_t port);
  void loop();

  // Адрес или порт ядра изменились: сокет закрывается, main.cpp поднимет его
  // заново по сохранённым настройкам на следующем шаге loop().
  void rebind();

  bool started() const { return started_; }
  bool connected();
  const CoreTelemetry &telemetry() const { return telemetry_; }

  // Новые параметры сети, присланные ядром. true — ровно один раз на кадр.
  bool takeCredentials(String &ssid, String &password, String &host, uint16_t &port);

  // true, если последний кадр пришёл не позже timeoutMs назад.
  bool frameFresh(uint32_t timeoutMs) const;

private:
  CoreLink() = default;

  static void onEvent(WStype_t type, uint8_t *payload, size_t length);
  void handleEvent(WStype_t type, uint8_t *payload, size_t length);
  void handleFrame(const char *payload, size_t length);
  void sendHello();
  void sendText(String &frame);

  WebSocketsClient socket_;
  CoreTelemetry telemetry_;

  String host_;
  uint16_t port_ = 0;

  bool started_ = false;
  bool connected_ = false;

  bool hasPendingCredentials_ = false;
  String pendingSsid_;
  String pendingPassword_;
  String pendingHost_;
  uint16_t pendingPort_ = 0;
};

#endif // NET_CORE_LINK_H
