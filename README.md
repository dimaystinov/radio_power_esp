# TX3000AC power controller — ESP32-S3

Controls a TX3000AC video transmitter using MAVLink RC11/RC12 from a flight
controller, or a Wi-Fi web interface. There is no GPS/distance-based power
algorithm: RC11 selects the power level directly.

## Готовая прошивка: ESP32-S3-0.42OLED

Скачайте бинарник из [последнего релиза](https://github.com/dimaystinov/radio_power_esp/releases/latest).
Поддерживается **01Space ESP32-S3-0.42OLED**: ESP32-S3, flash 4 MB, встроенный
OLED SSD1306 72×40, SDA GPIO41, SCL GPIO40, I2C `0x3C`.
Распиновка и драйвер соответствуют [примеру производителя](https://github.com/01Space/ESP32-S3-0.42OLED/blob/main/GraphicsTest/GraphicsTest.ino).
Для ESP32-C3 и плат с другими дисплеями эти бинарники не предназначены.

| Файл релиза | Возможности |
|---|---|
| `radio_power_esp32s3_oled_web.bin` | OLED + Wi-Fi и веб-интерфейс (рекомендуется) |
| `radio_power_esp32s3_oled_no_web.bin` | OLED, без Wi-Fi/веб-интерфейса |

Оба файла — **объединённые образы для адреса `0x0`**. Загрузчик, таблица
разделов, boot_app0 и приложение уже внутри; отдельно скачивать их не нужно.
Выберите один вариант. В командах ниже показан вариант `web`; для второго
замените имя файла. Не путайте эти файлы с `.pio/build/.../firmware.bin`,
который содержит только приложение.

### Что показывает OLED

- Верхняя строка: `AUTO`, `MANUAL` или `SAFE` (AUTO без корректного RC).
- Крупные цифры и `mW`: последняя подтверждённая мощность VTX, а не запрос.
- `SET 3000mW`, например: желаемая мощность отличается или ещё не подтверждена.
- `VTX OK`: подтверждённая мощность совпадает с заданной.
- `--` и `NO VTX`: ответ ещё не получен либо старше 6 секунд.

Экран обновляется не чаще четырёх раз в секунду и работает без веб-интерфейса.
Если OLED не отвечает по I2C, управление VTX продолжается; повторная
инициализация экрана выполняется после перезагрузки. Параметры — в `src/config.h`;
для платы без дисплея можно собрать с `-D CFG_OLED_ENABLED=0`.

### Перед подключением

Используйте USB-кабель с передачей данных. Закройте serial monitor/Arduino IDE
и другие программы, занявшие порт. При необходимости войдите в загрузчик:
удерживайте **B/BOOT**, нажмите и отпустите **R/RESET**, затем отпустите B.
USB-порт в режиме загрузчика может получить другое имя/номер.

### Прошивка на macOS

Установите Python 3 с [python.org](https://www.python.org/downloads/macos/), если
команда `python3` отсутствует. Сохраните выбранный `.bin` и `SHA256SUMS` из
одного релиза в `Downloads`, затем в Terminal:

```sh
cd ~/Downloads
python3 -m venv .venv-esp-flash
.venv-esp-flash/bin/python -m pip install esptool==4.9.0
.venv-esp-flash/bin/python -m serial.tools.list_ports
shasum -a 256 radio_power_esp32s3_oled_web.bin
```

Сравните хеш с соответствующей строкой `SHA256SUMS`. Найдите порт платы
(обычно `/dev/cu.usbmodem...`) и замените пример `/dev/cu.usbmodem1101`:

```sh
.venv-esp-flash/bin/python -m esptool --chip esp32s3 --port /dev/cu.usbmodem1101 --baud 460800 write_flash 0x0 radio_power_esp32s3_oled_web.bin
```

### Прошивка на Windows

Установите Python 3 с [python.org](https://www.python.org/downloads/windows/)
вместе с Python Launcher (`py`). Сохраните выбранный `.bin` и `SHA256SUMS` из
одного релиза в «Загрузки». Откройте PowerShell:

```powershell
cd "$env:USERPROFILE\Downloads"
py -3 -m venv .venv-esp-flash
.\.venv-esp-flash\Scripts\python.exe -m pip install esptool==4.9.0
.\.venv-esp-flash\Scripts\python.exe -m serial.tools.list_ports
Get-FileHash .\radio_power_esp32s3_oled_web.bin -Algorithm SHA256
```

Сравните хеш с `SHA256SUMS`. Номер порта также виден в «Диспетчер устройств →
Порты (COM и LPT)». Замените пример `COM5` своим номером:

```powershell
.\.venv-esp-flash\Scripts\python.exe -m esptool --chip esp32s3 --port COM5 --baud 460800 write_flash 0x0 .\radio_power_esp32s3_oled_web.bin
```

Активация venv не нужна: команды используют его Python напрямую, поэтому менять
политику выполнения PowerShell не требуется. Если `py` отсутствует, используйте
`python` вместо `py -3` при создании окружения. Для штатного USB CDC ESP32-S3
на Windows 10/11 обычно используется системный драйвер; не заменяйте COM-драйвер
на WinUSB через Zadig для этого способа прошивки.

### После записи и устранение проблем

Дождитесь сообщения проверки записанных данных (`Hash of data verified`).
Нажмите R/RESET **без удержания BOOT**, если приложение не запустилось автоматически.
До ответа VTX экран показывает `SAFE NO VTX`, `-- mW` и `SET 25mW`.
Для варианта `web` подключитесь к `TX3000AC`, пароль `robossembler`, и откройте
http://192.168.4.1/.

Если соединение зависло на `Connecting...`, повторите вход в загрузчик и снова
проверьте номер порта. При ошибках передачи уменьшите `--baud` до `115200`,
попробуйте другой кабель/USB-порт. Не используйте `--force`, если esptool
сообщает о другом типе чипа. Обычная прошивка не требует отдельного `erase_flash`.

Команды приведены для esptool **4.9.0** и его синтаксиса `write_flash`;
[документация Espressif](https://docs.espressif.com/projects/esptool/en/release-v4/esp32/esptool/basic-commands.html).
Бинарники проверены сборкой и тестами, но не прошивались на физическую плату.

## Wiring

| Flight controller / VTX | ESP32-S3 |
|---|---|
| FC T1 | GPIO9 through 1 kOhm |
| FC R1 | GPIO10 through 1 kOhm |
| TX3000AC SmartAudio | GPIO17 through 1 kOhm |
| FC and VTX GND | ESP GND |

SmartAudio is a bidirectional one-wire bus. Both TX and RX use GPIO17;
GPIO44 is not used. The firmware releases GPIO17 after each transmission.
Disconnect any other SmartAudio controller from this bus. See [schema.md](schema.md).

## Control and failsafe

The firmware binds to the first valid autopilot heartbeat (component 1) until
reboot and accepts RC_CHANNELS only from that system/component. It requests
RC_CHANNELS at 10 Hz over a 115200-baud UART.

| RC11 (microseconds) | Requested power |
|---|---|
| 800–1166 | 25 mW |
| 1167–1333 | 250 mW |
| 1334–1499 | 500 mW |
| 1500–1666 | 1000 mW |
| 1667–1833 | 2000 mW |
| 1834–2200 | 3000 mW |

In AUTO, startup/no RC data, an invalid RC11 sample, or more than 2 seconds
without valid RC11 requests 25 mW. Switching back to AUTO uses only a fresh,
valid RC11 value; otherwise it requests 25 mW. Pending high-power repetitions
are cancelled when the target changes. Hardware application still requires a
working SmartAudio bus and a responding VTX.

RC12 held at 1800–2200 steps to the next band; 800–1200 steps to the next
channel. The first step occurs after 1 second, then repeats each second.
1500 stops stepping. Manual power mode does not disable RC12 frequency control.

Desired power/frequency and VTX-reported settings are separate. Unconfirmed
settings are retried at intervals of at least 2 seconds between attempts.
GET_SETTINGS normally runs every 2 seconds, with extra queries after commands
and during baud discovery. Readback expires after 6 seconds without a reply.
Commands use an unlock followed by three transmissions; the 200/120 ms gaps
and reply timeouts are scheduled across loop iterations. Only UART frame
transmission is synchronous (about 14–19 ms per SmartAudio frame).

STATUSTEXT messages say **VTX request**, not confirmation of applied power.
GET_SETTINGS readback is the source for reported state. Parsing is specific to
the observed TX3000AC reply (AA 55 11 0E, zero-based channel/power); it is not a
general SmartAudio implementation for arbitrary transmitters.

## IP-адрес и просмотр данных в браузере

Этот раздел относится к прошивке **`radio_power_esp32s3_oled_web.bin`**.
Вариант `no_web` не создаёт Wi-Fi-сеть и недоступен в браузере.

### Как устроена сеть

ESP32 сама создаёт точку доступа **`TX3000AC`**. Домашний роутер, интернет,
облачный сервер и SIM-карта для неё не нужны. Подключённый компьютер или телефон
автоматически получает локальный IP-адрес по DHCP. Адрес самой платы по умолчанию —
**`192.168.4.1`**; это адрес внутри Wi-Fi-сети платы, а не публичный адрес в интернете.
Прошивка не подключается к вашему домашнему роутеру в качестве Wi-Fi-клиента.

```text
Браузер телефона/компьютера
    | Wi-Fi: TX3000AC
    v
ESP32: http://192.168.4.1/
    | SmartAudio
    v
Видеопередатчик TX3000AC
```

### Как открыть страницу

1. Подайте питание на ESP32 с прошивкой `web`, дождитесь загрузки.
2. В списке Wi-Fi на macOS, Windows или телефоне выберите **TX3000AC**.
3. Введите пароль по умолчанию **robossembler**.
4. Если система пишет «Без интернета», оставьте подключение к этой сети:
   это нормально, поскольку плата обслуживает только локальную страницу.
5. В адресную строку браузера введите **http://192.168.4.1/**.
   Используйте `http://`, без `https://`; это адрес страницы, а не поисковый запрос.

Альтернативный адрес — **http://tx3000ac.local/**. Имя `.local` разрешается
через mDNS и зависит от поддержки в системе/сети. Если оно не открывается,
используйте числовой IP `192.168.4.1`.

### Что видно на странице

| Поле | Значение |
|---|---|
| Mode | Автоматическое управление RC11 или ручное через веб |
| RC11 | Последнее значение канала в микросекундах; 0 при отсутствии/невалидности данных в AUTO |
| Requested power | Заданная мощность — значение, которое прошивка пытается установить |
| VTX reported state | Мощность и частота, полученные из ответа передатчика |
| Updated … s ago | Возраст последнего ответа VTX |

Страница сама запрашивает данные **раз в 2 секунды**, перезагружать её вручную
не требуется. SmartAudio опрашивается отдельно; при отсутствии/устаревании ответа
блок VTX сообщает `No recent reply from the VTX`. Заданная мощность при этом
может отображаться: это ещё не доказательство, что VTX её применил.
Если браузер потерял соединение, последний текст может остаться на странице;
проверьте подключение к Wi-Fi и обновите страницу.

Кнопки **25…3000 mW** включают ручной режим мощности: RC11 и его таймаут
перестают влиять на мощность до нажатия **AUTO: MAVLink RC11**.
Выбор **Band / Channel → Set frequency** меняет частоту. RC12 продолжает
управлять частотой и при ручном режиме мощности.

### Данные без интерфейса: /status

В той же Wi-Fi-сети откройте **http://192.168.4.1/status** — браузер покажет
JSON с текущими данными. Для обновления JSON-страницы нажмите Refresh;
автоматический опрос реализован на основной странице `/`.

Основные поля: `mode` (режим), `rc11` (канал), `req` (заданная мощность),
`valid` (есть свежий ответ VTX). Если `valid: true`, также передаются `mw`
(подтверждённая мощность в мВт), `freq` (МГц), `band`, `ch`, `lvl`, `ver`
и `age` (секунды с последнего ответа). При `valid: false` этих дополнительных
полей нет; их нельзя трактовать как нулевую мощность. Этот URL можно использовать
для собственного мониторинга с компьютера, подключённого к Wi-Fi платы.

### Если страница не открывается

- Убедитесь, что выбрана сеть **TX3000AC**, а не домашний Wi-Fi, и записан вариант `web`.
- Проверьте адрес **http://192.168.4.1/**. При необходимости используйте его вместо `.local`.
- Оставьте получение IP по DHCP/«Автоматически». Плата и клиент должны быть в её локальной сети.
- Проверьте, не блокирует ли локальную сеть VPN или автоматическое переключение телефона на другую сеть.
- Перезапустите плату и подключитесь заново. В USB-логе прошивки выводятся IP и адрес страницы:
  `pio device monitor` (115200 бод; закройте монитор перед перепрошивкой).

Пароль Wi-Fi ограничивает доступ к сети; отдельного логина на HTTP-странице нет.
Имя сети, пароль и mDNS-имя меняются в `src/config.h` с последующей пересборкой.
Доступ с другого компьютера возможен после его подключения к Wi-Fi платы;
из домашней сети или интернета этот адрес сам по себе недоступен.

## Build and upload

Install Python 3 and PlatformIO Core. The ESP32 platform and MAVLink headers
are pinned in `platformio.ini`; no machine-specific include paths are needed.
The first build downloads dependencies.

```sh
python3 -m pip install platformio==6.1.18
pio run -e esp32-s3
pio run -e esp32-s3-no-web
pio run -e esp32-s3 -t upload
pio device monitor
```

Upload only the environment you intend to use. `esp32-s3-no-web` omits Wi-Fi,
mDNS and the HTTP UI. Builds appear in `.pio/build/<environment>/`.

## Tests

```sh
python3 -m unittest discover -s tests -v
```

Tests require a C++17 compiler and Python 3, with no Python test dependencies.
They compile the full firmware source against simulated serial/clock/HTTP
adapters and exercise control, failsafe, SmartAudio scheduling and parsing.
MAVLink serialization is stubbed in host firmware tests; both real ESP32 builds
validate integration with the pinned headers. Utility tests inspect actual
MAVLink override frames, including cleanup. Tests do not verify electrical
signals, VTX behavior, RF power, or OSD display on hardware.

GitHub Actions runs the host tests and both firmware builds.

## RC test utilities

Linux/macOS, with the flight controller connected by USB:

```sh
python3 tools/rc_override.py --port /dev/ttyACM1 --rc11 1500 --rc12 1500
python3 tools/rc_override_web.py --port /dev/ttyACM1
```

For the latter, open http://127.0.0.1:8080/. Both tools send override continuously;
closing the browser does not stop the server. Stop with Ctrl+C. RC11/RC12 are
released using 65534 (the MAVLink extension-channel release value). FC override
acceptance and timeout depend on the FC configuration.

The console/log diagnostic scripts additionally require `pymavlink`.
To regenerate the Russian PDF manual, install `reportlab` and run
`python3 tools/make_user_manual.py`. The generator uses DejaVu fonts on Linux (`fonts-dejavu-core`) or system fonts on macOS.

## Hardware acceptance checks

Before relying on the firmware, check the actual TX3000AC: every power level,
frequency changes, unplugged RC UART, no RC since boot, AUTO/manual transitions,
VTX disconnect/reconnect, and receiver display. Confirm actual reported settings
and measure bus timing; a successful build is not an on-device test.

## Подготовка релиза из исходников

В чистом Git checkout, с PlatformIO 6.1.18 и esptool 4.9.0 в Python-окружении:

```sh
python3 -m unittest discover -s tests -v
pio run -e esp32-s3 -e esp32-s3-no-web
python3 tools/package_firmware.py --version v1.0.0
```

В `dist/` появятся два объединённых образа, `manifest.json`, `SHA256SUMS` и
README. Упаковщик проверяет совпадение каждого сегмента с исходным файлом сборки.
Адреса соответствуют конфигурации PlatformIO: `0x0` загрузчик, `0x8000` разделы,
`0xe000` boot_app0, `0x10000` приложение. GitHub Actions при push тега `v*`
повторяет тесты/сборку и публикует релиз только после загрузки всех пяти файлов.
