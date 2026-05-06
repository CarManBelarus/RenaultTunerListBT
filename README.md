# Renault Tuner List Bluetooth A2DP Emulator

A mathematically deterministic, lock-free Bluetooth A2DP receiver and CD-changer emulator for Renault/VDO Dayton Tuner List head units (up to 2005). Designed specifically to interface directly with the Philips **SAA7708H** Digital Signal Processor via S/PDIF, bypassing analog signal degradation entirely.

## Abstract

Bridging modern asynchronous Bluetooth IoT stacks (like Android 15 L2CAP/AVRCP) with legacy synchronous automotive silicon presents fundamental architectural conflicts. Standard ESP32 A2DP implementations fail in this environment due to RTOS priority inversion, I2S hardware spinlock contention, and S/PDIF phase-lock loop (PLL) degradation during buffer underruns. 

This firmware implements aggressive hardware-level maneuvering to ensure zero-jitter *Biphase Mark Code (BMC)* generation, seamless steering wheel control integration, and native ID3 tag extraction for the Renault AFFA dashboard display.

## Architectural Triumphs

* **I2S Mutex Isolation (Phantom Porting):** Real-time S/PDIF generation requires maximum FreeRTOS priority (`configMAX_PRIORITIES - 1`). Standard Bluetooth libraries attempt to dynamically configure hardware I2S sample rates, leading to fatal mutual spinlock blocking on the I2S hardware bus. We trick the Bluedroid stack into configuring a phantom **I2S_NUM_1** port, entirely eliminating RTOS priority inversion. The physical S/PDIF stream remains locked to **I2S_NUM_0** and is driven by an uninterrupted, zero-latency task loop.
* **BMC Clock Recovery Engine:** The SAA7708H DSP relies on the continuous transmission of the S/PDIF stream to maintain its PLL lock. A single `vTaskDelay` during audio starvation drops the clock, muting the head unit. This firmware utilizes a consumer-driven ring buffer drain mechanism that mathematically pads `silence[2] = {0, 0}` to maintain the BMC carrier mechanically during track changes.
* **Adaptive Hard Mute & L2CAP Starvation Prevention:** Modern Android devices flood target receivers with `ESP_AVRC_RN_PLAY_POS_CHANGED` events. This overflows standard queues. We expanded the internal Bluedroid event queue to 150 slots to survive the initial L2CAP storm. Furthermore, a rigid 1200ms `mute_audio_until` blackout is imposed upon receiving `0x17` (Next Track) commands. This simulates the physical latency of a mechanical CD swap, ensuring L2CAP buffers are purged before re-engaging the I2S DMA payload.

## Hardware Integration

The system requires an ESP32 operating in an SMP (Symmetric Multiprocessing) environment. 

### Pinout Configuration
| Signal | ESP32 Pin | Renault ISO C (Mini-ISO) Pin | Description |
| :--- | :--- | :--- | :--- |
| **UART RX** | GPIO 16 | Pin 13 (TX from Radio) | 9600 baud, 8E1, Hardware Inverted |
| **UART TX** | GPIO 15 | Pin 14 (RX to Radio) | 9600 baud, 8E1, Hardware Inverted |
| **S/PDIF OUT** | GPIO 22 | DSP S/PDIF IN | Directly coupled via capacitor filter |
| **GND** | GND | Pin 15 (Signal Ground) | Common Ground |

*Note: UART signals must be electrically leveled/inverted depending on your specific optocoupler or transceiver hardware. The ESP32 UART peripheral is configured for internal logic inversion (`UART_SIGNAL_RXD_INV | UART_SIGNAL_TXD_INV`).*

## Firmware Compilation

This project is structured for PlatformIO. It utilizes a heavily modified initialization flow of the `ESP32-A2DP` library.

### `platformio.ini`
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
board_build.partitions = huge_app.csv

lib_deps =
    https://github.com/pschatzmann/ESP32-A2DP.git
```

## System State Machine

The `TLCDCEmu` class emulates the strict Master-Slave polling requirements of the VDO Dayton protocol.
1. `WAIT_BOOT` / `BOOT_SEQUENCE`: Negotiates CD-Changer presence upon radio power-on.
2. `OPERATE_PLAYING`: Periodically transmits `0x47` packets containing BCD-encoded track times and numbers. 
3. `AVRCP` Interception: Modifies the BCD payload dynamically based on `ESP_AVRC_MD_ATTR_TRACK_NUM` or `ESP_AVRC_MD_ATTR_TITLE` (for audiobooks) received from the paired smartphone.

## License
MIT License
