#include <Arduino.h>
#include <WiFi.h>

#include "app/LedPanel.h"
#include "app/Status.h"
#include "app/Tasks.h"
#include "config/CONFIG.h"
#include "config/PROPS.h"
#include "net/CoreLink.h"
#include "net/PairingLink.h"
#include "net/Settings.h"
#include "net/SetupPortal.h"
#include "net/WifiLink.h"

RuntimeStatus runtime;

namespace {

unsigned long g_pairAttemptAt = 0;
bool g_pairAttemptDone = false;
unsigned long g_wifiDownSince = 0;

// explicitRequest — запуск из портала: ищем любое ядро. Автоматически после
// потери связи ищем только то ядро, с которым уже сопряжены (по BSSID), иначе
// чужая DED_PAIR_* увела бы модуль на поддельный блок.
void runPairing(bool explicitRequest) {
  CoreCredentials credentials;
  Settings &settings = Settings::instance();

  const String expectedBssid = explicitRequest ? String() : settings.bssid();
  const String pairCode = settings.pairCode();

  if (pairCode.length() < 8) {
    SetupPortal::instance().setPairingStatus(
        "Нужен код сопряжения: откройте «Модули» в приложении блока, нажмите "
        "«Найти модуль» и введите показанный код в портале модуля.");
    return;
  }

  SetupPortal::instance().setPairingStatus("Ищу сеть сопряжения ядра (DED_PAIR_*)…");

  if (!PairingLink::instance().pair(credentials, pairCode, expectedBssid)) {
    SetupPortal::instance().setPairingStatus(
        "Сеть сопряжения не найдена или код не подошёл. Откройте в приложении "
        "«Модули» → «Найти модуль» и повторите.");
    return;
  }

  settings.saveCore(credentials.ssid,
                    credentials.password,
                    credentials.host,
                    credentials.port);
  settings.saveToken(credentials.token);
  settings.saveBssid(credentials.bssid);

  // Без NVS перезагрузка стёрла бы только что полученные параметры, и модуль
  // бесконечно ходил бы по кругу «сопрягся — перезагрузился — снова не настроен».
  if (!settings.nvsReady()) {
    SetupPortal::instance().setPairingStatus(
        "Ядро найдено, но NVS недоступна: параметры не сохранятся. Модуль "
        "остался в портале настройки.");
    return;
  }

  SetupPortal::instance().setPairingStatus("Ядро найдено: " + credentials.ssid + ". Перезагрузка…");

  delay(1500);
  ESP.restart();
}

// Сопряжение ищется только когда линка нет: сканирование сетей занимает
// секунды и на время связи с ядром не нужно.
void schedulePairing() {
  Settings &settings = Settings::instance();
  const unsigned long now = millis();

  if (WifiLink::instance().isConnected()) {
    g_wifiDownSince = now;
    return;
  }

  if (g_pairAttemptDone &&
      static_cast<uint32_t>(now - g_pairAttemptAt) < PAIR_SCAN_INTERVAL_MS) {
    return;
  }

  const bool unprovisioned = !settings.provisioned();
  const bool linkLostLongEnough = settings.provisioned() &&
                                  static_cast<uint32_t>(now - g_wifiDownSince) >= PAIR_AFTER_LINK_LOSS_MS;

  if (!unprovisioned && !linkLostLongEnough) {
    return;
  }

  // Настроенный модуль без сохранённого BSSID не знает, какое ядро искать:
  // автоматическое сопряжение выключено, новое ядро выбирают из портала.
  if (!unprovisioned && settings.bssid().length() == 0) {
    return;
  }

  g_pairAttemptDone = true;
  g_pairAttemptAt = now;
  runPairing(false);
}

// Ядро сменило имя или пароль точки доступа и прислало новые параметры до
// перезапуска сети: сохраняем и переподключаемся, не теряя связку.
void applyCoreCredentials() {
  String ssid;
  String password;
  String host;
  uint16_t port = 0;

  if (!CoreLink::instance().takeCredentials(ssid, password, host, port)) {
    return;
  }

  Settings::instance().saveCore(ssid, password, host, port);

  WifiLink::instance().applyCredentials(ssid, password);

  // В кадре мог прийти и новый адрес. Сокет, открытый на прежний, сам не
  // переедет: следующий loop() поднимет его по сохранённым настройкам.
  CoreLink::instance().rebind();
}

void updateLinkState() {
  const bool online = CoreLink::instance().frameFresh(LINK_TIMEOUT_MS);

  if (online == runtime.linkOnline) {
    return;
  }

  runtime.linkOnline = online;
  LedPanel::instance().setLinkOnline(online);

  if (online) {
    Serial.println("CORE: телеметрия пошла");
    Settings::instance().setFailedBoots(0);

    // Портал закроется сам через PORTAL_CLOSE_AFTER_LINK_MS: страницу могли
    // открыть для настройки, и обрывать её сразу после линка нельзя.
  } else {
    Serial.println("CORE: кадров нет, светодиоды погашены");
  }
}

void applyTelemetry() {
  if (!CoreLink::instance().frameFresh(LINK_TIMEOUT_MS)) {
    return;
  }

  LedPanel::instance().setCard(CoreLink::instance().telemetry().card);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);

  // До первого WiFi.mode(): режим хранения WiFi-конфига фиксируется при
  // инициализации стека. Портал поднимает AP раньше WifiLink, и без этого
  // параметры сопряжения писались бы в NVS.
  WiFi.persistent(false);

  // Заводская прошивка опускала частоту до 160 МГц: модуль стоит рядом с
  // двигателем, лишний нагрев там ни к чему.
  setCpuFrequencyMhz(160);

  Serial.printf("\nDED CORE LED %s\n", FIRMWARE_VERSION);

  // Самопроверка панели запускается здесь и дальше идёт в задаче: задержки в
  // setup() отложили бы подключение к ядру.
  LedPanel::instance().begin();

  xTaskCreatePinnedToCore(TaskLedCode, "LedPanel", 2048, nullptr, 2, nullptr, 0);

  Settings &settings = Settings::instance();
  settings.begin();

  // Счётчик неудачных стартов: модуль с неверным паролем ядра иначе остался бы
  // без сети и без портала настройки.
  const uint8_t failedBoots = settings.failedBoots();
  settings.setFailedBoots(static_cast<uint8_t>(failedBoots + 1));

  if (!settings.provisioned() || failedBoots >= PORTAL_AFTER_FAILED_BOOTS) {
    Serial.println("SETUP: открыт портал настройки");
    SetupPortal::instance().begin();
  }

  if (settings.provisioned()) {
    WifiLink::instance().begin(settings.ssid(), settings.password());
  }

  g_wifiDownSince = millis();
}

void loop() {
  WifiLink::instance().loop();
  SetupPortal::instance().loop();

  if (SetupPortal::instance().takePairingRequest()) {
    runPairing(true);
  }

  CoreLink &core = CoreLink::instance();

  if (WifiLink::instance().isConnected() && !core.started()) {
    core.begin(Settings::instance().host(), Settings::instance().port());
  }

  core.loop();

  applyCoreCredentials();

  updateLinkState();
  applyTelemetry();
  schedulePairing();
}
