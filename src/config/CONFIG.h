#ifndef CONFIG_CONFIG_H
#define CONFIG_CONFIG_H

#include <Arduino.h>

// --- Индикация --------------------------------------------------------------

// Сколько карт умеет показывать панель. В DED CORE V4 карт три (active_cart
// 0…2), столько же светодиодов на заводской плате.
constexpr int LED_COUNT = 3;
constexpr int CARD_MIN = 0;
constexpr int CARD_MAX = LED_COUNT - 1;

// Самопроверка при включении: все светодиоды разом, затем по одному.
constexpr uint32_t LED_SELFTEST_ALL_MS = 250;
constexpr uint32_t LED_SELFTEST_STEP_MS = 120;

// Период задачи индикации. Светодиод не требует точного таймера: 50 Гц хватает,
// чтобы смена карты была видна сразу.
constexpr uint32_t CONTROL_TICK_MS = 20;

// --- Связь с ядром ----------------------------------------------------------

// Сколько ждём кадр телеметрии, прежде чем считать линк потерянным.
constexpr uint32_t LINK_TIMEOUT_MS = 3000;
constexpr uint32_t WS_RECONNECT_MS = 1000;
constexpr uint32_t WS_PING_INTERVAL_MS = 15000;
constexpr uint32_t WS_PONG_TIMEOUT_MS = 3000;
constexpr uint8_t WS_PONG_MISSES = 2;

// --- Сопряжение -------------------------------------------------------------

constexpr uint32_t WIFI_RETRY_MS = 20000;
constexpr uint32_t PAIR_SCAN_TIMEOUT_MS = 25000;
constexpr uint32_t PAIR_SCAN_INTERVAL_MS = 20000;
constexpr uint32_t PAIR_AFTER_LINK_LOSS_MS = 60000;

// Сколько занятых сетей видим в сканировании — столько и обходим.
constexpr uint16_t WIFI_SCAN_LIMIT = 40;

// --- Портал настройки -------------------------------------------------------

// После стольких неудачных стартов портал поднимается даже при сохранённых
// настройках: иначе модуль с неверным паролем ядра недоступен по сети.
// Порог намеренно высокий: каждый запуск без связи — это ещё один старт, и
// случайные перезагрузки не должны открывать точку доступа.
constexpr uint8_t PORTAL_AFTER_FAILED_BOOTS = 10;

#endif // CONFIG_CONFIG_H
