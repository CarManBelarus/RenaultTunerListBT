#include "TLCDCEmu.h"
#include "BluetoothA2DPSink.h"

extern BluetoothA2DPSink a2dp_sink;
extern volatile int pending_bt_cmd;


// ---------------------------------------------------------------------
// VDO / PHILIPS TUNER LIST PROTOCOL CONSTANTS
// ---------------------------------------------------------------------

const uint8_t ACK[1] = {0xC5};

const uint8_t CDC_Payload_WaitBoot[3] = {0x11, 0x61, 0x06};
const uint8_t CDC_Payload_BootSequence_1[3] = {0x15, 0x00, 0x25};
const uint8_t CDC_Payload_BootSequence_2[6] = {0x20, 0x09, 0x03, 0x09, 0x05, 0x01};
const uint8_t CDC_Payload_BootSequence_3[2] = {0x25, 0x03};
const uint8_t CDC_Payload_BootSequence_4[5] = {0x26, 0x15, 0xFC, 0x80, 0x80}; // 6 Discs

const uint8_t CDC_Payload_ConfirmHuVersion[2] = {0x62, 0x01};

const uint8_t CDC_Payload_ConfirmPlay[2]    = {0x21, 0x05};
const uint8_t CDC_Payload_ConfirmPause[2]   = {0x21, 0x03};
const uint8_t CDC_Payload_ConfirmStandby[2] = {0x21, 0x01};

// ВНЕДРЕНО: Восстановлены критические пакеты 0x27 (Без них магнитола кидает STOP)
const uint8_t CDC_Payload_ConfirmSongChange_1[4] = {0x27, 0x80, 0x01, 0x22};
const uint8_t CDC_Payload_ConfirmSongChange_Searching[2] = {0x21, 0x0A};
const uint8_t CDC_Payload_ConfirmSongChange_Playing[2]   = {0x21, 0x05};
const uint8_t CDC_Payload_ConfirmSongChange_4[4] = {0x27, 0x15, 0x00, 0x22};

const uint8_t CDC_Payload_OperateStandby[6]     = {0x20, 0x09, 0x03, 0x09, 0x05, 0x01};
const uint8_t CDC_Payload_OperatePreperePlay[6] = {0x20, 0x05, 0x03, 0x09, 0x05, 0x01};
const uint8_t CDC_Payload_OperatePaused[6]      = {0x20, 0x03, 0x03, 0x09, 0x05, 0x01};

const uint8_t CDC_Payload_CueingFwd[2] = {0x21, 0x07};
const uint8_t CDC_Payload_Rewinding[2] = {0x21, 0x08};

// [0]=Cmd, [1]=Track, [2]=Const, [3]=Disc, [4]=DiscMin, [5]=DiscSec, [6]=DiscFrame, [7]=TrkMin, [8]=TrkSec, [9]=TrkFrame
uint8_t CDC_Payload_OperatePlaying[11] = {0x47, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static volatile uint8_t CDC_TX_buffer[32];
static volatile uint8_t CDC_RX_buffer[32];
static volatile uint8_t CDC_RX_Ptr;



TLCDCEmu* pTLCDCEmu;

TLCDCEmu::TLCDCEmu() {
    pTLCDCEmu = this;
    con1 = con2 = con3 = con4 = 0;
    CDC_Wait = WAITING;
    CDC_CurrentState = WAIT_BOOT;
    CDC_SendSequence = 0;
    CDC_PlaySequence = 0;

    uart_config.baud_rate = 9600;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_EVEN;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.rx_flow_ctrl_thresh = 122;
    uart_config.source_clk = UART_SCLK_APB;
}

TLCDCEmu::~TLCDCEmu() {
    uart_driver_delete(UART_NUM_2);
    if(play_timer) esp_timer_delete(play_timer);
    if(timeout_timer) esp_timer_delete(timeout_timer);
}

void TLCDCEmu::init() {
    CDC_RX_Ptr = 0;
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, 15, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    uart_set_line_inverse(UART_NUM_2, UART_SIGNAL_RXD_INV | UART_SIGNAL_TXD_INV);
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, BUFF_SIZE, BUFF_SIZE, 10, &uart_queue, 0));

    xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, configMAX_PRIORITIES - 2, NULL);

    esp_timer_create_args_t play_timer_args = {};
    play_timer_args.callback = &TLCDCEmu::play_timer_callback;
    play_timer_args.arg = this;
    play_timer_args.name = "cdc_play_timer";
    ESP_ERROR_CHECK(esp_timer_create(&play_timer_args, &play_timer));

    esp_timer_create_args_t timeout_timer_args = {};
    timeout_timer_args.callback = &TLCDCEmu::timeout_timer_callback;
    timeout_timer_args.arg = this;
    timeout_timer_args.name = "cdc_timeout_timer";
    ESP_ERROR_CHECK(esp_timer_create(&timeout_timer_args, &timeout_timer));
}

void TLCDCEmu::talk() {
    process_async_events(); // 1. Вычитываем все скопившиеся прерывания и флаги

    switch(CDC_CurrentState) {
        case WAIT_BOOT:
            if(CDC_SendPacket((uint8_t*)CDC_Payload_WaitBoot, 3, 1)) {
                CDC_CurrentState = BOOT_SEQUENCE;
            }
            break;

        case BOOT_SEQUENCE:
            if (!con1) { con1 = CDC_SendPacket((uint8_t*)CDC_Payload_BootSequence_1, 3, 1); break; }
            if (!con2) { con2 = CDC_SendPacket((uint8_t*)CDC_Payload_BootSequence_2, 6, 1); break; }
            if (!con3) { con3 = CDC_SendPacket((uint8_t*)CDC_Payload_BootSequence_3, 2, 1); break; }
            if (!con4) { con4 = CDC_SendPacket((uint8_t*)CDC_Payload_BootSequence_4, 5, 1); break; }

            if(con1 && con2 && con3 && con4) {
                // Возвращаем немедленное открытие S/PDIF гейта
                CDC_CurrentState = OPERATE_PLAYING;
                CDC_PlaySequence = 0;
                CDC_Payload_OperatePlaying[3] = 0x01; // Форсируем старт с 1-го трека
                if (play_timer) esp_timer_stop(play_timer);
                esp_timer_start_periodic(play_timer, 1000000); 
            } else {
                CDC_SendSequence = 0;
                CDC_CurrentState = WAIT_BOOT;
            }
            break;

        case WAIT_HU_VERSION:
        case CONFIRM_HU_VERSION:
            break;

        case RECEIVED_CD_CHANGE:
        case RECEIVED_PREPARE_PLAY:
            CDC_CurrentState = OPERATE_PREPARE_PLAY;
            CDC_SendPacket((uint8_t*)CDC_Payload_ConfirmSongChange_Searching, 2, 1);
            pending_bt_cmd = 1; // ASYNC PLAY (Автостарт при переходе с Радио!)
            vTaskDelay(150 / portTICK_PERIOD_MS);
            CDC_SendPacket((uint8_t*)CDC_Payload_ConfirmSongChange_Playing, 2, 1);
            CDC_CurrentState = OPERATE_PLAYING;
            break;

        case RECEIVED_PLAY:
            CDC_SendPacket((uint8_t*)CDC_Payload_ConfirmPlay, 2, 1); 
            pending_bt_cmd = 1; // ASYNC PLAY (Автозапуск звука смартфона)
            CDC_CurrentState = OPERATE_PLAYING;
            break;

        case RECEIVED_PAUSE:
            CDC_SendPacket((uint8_t*)CDC_Payload_ConfirmPause, 2, 1);
            pending_bt_cmd = 2; // ASYNC PAUSE
            CDC_CurrentState = OPERATE_PAUSED;
            break;

        case RECEIVED_STANDBY:
            CDC_SendPacket((uint8_t*)CDC_Payload_ConfirmStandby, 2, 1);
            pending_bt_cmd = 2; // ASYNC PAUSE
            CDC_CurrentState = OPERATE_STANDBY;
            break;

        case RECEIVED_NEXT:
            CDC_ConfirmSongChange(1);
            pending_bt_cmd = 3; // ASYNC NEXT
            CDC_CurrentState = OPERATE_PLAYING;
            break;

        case RECEIVED_PREV:
            CDC_ConfirmSongChange(-1);
            pending_bt_cmd = 4; // ASYNC PREV
            CDC_CurrentState = OPERATE_PLAYING;
            break;
        // ... (дальше без изменений)    // ВНЕДРЕНО: Ответ на старт перемотки вперед
        case RECEIVED_FAST_FORWARD:
            CDC_SendPacket((uint8_t*)CDC_Payload_CueingFwd, 2, 1);
            CDC_CurrentState = OPERATE_FAST_FORWARD;
            break;

        // ВНЕДРЕНО: Ответ на старт перемотки назад
        case RECEIVED_REWIND:
            CDC_SendPacket((uint8_t*)CDC_Payload_Rewinding, 2, 1);
            CDC_CurrentState = OPERATE_REWIND;
            break;

        case OPERATE_PREPARE_PLAY:
        case OPERATE_PAUSED:
        case OPERATE_STANDBY:
        case OPERATE_PLAYING:
            break;
    }
}

uint8_t TLCDCEmu::CDC_SendPacket(uint8_t *data, uint8_t length, uint8_t retries) {
    if(length <= 28) {
        for(int ret = 0; ret < retries; ret++) {
            CDC_TX_buffer[0] = 0x3D;
            CDC_TX_buffer[1] = CDC_SendSequence;
            CDC_TX_buffer[2] = length;
            for(uint8_t i=0; i<length; i++) {
                CDC_TX_buffer[3+i] = data[i];
            }
            CDC_TX_buffer[3+length] = this->CDC_checksum((const uint8_t*)CDC_TX_buffer, length+3);

            uart_write_bytes(UART_NUM_2, (const char*)CDC_TX_buffer, length+4);
            CDC_Wait = WAITING;
            uart_wait_tx_done(UART_NUM_2, 100);

            esp_timer_start_once(timeout_timer, 200000);
            while(CDC_Wait == WAITING) { vTaskDelay(1 / portTICK_PERIOD_MS); }
            esp_timer_stop(timeout_timer);

            if(CDC_Wait == CONFIRMED) {
                CDC_SendSequence++;
                vTaskDelay(40 / portTICK_PERIOD_MS);
                return 1;
            }
        }
    }
    return 0;
}

void TLCDCEmu::uart_event_task(void *pvParameters) {
    uart_event_t event;
    uint8_t* dtmp = (uint8_t*) malloc(BUFF_SIZE);

    for(;;) {
        if(xQueueReceive(pTLCDCEmu->uart_queue, (void*)&event, (TickType_t)portMAX_DELAY)) {
            bzero(dtmp, BUFF_SIZE);
            switch(event.type) {
                case UART_DATA:
                    uart_read_bytes(UART_NUM_2, dtmp, event.size, portMAX_DELAY);
                    readHU((const uint8_t*)dtmp, event.size);
                    break;
                case UART_FIFO_OVF:
                case UART_BUFFER_FULL:
                    uart_flush_input(UART_NUM_2);
                    xQueueReset(pTLCDCEmu->uart_queue);
                    break;
                default:
                    break;
            }
        }
    }
    free(dtmp);
    vTaskDelete(NULL);
}

void TLCDCEmu::readHU(const uint8_t *data, uint16_t length) {
    Serial.print("[UART RX] ");
    for(int i=0; i<length; i++) Serial.printf("%02X ", data[i]);
    Serial.println();

    for(int i=0; i<length; i++) {
        CDC_RX_buffer[CDC_RX_Ptr] = data[i];
        CDC_RX_Ptr++;

        if(CDC_RX_buffer[0] == 0xC5) {
            CDC_RX_Ptr = 0;
            pTLCDCEmu->setWaitState(CONFIRMED);
        }
        else if(CDC_RX_buffer[0] != 0x3D) {
            CDC_RX_Ptr = 0;
        }

        if(CDC_RX_Ptr >= 4) {
            if(CDC_RX_Ptr == CDC_RX_buffer[2] + 4) {
                if(CDC_RX_buffer[CDC_RX_Ptr-1] == CDC_checksum((const uint8_t*)CDC_RX_buffer, CDC_RX_buffer[2]+3)) {

                    uart_write_bytes(UART_NUM_2, (const char*)ACK, 1);
                    uart_wait_tx_done(UART_NUM_2, 100);

                    uint8_t pkt_len = CDC_RX_buffer[2];
                    uint8_t cmd     = CDC_RX_buffer[3];
                    uint8_t arg1    = (pkt_len >= 1) ? CDC_RX_buffer[4] : 0;
                    uint8_t arg2    = (pkt_len >= 2) ? CDC_RX_buffer[5] : 0;

                    // 1. Отвечаем на запрос версии НЕВИДИМО, не меняя стейт!
                    if(pkt_len == 2 && cmd == 0x07 && arg1 == 0x31) {
                        uart_write_bytes(UART_NUM_2, (const char*)CDC_Payload_ConfirmHuVersion, 2);
                    }

                    // 2. Однобайтовые команды
                    if(pkt_len == 1) {
                        if(cmd == 0x13) pTLCDCEmu->CDC_CurrentState = RECEIVED_PLAY; // СТРОГО только смена стейта!
                        if(cmd == 0x1C) pTLCDCEmu->CDC_CurrentState = RECEIVED_PAUSE;
                        if(cmd == 0x19) pTLCDCEmu->CDC_CurrentState = RECEIVED_STANDBY;
                        
                        // Переключение Play/Pause с магнитолы
                        if(cmd == 0x24) { 
                            if (pTLCDCEmu->CDC_CurrentState == OPERATE_PLAYING) {
                                pTLCDCEmu->CDC_CurrentState = RECEIVED_PAUSE;
                            } else {
                                pTLCDCEmu->CDC_CurrentState = RECEIVED_PLAY;
                            }
                        }

                        // ПИНГ ОТ МАГНИТОЛЫ (0x86).
                        if(cmd == 0x86) {
                            if (pTLCDCEmu->CDC_CurrentState == OPERATE_STANDBY) {
                                // Возврат с радио на CD. Форсируем пробуждение!
                                pTLCDCEmu->CDC_CurrentState = RECEIVED_PLAY;
                            }
                            pTLCDCEmu->flag_ping_request = true;
                        }
                    }

                    // 3. Двухбайтовые команды
                    if(pkt_len == 2) {
                        if(cmd == 0x17 && arg1 == 0x01) pTLCDCEmu->CDC_CurrentState = RECEIVED_NEXT;
                        if(cmd == 0x17 && arg1 == 0x02) pTLCDCEmu->CDC_CurrentState = RECEIVED_PREV;
                        if(cmd == 0x2C && arg1 == 0xFF) pTLCDCEmu->CDC_CurrentState = RECEIVED_PREPARE_PLAY; 
                        if(cmd == 0x26) pTLCDCEmu->CDC_CurrentState = RECEIVED_CD_CHANGE;

                        // ВНЕДРЕНО: Перехват сигналов удержания лепестка (Fast Forward / Rewind)
                        if(cmd == 0x20 && arg1 == 0x0A) pTLCDCEmu->CDC_CurrentState = RECEIVED_FAST_FORWARD;
                        if(cmd == 0x21 && arg1 == 0x0A) pTLCDCEmu->CDC_CurrentState = RECEIVED_REWIND;
                    }

                    // 4. Трехбайтовые команды
                    if(pkt_len == 3) {
                        if(cmd == 0x22 && arg1 == 0x01 && arg2 == 0x02) {
                            pTLCDCEmu->CDC_CurrentState = RECEIVED_PREV;
                        }
                    }
                }
                CDC_RX_Ptr = 0;
            }
            else if(CDC_RX_Ptr > CDC_RX_buffer[2] + 4) {
                CDC_RX_Ptr = 0;
            }
        }
        if(CDC_RX_Ptr == 32) CDC_RX_Ptr = 0;
    }
}

void TLCDCEmu::fakePlay() {
    switch(CDC_CurrentState) {
        case WAIT_HU_VERSION:
        case OPERATE_STANDBY:
            CDC_SendPacket((uint8_t*)CDC_Payload_OperateStandby, 6, 1);
            break;
        case OPERATE_PREPARE_PLAY:
            CDC_SendPacket((uint8_t*)CDC_Payload_OperatePreperePlay, 6, 1);
            break;
        case OPERATE_PLAYING:
            CDC_Payload_OperatePlaying[5] = CDC_PlaySequence; // Disc Sec
            CDC_Payload_OperatePlaying[8] = CDC_PlaySequence; // Track Sec
            CDC_SendPacket((uint8_t*)CDC_Payload_OperatePlaying, 11, 1);

            // BCD Инкремент
            CDC_PlaySequence++;
            if ((CDC_PlaySequence & 0x0F) > 9) CDC_PlaySequence += 6;
            if (CDC_PlaySequence >= 0x60) {
                CDC_PlaySequence = 0x00;
                CDC_Payload_OperatePlaying[4]++;
                if ((CDC_Payload_OperatePlaying[4] & 0x0F) > 9) CDC_Payload_OperatePlaying[4] += 6;
                CDC_Payload_OperatePlaying[7] = CDC_Payload_OperatePlaying[4]; // Синхронизация минут трека
            }
            break;

        case OPERATE_FAST_FORWARD:
            for(int i=0; i<3; i++) { // Ускорение x3
                CDC_PlaySequence++;
                if ((CDC_PlaySequence & 0x0F) > 9) CDC_PlaySequence += 6;
                if (CDC_PlaySequence >= 0x60) {
                    CDC_PlaySequence = 0x00;
                    CDC_Payload_OperatePlaying[4]++;
                    if ((CDC_Payload_OperatePlaying[4] & 0x0F) > 9) CDC_Payload_OperatePlaying[4] += 6;
                    CDC_Payload_OperatePlaying[7] = CDC_Payload_OperatePlaying[4];
                }
            }
            CDC_Payload_OperatePlaying[5] = CDC_PlaySequence;
            CDC_Payload_OperatePlaying[8] = CDC_PlaySequence;
            CDC_SendPacket((uint8_t*)CDC_Payload_CueingFwd, 2, 1);
            CDC_SendPacket((uint8_t*)CDC_Payload_OperatePlaying, 11, 1);
            break;

        case OPERATE_REWIND:
            for(int i=0; i<3; i++) { // Реверс x3
                if (CDC_PlaySequence == 0x00 && CDC_Payload_OperatePlaying[4] == 0x00) break;
                if (CDC_PlaySequence == 0x00) {
                    CDC_Payload_OperatePlaying[4]--;
                    if ((CDC_Payload_OperatePlaying[4] & 0x0F) > 9) CDC_Payload_OperatePlaying[4] -= 6;
                    CDC_Payload_OperatePlaying[7] = CDC_Payload_OperatePlaying[4];
                    CDC_PlaySequence = 0x59;
                } else {
                    CDC_PlaySequence--;
                    if ((CDC_PlaySequence & 0x0F) > 9) CDC_PlaySequence -= 6;
                }
            }
            CDC_Payload_OperatePlaying[5] = CDC_PlaySequence;
            CDC_Payload_OperatePlaying[8] = CDC_PlaySequence;
            CDC_SendPacket((uint8_t*)CDC_Payload_Rewinding, 2, 1);
            CDC_SendPacket((uint8_t*)CDC_Payload_OperatePlaying, 11, 1);
            break;

        case OPERATE_PAUSED:
            CDC_SendPacket((uint8_t*)CDC_Payload_OperatePaused, 6, 1);
            break;
        default:
            break;
    }
}

uint8_t TLCDCEmu::CDC_checksum(const uint8_t *data, uint8_t length) {
    uint8_t res = 0;
    for (uint8_t i=0; i<length; i++) res ^= data[i];
    return res;
}

void TLCDCEmu::CDC_ConfirmSongChange(int direction) {
    // 1. Переводим логику в поиск (Mute)
    CDC_SendPacket((uint8_t*)CDC_Payload_ConfirmSongChange_Searching, 2, 1);

    // 2. Высчитываем BCD трека
    uint8_t track = CDC_Payload_OperatePlaying[1];
    if (direction > 0) {
        track++; if((track & 0x0F) > 9) track += 6;
    } else if (direction < 0) {
        track--; if((track & 0x0F) > 9) track -= 6;
        if(track == 0x00 || track > 0x99) track = 0x99;
    }
    if(track > 0x99 || track == 0x00) track = 0x01;
    CDC_Payload_OperatePlaying[1] = track;

    // 3. Обнуляем время
    CDC_PlaySequence = 0;
    CDC_Payload_OperatePlaying[4] = 0x00;
    CDC_Payload_OperatePlaying[5] = 0x00;
    CDC_Payload_OperatePlaying[7] = 0x00;
    CDC_Payload_OperatePlaying[8] = 0x00;

    vTaskDelay(100 / portTICK_PERIOD_MS); // 100 мс достаточно

    // 4. ИНЪЕКЦИЯ ПАКЕТА: Форсируем обновление дисплея
    CDC_SendPacket((uint8_t*)CDC_Payload_OperatePlaying, 11, 1);

    // 5. Разблокируем аудиоканал DSP
    CDC_SendPacket((uint8_t*)CDC_Payload_ConfirmSongChange_Playing, 2, 1);
}


void TLCDCEmu::play_timer_callback(void* arg) {
    // Кастуем указатель на экземпляр класса и устанавливаем флаг объекта
    TLCDCEmu* instance = static_cast<TLCDCEmu*>(arg);
    instance->flag_send_play_update = true; 
}

void TLCDCEmu::process_async_events() {
    // 1. Аппаратный запрос статуса от магнитолы (0x86)
    if (flag_ping_request) {
        flag_ping_request = false;
        // Если мы уже играем ИЛИ в процессе запуска - отвечаем PLAYING
        if (CDC_CurrentState == OPERATE_PLAYING || CDC_CurrentState == RECEIVED_PLAY) {
            CDC_SendPacket((uint8_t*)CDC_Payload_OperatePlaying, 11, 1);
        } 
        else if (CDC_CurrentState == OPERATE_PAUSED || CDC_CurrentState == RECEIVED_PAUSE) {
            CDC_SendPacket((uint8_t*)CDC_Payload_OperatePaused, 6, 1);
        } 
        else {
            CDC_SendPacket((uint8_t*)CDC_Payload_OperateStandby, 6, 1);
        }
    }

    // 2. Синхронизация номера трека от телефона (AVRCP)
    if (pending_avrcp_track != -1) {
        int new_track = pending_avrcp_track;
        pending_avrcp_track = -1; // Сбрасываем флаг
        
        // Математика перевода из DEC в BCD формат магнитолы
        uint8_t bcd = ((new_track / 10) << 4) | (new_track % 10);
        
        if (CDC_Payload_OperatePlaying[1] != bcd) { 
            CDC_Payload_OperatePlaying[1] = bcd;
            
            // Сброс счетчиков времени трека
            CDC_PlaySequence = 0;
            CDC_Payload_OperatePlaying[4] = 0x00; 
            CDC_Payload_OperatePlaying[5] = 0x00; 
            CDC_Payload_OperatePlaying[7] = 0x00; 
            CDC_Payload_OperatePlaying[8] = 0x00; 
            
            // Имитация физической смены диска для обмана графического драйвера дисплея AFFA
            CDC_SendPacket((uint8_t*)CDC_Payload_ConfirmSongChange_Searching, 2, 1);
            vTaskDelay(50 / portTICK_PERIOD_MS); // В главном цикле delay безопасен
            CDC_SendPacket((uint8_t*)CDC_Payload_OperatePlaying, 11, 1);
            CDC_SendPacket((uint8_t*)CDC_Payload_ConfirmSongChange_Playing, 2, 1);
        }
    }

    // 3. Плановый таймер воспроизведения (инкремент секунд)
    if (flag_send_play_update) {
        flag_send_play_update = false;
        fakePlay(); 
    }
}



void TLCDCEmu::timeout_timer_callback(void* arg) {
    TLCDCEmu* instance = static_cast<TLCDCEmu*>(arg);
    instance->setWaitState(TIMEOUT);
}

void TLCDCEmu::updateTrackFromMetadata(int new_track) {
    if (new_track > 0 && new_track <= 99) {
        pending_avrcp_track = new_track; // Сохраняем для обработки в главном цикле
    }
}

// Реализация авто-переключения (Добавь в конец файла) ---
void TLCDCEmu::triggerNaturalTrackChange() {
    // Защита от двойного инкремента (если мы только что переключили трек вручную)
    // Если трек играл хотя бы 2 секунды, значит это естественная смена
    if (CDC_PlaySequence > 2 || CDC_Payload_OperatePlaying[4] > 0) {
        uint8_t track = CDC_Payload_OperatePlaying[1];
        track++; if((track & 0x0F) > 9) track += 6;
        if(track > 0x99 || track == 0x00) track = 0x01;
        CDC_Payload_OperatePlaying[1] = track;
    }
    
    // Всегда жестко сбрасываем таймеры в 00:00
    CDC_PlaySequence = 0;
    CDC_Payload_OperatePlaying[4] = 0x00;
    CDC_Payload_OperatePlaying[5] = 0x00;
    CDC_Payload_OperatePlaying[7] = 0x00;
    CDC_Payload_OperatePlaying[8] = 0x00;
}
