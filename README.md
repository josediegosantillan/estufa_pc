# esp32_estufa

Firmware ESP-IDF para un nodo remoto de estufa basado en ESP32/ESP32-C3.
Lee hasta dos sensores `DS18B20`, controla un rele local, regula un motor de
12 V por PWM, dispara un buzzer de advertencia y reporta estado por `ESP-NOW`.
Opcionalmente expone una pagina web local por Wi-Fi.

Este proyecto es independiente. No debe fusionarse dentro de `esp-claw-control-1`;
la integracion con el maestro debe hacerse solo por `ESP-NOW`.

## Rol del nodo

`esp32_estufa` actua como nodo remoto controlado por un maestro `ESP32-S3`.

- Proyecto separado
- Build separado
- Flash separado
- Integracion exclusiva por `ESP-NOW`
- Web local opcional solo para monitoreo y ajustes del nodo

## Hardware

Cableado basico del `DS18B20`:

- `VCC` -> `3.3V`
- `GND` -> `GND`
- `DQ` -> pin definido como `DS18B20_GPIO` en `main/app_board.h`
- Resistencia `4.7k` entre `DQ` y `3.3V`

Para usar dos sensores `DS18B20`, ambos comparten el mismo bus `1-Wire`:

- Ambos `VCC` a `3.3V`
- Ambos `GND` a `GND`
- Ambos `DQ` al mismo `DS18B20_GPIO`
- Una sola resistencia `4.7k` entre `DQ` y `3.3V`

Mapa de pines por target:

| Funcion | ESP32-C3 | ESP32 clasico |
| --- | --- | --- |
| Bus `DS18B20` | `GPIO3` | `GPIO4` |
| Rele | `GPIO4` | `GPIO16` |
| Boton local | `GPIO5` | `GPIO17` |
| Buzzer pasivo | `GPIO6` | `GPIO19` |
| PWM motor 12 V | `GPIO7` | `GPIO18` |

Nota para `ESP32` clasico: no usar `GPIO6` a `GPIO11`; esos pines quedan
conectados a la memoria flash.

## Configuracion fija

Los valores fijos del nodo estan en `main/app_state.c`:

- `EDGE_AGENT_MAC_1` y `EDGE_AGENT_MAC_2`: peers autorizados por `ESP-NOW`
- `ESPNOW_CHANNEL`: canal usado cuando no hay conexion Wi-Fi STA
- `WIFI_STA_SSID` y `WIFI_STA_PASSWORD`: credenciales para habilitar web local
- `DS18B20_SENSOR1_ROM`: ROM esperada para identificar `t1_frente`

Si el `SSID` queda vacio, la web local queda deshabilitada y el nodo trabaja
solo por `ESP-NOW` en `ESPNOW_CHANNEL`.

## Configuracion persistente

La configuracion operativa se carga desde NVS. Si no existe o es invalida, se
restauran defaults y se guardan de nuevo.

Defaults actuales:

| Parametro | Default | Uso |
| --- | ---: | --- |
| `motor_temp_off_c` | `35.0 C` | Temperatura desde la que empieza el PWM |
| `motor_temp_max_c` | `60.0 C` | Temperatura de PWM maximo |
| `motor_pwm_min_pct` | `35 %` | PWM minimo util al arrancar motor |
| `relay_cutoff_c` | `48.0 C` | Corte termico del rele |
| `relay_resume_c` | `47.0 C` | Rearme/encendido automatico del rele |
| `buzzer_warning_c` | `47.0 C` | Umbral de alarma sonora |
| `buzzer_disarm_c` | `44.0 C` | Rearme de alarma sonora |
| `buzzer_enabled` | `true` | Habilita/deshabilita buzzer |
| `buzzer_tone_type` | `0` | Tipo de sonido del buzzer |

Validaciones actuales:

- `motor_temp_max_c` debe ser mayor que `motor_temp_off_c`
- `relay_cutoff_c` no puede superar `60.0 C`
- `relay_resume_c` debe ser menor que `relay_cutoff_c`
- `motor_pwm_min_pct` no puede superar `100`
- `buzzer_disarm_c` debe ser menor que `buzzer_warning_c`

## Sensores y control

El firmware soporta hasta dos sensores:

- `t1_frente`: sensor identificado por `DS18B20_SENSOR1_ROM`
- `t2_superior`: segundo sensor detectado en el bus

Si hay un solo sensor, ese sensor gobierna motor, rele y buzzer. Si hay dos,
`t2_superior` gobierna motor, rele y buzzer, mientras `t1_frente` queda como
temperatura informativa.

El bus se escanea al iniciar y se reintenta si no se detectan sensores. La tarea
lee temperaturas cada `2 s`.

## Control del sistema

El sistema arranca apagado (`system_enabled = false`). Desde la web se puede
encender o apagar con `/system?state=1` o `/system?state=0`.

Cuando el sistema esta apagado:

- El motor queda en `0 %`
- El control automatico apaga el rele si estaba encendido
- No se aplican la curva de motor, el buzzer ni el control termico automatico

Cuando el sistema esta encendido:

- El motor puede habilitarse o deshabilitarse desde la web
- El rele se gobierna automaticamente por temperatura
- El buzzer avisa cuando se supera su umbral

## Motor 12 V por PWM

El motor usa PWM LEDC a `20 kHz`, resolucion de 10 bits, sobre `MOTOR_PWM_GPIO`.

Curva actual segun el sensor de control:

- Menor a `motor_temp_off_c` -> motor apagado
- Entre `motor_temp_off_c` y `motor_temp_max_c` -> gradiente lineal
- A `motor_temp_max_c` o mas -> velocidad maxima

Conexion recomendada con `IRLZ24N`:

- `MOTOR_PWM_GPIO` -> resistencia `100 ohm` a `220 ohm` -> `gate`
- `gate` -> resistencia `10k` a `GND`
- `source` -> `GND`
- `drain` -> negativo del motor
- Positivo del motor -> `+12V`
- Diodo flyback en paralelo con el motor
- `GND` de `12V` y `GND` del `ESP32` unidos

Nota: para este motor, el `IRLZ24N` trabaja mas holgado y con menos caida de
tension que una etapa con `2N2222`.

## Rele y proteccion termica

El rele se maneja localmente en este ESP32. El maestro no puede forzar
`on/off/toggle` por `ESP-NOW`; esos intentos responden `err local_only`.

Con el sistema encendido:

- Si la temperatura de control baja a `relay_resume_c` o menos, el rele enciende
- Si llega a `relay_cutoff_c` o mas, el rele se apaga
- Luego queda bloqueado por proteccion termica
- Solo vuelve a encender cuando baja a `relay_resume_c` o menos

El boton local alterna el estado del rele fisico y publica el evento por
`ESP-NOW`.

## Buzzer

El buzzer pasivo usa LEDC y se dispara de forma asincronica cuando la temperatura
de control llega a `buzzer_warning_c`.

El aviso se rearma cuando la temperatura baja a `buzzer_disarm_c` o menos.

Tipos de sonido:

- `0`: beeps cortos
- `1`: tono largo
- `2`: alarma doble

## Web local por Wi-Fi

Para entrar desde un celular o PC en la misma red:

1. Completar `WIFI_STA_SSID` y `WIFI_STA_PASSWORD` en `main/app_state.c`.
2. Compilar y flashear.
3. Mirar el monitor serial para ver la IP.
4. Abrir `http://IP_DEL_ESP/`.
5. Tambien deberia responder por mDNS en `http://estufa.local/`.

La pagina web permite:

- Ver `t1_frente`, `t2_superior`, rele, PWM, RSSI y uptime
- Encender/apagar el sistema
- Habilitar/deshabilitar motor
- Ajustar corte/rearme del rele
- Ajustar arranque, maximo y PWM minimo del motor
- Ajustar umbral, rearme, tipo y habilitacion del buzzer
- Restaurar defaults guardados en NVS
- Ver estado JSON desde `/status`

Endpoints HTTP:

| Endpoint | Descripcion |
| --- | --- |
| `/` | Panel web |
| `/status` | Estado JSON |
| `/set?...` | Guarda configuracion en NVS |
| `/set?reset=1` | Restaura defaults |
| `/motor?state=1` | Habilita motor |
| `/motor?state=0` | Deshabilita motor y fuerza PWM a 0 |
| `/system?state=1` | Enciende control automatico |
| `/system?state=0` | Apaga control automatico |

Ejemplo de `/status`:

```json
{
  "system_enabled": true,
  "motor_enabled": true,
  "relay": "on",
  "relay_overheat": false,
  "motor_pwm_pct": 35,
  "motor_pwm_duty": 358,
  "sensor": "t2_superior",
  "sensor_temp_c": 42.50,
  "t1_valid": true,
  "t2_valid": true,
  "t1_c": 24.75,
  "t2_c": 42.50,
  "rssi": -55,
  "uptime_s": 120
}
```

Nota: si se usa `ESP-NOW` al mismo tiempo que Wi-Fi STA, el maestro y este nodo
deben convivir en el mismo canal Wi-Fi.

## Protocolo ESP-NOW

Solo se aceptan mensajes desde las MAC configuradas como peers autorizados.

Comandos aceptados desde el maestro:

- `hola`
- `rele status`
- `relay status`

Comandos rechazados porque el rele es local:

- `rele on`
- `relay on`
- `rele off`
- `relay off`
- `rele toggle`
- `relay toggle`

Respuestas posibles:

- `ack desde <mac>`
- `status temp=24.75 relay=on`
- `status temp=na relay=off`
- `status t1_frente=24.75 t2_superior=23.50 relay=on`
- `status t1_frente=24.75 t2_superior=na relay=off`
- `event relay=on`
- `event relay=off`
- `err local_only`
- `err invalid_cmd`

El nodo envia estado:

- Cuando recibe `hola`
- Cuando recibe `relay status` o `rele status`
- Cuando cambia el estado del rele
- Cuando alguna temperatura cambia al menos `0.5 C`
- Como heartbeat cada `30 s`

## Que modificar en esp-claw-control-1

No modificar este repo desde aca. Solo tomar estas indicaciones en el maestro:

1. Agregar la MAC de esta placa como peer `ESP-NOW`.
2. Usar el mismo canal `ESP-NOW`/Wi-Fi que este firmware.
3. Enviar `hola` para deteccion inicial.
4. Considerar como nodo valido una respuesta `ack desde <mac>`.
5. Usar `relay status` o `rele status` para consulta de estado.
6. Interpretar `event relay=on` y `event relay=off` como cambios remotos del rele.
7. No intentar forzar el rele por ESP-NOW; el nodo respondera `err local_only`.

## Build

Ejemplo para `ESP32-C3`:

```powershell
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
idf.py set-target esp32c3
idf.py build
idf.py flash monitor
```

Ejemplo para `ESP32` clasico:

```powershell
$env:IDF_PATH='C:\esp\v5.5.4\esp-idf'
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

Dependencia declarada en `main/idf_component.yml`:

- `espressif/mdns`

## Archivos principales

- `main/app_board.h`: pines, labels y constantes de tiempos
- `main/app_state.c`: MACs, canal, Wi-Fi, ROM del sensor y estado global
- `main/app_config.c`: defaults, carga/guardado NVS y validacion
- `main/temperature_control.c`: DS18B20, control automatico y reportes
- `main/actuators.c`: PWM motor y buzzer
- `main/relay_control.c`: rele y boton local
- `main/network_services.c`: Wi-Fi, mDNS, web HTTP y ESP-NOW
