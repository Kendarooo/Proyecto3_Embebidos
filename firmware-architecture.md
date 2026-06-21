# Arquitectura de Firmware — AquaWatch CR
## ESP32 + FreeRTOS + ThingsBoard
**Grupo Bit Bakers · EL-5840 Taller de Sistemas Embebidos · ITCR · 2026**

---

## 1. Asignación de Cores

El ESP32 tiene dos cores. FreeRTOS en ESP-IDF reserva el **Core 0** para la pila de red (Wi-Fi, TCP/IP, Bluetooth). La asignación explícita de tareas por core es obligatoria usando `xTaskCreatePinnedToCore()`.

| Core | Responsabilidad | Tareas asignadas |
|---|---|---|
| **Core 0** | Conectividad y red | Tarea C (Wi-Fi + MQTT), Tarea E (logs, LED) |
| **Core 1** | Sensado y control | Tarea A (ADC), Tarea B (actuador), Tarea D (watchdog) |

**Justificación:** aislar el sensado (Tarea A) en Core 1 evita que la pila TCP/IP genere jitter en el periodo de muestreo, garantizando el determinismo temporal requerido por NFR-01 (±5 ms).

### Ejemplo de creación de tareas con pinning

```c
// Core 1 — sensado determinista
xTaskCreatePinnedToCore(tarea_sensado,   "TareaA", 4096, NULL, 5, NULL, 1);
xTaskCreatePinnedToCore(tarea_actuador,  "TareaB", 2048, NULL, 4, NULL, 1);
xTaskCreatePinnedToCore(tarea_watchdog,  "TareaD", 2048, NULL, 3, NULL, 1);

// Core 0 — conectividad y servicios
xTaskCreatePinnedToCore(tarea_mqtt,      "TareaC", 8192, NULL, 2, NULL, 0);
xTaskCreatePinnedToCore(tarea_log,       "TareaE", 1024, NULL, 1, NULL, 0);
```

---

## 2. Muestreo ADC — 3 Canales en Tarea A

Los tres sensores analógicos se leen secuencialmente dentro del mismo periodo de 500 ms en la Tarea A. Se usa exclusivamente el **ADC1** (GPIO 32–39), ya que el ADC2 es incompatible con el módulo Wi-Fi activo (CON-03).

### Asignación de pines

| Sensor | GPIO | Canal ADC1 |
|---|---|---|
| pH | GPIO 32 | ADC1_CHANNEL_4 |
| Turbidez | GPIO 33 | ADC1_CHANNEL_5 |
| Conductividad | GPIO 34 | ADC1_CHANNEL_6 |

### Implementación de la Tarea A

```c
void tarea_sensado(void *pvParameters) {
    TelemetriaPayload payload;

    while (1) {
        // Tomar mutex antes de acceder al bus ADC1
        xSemaphoreTake(mutex_adc, portMAX_DELAY);

        int raw_ph   = adc1_get_raw(ADC1_CHANNEL_4);
        int raw_turb = adc1_get_raw(ADC1_CHANNEL_5);
        int raw_cond = adc1_get_raw(ADC1_CHANNEL_6);

        xSemaphoreGive(mutex_adc);

        // Conversión a unidades físicas
        payload.ph            = convertir_ph(raw_ph);
        payload.turbidez      = convertir_turbidez(raw_turb);
        payload.conductividad = convertir_conductividad(raw_cond);

        // Enviar a Cola 1 sin bloqueo
        xQueueSend(cola_telemetria, &payload, 0);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
```

---

## 3. Emulación de Sensores con Potenciómetro

Dado que los sensores físicos de pH, turbidez y conductividad requieren calibración y acceso especializado, se emula cada uno con un **potenciómetro de 10 kΩ** conectado al pin ADC correspondiente (CON-04).

El potenciómetro entrega 0–3.3 V al ADC, que lo convierte a un valor crudo de 0–4095 (12 bits). Ese valor se mapea linealmente al rango físico de cada variable:

### Funciones de conversión

```c
// pH: rango 0.0 – 14.0
float convertir_ph(int raw) {
    return (raw * 14.0f) / 4095.0f;
}

// Turbidez: rango 0.0 – 10.0 NTU
float convertir_turbidez(int raw) {
    return (raw * 10.0f) / 4095.0f;
}

// Conductividad: rango 0.0 – 1000.0 µS/cm
float convertir_conductividad(int raw) {
    return (raw * 1000.0f) / 4095.0f;
}
```

### Tabla de mapeo referencial

| Posición potenciómetro | Voltaje (V) | Raw ADC | pH equivalente | Turbidez (NTU) | Conductividad (µS/cm) |
|---|---|---|---|---|---|
| 0% | 0.00 | 0 | 0.0 | 0.0 | 0 |
| 25% | 0.83 | 1024 | 3.5 | 2.5 | 250 |
| 50% | 1.65 | 2048 | 7.0 | 5.0 | 500 |
| 75% | 2.48 | 3072 | 10.5 | 7.5 | 750 |
| 100% | 3.30 | 4095 | 14.0 | 10.0 | 1000 |

**Nota:** los umbrales críticos definidos en ThingsBoard (pH < 6.5 o pH > 8.5; turbidez > 4 NTU) se pueden activar deliberadamente girando el potenciómetro durante la demostración.

---

## 4. Mecanismos de Sincronización

| Mecanismo | Tipo | Recurso protegido | Tareas involucradas |
|---|---|---|---|
| `mutex_adc` | Mutex | Bus ADC1 | Tarea A |
| `mutex_actuador` | Mutex | Estado GPIO actuador | Tarea B |
| `cola_telemetria` | Queue | Datos sensor → MQTT | Tarea A → Tarea C |
| `cola_rpc` | Queue | Comandos RPC → actuador | Tarea C → Tarea B |

---

## 5. Payload MQTT

```json
{
  "ph": 7.2,
  "turbidez": 2.1,
  "conductividad": 320.0,
  "actuador": false,
  "uptime_ms": 45231
}
```

Publicado cada 1000 ms por Tarea C hacia el topic:
```
v1/devices/me/telemetry
```

---

## 6. Diagrama de flujo entre cores

```
CORE 1 (App)                          CORE 0 (Protocol)
─────────────────────────────         ──────────────────────────
Tarea A (P5) ──[Cola 1]──────────────► Tarea C (P2) ──► MQTT/ThingsBoard
     │                                      │
     └──[mutex_adc]                   ◄─────┘ (RPC entrada)
                                            │
Tarea B (P4) ◄──[Cola 2]────────────────────┘
     │
     └──[mutex_actuador]──► GPIO → Relé → Válvula

Tarea D (P3) supervisa A, B, C
Tarea E (P1) logs + LED estado        (Core 0)
```

---

## 7. Notas de implementación

- **No usar** `vTaskDelay()` en tareas de alta prioridad con periodos críticos — usar `vTaskDelayUntil()` para garantizar periodo absoluto en Tarea A.
- **No usar** `while(!connected)` bloqueante en Tarea C — la reconexión debe ser asíncrona para no bloquear la cola.
- El ADC del ESP32 tiene no-linealidad en los extremos (0–100 mV y 3200–3300 mV). Evitar operar en esos rangos o aplicar corrección por tabla de lookup.
- Usar `adc1_config_width(ADC_WIDTH_BIT_12)` y `adc1_config_channel_atten()` con `ADC_ATTEN_DB_11` para rango completo 0–3.3 V.
