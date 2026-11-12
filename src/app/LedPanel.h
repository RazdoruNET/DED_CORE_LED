#ifndef APP_LED_PANEL_H
#define APP_LED_PANEL_H

#include <Arduino.h>

// Панель из трёх светодиодов: горит ровно один — номер активной карты.
//
// Пины пишет только update() в задаче индикации: остальные методы меняют
// состояние и просят перерисовать панель. Так кадр телеметрии из loop() и
// кнопка в портале не могут одновременно переключить выходы.
class LedPanel {
public:
  static LedPanel &instance();

  void begin();

  // Периодическая работа: самопроверка и вывод текущего состояния на пины.
  void update();

  // Связь с ядром. Пропала — все светодиоды гаснут: горящий светодиод при
  // молчащем ядре показывал бы карту, которую уже никто не подтверждает.
  void setLinkOnline(bool online);

  // Активная карта из кадра телеметрии. Значение вне 0…2 гасит панель.
  void setCard(int card);

  // Самопроверка: все светодиоды, затем по одному. Запускается при включении
  // и из портала настройки. Пины переключит update() в задаче индикации.
  void requestSelfTest();

  int litIndex() const { return lit_; }

private:
  LedPanel() = default;

  void startSelfTest();
  void applyPins();
  void lightOnly(int index);
  void allOff();

  bool online_ = false;
  int card_ = -1;
  int lit_ = -1;

  volatile bool selfTestRequested_ = false;

  bool animating_ = false;
  uint8_t animStep_ = 0;
  unsigned long animAt_ = 0;
};

#endif // APP_LED_PANEL_H
