#pragma once
#include <Arduino.h>

// ── Multiplexor analógico para sensores ──────────────────────────
// Salida común del MUX hacia ADC1. Mantener en GPIO 32-39 para evitar ADC2 con WiFi.
#define PIN_MUX_ADC     35

// Líneas digitales de selección del MUX. Código en formato S1S0:
// 00 = turbidez, 10 = pH, 11 = conductividad.
#define PIN_MUX_S0      33   // Bit bajo
#define PIN_MUX_S1      14   // Bit alto

#define MUX_CH_TURBIDEZ     0b00
#define MUX_CH_PH           0b10
#define MUX_CH_CONDUCTIV    0b11
#define MUX_SETTLE_US       1000

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

// ── Configuración ADC ─────────────────────────────────────────────
#define ADC_MAX_VALUE          4095.0f
#define ADC_VREF               3.3f
#define ADC_MUESTRAS           16

// ── Periodos de tareas (ms) ───────────────────────────────────────
#define PERIODO_SENSADO_MS    1000
#define PERIODO_WATCHDOG_MS   2000
#define PERIODO_LED_MS        200

// ── Umbrales por defecto (modificables por RPC) ───────────────────
#define UMBRAL_PH_MIN_DEF     6.5f
#define UMBRAL_PH_MAX_DEF     8.5f
#define UMBRAL_TURBIDEZ_DEF   8.0f

// ── Tamaños de colas ──────────────────────────────────────────────
#define COLA1_TAMANO    10   // TareaA → TareaC (10 segundos de buffer)
#define COLA2_TAMANO     5   // TareaC → TareaB (comandos RPC)

// ── Estructura de telemetría (viaja por Cola 1) ───────────────────
struct TelemetriaData {
    float    ph;
    float    turbidez;
    float    conductividad;
    float    ph_voltage;
    float    turbidez_voltage;
    float    conductividad_voltage;
    uint16_t ph_raw;
    uint16_t turbidez_raw;
    uint16_t conductividad_raw;
    bool     actuador;    // Estado de la válvula: false=abierta, true=cerrada
    uint32_t uptime_ms;  // Tiempo desde boot en milisegundos
};

// ── Estructura de comando RPC (viaja por Cola 2) ──────────────────
struct ComandoRPC {
    char  metodo[32];
    float valor;
    char  requestId[8];
};
