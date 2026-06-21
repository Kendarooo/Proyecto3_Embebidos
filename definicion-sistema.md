# AquaWatch CR — Definición del Sistema Físico
## ESP32 + FreeRTOS + ThingsBoard Cloud
**Grupo Bit Bakers · EL-5840 Taller de Sistemas Embebidos · ITCR · 2026**

---

## 1. Descripción general

AquaWatch CR es un sistema de monitoreo de calidad del agua en tiempo real orientado a ASADAS de la provincia de Cartago, Costa Rica. Opera como capa de alerta temprana ante eventos de contaminación físico-química.

El sistema es físico: ESP32-DevKit real con potenciómetros emulando sensores, LEDs y buzzer como actuadores visuales/sonoros, conectado a ThingsBoard Cloud vía MQTT sobre Wi-Fi.

---

## 2. Hardware del sistema

### 2.1 Entradas — Sensores emulados

Tres potenciómetros de 10 kΩ conectados al ADC1 del ESP32. Entregan 0–3.3 V mapeados linealmente al rango físico de cada variable.

| Variable | GPIO | Canal ADC1 | Rango emulado | Unidad |
|---|---|---|---|---|
| pH | GPIO 32 | ADC1_CH4 | 0.0 – 14.0 | pH |
| Turbidez | GPIO 33 | ADC1_CH5 | 0.0 – 10.0 | NTU |
| Conductividad | GPIO 34 | ADC1_CH6 | 0.0 – 1000.0 | µS/cm |

**Nota:** ADC2 no se usa por incompatibilidad con Wi-Fi activo.

### 2.2 Salidas — Actuadores físicos

| Componente | GPIO | Función |
|---|---|---|
| LED verde | GPIO 26 | Indica válvula abierta (sistema normal) |
| LED rojo | GPIO 27 | Indica válvula cerrada (alarma crítica activa) |
| Buzzer pasivo | GPIO 25 | Señal sonora en transición de válvula |

Resistencias de protección para LEDs: 330 Ω en serie.

---

## 3. Variables monitoreadas y umbrales

### 3.1 pH

| Condición | Umbral | Severidad | Acción automática |
|---|---|---|---|
| pH alto | > 8.5 | Critical | Cierre de válvula + alarma |
| pH bajo | < 6.5 | Critical | Cierre de válvula + alarma |
| Normal | 6.5 – 8.5 | — | Apertura de válvula si estaba cerrada |

Fuente de umbrales: AyA Decreto 38924-S.

### 3.2 Turbidez

| Condición | Umbral | Severidad | Acción automática |
|---|---|---|---|
| Turbidez alta | > 4.0 NTU | Critical | Cierre de válvula + alarma |
| Normal | ≤ 4.0 NTU | — | Apertura de válvula si estaba cerrada |

Fuente de umbral: AyA Decreto 38924-S.

### 3.3 Conductividad

| Condición | Umbral | Severidad | Acción automática |
|---|---|---|---|
| Conductividad elevada | > 800 µS/cm | Warning | Solo alerta — sin cierre de válvula |
| Normal | ≤ 800 µS/cm | — | Sin acción |

**Justificación:** AyA Decreto 38924-S no establece umbral normado de conductividad para cierre de suministro. La decisión ante conductividad elevada es responsabilidad del operario, no del sistema automático. El sistema escala a supervisión humana mediante alarma Warning.

---

## 4. Comportamiento del actuador

### 4.1 LED verde (GPIO 26)

- **ON:** sistema en estado normal, válvula abierta
- **OFF:** válvula cerrada por alarma crítica

### 4.2 LED rojo (GPIO 27)

- **ON:** válvula cerrada, alarma crítica activa
- **OFF:** sistema en estado normal

Los dos LEDs son mutuamente excluyentes. En todo momento uno está encendido y el otro apagado.

### 4.3 Buzzer pasivo (GPIO 25)

El buzzer suena **únicamente en el momento de la transición** de estado de la válvula. No suena de forma continua.

| Evento | Comportamiento del buzzer |
|---|---|
| Válvula se cierra (alarma detectada) | Beep corto — 200 ms |
| Válvula se abre (alarma despejada) | Beep corto — 200 ms |
| Válvula ya cerrada, nueva alarma | Sin sonido (sin transición) |
| Sistema en estado normal estable | Silencio |

---

## 5. Lógica de control automático

### 5.1 Condición de cierre de válvula

La válvula se cierra automáticamente si se cumple **cualquiera** de las siguientes condiciones:

```
ph > 8.5  →  cierre
ph < 6.5  →  cierre
turbidez > 4.0  →  cierre
```

### 5.2 Condición de apertura de válvula

La válvula se abre automáticamente cuando **todas** las condiciones críticas se despeja simultáneamente:

```
ph >= 6.5  Y  ph <= 8.5  Y  turbidez <= 4.0  →  apertura
```

### 5.3 Conductividad — sin acción sobre válvula

La conductividad no interviene en el control de la válvula bajo ninguna condición. Su única consecuencia es:

- Alarma Warning en ThingsBoard cuando > 800 µS/cm
- Color amarillo en el semáforo del dashboard
- Sin RPC enviado al ESP32

---

## 6. Flujo de datos

```
Potenciómetros (físicos)
        │
        ▼
ESP32 — ADC1 — Tarea A (muestreo 500 ms)
        │
        ▼
    Cola telemetría
        │
        ▼
Tarea C — MQTT publish cada 1000 ms
        │
        ▼
ThingsBoard Cloud
        │
        ├── Dashboard (visualización)
        ├── Alarm Rules (pH alto/bajo, turbidez, conductividad)
        └── Rule Chain (detección automática → RPC)
                │
                ▼
        RPC: activarValvula / desactivarValvula
                │
                ▼
        Tarea C — recibe RPC → Cola RPC
                │
                ▼
        Tarea B — ejecuta acción
                │
                ├── GPIO 26 (LED verde)
                ├── GPIO 27 (LED rojo)
                └── GPIO 25 (buzzer — solo en transición)
```

---

## 7. Payload MQTT

### 7.1 Telemetría — topic `v1/devices/me/telemetry`

Publicado cada 1000 ms por Tarea C.

```json
{
  "ph": 7.2,
  "turbidez": 2.1,
  "conductividad": 320.0,
  "actuador": false,
  "uptime_ms": 45231
}
```

El campo `actuador` refleja el estado actual de la válvula: `false` = abierta, `true` = cerrada.

### 7.2 Atributos — topic `v1/devices/me/attributes`

Publicado por Tarea B cada vez que el estado de la válvula cambia.

```json
{
  "estadoValvula": false
}
```

Este dato persiste en ThingsBoard entre reconexiones. La telemetría no persiste; el atributo sí.

---

## 8. Comandos RPC

Enviados desde ThingsBoard al ESP32. Recibidos en el topic `v1/devices/me/rpc/request/+`.

| Método | Origen | Acción en ESP32 |
|---|---|---|
| `activarValvula` | Rule Chain automático o dashboard manual | LED rojo ON, LED verde OFF, buzzer 200 ms |
| `desactivarValvula` | Rule Chain automático o dashboard manual | LED verde ON, LED rojo OFF, buzzer 200 ms |
| `setUmbralPH` | Dashboard manual (solo operario admin) | Actualiza umbral de pH en memoria del ESP32 |

---

## 9. Arquitectura de entidades en ThingsBoard

```
Tenant
└── Device Profile: AquaWatch
    ├── Transport: MQTT
    ├── Alarm Rules:
    │   ├── pHAltoAlarm     (Critical) — ph > 8.5
    │   ├── pHBajoAlarm     (Critical) — ph < 6.5
    │   ├── TurbidezAlarm   (Critical) — turbidez > 4.0
    │   └── ConductividadAlarm (Warning) — conductividad > 800
    └── Device: ESP32-AquaWatch-BitBakers
        ├── Telemetría: ph, turbidez, conductividad, actuador, uptime_ms
        ├── Atributos servidor: nombre_asada, ubicacion, fecha_instalacion,
        │                       responsable, normativa
        ├── Atributos cliente: estadoValvula
        └── Dashboard: AquaWatch-Dashboard
            └── Alias: SensorAgua → ESP32-AquaWatch-BitBakers
```

---

## 10. Dashboard — estados y roles

### 10.1 Vista Operador (estado Default)

- Telemetría en tiempo real (pH, turbidez, conductividad)
- Semáforo de calidad (verde / amarillo / rojo)
- Indicador de estado de válvula (abierta / cerrada)
- Tabla de alarmas activas (solo lectura)

### 10.2 Vista Administrador (estado Admin)

Todo lo anterior más:

- Control manual de válvula (RPC activar/desactivar)
- Modificación de umbral de pH (RPC setUmbralPH)
- Tabla de alarmas con columna de duración
- Exportación de historial

---

## 11. Semáforo de calidad

| Color | Condición |
|---|---|
| Rojo | ph > 8.5 ó ph < 6.5 ó turbidez > 4.0 |
| Amarillo | conductividad > 800 µS/cm (sin condición crítica activa) |
| Verde | Todas las variables dentro de rango normal |

---

## 12. Atributos estáticos del servidor

| Atributo | Valor |
|---|---|
| `nombre_asada` | ASADA Cipreses de Oreamuno |
| `ubicacion` | Cartago, Costa Rica |
| `responsable` | Bit Bakers — ITCR |
| `normativa` | AyA Decreto 38924-S |
