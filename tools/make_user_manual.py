#!/usr/bin/env python3
"""Generate the TX3000AC user manual PDF (Russian)."""

from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    Paragraph,
    Preformatted,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)

FONT_SETS = [
    (Path("/usr/share/fonts/truetype/dejavu"),
     ("DejaVuSans.ttf", "DejaVuSans-Bold.ttf", "DejaVuSansMono.ttf")),
    (Path("/System/Library/Fonts/Supplemental"),
     ("Arial.ttf", "Arial Bold.ttf", "Courier New.ttf")),
]
for directory, filenames in FONT_SETS:
    if all((directory / name).is_file() for name in filenames):
        for alias, filename in zip(("DejaVu", "DejaVu-Bold", "DejaVuMono"), filenames):
            pdfmetrics.registerFont(TTFont(alias, str(directory / filename)))
        break
else:
    raise RuntimeError("Install DejaVu fonts (fonts-dejavu-core on Linux) or use macOS system fonts")
pdfmetrics.registerFontFamily("DejaVu", normal="DejaVu", bold="DejaVu-Bold", italic="DejaVu", boldItalic="DejaVu-Bold")

TITLE = ParagraphStyle("Title", fontName="DejaVu-Bold", fontSize=20,
                       leading=24, spaceAfter=4,
                       textColor=colors.HexColor("#12355b"))
SUBTITLE = ParagraphStyle("Subtitle", fontName="DejaVu", fontSize=11,
                          textColor=colors.HexColor("#555555"), spaceAfter=10)
H1 = ParagraphStyle("H1", fontName="DejaVu-Bold", fontSize=13.5,
                    spaceBefore=12, spaceAfter=5, keepWithNext=True,
                    textColor=colors.HexColor("#12355b"))
BODY = ParagraphStyle("Body", fontName="DejaVu", fontSize=10.2,
                      leading=14, spaceAfter=5)
MONO = ParagraphStyle("Mono", fontName="DejaVuMono", fontSize=8.8,
                      leading=12, backColor=colors.HexColor("#f2f2f2"),
                      borderPadding=6, spaceAfter=6)
CELL = ParagraphStyle("Cell", fontName="DejaVu", fontSize=9.3, leading=12)
CELLB = ParagraphStyle("CellB", fontName="DejaVu-Bold", fontSize=9.3,
                       leading=12)


def table(data, widths, header=True):
    style = [
        ("GRID", (0, 0), (-1, -1), 0.5, colors.HexColor("#999999")),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 5),
        ("RIGHTPADDING", (0, 0), (-1, -1), 5),
        ("TOPPADDING", (0, 0), (-1, -1), 3.5),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 3.5),
    ]
    if header:
        style.append(("BACKGROUND", (0, 0), (-1, 0),
                      colors.HexColor("#dce6f1")))
    return Table(data, colWidths=widths, style=TableStyle(style),
                 hAlign="LEFT", repeatRows=1 if header else 0)


def p(text, style=BODY):
    return Paragraph(text, style)


def c(text, bold=False):
    return Paragraph(text, CELLB if bold else CELL)


story = [
    p("TX3000AC — контроллер видеопередатчика", TITLE),
    p("ESP32-S3 · MAVLink RC11/RC12 · SmartAudio · веб-интерфейс", SUBTITLE),

    p("Контроллер управляет мощностью и частотой видеопередатчика TX3000AC "
      "по шине SmartAudio: автоматически по каналам RC11/RC12 от полётного "
      "контроллера (MAVLink) или вручную через веб-интерфейс. Каждое "
      "запрошенное изменение также отправляется сообщением MAVLink STATUSTEXT "
      "(VTX request). Это запрос, а не подтверждение применения настройки. "
      "Алгоритма мощности по расстоянию в прошивке нет.", BODY),

    p("1. Подключение", H1),
    p("Шина SmartAudio однопроводная (half-duplex): один вывод SA "
      "передатчика служит и входом команд от ESP32, и выходом ответов VTX. "
      "Схема подключения (по schema.md):", BODY),
    Preformatted(
        "ESP32 GPIO17 (TX/RX) -- 1 кОм -- VTX SA\n"
        "ESP32 GND ------------------- VTX GND",
        MONO),
    p("Передача и приём SmartAudio используют GPIO17. GPIO44 не нужен. "
      "Резистор 1 кОм в линии SA обязателен: после передачи ESP32 "
      "отпускает вывод, чтобы передатчик мог ответить.", BODY),
    p("На время настройки отключите SmartAudio-провод полётного "
      "контроллера от SA-пада, если он туда подключён.", BODY),
    table([
        [c("Сигнал", True), c("Подключение", True)],
        [c("FC T1 (TX полётника)"), c("GPIO9 через 1 кОм")],
        [c("FC R1 (RX полётника)"), c("GPIO10 через 1 кОм")],
        [c("VTX SmartAudio"), c("GPIO17 через 1 кОм")],
        [c("GND полётника и VTX"), c("общая земля с ESP32")],
    ], [70 * mm, 100 * mm]),
    Spacer(1, 4),

    p("2. Первый запуск и веб-интерфейс", H1),
    p("После прошивки ESP32 поднимает точку доступа Wi-Fi:", BODY),
    table([
        [c("Сеть (SSID)", True), c("Пароль", True), c("Адрес", True)],
        [c("TX3000AC"), c("robossembler"),
         c("http://192.168.4.1/ или http://tx3000ac.local/")],
    ], [45 * mm, 45 * mm, 80 * mm]),
    Spacer(1, 4),
    p("На странице доступны:", BODY),
    p("• <b>AUTO: MAVLink RC11</b> — мощность задаётся каналом RC11 "
      "(режим по умолчанию).<br/>"
      "• <b>Кнопки 25…3000 mW</b> — ручной выбор мощности (игнорирует RC11 "
      "и таймаут телеметрии).<br/>"
      "• <b>Band / Channel + Set frequency</b> — смена бэнда и канала "
      "передатчика.<br/>"
      "• <b>VTX reported state</b> — фактические частота и мощность, "
      "которые сообщает сам передатчик (опрос SmartAudio GET_SETTINGS "
      "каждые 2 секунды). По этому блоку видно, применилась ли команда. "
      "Данные считаются устаревшими через 6 секунд без ответа. "
      "Заданная мощность хранится отдельно; неприменённые команды повторяются "
      "с интервалом не менее 2 секунд между попытками. "
      "Страница обновляет данные в фоне, не перезагружаясь.", BODY),

    p("3. Управление по RC-каналам", H1),
    p("RC11 — мощность. Диапазон 1000–2000 мкс разбит на шесть равных "
      "зон:", BODY),
    table([
        [c("RC11, мкс", True), c("Мощность", True)],
        [c("800–1166"), c("25 mW")],
        [c("1167–1333"), c("250 mW")],
        [c("1334–1499"), c("500 mW")],
        [c("1500–1666"), c("1000 mW")],
        [c("1667–1833"), c("2000 mW")],
        [c("1834–2200"), c("3000 mW")],
    ], [60 * mm, 60 * mm]),
    Spacer(1, 4),
    p("RC12 — частота. Верхнее положение (1800–2200 мкс) — следующий "
      "бэнд, нижнее (800–1200 мкс) — следующий канал; при удержании "
      "переключение повторяется раз в секунду.", BODY),
    p("Защита AUTO: при старте, отсутствии RC с момента запуска, "
      "невалидном RC11 либо таймауте более 2 секунд запрашивается 25 mW. "
      "Команда повторяется, если VTX не подтверждает значение. "
      "Фактическое снижение зависит от исправности связи и передатчика. "
      "В ручном режиме таймаут RC не меняет мощность; RC12 продолжает "
      "управлять частотой. Источник RC фиксируется по первому корректному "
      "heartbeat автопилота до перезагрузки.", BODY),

    p("4. Таблица частот (МГц)", H1),
]

freq_header = [c("Бэнд", True)] + [c(str(i), True) for i in range(1, 9)]
freq_rows = [
    ("A", [5865, 5845, 5825, 5805, 5785, 5765, 5745, 5725]),
    ("B", [5733, 5752, 5771, 5790, 5809, 5828, 5847, 5866]),
    ("E", [5705, 5685, 5665, 5645, 5885, 5905, 5925, 5945]),
    ("F", [5740, 5760, 5780, 5800, 5820, 5840, 5860, 5880]),
    ("R", [5658, 5695, 5732, 5769, 5806, 5843, 5880, 5917]),
]
freq_data = [freq_header] + [
    [c(band, True)] + [c(str(freq)) for freq in freqs]
    for band, freqs in freq_rows
]
story.append(table(freq_data, [16 * mm] + [19.25 * mm] * 8))

story += [
    Spacer(1, 4),
    p("5. Настройки (файл src/config.h)", H1),
    p("Все основные параметры вынесены в файл src/config.h. После его "
      "изменения пересоберите и перепрошейте контроллер "
      "(pio run -t upload).", BODY),
    table([
        [c("Параметр", True), c("По умолчанию", True), c("Описание", True)],
        [c("CFG_AP_SSID"), c("\"TX3000AC\""),
         c("Имя Wi-Fi сети (точки доступа)")],
        [c("CFG_AP_PASSWORD"), c("\"robossembler\""),
         c("Пароль Wi-Fi, минимум 8 символов")],
        [c("CFG_AP_HOSTNAME"), c("\"tx3000ac\""),
         c("mDNS-имя: http://<hostname>.local/")],
        [c("CFG_MAV_RX_PIN / CFG_MAV_TX_PIN"), c("9 / 10"),
         c("Выводы UART к полётному контроллеру")],
        [c("CFG_MAV_BAUD"), c("115200"),
         c("Скорость MAVLink UART; должна совпадать с настройкой "
           "порта полётника")],
        [c("CFG_SMARTAUDIO_PIN"), c("17"),
         c("Вывод шины SmartAudio")],
        [c("CFG_RC_VALID_MIN / MAX"), c("800 / 2200"),
         c("Окно валидных значений RC, мкс; вне его — «нет сигнала»")],
        [c("CFG_RC11_ZONE_MIN / MAX"), c("1000 / 2000"),
         c("Диапазон RC11, мкс; делится на 6 равных зон мощности")],
        [c("CFG_RC12_BAND_MIN / MAX"), c("1800 / 2200"),
         c("Диапазон RC12, мкс: переключение бэнда")],
        [c("CFG_RC12_CHANNEL_MIN / MAX"), c("800 / 1200"),
         c("Диапазон RC12, мкс: переключение канала")],
        [c("CFG_RC12_REPEAT_MS"), c("1000"),
         c("Период повтора шагов RC12 при удержании, мс")],
        [c("CFG_RC_TIMEOUT_MS"), c("2000"),
         c("Таймаут потери RC до перехода на 25 mW, мс")],
        [c("CFG_POWER_MW"), c("25, 250, 500, 1000, 2000, 3000"),
         c("Таблица мощностей, мВт (ровно 6 уровней)")],
    ], [52 * mm, 42 * mm, 76 * mm]),
    Spacer(1, 4),

    p("6. Сборка и прошивка", H1),
    Preformatted(
        "# Сборка (зависимости загрузятся автоматически)\n"
        "pio run -e esp32-s3\n"
        "pio run -e esp32-s3-no-web\n"
        "\n"
        "# Прошивка (ESP32 подключена по USB)\n"
        "pio run -e esp32-s3 -t upload\n"
        "\n"
        "# Монитор логов\n"
        "pio device monitor",
        MONO),
    p("В логе USB-serial (115200) выводятся только рабочие события: "
      "запуск точки доступа, изменение мощности/канала и фактическое "
      "состояние, сообщаемое передатчиком.", BODY),
]

doc = SimpleDocTemplate(
    str(Path(__file__).resolve().parents[1] / "TX3000AC_manual.pdf"),
    pagesize=A4,
    leftMargin=20 * mm,
    rightMargin=20 * mm,
    topMargin=18 * mm,
    bottomMargin=16 * mm,
    title="TX3000AC — контроллер видеопередатчика",
    author="distance_change_force",
)
doc.build(story)
print("TX3000AC_manual.pdf written")
