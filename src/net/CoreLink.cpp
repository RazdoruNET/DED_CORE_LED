#include "net/CoreLink.h"

#include <ArduinoJson.h>
#include <WiFi.h>

#include "app/Status.h"
#include "config/CONFIG.h"
#include "config/PROPS.h"
#include "net/Settings.h"

namespace {

CoreLink *g_coreLink = nullptr;

// Кадр ядра вместе с картой занимает около 1,5 КБ текста: 22 точки по пять
// чисел плюс поля телеметрии. Буферы статические, чтобы разбор кадра не жил
// на стеке loop()-задачи.
char g_frameBuffer[2560];
StaticJsonBuffer<6144> g_jsonBuffer;

}  // namespace

CoreLink &CoreLink::instance() {
  static CoreLink link;
  return link;
}

void CoreLink::begin(const String &host, uint16_t port) {
  host_ = host;
  port_ = port;
  started_ = true;
  g_coreLink = this;

  socket_.begin(host.c_str(), port, "/");
  socket_.onEvent(&CoreLink::onEvent);
  socket_.setReconnectInterval(WS_RECONNECT_MS);

  // Без heartbeat разрыв связи по радио остаётся незамеченным: TCP-соединение
  // висит открытым, кадры просто перестают приходить, и светодиод продолжает
  // показывать устаревшую карту.
  socket_.enableHeartbeat(WS_PING_INTERVAL_MS, WS_PONG_TIMEOUT_MS, WS_PONG_MISSES);

  Serial.printf("CORE: WebSocket %s:%u\n", host_.c_str(), static_cast<unsigned>(port_));
}

void CoreLink::loop() {
  if (!started_) {
    return;
  }

  socket_.loop();
}

void CoreLink::rebind() {
  if (!started_) {
    return;
  }

  socket_.disconnect();
  started_ = false;

  // Кадры прежнего соединения больше не описывают текущую сеть: иначе панель
  // ещё LINK_TIMEOUT_MS показывала бы карту от старого ядра.
  telemetry_.valid = false;
}

bool CoreLink::frameFresh(uint32_t timeoutMs) const {
  return telemetry_.valid && static_cast<uint32_t>(millis() - telemetry_.lastFrameMs) <= timeoutMs;
}

void CoreLink::sendHello() {
  String frame = "{\"event\":\"module_hello\",\"data\":{\"mac\":\"";
  frame += WiFi.macAddress();
  frame += "\",\"fw\":\"";
  frame += FIRMWARE_VERSION;
  frame += "\",\"token\":\"";
  frame += Settings::instance().token();
  frame += "\"}}";

  sendText(frame);
}

bool CoreLink::takeCredentials(String &ssid, String &password, String &host, uint16_t &port) {
  if (!hasPendingCredentials_) {
    return false;
  }

  hasPendingCredentials_ = false;

  ssid = pendingSsid_;
  password = pendingPassword_;
  host = pendingHost_;
  port = pendingPort_;

  return true;
}

void CoreLink::sendText(String &frame) {
  socket_.sendTXT(frame);

  Serial.printf("CORE > %s\n", frame.c_str());
}

void CoreLink::onEvent(WStype_t type, uint8_t *payload, size_t length) {
  if (g_coreLink != nullptr) {
    g_coreLink->handleEvent(type, payload, length);
  }
}

void CoreLink::handleEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      connected_ = true;

      Serial.println("CORE: WebSocket подключён");

      // Токен подтверждаем сразу: без него ядро не пришлёт смену сети.
      sendHello();
      break;

    case WStype_DISCONNECTED:
      if (connected_) {
        Serial.println("CORE: WebSocket отключён");
      }
      connected_ = false;
      break;

    case WStype_TEXT:
      handleFrame(reinterpret_cast<const char *>(payload), length);
      break;

    default:
      break;
  }
}

void CoreLink::handleFrame(const char *payload, size_t length) {
  if (payload == nullptr || length == 0) {
    return;
  }

  // Кадр, который не влез в буфер, иначе терялся бы молча: панель гасла бы без
  // единой строки в логе, и причину пришлось бы искать наугад.
  if (length >= sizeof(g_frameBuffer)) {
    Serial.printf("CORE: кадр %u байт не влез в %u — увеличьте g_frameBuffer\n",
                  static_cast<unsigned>(length),
                  static_cast<unsigned>(sizeof(g_frameBuffer)));
    return;
  }

  // payload не завершён нулём: длина приходит отдельно, и без неё разбор читал
  // бы память за концом кадра.
  memcpy(g_frameBuffer, payload, length);
  g_frameBuffer[length] = '\0';

  g_jsonBuffer.clear();
  JsonObject &root = g_jsonBuffer.parseObject(g_frameBuffer);

  if (!root.success()) {
    Serial.printf("CORE: кадр %u байт не разобран — проверьте g_jsonBuffer\n",
                  static_cast<unsigned>(length));
    return;
  }

  const char *event = root["event"];

  if (event == nullptr) {
    return;
  }

  JsonObject &data = root["data"];

  if (strcmp(event, "module_hello") == 0) {
    const bool trusted = (data["trusted"] | 0) != 0;

    Serial.println(trusted ? "CORE: ядро подтвердило модуль"
                           : "CORE: ядро не знает токен модуля");

    return;
  }

  // Ядро меняет имя или пароль точки доступа и присылает новые параметры до
  // перезапуска сети: связка не теряется, пересопряжение не нужно.
  if (strcmp(event, "module_credentials") == 0) {
    const char *ssid = data["ssid"];
    const char *password = data["pass"];
    const char *host = data["host"];

    // Пустые поля — это недописанное ядро, а не новые настройки: сохранив их,
    // модуль потерял бы связь до пересопряжения.
    if (ssid == nullptr || ssid[0] == '\0' || password == nullptr || password[0] == '\0') {
      Serial.println("CORE: пустые параметры сети — пропущено");
      return;
    }

    pendingSsid_ = ssid;
    pendingPassword_ = password;
    // Пустой host в кадре означает «адрес не менялся».
    pendingHost_ = (host != nullptr && host[0] != '\0') ? String(host) : host_;
    pendingPort_ = static_cast<uint16_t>(data["port"] | CORE_DEFAULT_PORT);
    hasPendingCredentials_ = true;

    Serial.println("CORE: ядро прислало новые параметры сети");

    return;
  }

  if (strcmp(event, "message") != 0 || !data.success()) {
    return;
  }

  telemetry_.card = data["card"] | -1;

  ++telemetry_.frames;
  telemetry_.lastFrameMs = millis();
  telemetry_.valid = true;

  runtime.frames = telemetry_.frames;
  runtime.lastFrameMs = telemetry_.lastFrameMs;
}
