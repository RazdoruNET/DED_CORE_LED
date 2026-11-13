#include "app/LedPanel.h"

#include "app/Status.h"
#include "config/CONFIG.h"
#include "config/PINS.h"

namespace {

constexpr uint8_t kLedPins[LED_COUNT] = {
    PIN_LED_CART_1,
    PIN_LED_CART_2,
    PIN_LED_CART_3,
};

}  // namespace

LedPanel &LedPanel::instance() {
  static LedPanel panel;
  return panel;
}

void LedPanel::begin() {
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    pinMode(kLedPins[i], OUTPUT);
    digitalWrite(kLedPins[i], LOW);
  }

  lit_ = -1;
  runtime.litIndex = lit_;

  requestSelfTest();
}

void LedPanel::setLinkOnline(bool online) {
  online_.store(online);

  if (!online) {
    // Связь потеряна: самопроверка больше не актуальна, а последняя карта
    // неизвестно актуальна ли. Состояние анимации меняет только update().
    cancelAnim_.store(true);
  }
}

void LedPanel::setCard(int card) {
  card_.store(card);
  runtime.card = card;
}

void LedPanel::requestSelfTest() {
  selfTestRequested_.store(true);
}

void LedPanel::startSelfTest() {
  animating_ = true;
  animStep_ = 0;
  animAt_ = millis();

  // Первый шаг — все три разом: сразу видно, что панель жива.
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    digitalWrite(kLedPins[i], HIGH);
  }

  lit_ = -1;
  runtime.litIndex = lit_;
}

void LedPanel::update() {
  // exchange, а не load+store: запрос самопроверки из портала не должен
  // потеряться между проверкой и сбросом флага.
  if (selfTestRequested_.exchange(false)) {
    startSelfTest();
  }

  if (cancelAnim_.exchange(false)) {
    animating_ = false;
    animStep_ = 0;
    selfTestRequested_.store(false);
  }

  if (!animating_) {
    applyPins();
    return;
  }

  const uint32_t elapsed = static_cast<uint32_t>(millis() - animAt_);
  const uint32_t stepMs = animStep_ == 0 ? LED_SELFTEST_ALL_MS : LED_SELFTEST_STEP_MS;

  if (elapsed < stepMs) {
    return;
  }

  animAt_ = millis();
  ++animStep_;

  // Шаг 0 — все светодиоды, шаги 1…LED_COUNT — по одному. Дальше обычная
  // работа: горит светодиод активной карты, если связь есть.
  if (animStep_ > LED_COUNT) {
    animating_ = false;
    animStep_ = 0;
    applyPins();
    return;
  }

  lightOnly(animStep_ - 1);
}

void LedPanel::applyPins() {
  if (!online_.load()) {
    allOff();
    return;
  }

  const int card = card_.load();

  if (card < CARD_MIN || card > CARD_MAX) {
    allOff();
    return;
  }

  lightOnly(card);
}

void LedPanel::lightOnly(int index) {
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    digitalWrite(kLedPins[i], i == index ? HIGH : LOW);
  }

  lit_ = index;
  runtime.litIndex = lit_;
}

void LedPanel::allOff() {
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    digitalWrite(kLedPins[i], LOW);
  }

  lit_ = -1;
  runtime.litIndex = lit_;
}
