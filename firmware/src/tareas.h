#pragma once
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// ── Colas (definidas en main.cpp) ────────────────────────────────
extern QueueHandle_t cola1;   // TareaA → TareaC  (telemetría)
extern QueueHandle_t cola2;   // TareaC → TareaB  (comandos RPC)

// ── Mutexes (definidos en main.cpp) ──────────────────────────────
extern SemaphoreHandle_t mutex1;   // protege bus ADC1
extern SemaphoreHandle_t mutex2;   // protege estado del actuador

// ── Estado compartido (protegido por mutex2) ──────────────────────
extern volatile bool  estadoActuador;
extern volatile float umbralPhMin;
extern volatile float umbralPhMax;
extern volatile float umbralTurbidez;

// ── Prototipos de las 5 tareas ────────────────────────────────────
void tareaA_Sensado        (void* param);   // Core 1 — prioridad 5
void tareaB_Actuador       (void* param);   // Core 1 — prioridad 4
void tareaC_Comunicaciones (void* param);   // Core 0 — prioridad 2
void tareaD_Watchdog       (void* param);   // Core 1 — prioridad 3
void tareaE_Log            (void* param);   // Core 1 — prioridad 1
