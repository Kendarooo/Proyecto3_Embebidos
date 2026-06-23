#include <Arduino.h>
#include "config.h"
#include "tareas.h"

// ── Colas ────────────────────────────────────────────────────────
QueueHandle_t cola1;
QueueHandle_t cola2;

// ── Mutexes ───────────────────────────────────────────────────────
SemaphoreHandle_t mutex1;
SemaphoreHandle_t mutex2;

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== AquaWatchCR — Sistema de Monitoreo de Calidad del Agua ===");

    // Crear colas
    cola1 = xQueueCreate(COLA1_TAMANO, sizeof(TelemetriaData));
    cola2 = xQueueCreate(COLA2_TAMANO, sizeof(ComandoRPC));

    // Crear mutexes
    mutex1 = xSemaphoreCreateMutex();
    mutex2 = xSemaphoreCreateMutex();

    if (!cola1 || !cola2 || !mutex1 || !mutex2) {
        Serial.println("[ERROR] Fallo al crear objetos FreeRTOS. Reiniciando...");
        esp_restart();
    }

    // ── Core 1: Tiempo real (sensado y control determinista) ─────
    xTaskCreatePinnedToCore(
        tareaA_Sensado,  "TareaA",
        4096, NULL, 5, NULL, 1
    );
    xTaskCreatePinnedToCore(
        tareaB_Actuador, "TareaB",
        4096, NULL, 4, NULL, 1
    );
    xTaskCreatePinnedToCore(
        tareaD_Watchdog, "TareaD",
        2048, NULL, 3, NULL, 1
    );

    // ── Core 0: Conectividad y servicios (WiFi stack en Core 0) ─
    xTaskCreatePinnedToCore(
        tareaC_Comunicaciones, "TareaC",
        8192, NULL, 2, NULL, 0
    );
    xTaskCreatePinnedToCore(
        tareaE_Log,      "TareaE",
        1024, NULL, 1, NULL, 0
    );

    Serial.println("[OK] Todas las tareas iniciadas");
    Serial.printf("  Core 1 → TareaA(P5) TareaB(P4) TareaD(P3)\n");
    Serial.printf("  Core 0 → TareaC(P2) TareaE(P1)\n");
}

void loop() {
    // FreeRTOS toma el control — loop no se usa
    vTaskDelete(NULL);
}
