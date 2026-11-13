#ifndef NET_SETTINGS_H
#define NET_SETTINGS_H

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Настройки модуля в NVS: к какому ядру подключаться и пароль обновления.
//
// Пароль точки доступа ядра больше не зашит в прошивку DED CORE V4: он уникален
// для каждого блока, поэтому модуль хранит его у себя и получает при сопряжении
// (см. net/PairingLink.h).
class Settings {
public:
  static Settings &instance();

  bool begin();

  // false — NVS не открылась: значения живут только до перезагрузки, и после
  // рестарта сопряжённый модуль снова окажется ненастроенным.
  bool nvsReady() const { return nvsReady_; }

  bool provisioned() const;

  // Геттеры отдают копию под мьютексом: значения читает и loop(), и задача
  // ESPAsyncWebServer, а String при переаллокации освобождает буфер.
  String ssid() const;
  String password() const;
  String host() const;
  uint16_t port() const;

  // Токен доверенного модуля: выдаётся при сопряжении, по нему ядро присылает
  // новые параметры сети при смене пароля точки доступа.
  String token() const;
  void saveToken(const String &token);

  // BSSID точки сопряжения ядра. По нему автоматическое пересопряжение находит
  // только своё ядро, а не любую сеть DED_PAIR_* в радиусе.
  String bssid() const;
  void saveBssid(const String &bssid);

  // Код сопряжения из приложения блока. Сеть DED_PAIR_* закрыта WPA2-паролем,
  // выведенным из этого кода: без него модуль не подключится и не получит пароль ядра.
  String pairCode() const;
  void savePairCode(const String &code);

  void saveCore(const String &ssid, const String &password, const String &host, uint16_t port);
  void clearCore();

  uint8_t failedBoots() const { return failedBoots_; }
  void setFailedBoots(uint8_t value);

  // Строка пароля OTA отдаётся указателем: ElegantOTA хранит его у себя и
  // читает при каждом запросе, поэтому буфер не должен переаллоцироваться.
  const char *otaPassword() const { return otaPassword_; }

private:
  Settings();

  void load();

  void lock() const;
  void unlock() const;

  // Возвращает true, если значение действительно попало в NVS.
  bool storeString(const char *key, const String &value);

  static void makeOtaPassword(char *out, size_t size);

  Preferences prefs_;

  String ssid_;
  String password_;
  String host_;
  uint16_t port_ = 0;
  String token_;
  String bssid_;
  String pairCode_;

  // 8 символов + NUL: столько же, сколько генерирует makeOtaPassword().
  char otaPassword_[9] = {0};

  bool nvsReady_ = false;

  // Рекурсивный: saveCore() вызывает setFailedBoots() внутри своей блокировки.
  mutable SemaphoreHandle_t mutex_ = nullptr;

  // Кэш счётчика: NVS пишется только при изменении значения, иначе «мигающий»
  // линк изнашивал бы flash на каждом переходе в online.
  uint8_t failedBoots_ = 0;
};

#endif // NET_SETTINGS_H
