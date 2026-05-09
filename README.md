# Renault Tuner List Bluetooth A2DP Emulator

![Build Status](.github/badges/build.svg)
![License](.github/badges/license.svg)

**Апаратны мост паміж сучаснымі асінхроннымі Bluetooth IoT-стэкамі і радыёэлектроннай архітэктурай аўтамабіляў пачатку нулявых гадоў.** Гэтая прашыўка ператварае стандартны ESP32 у матэматычна дэтэрмінаваны, свабодны ад блакіровак эмулятар CD-чэйнджэра і A2DP-рэсівер для магнітол Renault/VDO Dayton Tuner List (да 2005 года выпуска). Галоўная архітэктурная перавага праекта заключаецца ў прамой інтэграцыі з лічбавым сігнальным працэсарам Philips *SAA7708H* праз інтэрфейс S/PDIF. Гэта дазваляе цалкам абысці стадыю лічбава-аналагавага і аналагава-лічбавага пераўтварэнняў, выключаючы любую дэградацыю гуку. Прадукт забяспечвае нулявы джытэр пры генерацыі *Biphase Mark Code*, бясшвоўную падтрымку кіравання з рулявога кола і натыўную трансляцыю ID3-тэгаў на прыборную панэль AFFA.

## Hardware Bill of Materials (BOM)

Спецыфікацыя апаратнага забеспячэння сфарміравана ў выглядзе машыначытэльнага YAML-блока для гарантыі дакладнай рэплікацыі прылады без неабходнасці звароту да неструктураваных тэкставых апісанняў.

```yaml
hardware_bom:
  mcu:
    name: "ESP32 Development Board"
    architecture: "Xtensa Dual-Core 32-bit LX6"
    notes: "Strictly requires SMP support. Single-core variants (e.g. ESP32-S2) are physically incompatible with the locked I2S DMA pipeline."
  audio_dsp:
    name: "Philips SAA7708H"
    interface: "S/PDIF (Direct Coupling)"
    notes: "Requires a 100nF decoupling capacitor on the S/PDIF line to prevent DC bias injection into the DSP."
  logic_translation:
    name: "Optocoupler / Level Shifter"
    purpose: "Renault ISO C UART TX/RX translation"
    voltage_domain: "12V (Automotive) to 3.3V (ESP32)"
    notes: "Hardware inversion is handled internally by ESP32 UART registers. Optocoupler must strictly support 9600 baud without excessive slew rate degradation."
```

## Pinout Configuration & Hardware Integration

Сістэма патрабуе жорсткай ізаляцыі сілкавання для прадухілення траплення лічбавага шуму ад мікракантролера ў аўдыётракт аўтамабіля. Лагічныя сігналы UART павінны быць электрычна ўзгоднены праз оптапары, паколькі аўтамабільная шына аперыруе напружаннем 12V, што з'яўляецца фатальным для 3.3V-логікі ESP32.

| Signal | ESP32 Pin | Renault ISO C (Mini-ISO) Pin | Description |
| :--- | :--- | :--- | :--- |
| **UART RX** | GPIO 16 | Pin 13 (TX from Radio) | 9600 baud, 8E1, Hardware Inverted |
| **UART TX** | GPIO 15 | Pin 14 (RX to Radio) | 9600 baud, 8E1, Hardware Inverted |
| **S/PDIF OUT** | GPIO 22 | DSP S/PDIF IN | Directly coupled via 100nF capacitor filter |
| **GND** | GND | Pin 15 (Signal Ground) | Common Ground |

## Firmware Deployment

Праект цалкам інтэграваны ў экасістэму PlatformIO і пазбаўлены неабходнасці ручнога кіравання залежнасцямі. Для разгортвання асяроддзя і прашыўкі мікракантролера неабходна выканаць строгую паслядоўнасць аперацый у тэрмінале. Калі канфігурацыя *Continuous Integration* ужо настроена, гатовы бінарны файл заўсёды даступны ў раздзеле *Releases* дадзенага рэпазыторыя.

1. Усталяваць PlatformIO Core праз пакетны менеджар Python: `pip install platformio`.
2. Кланаваць рэпазыторый у лакальнае асяроддзе: `git clone <repository_url>`.
3. Ініцыялізаваць зборку і прашыўку падлучанай платы: `pio run -t upload -e esp32dev`.

## Architectural Triumphs

Асноўны канфлікт інтэграцыі ўзнікае з-за спробы аб'яднаць асінхронную прыроду стэка L2CAP/AVRCP з жорсткімі патрабаваннямі да сінхроннасці з боку аўтамабільнага крэмнію. Стандартныя рэалізацыі A2DP для ESP32 непазбежна правальваюцца з-за інверсіі прыярытэтаў *RTOS* і блакіровак спінлокаў на апаратнай шыне I2S, што прыводзіць да дэградацыі фазавай аўтападстройкі частаты (PLL) у S/PDIF прыёмніку падчас спусташэння буфераў.

* **I2S Mutex Isolation (Phantom Porting)**: Генерацыя S/PDIF у рэальным часе патрабуе максімальнага прыярытэту планавальніка FreeRTOS (`configMAX_PRIORITIES - 1`). Стандартныя бібліятэкі Bluetooth спрабуюць дынамічна канфігураваць частату дыскрэтызацыі апаратнага I2S, што выклікае фатальную ўзаемную блакіроўку. Мы падманваем стэк Bluedroid, прымушаючы яго ініцыялізаваць фантомны порт **I2S_NUM_1**, поўнасцю ізалюючы інверсію прыярытэтаў. Фізічны струмень S/PDIF застаецца жорстка прывязаным да **I2S_NUM_0** і кіруецца бесперапынным цыклам без затрымак.
* **BMC Clock Recovery Engine**: DSP *SAA7708H* абапіраецца на бесперапыннасць трансляцыі S/PDIF для ўтрымання захопу PLL. Адзіны выклік `vTaskDelay` падчас недахопу аўдыядадзеных выклікае скід гадзінніка і поўнае адключэнне гуку магнітолай. Гэтая прашыўка выкарыстоўвае механізм апаражнення колцавага буфера на баку спажыўца, які матэматычна дапаўняе струмень нулявымі сэмпламі `silence[2] = {0, 0}`, механічна падтрымліваючы апорную частату *BMC* падчас змены трэкаў.
* **Adaptive Hard Mute & L2CAP Starvation Prevention**: Сучасныя Android-прылады перапаўняюць мэтавыя рэсіверы штормам падзей `ESP_AVRC_RN_PLAY_POS_CHANGED`. Для выжывання сістэмы ўнутраная чарга падзей Bluedroid пашырана да 150 слотаў. Дадаткова ўкаранёны жорсткі блакаут `mute_audio_until` на 1200 мілісекунд пры атрыманні каманды `0x17` (Next Track). Гэта дакладна сімулюе фізічную латэнтнасць механічнай змены кампакт-дыска і гарантуе поўную ачыстку буфераў L2CAP да паўторнага запуску карыснай нагрузкі I2S DMA.

## System State Machine

Клас `TLCDCEmu` рэалізуе строгую эмуляцыю пратакола VDO Dayton па мадэлі Master-Slave, дэкадуючы і адказваючы на запыты магнітолы ў рэальным часе.

1. **WAIT_BOOT / BOOT_SEQUENCE**: Першасная ініцыялізацыя і ўзгадненне прысутнасці CD-чэйнджэра пры падачы сілкавання на магнітолу.
2. **OPERATE_PLAYING**: Цыклічная трансляцыя пакетаў `0x47`, якія ўтрымліваюць нумары трэкаў і часавыя адзнакі, закадаваныя ў фармаце *Binary-Coded Decimal (BCD)*.
3. **AVRCP Interception**: Дынамічная мадыфікацыя карыснай нагрузкі BCD на аснове метададзеных `ESP_AVRC_MD_ATTR_TRACK_NUM` або `ESP_AVRC_MD_ATTR_TITLE`, атрыманых ад спалучанага смартфона праз профіль AVRCP.

## License

Дадзены прадукт распаўсюджваецца на ўмовах ліцэнзіі MIT. Вы маеце поўнае права выкарыстоўваць, мадыфікаваць і інтэграваць гэты код у любыя сістэмы пры ўмове захавання арыгінальнага паведамлення аб аўтарскім праве.
