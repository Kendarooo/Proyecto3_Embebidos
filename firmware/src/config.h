#pragma once
#include <Arduino.h>

// ── Pines ADC1 (solo GPIO 32-39, ADC2 incompatible con WiFi) ─────
#define PIN_PH          34
#define PIN_TURBIDEZ    35
#define PIN_CONDUCTIV   32

// ── Actuadores ────────────────────────────────────────────────────
#define PIN_LED_VERDE   26   // Válvula abierta (sistema normal)
#define PIN_LED_ROJO    27   // Válvula cerrada (alarma crítica)
#define PIN_BUZZER      25   // Señal sonora en transición

// ── LED de estado de conectividad (TareaE) ────────────────────────
#define PIN_LED_STATUS   2   // LED integrado del ESP32

// ── Credenciales WiFi ─────────────────────────────────────────────
#define WIFI_SSID       "estudiantes.ie"
#define WIFI_PASS       "Estudiantes2024"

// ── ThingsBoard ───────────────────────────────────────────────────
#define TB_SERVER       "thingsboard.cloud"
#define TB_PORT         1883
#define TB_TOKEN        "rnddtwjj7eaqej4p6t7u"  // Token de acceso del dispositivo en ThingsBoard

// ── Periodos de tareas (ms) ───────────────────────────────────────
#define PERIODO_SENSADO_MS    500
#define PERIODO_WATCHDOG_MS   2000
#define PERIODO_LED_MS        200

// ── Umbrales por defecto (modificables por RPC) ───────────────────
#define UMBRAL_PH_MIN_DEF     6.5f
#define UMBRAL_PH_MAX_DEF     8.5f
#define UMBRAL_TURBIDEZ_DEF   4.0f

// ── Tamaños de colas ──────────────────────────────────────────────
#define COLA1_TAMANO    10   // TareaA → TareaC (5 segundos de buffer)
#define COLA2_TAMANO     5   // TareaC → TareaB (comandos RPC)

// ── Estructura de telemetría (viaja por Cola 1) ───────────────────
struct TelemetriaData {
    float    ph;
    float    turbidez;
    float    conductividad;
    bool     actuador;    // Estado de la válvula: false=abierta, true=cerrada
    uint32_t uptime_ms;  // Tiempo desde boot en milisegundos
};

// ── Estructura de comando RPC (viaja por Cola 2) ──────────────────
struct ComandoRPC {
    char  metodo[32];
    float valor;
    char  requestId[8];
};
