#ifndef _TLCDCEMU_H
#define _TLCDCEMU_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_log.h"

#define LOG_TAG "TLCDCEmu"
#define BUFF_SIZE 256

typedef enum { WAITING, CONFIRMED, TIMEOUT } CDC_Wait_E;

typedef enum {
    WAIT_BOOT, BOOT_SEQUENCE, WAIT_HU_VERSION, CONFIRM_HU_VERSION,
    RECEIVED_PLAY, RECEIVED_PAUSE, RECEIVED_STANDBY, RECEIVED_CD_CHANGE,
    RECEIVED_NEXT, RECEIVED_PREV, RECEIVED_PREPARE_PLAY,
    RECEIVED_FAST_FORWARD, RECEIVED_REWIND, // <-- ВНЕДРЕНО: Транзиты удержания кнопок
    OPERATE_STANDBY, OPERATE_PAUSED, OPERATE_PREPARE_PLAY, OPERATE_PLAYING,
    OPERATE_FAST_FORWARD, OPERATE_REWIND    // <-- ВНЕДРЕНО: Рабочие циклы перемотки
} CDC_State;

class TLCDCEmu {
    public:
        TLCDCEmu();
        ~TLCDCEmu();
        void init(); 
        void talk();

        void fakePlay();
        void updateTrackFromMetadata(int new_track); // <-- Внедрен метод обновления BCD
        void triggerNaturalTrackChange(); // <-- ВНЕДРЕНО: Хук для авто-переключения

        CDC_Wait_E getWaitState() { return CDC_Wait; }
        void setWaitState(CDC_Wait_E state) { CDC_Wait = state; }

        CDC_State CDC_CurrentState;

    private:

        volatile bool flag_send_play_update = false;
        volatile int pending_avrcp_track = -1;  // -1 означает "нет новых данных"
        volatile bool flag_ping_request = false; // Флаг ответа на 0x86
        
        void process_async_events(); // Делегат транзакций
        uint8_t CDC_SendPacket(uint8_t *data, uint8_t length, uint8_t retries);
        static uint8_t CDC_checksum(const uint8_t *data, uint8_t length);
        void CDC_ConfirmSongChange(int direction);
        static void readHU(const uint8_t *data, uint16_t length);
        static void uart_event_task(void *pvParameters);

        uint8_t con1, con2, con3, con4;
        uart_config_t uart_config;
        QueueHandle_t uart_queue;

        esp_timer_handle_t play_timer;
        esp_timer_handle_t timeout_timer;

        static void play_timer_callback(void* arg);
        static void timeout_timer_callback(void* arg);

        volatile CDC_Wait_E CDC_Wait;
        uint8_t CDC_SendSequence;
        uint8_t CDC_PlaySequence;
};

extern TLCDCEmu* pTLCDCEmu;

#endif