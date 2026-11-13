#include "net/Settings.h"

#include <esp_random.h>

#include "config/PROPS.h"

namespace {
constexpr const char *kNamespace = "led";

// Имена ключей короче 15 символов: столько вмещает NVS. В заводской прошивке
// ключ "count_run_succes" (16 символов) молча не сохранялся, и защита от
// bootloop не работала.
constexpr const char *kKeySsid = "ssid";
constexpr const char *kKeyPass = "pass";
constexpr const char *kKeyHost = "host";
constexpr const char *kKeyPort = "port";
constexpr const char *kKeyFails = "fails";
constexpr const char *kKeyOtaPass = "otapw";
constexpr const char *kKeyToken = "mtok";
constexpr const char *kKeyBssid = "bssid";
constexpr const char *kKeyPairCode = "paircode";
}  // namespace

Settings &Settings::instance() {
  static Settings settings;
  return settings;
}

Settings::Settings() {
  mutex_ = xSemaphoreCreateRecursiveMutex();
}

void Settings::lock() const {
  if (mutex_ != nullptr) {
    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
  }
}

void Settings::unlock() const {
  if (mutex_ != nullptr) {
    xSemaphoreGiveRecursive(mutex_);
  }
}

bool Settings::begin() {
  lock();

  nvsReady_ = prefs_.begin(kNamespace, false);

  if (nvsReady_) {
    load();
  } else {
    Serial.println("SETTINGS: NVS недоступна, настройки не сохранятся");
  }

  // Пароль OTA не должен оставаться пустым даже при сбое NVS: ElegantOTA с
  // пустым паролем отключает авторизацию, а точка доступа портала открытая.
  if (otaPassword_[0] == '\0') {
    makeOtaPassword(otaPassword_, sizeof(otaPassword_));

    if (nvsReady_) {
      prefs_.putString(kKeyOtaPass, otaPassword_);
    }

    // Пароль печатаем один раз: портал открытый, а обновление прошивки —
    // единственное действие, которым можно испортить модуль.
    Serial.printf("SETTINGS: пароль обновления прошивки: %s\n", otaPassword_);
  }

  unlock();

  return nvsReady_;
}

bool Settings::storeString(const char *key, const String &value) {
  if (!nvsReady_) {
    return false;
  }

  if (prefs_.putString(key, value) > 0) {
    return true;
  }

  Serial.printf("SETTINGS: '%s' не записался в NVS\n", key);
  return false;
}

bool Settings::provisioned() const {
  lock();
  const bool result = ssid_.length() > 0 && password_.length() >= 8;
  unlock();
  return result;
}

String Settings::ssid() const {
  lock();
  const String value = ssid_;
  unlock();
  return value;
}

String Settings::password() const {
  lock();
  const String value = password_;
  unlock();
  return value;
}

String Settings::host() const {
  lock();
  const String value = host_;
  unlock();
  return value;
}

uint16_t Settings::port() const {
  lock();
  const uint16_t value = port_;
  unlock();
  return value;
}

String Settings::token() const {
  lock();
  const String value = token_;
  unlock();
  return value;
}

String Settings::bssid() const {
  lock();
  const String value = bssid_;
  unlock();
  return value;
}

String Settings::pairCode() const {
  lock();
  const String value = pairCode_;
  unlock();
  return value;
}

void Settings::saveCore(const String &ssid, const String &password, const String &host, uint16_t port) {
  lock();

  ssid_ = ssid;
  password_ = password;
  host_ = host;
  port_ = port;

  // Проверяем запись: без неё сопряжение выглядело бы успешным, а после
  // перезагрузки модуль снова оказывался бы ненастроенным.
  const bool stored = storeString(kKeySsid, ssid_) && storeString(kKeyPass, password_) &&
                      storeString(kKeyHost, host_) &&
                      (nvsReady_ && prefs_.putUShort(kKeyPort, port_) > 0);

  if (!stored) {
    Serial.println("SETTINGS: параметры ядра не сохранены в NVS");
  }

  setFailedBoots(0);

  Serial.printf("SETTINGS: ядро '%s' (%s:%u) сохранено\n",
                ssid_.c_str(), host_.c_str(), static_cast<unsigned>(port_));

  unlock();
}

void Settings::saveToken(const String &token) {
  lock();
  token_ = token;
  storeString(kKeyToken, token_);
  unlock();
}

void Settings::saveBssid(const String &bssid) {
  lock();
  bssid_ = bssid;
  storeString(kKeyBssid, bssid_);
  unlock();
}

void Settings::savePairCode(const String &code) {
  lock();
  pairCode_ = code;
  storeString(kKeyPairCode, pairCode_);
  unlock();
}

void Settings::clearCore() {
  lock();

  ssid_ = "";
  password_ = "";
  host_ = CORE_DEFAULT_HOST;
  port_ = CORE_DEFAULT_PORT;

  prefs_.remove(kKeySsid);
  prefs_.remove(kKeyPass);
  prefs_.remove(kKeyHost);
  prefs_.remove(kKeyPort);
  prefs_.remove(kKeyBssid);
  bssid_ = "";

  // Токен принадлежит прежнему ядру: с ним модуль представился бы новому блоку
  // чужим пропуском и не получил бы смену параметров сети.
  prefs_.remove(kKeyToken);
  token_ = "";

  setFailedBoots(0);

  Serial.println("SETTINGS: настройки ядра сброшены");

  unlock();
}

void Settings::setFailedBoots(uint8_t value) {
  lock();

  if (value != failedBoots_) {
    failedBoots_ = value;
    prefs_.putUChar(kKeyFails, failedBoots_);
  }

  unlock();
}

void Settings::load() {
  ssid_ = prefs_.getString(kKeySsid, "");
  password_ = prefs_.getString(kKeyPass, "");
  host_ = prefs_.getString(kKeyHost, CORE_DEFAULT_HOST);
  port_ = prefs_.getUShort(kKeyPort, CORE_DEFAULT_PORT);
  token_ = prefs_.getString(kKeyToken, "");
  bssid_ = prefs_.getString(kKeyBssid, "");
  pairCode_ = prefs_.getString(kKeyPairCode, "");
  failedBoots_ = prefs_.getUChar(kKeyFails, 0);

  const String storedOta = prefs_.getString(kKeyOtaPass, "");

  if (storedOta.length() == sizeof(otaPassword_) - 1) {
    memcpy(otaPassword_, storedOta.c_str(), sizeof(otaPassword_));
  }

  if (host_.length() == 0) {
    host_ = CORE_DEFAULT_HOST;
  }

  if (port_ == 0) {
    port_ = CORE_DEFAULT_PORT;
  }
}

void Settings::makeOtaPassword(char *out, size_t size) {
  // Без похожих символов: пароль набирают вручную с экрана или из лога.
  static const char alphabet[] = "abcdefghijkmnpqrstuvwxyz23456789";

  const size_t length = size - 1;

  for (size_t i = 0; i < length; ++i) {
    out[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
  }

  out[length] = '\0';
}
