# Proyecto3_Embebidos
Sistema embebido Edge-to-Cloud para monitoreo de calidad del agua en tiempo real, desarrollado con ESP32 + FreeRTOS y ThingsBoard. Proyecto del curso Taller de Sistemas Embebidos, Escuela de Ingeniería Electrónica, Instituto Tecnológico de Costa Rica. Alineado al ODS 6 — Agua Limpia y Saneamiento. | Grupo Bit Bakers

## Justificación del Problema

### Contexto Nacional

Costa Rica enfrenta una crisis creciente en la calidad del agua para consumo humano a pesar de su abundancia hídrica. Análisis realizados durante los últimos 15 años identificaron cambios en la calidad del agua sujetos a variables climáticas y errores en los sistemas de distribución, donde una fuente apta en época seca puede presentar un aumento significativo de turbidez durante la temporada lluviosa [1].

### Problemática en Cartago

La provincia de Cartago concentra algunos de los casos más críticos del país:

- Desde **2022**, Cipreses de Oreamuno enfrenta distribución de agua contaminada con clorotalonil y otros plaguicidas [2].
- En **marzo de 2024**, la ARESEP solicitó declarar estado de emergencia en la zona norte de Cartago por contaminación con agroquímicos en fuentes de once ASADAS, afectando directamente a **33.000 habitantes**. El 80% del área de protección de 35 nacientes estudiadas estaba invadido por cultivos e infraestructura agrícola [3].
- En **febrero de 2024**, Turrialba entró en crisis por contaminación con hidrocarburos, requiriendo análisis rigurosos antes de reautorizar el consumo [4].
- El Ministerio de Salud, AyA, MINAE, MAG y universidades debieron emprender acciones conjuntas de vigilancia y contención en 2024 ante la magnitud del problema [5].

### Causa Raíz

El problema estructural no es únicamente la presencia de contaminantes, sino la **ausencia de vigilancia continua y automatizada** en los puntos de captación. El monitoreo actual es reactivo, manual y periódico, con tiempos de respuesta lentos que dependen de laboratorios institucionales.

### Limitación del Sistema

Este prototipo opera como capa de alerta temprana de bajo costo. No sustituye análisis de laboratorio certificados para bacterias, metales pesados o agroquímicos específicos.

---

## Referencias

[1] B. Camarillo, "¿Cómo está la calidad del agua en Costa Rica? Bacterias y contaminantes se encontraron en estas zonas," *La República*, 31 oct. 2024. [En línea]. Disponible en: https://www.larepublica.net/noticia/como-esta-la-calidad-del-agua-en-costa-rica-bacterias-y-contaminantes-se-encontraron-en-estas-zonas

[2] Delfino.cr, "Agua tóxica," 9 jul. 2025. [En línea]. Disponible en: https://delfino.cr/2025/07/agua-toxica

[3] Semanario Universidad, "Emergencia ambiental: más de 69 fuentes de agua en Cartago estarían contaminadas," 15 oct. 2024. [En línea]. Disponible en: https://semanariouniversidad.com/opinion/emergencia-ambiental-mas-de-69-fuentes-de-agua-en-cartago-estarian-contaminadas/

[4] Prensa Latina, "Autorizan en Costa Rica consumo de agua tras contaminación," 13 feb. 2024. [En línea]. Disponible en: https://www.prensa-latina.cu/2024/02/13/autorizan-en-costa-rica-consumo-de-agua-tras-contaminacion/

[5] Ministerio de Salud de Costa Rica, "Autoridades comparten resultados del análisis en fuentes de agua en Zona de Cartago," 28 oct. 2024. [En línea]. Disponible en: https://www.ministeriodesalud.go.cr/index.php/prensa/61-noticias-2024/1980-autoridades-comparten-resultados-del-analisis-en-fuentes-de-agua-en-zona-de-cartago
