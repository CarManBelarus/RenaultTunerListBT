#include <Arduino.h>
#include <atomic>
#include "freertos/ringbuf.h"
#include "BluetoothA2DPSink.h"
#include "SPDIFOut.h"
#include "TLCDCEmu.h"

BluetoothA2DPSink a2dp_sink;
SPDIFOut spdif_out;
TLCDCEmu cdcEmulator;

RingbufHandle_t audio_ringbuf;

// Атомарные флаги для кросс-ядерной очистки без блокировок
std::atomic<bool> flag_flush_buffer{false};
volatile unsigned long mute_audio_until = 0; 
volatile int pending_bt_cmd = 0; 

int parsed_title_num = 0;

void bt_command_task(void *pvParameters) {
    for(;;) {
        if (pending_bt_cmd != 0) {
            int cmd = pending_bt_cmd;
            pending_bt_cmd = 0;
            
            if (cmd == 1) a2dp_sink.play();
            else if (cmd == 2) a2dp_sink.pause();
            else if (cmd == 3) {
                // Жестко уничтожаем буфер и глушим эфир на 1.2 сек
                flag_flush_buffer.store(true, std::memory_order_release);
                mute_audio_until = millis() + 1200; 
                a2dp_sink.next();
            }
            else if (cmd == 4) {
                flag_flush_buffer.store(true, std::memory_order_release);
                mute_audio_until = millis() + 1200; 
                a2dp_sink.previous();
            }
        }
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

void spdif_data_stream(const uint8_t *data, uint32_t length) {
    // Входной фильтр жесткого мьюта. Уничтожает остатки старого трека.
    if (millis() < mute_audio_until) return; 
    xRingbufferSend(audio_ringbuf, data, length, 20 / portTICK_PERIOD_MS);
}

// Изохронная генерация BMC-потока (Core 1, Priority MAX)
void spdif_task_continuous(void *pvParameters) {
    size_t item_size;
    static uint8_t align_buf[4];
    static int align_count = 0;
    const size_t PREFILL_SIZE = 12288; 
    const size_t MAX_DRAIN_SIZE = 4096;
    bool prefilling = true;
    int16_t silence[2] = {0, 0}; 

    for(;;) {
        // Мгновенная зачистка старых PCM-данных
        if (flag_flush_buffer.load(std::memory_order_acquire)) {
            flag_flush_buffer.store(false, std::memory_order_release);
            void *flush_data;
            size_t flush_size;
            while ((flush_data = xRingbufferReceiveUpTo(audio_ringbuf, &flush_size, 0, MAX_DRAIN_SIZE)) != NULL) {
                vRingbufferReturnItem(audio_ringbuf, flush_data);
            }
            align_count = 0;
            prefilling = true; 
        }

        if (prefilling) {
            size_t available = 0;
            vRingbufferGetInfo(audio_ringbuf, NULL, NULL, NULL, NULL, &available);
            
            if (available >= PREFILL_SIZE) {
                prefilling = false; 
            } else {
                for(int i = 0; i < 64; i++) spdif_out.ConsumeSample(silence);
                continue; 
            }
        }

        uint8_t *data = (uint8_t *)xRingbufferReceive(audio_ringbuf, &item_size, 0);

        if (data != NULL && item_size > 0) {
            for (size_t i = 0; i < item_size; i++) {
                align_buf[align_count++] = data[i];
                if (align_count == 4) {
                    spdif_out.ConsumeSample((int16_t *)align_buf);
                    align_count = 0;
                }
            }
            vRingbufferReturnItem(audio_ringbuf, (void *)data);
        } else {
            // Поддержание I2S Carrier (защита PLL SAA7708H)
            for (int i = 0; i < 64; i++) {
                spdif_out.ConsumeSample(silence);
            }
        }
    }
}

// Нативный парсер метаданных (библиотека сама вызовет его при смене трека)
void avrc_metadata_callback(uint8_t id, const uint8_t *text) {
    Serial.printf("[AVRCP] Metadata ID 0x%x: %s\n", id, text);
    
    if (id == ESP_AVRC_MD_ATTR_TITLE) { 
        parsed_title_num = atoi((const char*)text); 
        if (parsed_title_num > 0) {
            cdcEmulator.updateTrackFromMetadata(parsed_title_num);
        }
    } 
    else if (id == ESP_AVRC_MD_ATTR_TRACK_NUM) { 
        int track_num = atoi((const char*)text);
        if (track_num > 0) {
            cdcEmulator.updateTrackFromMetadata(track_num);
        } 
        else if (parsed_title_num > 0) {
            cdcEmulator.updateTrackFromMetadata(parsed_title_num);
        }
        parsed_title_num = 0; 
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=========================================");
    Serial.println("[SYSTEM] FIRMWARE V47: NATIVE METADATA & HARD MUTE");
    Serial.println("=========================================");

    audio_ringbuf = xRingbufferCreate(32768, RINGBUF_TYPE_BYTEBUF);

    spdif_out.SetBitsPerSample(16); 
    spdif_out.SetChannels(2);
    spdif_out.begin();
    
    xTaskCreatePinnedToCore(spdif_task_continuous, "spdif_task", 4096, NULL, configMAX_PRIORITIES - 1, NULL, 1);
    xTaskCreatePinnedToCore(bt_command_task, "bt_cmd_task", 2048, NULL, configMAX_PRIORITIES - 2, NULL, 0);
    delay(150); 

    cdcEmulator.init(); 

    unsigned long handshake_start = millis();
    while (cdcEmulator.CDC_CurrentState < WAIT_HU_VERSION && (millis() - handshake_start < 2000)) {
        cdcEmulator.talk();
        delay(2);
    }

    // ХАК I2S: Изоляция мьютексов
    a2dp_sink.set_i2s_port(I2S_NUM_1);

    // Защита от L2CAP-шторма Android
    a2dp_sink.set_event_queue_size(150); 
    
    // Нативная подписка: библиотека САМА стянет теги при смене трека!
    a2dp_sink.set_avrc_metadata_attribute_mask(ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_TRACK_NUM);
    a2dp_sink.set_avrc_metadata_callback(avrc_metadata_callback);
    
    a2dp_sink.set_volume_control(new A2DPNoVolumeControl());
    a2dp_sink.set_stream_reader(spdif_data_stream, false); 
    a2dp_sink.set_auto_reconnect(true, 10000); 
    
    a2dp_sink.start("Renault Laguna BT");
}

void loop() {
    cdcEmulator.talk();
    vTaskDelay(5 / portTICK_PERIOD_MS);
}