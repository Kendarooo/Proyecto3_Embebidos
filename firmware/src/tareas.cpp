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

// ── Salidas del actuador ─────────────────────────────────────────
static void inicializarPinesActuador() {
    pinMode(PIN_LED_VERDE, OUTPUT);
    pinMode(PIN_LED_ROJO,  OUTPUT);
    pinMode(PIN_BUZZER,    OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
}

static void aplicarSalidaActuador(bool actuadorActivo) {
    digitalWrite(PIN_LED_VERDE, actuadorActivo ? LOW : HIGH);
    digitalWrite(PIN_LED_ROJO,  actuadorActivo ? HIGH : LOW);
}

static void beepActuador() {
    digitalWrite(PIN_BUZZER, HIGH);
    vTaskDelay(pdMS_TO_TICKS(200));
    digitalWrite(PIN_BUZZER, LOW);
}

// ── Selecciona canal del MUX. canal usa formato S1S0 ─────────────
static void seleccionarMux(uint8_t canal) {
    digitalWrite(PIN_MUX_S0, (canal & 0b01) ? HIGH : LOW);
    digitalWrite(PIN_MUX_S1, (canal & 0b10) ? HIGH : LOW);
    delayMicroseconds(MUX_SETTLE_US);
}

// ── Lee ADC1 común del MUX promediando muestras físicas ──────────
static uint16_t leerMuxPromediado(uint8_t canal) {
    seleccionarMux(canal);

    uint32_t suma = 0;
    for (int i = 0; i < ADC_MUESTRAS; i++) {
        suma += analogRead(PIN_MUX_ADC);
        delayMicroseconds(200);
    }
    return (uint16_t)((suma + (ADC_MUESTRAS / 2)) / ADC_MUESTRAS);
}

// ── Conversiones ADC físico → voltaje → unidades de sensores ─────
static float clamp_float(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static float adc_raw_to_voltage(uint16_t raw) {
    return ((float)raw / ADC_MAX_VALUE) * ADC_VREF;
}

static float voltage_to_ph(float voltage) {
    // Maqueta SEN0161-V2: potenciómetro 0.0-3.3 V representa pH 0-14.
    return clamp_float((voltage / ADC_VREF) * 14.0f, 0.0f, 14.0f);
}

static float voltage_to_ec(float voltage) {
    // Maqueta DFR0300: potenciómetro 0.0-3.3 V representa EC 0-20 mS/cm.
    return clamp_float((voltage / ADC_VREF) * 20.0f, 0.0f, 20.0f);
}

static float voltage_to_turbidity(float voltage) {
    // Maqueta SEN0189: salida inversa; 3.3 V agua clara, 0.0 V muy turbia.
    return clamp_float(((ADC_VREF - voltage) / ADC_VREF) * 10.0f, 0.0f, 10.0f);
}

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
    analogReadResolution(12);
    inicializarPinesActuador();
    aplicarSalidaActuador(false);
    pinMode(PIN_MUX_ADC, INPUT);
    pinMode(PIN_MUX_S0, OUTPUT);
    pinMode(PIN_MUX_S1, OUTPUT);
    seleccionarMux(MUX_CH_TURBIDEZ);

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t periodo = pdMS_TO_TICKS(PERIODO_SENSADO_MS);

    while (true) {
        TelemetriaData dato = {};

        if (xSemaphoreTake(mutex1, pdMS_TO_TICKS(10)) == pdTRUE) {
            // Cada canal del MUX conecta un potenciómetro que maqueta un sensor real.
            dato.turbidez_raw      = leerMuxPromediado(MUX_CH_TURBIDEZ);   // S1S0 = 00
            dato.ph_raw            = leerMuxPromediado(MUX_CH_PH);         // S1S0 = 10
            dato.conductividad_raw = leerMuxPromediado(MUX_CH_CONDUCTIV);  // S1S0 = 11
            xSemaphoreGive(mutex1);
        }

        dato.ph_voltage            = adc_raw_to_voltage(dato.ph_raw);
        dato.turbidez_voltage      = adc_raw_to_voltage(dato.turbidez_raw);
        dato.conductividad_voltage = adc_raw_to_voltage(dato.conductividad_raw);

        dato.ph            = voltage_to_ph(dato.ph_voltage);
        dato.turbidez      = voltage_to_turbidity(dato.turbidez_voltage);
        dato.conductividad = voltage_to_ec(dato.conductividad_voltage);

        bool cambioActuador = false;
        float umbralTurbidezUsado = UMBRAL_TURBIDEZ_DEF;
        if (xSemaphoreTake(mutex2, pdMS_TO_TICKS(5)) == pdTRUE) {
            umbralTurbidezUsado = umbralTurbidez;
            bool nuevoEstado = (dato.turbidez > umbralTurbidezUsado);
            if (nuevoEstado != estadoActuador) {
                estadoActuador = nuevoEstado;
                cambioActuador = true;
            }
            dato.actuador = estadoActuador;
            xSemaphoreGive(mutex2);
        } else {
            dato.actuador = false;
        }
        aplicarSalidaActuador(dato.actuador);
        if (cambioActuador) {
            beepActuador();
            Serial.printf("[TareaA] Control automático: turbidez %.2f NTU, umbral %.2f NTU, válvula %s\n",
                          dato.turbidez, umbralTurbidezUsado, dato.actuador ? "CERRADA" : "ABIERTA");
        }
        dato.uptime_ms = (uint32_t)millis();

        // Descartar dato más antiguo si la cola está llena
        if (uxQueueSpacesAvailable(cola1) == 0) {
            TelemetriaData descarte;
            xQueueReceive(cola1, &descarte, 0);
        }
        xQueueSend(cola1, &dato, 0);

        Serial.printf("[ADC] pH: %.2f | EC: %.2f mS/cm | Turbidity: %.2f NTU | VpH: %.2f V | VEC: %.2f V | VTurb: %.2f V | raw pH:%u EC:%u Turb:%u\n",
                      dato.ph, dato.conductividad, dato.turbidez,
                      dato.ph_voltage, dato.conductividad_voltage, dato.turbidez_voltage,
                      dato.ph_raw, dato.conductividad_raw, dato.turbidez_raw);

        // Periodo determinista — cumple NFR-01 (±5ms)
        vTaskDelayUntil(&xLastWakeTime, periodo);
    }
}

// ─────────────────────────────────────────────────────────────────
// TAREA B — Control actuador  |  Core 1  |  Prioridad 4
// ─────────────────────────────────────────────────────────────────
void tareaB_Actuador(void* param) {
    inicializarPinesActuador();
    aplicarSalidaActuador(false);

    ComandoRPC cmd;

    while (true) {
        if (xQueueReceive(cola2, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {

            if (strcmp(cmd.metodo, "activarValvula") == 0) {
                if (xSemaphoreTake(mutex2, pdMS_TO_TICKS(50)) == pdTRUE) {
                    estadoActuador = true;
                    xSemaphoreGive(mutex2);
                }
                aplicarSalidaActuador(true);
                beepActuador();
                Serial.println("[TareaB] Válvula CERRADA — alarma");

            } else if (strcmp(cmd.metodo, "desactivarValvula") == 0) {
                if (xSemaphoreTake(mutex2, pdMS_TO_TICKS(50)) == pdTRUE) {
                    estadoActuador = false;
                    xSemaphoreGive(mutex2);
                }
                aplicarSalidaActuador(false);
                beepActuador();
                Serial.println("[TareaB] Válvula ABIERTA — normal");

            } else if (strcmp(cmd.metodo, "setUmbralPhMin") == 0) {
                if (xSemaphoreTake(mutex2, pdMS_TO_TICKS(50)) == pdTRUE) {
                    umbralPhMin = cmd.valor;
                    xSemaphoreGive(mutex2);
                }
                Serial.printf("[TareaB] Umbral pH min: %.2f\n", cmd.valor);

            } else if (strcmp(cmd.metodo, "setUmbralPhMax") == 0) {
                if (xSemaphoreTake(mutex2, pdMS_TO_TICKS(50)) == pdTRUE) {
                    umbralPhMax = cmd.valor;
                    xSemaphoreGive(mutex2);
                }
                Serial.printf("[TareaB] Umbral pH max: %.2f\n", cmd.valor);

            } else if (strcmp(cmd.metodo, "setUmbralTurbidez") == 0) {
                if (xSemaphoreTake(mutex2, pdMS_TO_TICKS(50)) == pdTRUE) {
                    umbralTurbidez = cmd.valor;
                    xSemaphoreGive(mutex2);
                }
                Serial.printf("[TareaB] Umbral turbidez: %.2f NTU\n", cmd.valor);
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
    mqttClient.setBufferSize(1024);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    uint32_t ultimoIntentoWifi   = 0;
    uint32_t ultimoIntentoMQTT   = 0;
    bool     lastEstadoPublicado = false;
    bool     primeraPublicacion  = true;

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
                    primeraPublicacion = true;  // re-publicar atributo tras reconexión
                    Serial.println("[TareaC] MQTT conectado");
                }
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // ── Leer estado del actuador (compartido entre telemetría y atributo) ─
        bool estadoAhora;
        if (xSemaphoreTake(mutex2, pdMS_TO_TICKS(10)) == pdTRUE) {
            estadoAhora = estadoActuador;
            xSemaphoreGive(mutex2);
        } else {
            estadoAhora = lastEstadoPublicado;
        }

        // ── Publicar telemetría desde Cola 1 ─────────────────────
        TelemetriaData dato;
        if (xQueueReceive(cola1, &dato, 0) == pdTRUE) {
            StaticJsonDocument<768> doc;
            char phConUnidad[24];
            char ecConUnidad[32];
            char turbidezConUnidad[32];
            char phVoltajeConUnidad[24];
            char ecVoltajeConUnidad[24];
            char turbidezVoltajeConUnidad[24];

            snprintf(phConUnidad, sizeof(phConUnidad), "%.2f pH", dato.ph);
            snprintf(ecConUnidad, sizeof(ecConUnidad), "%.2f mS/cm", dato.conductividad);
            snprintf(turbidezConUnidad, sizeof(turbidezConUnidad), "%.2f NTU", dato.turbidez);
            snprintf(phVoltajeConUnidad, sizeof(phVoltajeConUnidad), "%.3f V", dato.ph_voltage);
            snprintf(ecVoltajeConUnidad, sizeof(ecVoltajeConUnidad), "%.3f V", dato.conductividad_voltage);
            snprintf(turbidezVoltajeConUnidad, sizeof(turbidezVoltajeConUnidad), "%.3f V", dato.turbidez_voltage);

            doc["ph"]            = serialized(String(dato.ph, 2));
            doc["turbidez"]      = serialized(String(dato.turbidez, 2));
            doc["conductividad"] = serialized(String(dato.conductividad, 2));
            doc["ph_con_unidad"]            = phConUnidad;
            doc["turbidez_con_unidad"]      = turbidezConUnidad;
            doc["conductividad_con_unidad"] = ecConUnidad;
            doc["ph_voltage"]            = serialized(String(dato.ph_voltage, 3));
            doc["turbidez_voltage"]      = serialized(String(dato.turbidez_voltage, 3));
            doc["conductividad_voltage"] = serialized(String(dato.conductividad_voltage, 3));
            doc["ph_voltage_con_unidad"]            = phVoltajeConUnidad;
            doc["turbidez_voltage_con_unidad"]      = turbidezVoltajeConUnidad;
            doc["conductividad_voltage_con_unidad"] = ecVoltajeConUnidad;
            doc["ph_raw"]            = dato.ph_raw;
            doc["turbidez_raw"]      = dato.turbidez_raw;
            doc["conductividad_raw"] = dato.conductividad_raw;
            doc["actuador"]      = estadoAhora;          // bool real: true / false
            doc["uptime_ms"]     = dato.uptime_ms;

            char buffer[1024];
            serializeJson(doc, buffer);
            mqttClient.publish("v1/devices/me/telemetry", buffer);
        }

        // ── Publicar estadoValvula en atributos solo cuando cambia ─
        if (primeraPublicacion || estadoAhora != lastEstadoPublicado) {
            lastEstadoPublicado = estadoAhora;
            primeraPublicacion  = false;
            char atributo[48];
            snprintf(atributo, sizeof(atributo), "{\"estadoValvula\":%s}", estadoAhora ? "true" : "false");
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
// TAREA E — Log + LED  |  Core 0  |  Prioridad 1
// ─────────────────────────────────────────────────────────────────
void tareaE_Log(void* param) {
    pinMode(PIN_LED_STATUS, OUTPUT);

    while (true) {
        bool wifiOk = (WiFi.status() == WL_CONNECTED);
        bool mqttOk = mqttClient.connected();

        if (wifiOk && mqttOk) {
            // Parpadeo rápido (100ms) = sistema OK
            digitalWrite(PIN_LED_STATUS, HIGH);
            vTaskDelay(pdMS_TO_TICKS(100));
            digitalWrite(PIN_LED_STATUS, LOW);
            vTaskDelay(pdMS_TO_TICKS(100));
        } else if (wifiOk) {
            // Parpadeo medio (300ms) = WiFi OK pero sin MQTT
            digitalWrite(PIN_LED_STATUS, HIGH);
            vTaskDelay(pdMS_TO_TICKS(300));
            digitalWrite(PIN_LED_STATUS, LOW);
            vTaskDelay(pdMS_TO_TICKS(300));
        } else {
            // Parpadeo lento (800ms) = sin conexión
            digitalWrite(PIN_LED_STATUS, HIGH);
            vTaskDelay(pdMS_TO_TICKS(800));
            digitalWrite(PIN_LED_STATUS, LOW);
            vTaskDelay(pdMS_TO_TICKS(800));
        }
    }
}
