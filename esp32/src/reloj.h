// Hora local: NTP por WiFi, zona horaria y, opcionalmente, un RTC DS3231 con pila
// para no perder el fotoperiodo si se va internet.
#pragma once

#include <cultivo.h>

#include <cstddef>
#include <cstdint>

void iniciarReloj();
bool horaValida();
// Rellena ms, dia, minutoDelDia y horaValida de `l`.
void rellenarHora(cultivo::Lecturas& l, uint64_t ms);
// "2026-10-03 14:05:00", o el tiempo desde el arranque si aún no hay hora.
void formatearHora(char* destino, size_t largo, uint64_t ms);
// Copia de vez en cuando la hora del NTP al RTC (si está instalado).
void sincronizarRtc(uint64_t ms);
