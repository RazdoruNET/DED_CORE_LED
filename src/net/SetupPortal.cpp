#include "net/SetupPortal.h"

#include <ElegantOTA.h>
#include <WiFi.h>

#include "app/LedPanel.h"
#include "app/Status.h"
#include "config/CONFIG.h"
#include "config/PROPS.h"
#include "net/Settings.h"
#include "net/WifiLink.h"

namespace {

String formParam(AsyncWebServerRequest *request, const char *name) {
  // Второй аргумент true — параметр из тела POST-формы, а не из строки запроса.
  if (!request->hasParam(name, true)) {
    return "";
  }

  return request->getParam(name, true)->value();
}

String htmlMessage(const String &text) {
  return "<!DOCTYPE html><html lang=\"ru\"><head><meta charset=\"utf-8\">"
         "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
         "<title>DED CORE LED</title></head><body style=\"font-family:system-ui,sans-serif;"
         "background:#111;color:#eee;padding:24px\">" + text +
         "<p><a style=\"color:#6ea8ff\" href=\"/\">Назад</a></p></body></html>";
}

// SSID и адрес ядра приходят из формы и из кадра сопряжения, то есть не
// доверены: кавычка в SSID ломала бы страницу и JSON, а разметка — исполнялась.
String htmlEscape(const String &value) {
  String out;

  for (size_t i = 0; i < value.length(); ++i) {
    switch (value[i]) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += value[i]; break;
    }
  }

  return out;
}

String jsonEscape(const String &value) {
  String out;
  out.reserve(value.length() + 8);

  for (size_t i = 0; i < value.length(); ++i) {
    const unsigned char c = static_cast<unsigned char>(value[i]);

    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char escaped[7];
          snprintf(escaped, sizeof(escaped), "\\u%04x", c);
          out += escaped;
        } else {
          out += static_cast<char>(c);
        }
        break;
    }
  }

  return out;
}

String cardText(int card) {
  if (card < CARD_MIN || card > CARD_MAX) {
    return "—";
  }

  // На корпусе карты нумеруются с единицы, в кадре — с нуля.
  return String(card + 1);
}

// Код вводят вручную и копируют из приложения: пробелы и переводы строки иначе
// попали бы в WPA2-пароль сети сопряжения и код перестал бы подходить.
String normalizedPairCode(const String &raw) {
  String code;
  code.reserve(raw.length());

  for (size_t i = 0; i < raw.length(); ++i) {
    const char c = raw[i];

    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
      code += c;
    }
  }

  return code;
}

}  // namespace

SetupPortal &SetupPortal::instance() {
  static SetupPortal portal;
  return portal;
}

SetupPortal::SetupPortal() {
  statusMutex_ = xSemaphoreCreateRecursiveMutex();
}

void SetupPortal::lockStatus() const {
  if (statusMutex_ != nullptr) {
    xSemaphoreTakeRecursive(statusMutex_, portMAX_DELAY);
  }
}

void SetupPortal::unlockStatus() const {
  if (statusMutex_ != nullptr) {
    xSemaphoreGiveRecursive(statusMutex_);
  }
}

String SetupPortal::statusSnapshot() const {
  lockStatus();
  const String text = pairingStatus_;
  unlockStatus();
  return text;
}

void SetupPortal::begin() {
  if (!active_) {
    // AP+STA: портал может быть открыт, пока модуль уже подключён к ядру,
    // и терять этот линк из-за настройки нельзя.
    //
    // Wi-Fi поднимается до server_.begin(): AsyncServer сразу создаёт сокет
    // через tcpip_api_call, а без инициализированного lwIP это assert и bootloop.
    WiFi.mode(WIFI_AP_STA);

    // Своя подсеть: сеть ядра — 192.168.4.0/24, и в AP+STA совпадающие подсети
    // делают маршрут до ядра зависимым от порядка интерфейсов в lwIP.
    static const IPAddress portalIp(192, 168, 5, 1);
    WiFi.softAPConfig(portalIp, portalIp, IPAddress(255, 255, 255, 0));
    WiFi.softAP(PORTAL_AP_SSID);

    active_ = true;
    setPairingStatus("");

    linkOnlineAt_ = runtime.linkOnline ? millis() : 0;
    linkWasOnline_ = runtime.linkOnline;

    Serial.printf("SETUP: сеть '%s', страница http://%s/\n",
                  PORTAL_AP_SSID, WiFi.softAPIP().toString().c_str());
  }

  if (!started_) {
    registerRoutes();
    server_.begin();
    started_ = true;
  }
}

void SetupPortal::stop() {
  if (!active_) {
    return;
  }

  WiFi.softAPdisconnect(true);
  active_ = false;
  linkOnlineAt_ = 0;

  // Обратно в STA: точку доступа портала больше никто не использует.
  if (WiFi.getMode() == WIFI_AP_STA) {
    WiFi.mode(WIFI_STA);
  }

  Serial.println("SETUP: портал закрыт");
}

void SetupPortal::loop() {
  // Обновление прошивки перезагружает модуль само, но только из этого вызова.
  if (started_) {
    ElegantOTA.loop();
  }

  // Закрытие по /close: ответ должен уйти до остановки точки доступа.
  if (stopAt_ != 0 && static_cast<int32_t>(millis() - stopAt_) >= 0) {
    stopAt_ = 0;
    stop();
  }

  // Связь восстановилась — портал больше не нужен, но закрываем его не сразу:
  // человек может в этот момент вводить пароль ядра на странице.
  const bool online = runtime.linkOnline;

  if (online && !linkWasOnline_) {
    linkOnlineAt_ = millis();
  } else if (!online) {
    linkOnlineAt_ = 0;
  }

  linkWasOnline_ = online;

  if (active_ && linkOnlineAt_ != 0 &&
      static_cast<uint32_t>(millis() - linkOnlineAt_) >= PORTAL_CLOSE_AFTER_LINK_MS) {
    Serial.println("SETUP: связь с ядром есть — закрываю портал");
    stop();
  }

  if (restartAt_ != 0 && static_cast<int32_t>(millis() - restartAt_) >= 0) {
    Serial.println("SETUP: перезагрузка по настройкам портала");
    ESP.restart();
  }
}

bool SetupPortal::takePairingRequest() {
  if (!pairingRequested_) {
    return false;
  }

  pairingRequested_ = false;
  return true;
}

void SetupPortal::setPairingStatus(const String &text) {
  lockStatus();
  pairingStatus_ = text;
  unlockStatus();
}

bool SetupPortal::allowRequest(AsyncWebServerRequest *request) const {
  if (active_) {
    return true;
  }

  request->send(403, "text/plain; charset=utf-8", "Setup portal is closed");
  return false;
}

void SetupPortal::registerRoutes() {
  server_.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!allowRequest(request)) {
      return;
    }

    request->send(200, "text/html; charset=utf-8", buildPage());
  });

  server_.on("/status.json", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!allowRequest(request)) {
      return;
    }

    request->send(200, "application/json", buildStatusJson());
  });

  server_.on("/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!allowRequest(request)) {
      return;
    }

    const String ssid = formParam(request, "ssid");
    const String password = formParam(request, "pass");
    const String host = formParam(request, "host");
    const String portText = formParam(request, "port");

    if (ssid.length() == 0) {
      request->send(400, "text/html; charset=utf-8",
                    htmlMessage("Имя точки доступа ядра не может быть пустым."));
      return;
    }

    Settings &settings = Settings::instance();

    uint16_t port = CORE_DEFAULT_PORT;

    if (portText.length() > 0) {
      const long value = portText.toInt();

      if (value > 0 && value <= 65535) {
        port = static_cast<uint16_t>(value);
      }
    }

    // Пустое поле пароля — «не менять»: страница портала открытая и пароль
    // ядра в неё не выводится.
    const String storedPassword = password.length() > 0 ? password : settings.password();

    if (storedPassword.length() < 8) {
      request->send(400, "text/html; charset=utf-8",
                    htmlMessage("Нужен пароль точки доступа ядра (не короче 8 символов)."));
      return;
    }

    settings.saveCore(ssid,
                      storedPassword,
                      host.length() > 0 ? host : String(CORE_DEFAULT_HOST),
                      port);

    // Неподтверждённая запись в NVS: перезагрузка стёрла бы всё, что человек
    // только что ввёл, и модуль молча вернулся бы к прежним настройкам.
    if (!settings.nvsReady()) {
      request->send(500, "text/html; charset=utf-8",
                    htmlMessage("NVS недоступна: настройки не сохранятся после перезагрузки."));
      return;
    }

    request->send(200, "text/html; charset=utf-8", htmlMessage("Сохранено. Перезагрузка…"));
    restartAt_ = millis() + 1500;
  });

  server_.on("/paircode", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!allowRequest(request)) {
      return;
    }

    const String code = normalizedPairCode(formParam(request, "code"));

    if (code.length() < 8 || code.length() > 32) {
      request->send(400, "text/html; charset=utf-8",
                    htmlMessage("Код сопряжения: от 8 до 32 символов без пробелов."));
      return;
    }

    Settings::instance().savePairCode(code);
    setPairingStatus("Код сохранён. Теперь нажмите «Найти ядро сопряжением».");

    request->redirect("/");
  });

  // POST, а не GET: это действие меняет состояние модуля, а GET дёргают
  // предзагрузка ссылок и чужая страница в браузере.
  server_.on("/pair", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!allowRequest(request)) {
      return;
    }

    setPairingStatus("Ищу сеть сопряжения ядра…");
    pairingRequested_ = true;
    request->redirect("/");
  });

  server_.on("/led/test", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!allowRequest(request)) {
      return;
    }

    LedPanel::instance().requestSelfTest();
    request->redirect("/");
  });

  server_.on("/reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!allowRequest(request)) {
      return;
    }

    Settings::instance().clearCore();
    request->send(200, "text/html; charset=utf-8", htmlMessage("Настройки сброшены. Перезагрузка…"));
    restartAt_ = millis() + 1500;
  });

  server_.on("/close", HTTP_POST, [this](AsyncWebServerRequest *request) {
    if (!allowRequest(request)) {
      return;
    }

    request->send(200, "text/html; charset=utf-8", htmlMessage("Портал закрыт."));

    // Останавливаем точку доступа не в обработчике: иначе ответ обрывается
    // вместе с соединением.
    stopAt_ = millis() + 300;
  });

  // Обновление прошивки закрыто паролем из NVS: точка доступа портала открытая.
  // Маршрут /update регистрирует ElegantOTA, поэтому он отвечает и после
  // закрытия портала — из сети ядра, но только с паролем.
  ElegantOTA.begin(&server_, "admin", Settings::instance().otaPassword());
}

String SetupPortal::buildPage() const {
  Settings &settings = Settings::instance();

  const bool provisioned = settings.provisioned();

  String page;
  page.reserve(3200);

  page += "<!DOCTYPE html><html lang=\"ru\"><head><meta charset=\"utf-8\">";
  page += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  page += "<title>DED CORE LED</title><style>";
  page += "body{font-family:system-ui,sans-serif;margin:0;padding:16px;background:#111;color:#eee}";
  page += "h1{font-size:19px;margin:0 0 14px}table{border-collapse:collapse;width:100%;margin-bottom:14px}";
  page += "td{padding:5px 8px;border-bottom:1px solid #333;font-size:14px}";
  page += "td:first-child{color:#999;width:46%}";
  page += "input{width:100%;padding:9px;margin:5px 0;border:1px solid #444;border-radius:6px;";
  page += "background:#1b1b1b;color:#eee;box-sizing:border-box;font-size:14px}";
  page += "button,a.btn{display:block;width:100%;padding:10px;margin:8px 0;border:0;border-radius:6px;";
  page += "background:#2d7ef7;color:#fff;text-align:center;text-decoration:none;font-size:15px}";
  page += "button.ghost,a.btn.ghost{background:#333}.note{color:#aaa;font-size:13px;line-height:1.4}";
  page += "</style></head><body>";

  page += "<h1>DED CORE LED " FIRMWARE_VERSION "</h1><table>";

  page += "<tr><td>Связь с ядром</td><td>";
  page += (runtime.linkOnline ? String("есть") : String("нет"));
  page += "</td></tr>";

  page += "<tr><td>Ядро</td><td>";
  page += (provisioned ? htmlEscape(settings.ssid()) : String("не настроено"));
  page += "</td></tr>";

  page += "<tr><td>Адрес ядра</td><td>";
  page += htmlEscape(settings.host());
  page += ":";
  page += String(settings.port());
  page += "</td></tr>";

  page += "<tr><td>Хранилище настроек</td><td>";
  page += (settings.nvsReady() ? String("NVS") : String("NVS недоступна"));
  page += "</td></tr>";

  page += "<tr><td>Мой адрес</td><td>";
  page += (WifiLink::instance().isConnected() ? WifiLink::instance().ip() : String("—"));
  page += "</td></tr>";

  page += "<tr><td>Активная карта</td><td>";
  page += cardText(runtime.card);
  page += "</td></tr>";

  page += "<tr><td>Горит светодиод</td><td>";
  page += (runtime.litIndex < 0 ? String("—") : cardText(runtime.litIndex));
  page += "</td></tr>";

  page += "<tr><td>Кадров от ядра</td><td>";
  page += String(runtime.frames);
  page += "</td></tr>";

  page += "</table>";

  page += "<form method=\"post\" action=\"/save\">";
  page += "<input name=\"ssid\" placeholder=\"Имя точки доступа ядра\" value=\"";
  page += (provisioned ? htmlEscape(settings.ssid()) : String(""));
  page += "\">";
  page += "<input name=\"pass\" type=\"password\" autocomplete=\"new-password\" placeholder=\"";
  page += (provisioned ? "Пароль ядра (пусто — не менять)" : "Пароль точки доступа ядра");
  page += "\">";
  page += "<input name=\"host\" placeholder=\"Адрес ядра\" value=\"";
  page += htmlEscape(settings.host());
  page += "\">";
  page += "<input name=\"port\" placeholder=\"Порт WebSocket\" value=\"";
  page += String(settings.port());
  page += "\">";
  page += "<button type=\"submit\">Сохранить</button></form>";

  page += "<form method=\"post\" action=\"/paircode\">";
  page += "<input name=\"code\" type=\"password\" autocomplete=\"off\" placeholder=\"Код сопряжения из приложения блока\">";
  page += "<button type=\"submit\">Сохранить код</button></form>";

  page += "<form method=\"post\" action=\"/pair\">";
  page += "<button type=\"submit\">Найти ядро сопряжением</button></form>";
  page += "<form method=\"post\" action=\"/led/test\">";
  page += "<button class=\"ghost\" type=\"submit\">Проверить светодиоды</button></form>";
  page += "<a class=\"btn ghost\" href=\"/update\">Обновление прошивки</a>";

  page += "<form method=\"post\" action=\"/reset\" onsubmit=\"return confirm('Сбросить настройки ядра?')\">";
  page += "<button class=\"ghost\" type=\"submit\">Сбросить настройки</button></form>";

  if (pairingStatus_.length() > 0) {
    page += "<p class=\"note\">";
    page += htmlEscape(statusSnapshot());
    page += "</p>";
  }

  page += "<p class=\"note\">Сопряжение: в приложении блока откройте «Модули», нажмите "
          "«Найти модуль» и запишите код. Введите код выше, нажмите «Сохранить код», "
          "затем «Найти ядро сопряжением». Сеть сопряжения закрыта кодом и живёт около трёх минут.</p>";

  // Обновляем страницу, только пока в неё не вводят: автообновление стирало
  // набранный пароль.
  page += "<script>setTimeout(function(){var busy=false;"
          "document.querySelectorAll('input').forEach(function(el){"
          "if(el.value||el===document.activeElement)busy=true;});"
          "if(!busy)location.reload();},5000);</script>";

  page += "</body></html>";

  return page;
}

String SetupPortal::buildStatusJson() const {
  const Settings &settings = Settings::instance();

  String json = "{\"event\":\"status\",\"data\":{";
  json += "\"fw\":\"" FIRMWARE_VERSION "\",";
  json += "\"link\":" + String(runtime.linkOnline ? 1 : 0) + ",";
  json += "\"core_ssid\":\"" + jsonEscape(settings.ssid()) + "\",";
  json += "\"core_host\":\"" + jsonEscape(settings.host()) + "\",";
  json += "\"nvs\":" + String(settings.nvsReady() ? 1 : 0) + ",";
  json += "\"card\":" + String(runtime.card) + ",";
  json += "\"led\":" + String(runtime.litIndex) + ",";
  json += "\"frames\":" + String(runtime.frames);
  json += "}}";

  return json;
}
