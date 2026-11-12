#ifndef CONFIG_PINS_H
#define CONFIG_PINS_H

#include <Arduino.h>

// Распиновка модуля. Пины совпадают с заводской платой DED_VISION_LED, чтобы
// новая прошивка встала на готовое железо без перепайки.

// Светодиоды карт 1…3. Номер в имени — карта на корпусе (нумерация с единицы),
// card в кадре ядра — с нуля: card 0 → PIN_LED_CART_1.
constexpr uint8_t PIN_LED_CART_1 = 18;
constexpr uint8_t PIN_LED_CART_2 = 17;
constexpr uint8_t PIN_LED_CART_3 = 16;

#endif // CONFIG_PINS_H
