# esp32_estufa

Lectura de temperatura con `DS18B20` integrada en el firmware.

Este proyecto es independiente. No debe fusionarse dentro de `esp-claw-control-1`; la integracion con el maestro debe hacerse solo por `ESP-NOW`.

Cableado basico del `DS18B20`:
- `VCC` -> `3.3V`
- `GND` -> `GND`
- `DQ` -> pin de datos definido en `main/main.c`
- resistencia `4.7k` entre `DQ` y `3.3V`

Mapa de pines por target:

`ESP32-C3` actual:
- `GPIO3` -> bus `DS18B20`
- `GPIO4` -> rele
- `GPIO5` -> boton local
- `GPIO6` -> buzzer pasivo
- `GPIO7` -> PWM para motor `12V` con etapa externa

`ESP32` clasico:
- `GPIO4` -> bus `DS18B20`
- `GPIO16` -> rele
- `GPIO17` -> boton local
- `GPIO19` -> buzzer pasivo
- `GPIO18` -> PWM para motor `12V` con etapa externa

Nota:
- en `ESP32` clasico no usar `GPIO6` a `GPIO11`; esos pines quedan conectados a la memoria flash

Para usar dos sensores `DS18B20`, la mejor integracion es compartir el mismo bus `1-Wire`:

- ambos `VCC` a `3.3V`
- ambos `GND` a `GND`
- ambos `DQ` al mismo pin definido en `DS18B20_GPIO`
- una sola resistencia `4.7k` entre `DQ` y `3.3V`

El monitor serial muestra una linea periodica como:

`temperatura ds18b20 = 24.75 C`

Si queres usar otro pin, cambia `DS18B20_GPIO` en `main/main.c`.

## Motor 12V por PWM

Para el motor `12V / 0.14 A`, queda reservado `MOTOR_PWM_GPIO` como salida PWM.

Curva actual segun `t2_superior`:
- menor a `35 C` -> motor apagado
- entre `35 C` y `60 C` -> gradiente lineal proporcional a la temperatura, con duty minimo util para evitar que el motor se frene
- a `60 C` o mas -> velocidad maxima

Conexion recomendada con `IRLZ24N`:
- pin `MOTOR_PWM_GPIO` -> resistencia `100 ohm` a `220 ohm` -> `gate`
- `gate` -> resistencia `10k` a `GND`
- `source` -> `GND`
- `drain` -> negativo del motor
- positivo del motor -> `+12V`
- diodo flyback en paralelo con el motor
- `GND` de `12V` y `GND` del `ESP32` unidos

Nota:
- con `IRLZ24N` el manejo del motor queda mucho mas comodo que con `2N2222`
- para este motor, el `IRLZ24N` trabaja mucho mas holgado y con menos caida de tension

## Control local del rele

El rele se maneja solo desde este `ESP32`.

- `t1_frente` solo informa temperatura
- `t1_frente` no participa en la logica del rele
- `t2_superior` gobierna el rele automaticamente
- si `t2_superior` baja de `45.0 C`, el rele se enciende
- si `t2_superior` llega a `48.0 C` o mas, el rele se apaga

El maestro no puede forzar `on/off/toggle` del rele por `ESP-NOW`; esos intentos responden `err local_only`.

## Proteccion termica del rele

`t2_superior` actua como proteccion termica:

- si `t2_superior` llega a `48.0 C`, el rele se apaga
- luego queda bloqueado
- solo puede volver a encenderse cuando `t2_superior` baja a `45.0 C` o menos

## Rol del nodo

`esp32_estufa` actua como nodo remoto `ESP32` controlado por un maestro `ESP32-S3`.

- proyecto separado
- build separado
- flash separado
- integracion exclusiva por `ESP-NOW`

## Protocolo ESP-NOW actual

Comandos aceptados desde el maestro:

- `hola`
- `rele status`
- `relay status`

Respuestas del nodo:

- `ack desde <mac>`
- `status temp=24.75 relay=on`
- `status temp=na relay=off`
- `err local_only`
- `event relay=on`
- `event relay=off`
- `err invalid_cmd`

El nodo envia `status temp=... relay=...` al maestro:

- cuando recibe `hola`
- cuando recibe `relay status`
- cuando cambia el estado del rele
- cuando la temperatura cambia al menos `0.5 C`
- como heartbeat cada `30 s`

Si hay dos sensores en el mismo bus, el formato pasa a ser:

- `status t1_frente=24.75 t2_superior=23.50 relay=on`
- `status t1_frente=24.75 t2_superior=na relay=off`

## Que modificar en esp-claw-control-1

No modificar este repo desde aca. Solo tomar estas indicaciones en el maestro:

1. Agregar la MAC de esta placa como peer `ESP-NOW`.
2. Usar el mismo canal `ESP-NOW` configurado en este firmware.
3. Enviar `hola` para deteccion inicial.
4. Considerar como nodo valido una respuesta `ack desde <mac>`.
5. Usar `relay status` para consulta de estado.
6. Interpretar `event relay=on` y `event relay=off` como cambios remotos del rele.

## Build

Ejemplo:

```powershell
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
idf.py set-target esp32c3
idf.py build
idf.py flash monitor
```

Si migras a `ESP32` clasico:

```powershell
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

## Web local por Wi-Fi

Si queres entrar desde el celular en la misma red Wi-Fi:

- completa `WIFI_STA_SSID` y `WIFI_STA_PASSWORD` en `main/main.c`
- compila y flashea
- mira el monitor serial
- cuando conecte, vas a ver la IP local y entras desde el celular con `http://IP_DEL_ESP`
- tambien deberia responder por mDNS en `http://estufa.local/`

La pagina web permite ajustar:
- arranque del motor
- temperatura maxima del motor
- PWM minimo del motor
- corte y rearme del rele
- umbral del buzzer
- habilitar o deshabilitar el buzzer

Nota:
- si usas `ESP-NOW` al mismo tiempo, el nodo y el maestro deben convivir en el mismo canal Wi-Fi
# estufa_pc
