#include "reloj.h"

#include <Arduino.h>
#include <RTClib.h>
#include <WiFi.h>
#include <sys/time.h>
#include <time.h>

#include "config.h"

namespace {

constexpr time_t HORA_MINIMA_VALIDA = 1700000000;  // nov. 2023: antes, el reloj no está en hora
constexpr uint64_t PRIMERA_COPIA_RTC_MS = 15 * 60 * 1000ULL;  // margen para que el NTP sincronice
constexpr uint64_t COPIA_RTC_CADA_MS = 6 * 3600 * 1000ULL;
constexpr uint64_t MS_POR_DIA = 24 * 3600 * 1000ULL;

RTC_DS3231 rtc;
bool rtcOk = false;
bool rtcCopiado = false;
uint64_t ultimaCopiaRtc = 0;

}  // namespace

void iniciarReloj() {
  setenv("TZ", ZONA_HORARIA, 1);
  tzset();
  if (USAR_RTC_DS3231) {
    rtcOk = rtc.begin();
    if (rtcOk && !rtc.lostPower()) {
      timeval tv = {static_cast<time_t>(rtc.now().unixtime()), 0};
      settimeofday(&tv, nullptr);
    }
  }
  // Si hay WiFi, el NTP corrige la hora en cuanto conecta (configTzTime en iniciarWifi).
}

bool horaValida() { return time(nullptr) >= HORA_MINIMA_VALIDA; }

void rellenarHora(cultivo::Lecturas& l, uint64_t ms) {
  l.ms = ms;
  const time_t ahora = time(nullptr);
  l.horaValida = ahora >= HORA_MINIMA_VALIDA;
  if (l.horaValida) {
    tm local;
    localtime_r(&ahora, &local);
    l.minutoDelDia = static_cast<int16_t>(local.tm_hour * 60 + local.tm_min);
    l.dia = local.tm_year * 400 + local.tm_yday;  // distinto cada día, también al cambiar de año
  } else {
    l.minutoDelDia = 0;
    l.dia = static_cast<int32_t>(ms / MS_POR_DIA);  // sin hora, los límites diarios cuentan desde el arranque
  }
}

void formatearHora(char* destino, size_t largo, uint64_t ms) {
  const time_t ahora = time(nullptr);
  if (ahora < HORA_MINIMA_VALIDA) {
    snprintf(destino, largo, "+%lus", static_cast<unsigned long>(ms / 1000));
    return;
  }
  tm local;
  localtime_r(&ahora, &local);
  strftime(destino, largo, "%Y-%m-%d %H:%M:%S", &local);
}

void sincronizarRtc(uint64_t ms) {
  if (!rtcOk || WiFi.status() != WL_CONNECTED || !horaValida() || ms < PRIMERA_COPIA_RTC_MS) return;
  if (rtcCopiado && ms - ultimaCopiaRtc < COPIA_RTC_CADA_MS) return;
  rtc.adjust(DateTime(static_cast<uint32_t>(time(nullptr))));
  rtcCopiado = true;
  ultimaCopiaRtc = ms;
}
