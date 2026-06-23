#include "tareas.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ── Variables compartidas (definidas aquí, declaradas extern en tareas.h) ──
volatile bool  estadoActuador = false;
volatile float umbralPhMin    = UMBRAL_PH_MIN_DEF;
volatile float umbralPhMax    = UMBRAL_PH_MAX_DEF;
volatile float umbralTurbidez = UMBRAL_TURBIDEZ_DEF;

// ── Cliente MQTT (local a este archivo) ──────────────────────────
static WiFiClient  espClient;
static PubSubClient mqttClient(espClient);

// ── Lee ADC1 promediando 10 muestras (reduce ruido) ──────────────
static float leerADC(int pin) {
    uint32_t suma = 0;
    for (int i = 0; i < 10; i++) suma += analogRead(pin);
    return suma / 10.0f;
}

// ── Conversiones ADC → unidades físicas ──────────────────────────
static float adcAPh(float raw)          { return raw * (14.0f / 4095.0f); }
static float adcATurbidez(float raw)    { return raw * (100.0f / 4095.0f); }
static float adcAConductividad(float raw){ return raw * (2000.0f / 4095.0f); }

// ─────────────────────────────────────────────────────────────────
// CALLBACK MQTT — corre en contexto de TareaC
// ─────────────────────────────────────────────────────────────────
static void callbackMQTT(char* topic, byte* payload, unsigned int length) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, payload, length) != DeserializationError::Ok) return;

    ComandoRPC cmd;
    strlcpy(cmd.metodo, doc["method"] | "", sizeof(cmd.metodo));
    cmd.valor = 0.0f;

    // params puede ser bool, número o objeto con "value"
    if (doc["params"].is<bool>())        cmd.valor = doc["params"].as<bool>() ? 1.0f : 0.0f;
    else if (doc["params"].is<float>())  cmd.valor = doc["params"].as<float>();
    else if (doc["params"]["value"].is<float>()) cmd.valor = doc["params"]["value"].as<float>();

    // Extraer requestId del topic: "v1/devices/me/rpc/request/123"
    char* ultimaBarra = strrchr(topic, '/');
    strlcpy(cmd.requestId, ultimaBarra ? ultimaBarra + 1 : "0", sizeof(cmd.requestId));

    xQueueSend(cola2, &cmd, 0);

    // Respuesta inmediata a ThingsBoard (confirmación de recepción)
    char respTopic[64];
    snprintf(respTopic, sizeof(respTopic), "v1/devices/me/rpc/response/%s", cmd.requestId);
    mqttClient.publish(respTopic, "{\"status\":\"ok\"}");
}

// ─────────────────────────────────────────────────────────────────
// TAREA A — Sensado  |  Core 1  |  Prioridad 5
// ─────────────────────────────────────────────────────────────────
void tareaA_Sensado(void* param) {
    analogSetWidth(12);
    analogSetAttenuation(ADC_11db);

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t periodo = pdMS_TO_TICKS(PERIODO_SENSADO_MS);

    while (true) {
        TelemetriaData dato;

        if (xSemaphoreTake(mutex1, pdMS_TO_TICKS(10)) == pdTRUE) {
            dato.ph            = adcAPh(leerADC(PIN_PH));
            dato.turbidez      = adcATurbidez(leerADC(PIN_TURBIDEZ));
            dato.conductividad = adcAConductividad(leerADC(PIN_CONDUCTIV));
            xSemaphoreGive(mutex1);
        }

        // Descartar dato más antiguo si la cola está llena
        if (uxQueueSpacesAvailable(cola1) == 0) {
            TelemetriaData descarte;
            xQueueReceive(cola1, &descarte, 0);
        }
        xQueueSend(cola1, &dato, 0);

        // Periodo determinista — cumple NFR-01 (±5ms)
        vTaskDelayUntil(&xLastWakeTime, periodo);
    }
}

// ─────────────────────────────────────────────────────────────────
// TAREA B — Control actuador  |  Core 1  |  Prioridad 4
// ─────────────────────────────────────────────────────────────────
void tareaB_Actuador(void* param) {
    pinMode(PIN_RELE, OUTPUT);
    digitalWrite(PIN_RELE, LOW);

    ComandoRPC cmd;

    while (true) {
        if (xQueueReceive(cola2, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (xSemaphoreTake(mutex2, pdMS_TO_TICKS(50)) == pdTRUE) {

                if (strcmp(cmd.metodo, "setActuador") == 0) {
                    estadoActuador = (cmd.valor > 0.5f);
                    digitalWrite(PIN_RELE, estadoActuador ? HIGH : LOW);
                    Serial.printf("[TareaB] Actuador: %s\n", estadoActuador ? "ON" : "OFF");

                } else if (strcmp(cmd.metodo, "setUmbralPhMin") == 0) {
                    umbralPhMin = cmd.valor;
                    Serial.printf("[TareaB] Umbral pH min: %.2f\n", umbralPhMin);

                } else if (strcmp(cmd.metodo, "setUmbralPhMax") == 0) {
                    umbralPhMax = cmd.valor;
                    Serial.printf("[TareaB] Umbral pH max: %.2f\n", umbralPhMax);

                } else if (strcmp(cmd.metodo, "setUmbralTurbidez") == 0) {
                    umbralTurbidez = cmd.valor;
                    Serial.printf("[TareaB] Umbral turbidez: %.2f NTU\n", umbralTurbidez);
                }

                xSemaphoreGive(mutex2);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// TAREA C — WiFi / MQTT  |  Core 0  |  Prioridad 2
// ─────────────────────────────────────────────────────────────────
void tareaC_Comunicaciones(void* param) {
    mqttClient.setServer(TB_SERVER, TB_PORT);
    mqttClient.setCallback(callbackMQTT);
    mqttClient.setBufferSize(512);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    uint32_t ultimoIntentoWifi  = 0;
    uint32_t ultimoIntentoMQTT  = 0;
    uint32_t ultimoAtributo     = 0;

    while (true) {
        uint32_t ahora = millis();

        // ── Reconexión WiFi (no bloqueante) ──────────────────────
        if (WiFi.status() != WL_CONNECTED) {
            if (ahora - ultimoIntentoWifi > 5000) {
                ultimoIntentoWifi = ahora;
                WiFi.reconnect();
                Serial.println("[TareaC] Reconectando WiFi...");
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // ── Reconexión MQTT (no bloqueante) ──────────────────────
        if (!mqttClient.connected()) {
            if (ahora - ultimoIntentoMQTT > 5000) {
                ultimoIntentoMQTT = ahora;
                Serial.println("[TareaC] Conectando MQTT...");
                if (mqttClient.connect("AquaWatchCR", TB_TOKEN, NULL)) {
                    mqttClient.subscribe("v1/devices/me/rpc/request/+");
                    Serial.println("[TareaC] MQTT conectado");
                }
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // ── Publicar telemetría desde Cola 1 ─────────────────────
        TelemetriaData dato;
        if (xQueueReceive(cola1, &dato, 0) == pdTRUE) {
            StaticJsonDocument<256> doc;
            doc["ph"]            = serialized(String(dato.ph, 2));
            doc["turbidez"]      = serialized(String(dato.turbidez, 2));
            doc["conductividad"] = serialized(String(dato.conductividad, 2));

            char buffer[256];
            serializeJson(doc, buffer);
            mqttClient.publish("v1/devices/me/telemetry", buffer);
        }

        // ── Publicar estado del actuador como atributo (cada 2s) ─
        if (ahora - ultimoAtributo > 2000) {
            ultimoAtributo = ahora;
            bool estado;
            if (xSemaphoreTake(mutex2, pdMS_TO_TICKS(10)) == pdTRUE) {
                estado = estadoActuador;
                xSemaphoreGive(mutex2);
            }
            char atributo[64];
            snprintf(atributo, sizeof(atributo), "{\"actuador\":%s}", estado ? "true" : "false");
            mqttClient.publish("v1/devices/me/attributes", atributo);
        }

        mqttClient.loop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ─────────────────────────────────────────────────────────────────
// TAREA D — Watchdog  |  Core 1  |  Prioridad 3
// ─────────────────────────────────────────────────────────────────
void tareaD_Watchdog(void* param) {
    while (true) {
        bool wifiOk = (WiFi.status() == WL_CONNECTED);
        bool mqttOk = mqttClient.connected();
        UBaseType_t espaciosCola1 = uxQueueSpacesAvailable(cola1);

        Serial.printf("[WDG] WiFi:%d MQTT:%d Cola1_libre:%d/%d Actuador:%d\n",
                      wifiOk, mqttOk, espaciosCola1, COLA1_TAMANO, (bool)estadoActuador);

        if (espaciosCola1 == 0) {
            Serial.println("[WDG] ALERTA: Cola1 llena — TareaC puede estar bloqueada");
        }

        vTaskDelay(pdMS_TO_TICKS(PERIODO_WATCHDOG_MS));
    }
}

// ─────────────────────────────────────────────────────────────────
// TAREA E — Log + LED  |  Core 1  |  Prioridad 1
// ─────────────────────────────────────────────────────────────────
void tareaE_Log(void* param) {
    pinMode(PIN_LED, OUTPUT);

    while (true) {
        bool wifiOk = (WiFi.status() == WL_CONNECTED);
        bool mqttOk = mqttClient.connected();

        if (wifiOk && mqttOk) {
            // Parpadeo rápido (100ms) = sistema OK
            digitalWrite(PIN_LED, HIGH);
            vTaskDelay(pdMS_TO_TICKS(100));
            digitalWrite(PIN_LED, LOW);
            vTaskDelay(pdMS_TO_TICKS(100));
        } else if (wifiOk) {
            // Parpadeo medio (300ms) = WiFi OK pero sin MQTT
            digitalWrite(PIN_LED, HIGH);
            vTaskDelay(pdMS_TO_TICKS(300));
            digitalWrite(PIN_LED, LOW);
            vTaskDelay(pdMS_TO_TICKS(300));
        } else {
            // Parpadeo lento (800ms) = sin conexión
            digitalWrite(PIN_LED, HIGH);
            vTaskDelay(pdMS_TO_TICKS(800));
            digitalWrite(PIN_LED, LOW);
            vTaskDelay(pdMS_TO_TICKS(800));
        }
    }
}
