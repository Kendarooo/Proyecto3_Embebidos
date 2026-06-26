# Proyecto3_Embebidos

Sistema embebido Edge-to-Cloud para monitoreo de calidad del agua en tiempo real, desarrollado con ESP32, FreeRTOS, MQTT y ThingsBoard. El proyecto pertenece al curso Taller de Sistemas Embebidos de la Escuela de Ingenieria Electronica del Instituto Tecnologico de Costa Rica y se alinea con el ODS 6: Agua Limpia y Saneamiento.

Grupo: Bit Bakers

## Resumen del Sistema

El firmware implementa un nodo IoT con ESP32 que lee tres variables de calidad de agua por medio de entradas analogicas emuladas con potenciometros y un multiplexor analogico:

- pH, en escala 0 a 14.
- Turbidez, en NTU, con salida inversa: mayor voltaje equivale a agua mas clara.
- Conductividad electrica, en mS/cm, con rango de maqueta 0 a 20 mS/cm.

Los datos se procesan localmente en el ESP32, se envian a una cola FreeRTOS y se publican por MQTT hacia ThingsBoard. El sistema tambien recibe comandos RPC desde ThingsBoard para abrir o cerrar una valvula representada en el prototipo por LEDs y buzzer, y para modificar umbrales en tiempo de ejecucion.

El firmware actual usa:

- ESP32 DevKit (`esp32dev`) con framework Arduino sobre PlatformIO.
- FreeRTOS con 5 tareas de usuario.
- 2 colas FreeRTOS.
- 2 mutexes.
- Wi-Fi y MQTT hacia `thingsboard.cloud`.
- Librerias `PubSubClient` y `ArduinoJson`.

## Justificacion del Problema

### Contexto Nacional

Costa Rica enfrenta una crisis creciente en la calidad del agua para consumo humano a pesar de su abundancia hidrica. Analisis realizados durante los ultimos anos identificaron cambios en la calidad del agua sujetos a variables climaticas y errores en los sistemas de distribucion, donde una fuente apta en epoca seca puede presentar aumentos significativos de turbidez durante la temporada lluviosa [1].

### Problematica en Cartago

La provincia de Cartago concentra algunos de los casos mas criticos del pais:

- Desde 2022, Cipreses de Oreamuno enfrenta distribucion de agua contaminada con clorotalonil y otros plaguicidas [2].
- En marzo de 2024, la ARESEP solicito declarar estado de emergencia en la zona norte de Cartago por contaminacion con agroquimicos en fuentes de once ASADAS, afectando directamente a 33.000 habitantes. El 80% del area de proteccion de 35 nacientes estudiadas estaba invadido por cultivos e infraestructura agricola [3].
- En febrero de 2024, Turrialba entro en crisis por contaminacion con hidrocarburos, requiriendo analisis rigurosos antes de reautorizar el consumo [4].
- El Ministerio de Salud, AyA, MINAE, MAG y universidades debieron emprender acciones conjuntas de vigilancia y contencion en 2024 ante la magnitud del problema [5].

### Causa Raiz

El problema estructural no es solamente la presencia de contaminantes, sino la ausencia de vigilancia continua y automatizada en puntos de captacion. El monitoreo tradicional es reactivo, manual y periodico, con tiempos de respuesta que dependen de visitas tecnicas y analisis de laboratorio.

### Alcance del Prototipo

Este prototipo funciona como una capa de alerta temprana de bajo costo. No sustituye analisis certificados de laboratorio para bacterias, metales pesados, hidrocarburos o agroquimicos especificos. Las senales analogicas del prototipo se emulan fisicamente con potenciometros conectados al hardware, no por software.

## Requisitos del Sistema

### Requisitos Funcionales

| ID | Requisito | Estado en el firmware |
|----|-----------|-----------------------|
| FR-01 | Muestreo periodico de pH, turbidez y conductividad. | Implementado en `tareaA_Sensado`, periodo de 1000 ms. |
| FR-02 | Conversion de ADC crudo a unidades fisicas. | Implementado: pH 0-14, turbidez 0-10 NTU, conductividad 0-20 mS/cm. |
| FR-03 | Arquitectura multitarea estricta con al menos 5 tareas. | Implementado con tareas A, B, C, D y E. |
| FR-04 | Comunicacion entre tareas mediante colas. | Implementado con Cola 1 para telemetria y Cola 2 para RPC. |
| FR-05 | Proteccion de recursos compartidos. | Implementado con Mutex 1 para ADC/MUX y Mutex 2 para actuador/umbrales. |
| FR-06 | Publicacion MQTT con payload JSON. | Implementado en `tareaC_Comunicaciones`. |
| FR-07 | Reconexion automatica Wi-Fi/MQTT no bloqueante. | Implementado con reintentos temporizados cada 5 s. |
| FR-08 | Visualizacion en ThingsBoard. | Soportado por telemetria MQTT; el dashboard se configura en ThingsBoard. |
| FR-09 | Alarmas automaticas en ThingsBoard. | Soportado por payload y umbrales; la Rule Chain se configura en ThingsBoard. |
| FR-10 | Control remoto por RPC. | Implementado para valvula y umbrales. |
| FR-11 | Control de actuador con retroalimentacion. | Implementado mediante `estadoValvula` como atributo MQTT. |

### Requisitos No Funcionales

| ID | Requisito | Estado en el firmware |
|----|-----------|-----------------------|
| NFR-01 | Determinismo temporal de la tarea de sensado. | Implementado con `vTaskDelayUntil()` y prioridad maxima de usuario. |
| NFR-02 | Operacion autonoma ante desconexion de red. | Implementado: sensado y actuacion siguen en Core 1 aunque Wi-Fi/MQTT falle. |
| NFR-03 | Latencia de publicacion MQTT menor a 2 s en red normal. | Soportado por periodo de sensado de 1 s y ciclo MQTT de 100 ms. |
| NFR-04 | Respuesta RPC menor a 1 s. | Soportado por callback MQTT y Cola 2; se responde recepcion inmediatamente. |

### Restricciones

| ID | Restriccion | Aplicacion |
|----|-------------|------------|
| CON-01 | Plataforma ESP32. | Proyecto configurado para `esp32dev`. |
| CON-02 | Uso obligatorio de FreeRTOS. | `loop()` se elimina con `vTaskDelete(NULL)`; el control queda en tareas. |
| CON-03 | Uso de ADC1 y no ADC2. | Salida comun del MUX conectada a GPIO35, perteneciente a ADC1. |
| CON-04 | Emulacion analogica permitida por hardware. | Sensores maquetados con potenciometros conectados al MUX. |
| CON-05 | MQTT con JSON. | Implementado con `PubSubClient` y `ArduinoJson`. |
| CON-06 | Plataforma ThingsBoard. | Firmware publica y recibe comandos usando topics de ThingsBoard. |

## Hardware Implementado

### Placa

- ESP32 DevKit compatible con `board = esp32dev`.
- Comunicacion serial a 115200 baudios.
- Carga por `/dev/ttyUSB0` a 921600 baudios, segun `platformio.ini`.

### Sensado Analogico con MUX

La propuesta inicial contemplaba sensores analogicos conectados directamente a pines ADC1. En la implementacion final se uso un multiplexor analogico para concentrar las tres senales en una sola entrada ADC1 del ESP32.

| Senal | GPIO / Canal | Macro | Descripcion |
|-------|--------------|-------|-------------|
| Salida comun del MUX | GPIO35 | `PIN_MUX_ADC` | Entrada ADC1 usada por los tres sensores. |
| Selector S0 | GPIO33 | `PIN_MUX_S0` | Bit bajo de seleccion del MUX. |
| Selector S1 | GPIO14 | `PIN_MUX_S1` | Bit alto de seleccion del MUX. |
| Turbidez | Canal `00` | `MUX_CH_TURBIDEZ` | Potenciometro de turbidez. |
| pH | Canal `10` | `MUX_CH_PH` | Potenciometro de pH. |
| Conductividad | Canal `11` | `MUX_CH_CONDUCTIV` | Potenciometro de conductividad. |

La lectura de cada canal usa 16 muestras ADC y un tiempo de asentamiento del MUX de 1000 us. Esto reduce ruido y evita lecturas contaminadas por el cambio de canal.

### Actuador y Senalizacion

El actuador final del sistema se representa con LEDs y buzzer. En una instalacion real, el mismo estado logico podria conectarse a una etapa de potencia o rele para manejar una valvula solenoide.

| GPIO | Macro | Funcion |
|------|-------|---------|
| GPIO26 | `PIN_LED_VERDE` | Valvula abierta / sistema normal. |
| GPIO27 | `PIN_LED_ROJO` | Valvula cerrada / alarma critica. |
| GPIO25 | `PIN_BUZZER` | Beep de 200 ms en transiciones. |
| GPIO2 | `PIN_LED_STATUS` | LED de estado Wi-Fi/MQTT. |

Los LEDs verde y rojo son mutuamente excluyentes. El buzzer no queda activo de forma continua; solo emite un pulso cuando cambia el estado de la valvula.

## Configuracion del Proyecto

Archivo principal de configuracion: `platformio.ini`.

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 921600
upload_port = /dev/ttyUSB0
lib_deps =
    knolleary/PubSubClient@^2.8
    bblanchon/ArduinoJson@^6.21.0
```

Antes de compilar o cargar, revisar en `src/config.h`:

- `WIFI_SSID`
- `WIFI_PASS`
- `TB_SERVER`
- `TB_PORT`
- `TB_TOKEN`
- Pines usados por MUX, actuador y LED de estado
- Umbrales por defecto

Comandos utiles:

```bash
pio run
pio run --target upload
pio device monitor
```

## Arquitectura FreeRTOS

El sistema divide responsabilidades entre cinco tareas de usuario. Las tareas de tiempo real se fijan al Core 1 y las tareas asociadas a conectividad se fijan al Core 0, donde tambien opera la pila Wi-Fi del ESP32.

### Asignacion de Cores

| Core | Tareas | Proposito |
|------|--------|-----------|
| Core 1 | TareaA P5, TareaB P4, TareaD P3 | Sensado, control y supervision local. |
| Core 0 | TareaC P2, TareaE P1 | Wi-Fi, MQTT, RPC y LED de conectividad. |

### Tareas

| Tarea | Prioridad | Core | Funcion |
|-------|-----------|------|---------|
| Tarea A: Sensado | 5 | 1 | Lee MUX/ADC, convierte unidades, aplica control automatico por turbidez y envia telemetria a Cola 1. |
| Tarea B: Actuador | 4 | 1 | Procesa comandos RPC desde Cola 2 y actualiza actuador/umbrales. |
| Tarea C: Comunicaciones | 2 | 0 | Gestiona Wi-Fi, MQTT, publicacion de telemetria, atributos y suscripcion RPC. |
| Tarea D: Watchdog | 3 | 1 | Reporta por serial el estado de Wi-Fi, MQTT, Cola 1 y actuador. |
| Tarea E: Log/LED | 1 | 0 | Parpadea el LED integrado segun estado de conectividad. |

### Colas y Mutexes

| Recurso | Tipo | Uso |
|---------|------|-----|
| `cola1` | Queue de `TelemetriaData`, tamano 10 | Tarea A envia telemetria a Tarea C. Si se llena, se descarta el dato mas antiguo. |
| `cola2` | Queue de `ComandoRPC`, tamano 5 | Tarea C envia comandos RPC a Tarea B. |
| `mutex1` | Mutex | Protege el acceso al MUX/ADC durante lectura de sensores. |
| `mutex2` | Mutex | Protege `estadoActuador`, `umbralPhMin`, `umbralPhMax` y `umbralTurbidez`. |

## Procesamiento de Sensores

El ADC se configura a 12 bits con atenuacion `ADC_11db`, por lo que el firmware trabaja con valores crudos de 0 a 4095 y referencia de 3.3 V.

Conversiones implementadas:

```cpp
voltaje = (raw / 4095.0) * 3.3
pH = (voltaje / 3.3) * 14.0
conductividad = (voltaje / 3.3) * 20.0
turbidez = ((3.3 - voltaje) / 3.3) * 10.0
```

Rangos finales:

| Variable | Rango | Unidad | Nota |
|----------|-------|--------|------|
| pH | 0 a 14 | pH | Maqueta tipo SEN0161-V2. |
| Conductividad | 0 a 20 | mS/cm | Maqueta tipo DFR0300. |
| Turbidez | 0 a 10 | NTU | Salida inversa: 3.3 V representa agua clara. |

El periodo real de sensado es de 1000 ms, definido por `PERIODO_SENSADO_MS`.

## Control Local

La Tarea A aplica control automatico local usando el umbral de turbidez:

- Si `turbidez > umbralTurbidez`, el actuador pasa a activo, interpretado como valvula cerrada.
- Si `turbidez <= umbralTurbidez`, el actuador pasa a inactivo, interpretado como valvula abierta.
- Cuando el estado cambia, se actualizan LEDs y se emite un beep.

Los umbrales de pH (`umbralPhMin` y `umbralPhMax`) se pueden modificar por RPC y quedan protegidos por `mutex2`. En el firmware actual, esos umbrales quedan disponibles para logica remota/dashboard, pero el cierre automatico local se ejecuta por turbidez.

Umbrales por defecto:

| Umbral | Valor |
|--------|-------|
| pH minimo | 6.5 |
| pH maximo | 8.5 |
| Turbidez | 8.0 NTU |

## Integracion MQTT y ThingsBoard

### Conexion

El firmware se conecta a:

- Servidor: `thingsboard.cloud`
- Puerto MQTT: `1883`
- Cliente: `AquaWatchCR`
- Token: definido en `TB_TOKEN`

La reconexion Wi-Fi y MQTT se realiza sin ciclos bloqueantes indefinidos. Los reintentos se espacian cada 5 s y las tareas criticas continuan operando.

### Topic de Telemetria

Topic:

```text
v1/devices/me/telemetry
```

Payload real publicado:

```json
{
  "ph": 7.23,
  "turbidez": 2.15,
  "conductividad": 10.42,
  "ph_con_unidad": "7.23 pH",
  "turbidez_con_unidad": "2.15 NTU",
  "conductividad_con_unidad": "10.42 mS/cm",
  "ph_voltage": 1.704,
  "turbidez_voltage": 2.591,
  "conductividad_voltage": 1.719,
  "ph_voltage_con_unidad": "1.704 V",
  "turbidez_voltage_con_unidad": "2.591 V",
  "conductividad_voltage_con_unidad": "1.719 V",
  "ph_raw": 2115,
  "turbidez_raw": 3215,
  "conductividad_raw": 2133,
  "actuador": false,
  "uptime_ms": 14823
}
```

El payload incluye valores numericos, versiones con unidad para visualizacion directa, voltajes, lecturas ADC crudas, estado del actuador y tiempo desde el arranque.

### Topic de Atributos

Topic:

```text
v1/devices/me/attributes
```

Payload:

```json
{ "estadoValvula": false }
```

Este atributo se publica al conectar, al reconectar MQTT y cada vez que cambia el estado de la valvula. ThingsBoard lo puede usar como ultimo estado conocido del dispositivo.

### RPC

Suscripcion:

```text
v1/devices/me/rpc/request/+
```

Respuesta:

```text
v1/devices/me/rpc/response/{requestId}
```

El callback MQTT responde inmediatamente:

```json
{ "status": "ok" }
```

Luego encola el comando en `cola2` para que la Tarea B lo ejecute.

Metodos soportados:

| Metodo | Parametro | Efecto |
|--------|-----------|--------|
| `activarValvula` | opcional; el valor no se usa | Cierra la valvula: LED rojo ON, LED verde OFF, beep. |
| `desactivarValvula` | opcional; el valor no se usa | Abre la valvula: LED verde ON, LED rojo OFF, beep. |
| `setUmbralPhMin` | float o `params.value` | Actualiza `umbralPhMin` en RAM. |
| `setUmbralPhMax` | float o `params.value` | Actualiza `umbralPhMax` en RAM. |
| `setUmbralTurbidez` | float o `params.value` | Actualiza `umbralTurbidez` en RAM. |

## Casos de Uso

### CU01: Monitorear Calidad del Agua en Tiempo Real

Actor: operador de ASADA.

El operador accede al dashboard de ThingsBoard y observa valores actuales e historicos de pH, turbidez y conductividad. El ESP32 publica los datos por MQTT cada vez que la Tarea C consume una muestra de Cola 1.

### CU02: Recibir Alerta de Contaminacion

Actor: operador de ASADA.

ThingsBoard puede generar alarmas mediante Rule Chains cuando una variable supera un umbral configurado. El firmware entrega las variables necesarias para esas reglas.

### CU03: Controlar Actuador Remotamente

Actor: operador de ASADA.

El operador envia un RPC desde ThingsBoard. El ESP32 recibe el mensaje, responde al request, encola el comando y la Tarea B actualiza el estado fisico representado por LEDs y buzzer.

### CU04: Modificar Umbral de Alarma

Actor: administrador tecnico.

El administrador envia un RPC con el nuevo valor de umbral. El ESP32 actualiza el valor en RAM bajo `mutex2`. Estos valores no se persisten en memoria no volatil, por lo que vuelven a los valores por defecto despues de reiniciar.

### CU05: Consultar Historial de Telemetria

Actor: operador de ASADA o administrador tecnico.

ThingsBoard almacena los datos publicados por MQTT y permite consultar series historicas para identificar patrones, eventos o anomalias.

## Concepto de Operaciones

### Operacion Normal

1. La Tarea A selecciona cada canal del MUX, lee el ADC, promedia muestras y convierte a unidades fisicas.
2. La Tarea A deposita la estructura `TelemetriaData` en Cola 1.
3. La Tarea C consume Cola 1 y publica el JSON en ThingsBoard.
4. El dashboard muestra telemetria actual e historica.
5. La Tarea E muestra estado de conectividad por LED.

### Desconexion de Red

1. Si Wi-Fi o MQTT caen, la Tarea C intenta reconectar cada 5 s.
2. La Tarea A sigue sensando cada 1000 ms.
3. La Tarea B sigue disponible para control local.
4. Cola 1 conserva hasta 10 muestras; si se llena, se descarta la mas antigua para mantener el dato reciente.

### Condicion de Alarma

1. Si la turbidez supera `umbralTurbidez`, la Tarea A cierra automaticamente la valvula logica.
2. Se enciende LED rojo, se apaga LED verde y suena el buzzer.
3. Tarea C publica `actuador` en telemetria y `estadoValvula` como atributo.
4. ThingsBoard puede mostrar la alarma y registrar el evento.

## Dashboard ThingsBoard

El dashboard recomendado debe incluir:

- Tarjetas de valor actual para pH, turbidez y conductividad.
- Graficas historicas para cada variable.
- Indicador de estado de valvula usando `estadoValvula`.
- Widget RPC para `activarValvula`.
- Widget RPC para `desactivarValvula`.
- Controles RPC para `setUmbralPhMin`, `setUmbralPhMax` y `setUmbralTurbidez`.
- Panel de alarmas con reglas sobre pH, turbidez y conductividad.

Reglas sugeridas:

| Variable | Condicion sugerida | Accion |
|----------|--------------------|--------|
| pH | `< 6.5` o `> 8.5` | Alarma visual en dashboard. |
| Turbidez | `> umbral configurado` | Alarma y posible cierre de valvula. |
| Conductividad | Segun criterio de calibracion | Alarma informativa o preventiva. |

## Estructura del Repositorio

```text
firmware/
|-- platformio.ini
|-- README.md
|-- src/
|   |-- main.cpp
|   |-- config.h
|   |-- tareas.h
|   `-- tareas.cpp
|-- include/
|   `-- README
|-- lib/
|   `-- README
`-- test/
    `-- README
```

Archivos principales:

- `src/main.cpp`: crea colas, mutexes y tareas FreeRTOS.
- `src/config.h`: centraliza pines, credenciales, umbrales, periodos y estructuras de datos.
- `src/tareas.h`: declara colas, mutexes, estado compartido y prototipos.
- `src/tareas.cpp`: implementa sensado, actuacion, MQTT, watchdog y LED de estado.
- `platformio.ini`: define placa, framework, puerto, velocidad y librerias.

## Cambios Respecto a la Propuesta Inicial

Durante la implementacion se ajustaron algunos puntos para que el sistema fuera demostrable y coherente con el hardware disponible:

- Se uso un multiplexor analogico para leer tres senales con una sola entrada ADC1 (`GPIO35`) en lugar de conectar cada sensor a un ADC individual.
- Los sensores fisicos se maquetaron con potenciometros, manteniendo emulacion analogica por hardware.
- La conductividad se documento segun el firmware final como mS/cm y no como uS/cm.
- El periodo de sensado final es 1000 ms, no 500 ms.
- El actuador se implemento en el prototipo con LED verde, LED rojo y buzzer. La valvula solenoide queda representada logicamente por `estadoActuador`/`estadoValvula`.
- El control automatico local cierra la valvula por turbidez. Los umbrales de pH se pueden modificar por RPC, pero no disparan cierre local en el firmware actual.
- La respuesta RPC confirma recepcion del comando; la ejecucion fisica queda a cargo de la Tarea B mediante Cola 2.
- No se incluyen archivos de imagen dentro del workspace actual, por lo que los diagramas se describen textualmente en este README.

## Que se Aplico al Final

Al final del proyecto quedo aplicado un sistema Edge-to-Cloud funcional con ESP32 y FreeRTOS. El nodo toma muestras analogicas reales desde el hardware, las procesa localmente, mantiene separadas las tareas criticas de las tareas de red, publica telemetria enriquecida a ThingsBoard y acepta control remoto mediante RPC.

En terminos de firmware, quedaron implementadas las cinco tareas requeridas: sensado determinista, actuacion, comunicaciones, watchdog y LED de estado. Tambien quedaron implementadas las colas entre tareas, los mutexes para evitar condiciones de carrera, la reconexion Wi-Fi/MQTT no bloqueante, el payload JSON completo, la publicacion de atributos y el control remoto de la valvula logica.

En terminos de prototipo fisico, se aplico una maqueta analogica con MUX y potenciometros para representar pH, turbidez y conductividad. Para la salida, se aplico una representacion segura del actuador mediante LEDs y buzzer, suficiente para demostrar el comportamiento de alarma, apertura/cierre logico y confirmacion hacia ThingsBoard.

## Referencias

[1] B. Camarillo, "Como esta la calidad del agua en Costa Rica? Bacterias y contaminantes se encontraron en estas zonas," La Republica, 31 oct. 2024. Disponible en: https://www.larepublica.net/noticia/como-esta-la-calidad-del-agua-en-costa-rica-bacterias-y-contaminantes-se-encontraron-en-estas-zonas

[2] Delfino.cr, "Agua toxica," 9 jul. 2025. Disponible en: https://delfino.cr/2025/07/agua-toxica

[3] Semanario Universidad, "Emergencia ambiental: mas de 69 fuentes de agua en Cartago estarian contaminadas," 15 oct. 2024. Disponible en: https://semanariouniversidad.com/opinion/emergencia-ambiental-mas-de-69-fuentes-de-agua-en-cartago-estarian-contaminadas/

[4] Prensa Latina, "Autorizan en Costa Rica consumo de agua tras contaminacion," 13 feb. 2024. Disponible en: https://www.prensa-latina.cu/2024/02/13/autorizan-en-costa-rica-consumo-de-agua-tras-contaminacion/

[5] Ministerio de Salud de Costa Rica, "Autoridades comparten resultados del analisis en fuentes de agua en Zona de Cartago," 28 oct. 2024. Disponible en: https://www.ministeriodesalud.go.cr/index.php/prensa/61-noticias-2024/1980-autoridades-comparten-resultados-del-analisis-en-fuentes-de-agua-en-zona-de-cartago
