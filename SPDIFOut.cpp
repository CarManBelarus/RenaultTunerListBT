#include <Arduino.h>
#include "driver/i2s.h"
#include "soc/rtc.h"
#include "SPDIFOut.h"

static const uint16_t spdif_bmclookup[256] PROGMEM = {
    0xcccc, 0x4ccc, 0x2ccc, 0xaccc, 0x34cc, 0xb4cc, 0xd4cc, 0x54cc,
    0x32cc, 0xb2cc, 0xd2cc, 0x52cc, 0xcacc, 0x4acc, 0x2acc, 0xaacc,
    0x334c, 0xb34c, 0xd34c, 0x534c, 0xcb4c, 0x4b4c, 0x2b4c, 0xab4c,
    0xcd4c, 0x4d4c, 0x2d4c, 0xad4c, 0x354c, 0xb54c, 0xd54c, 0x554c,
    0x332c, 0xb32c, 0xd32c, 0x532c, 0xcb2c, 0x4b2c, 0x2b2c, 0xab2c,
    0xcd2c, 0x4d2c, 0x2d2c, 0xad2c, 0x352c, 0xb52c, 0xd52c, 0x552c,
    0xccac, 0x4cac, 0x2cac, 0xacac, 0x34ac, 0xb4ac, 0xd4ac, 0x54ac,
    0x32ac, 0xb2ac, 0xd2ac, 0x52ac, 0xcaac, 0x4aac, 0x2aac, 0xaaac,
    0x3334, 0xb334, 0xd334, 0x5334, 0xcb34, 0x4b34, 0x2b34, 0xab34,
    0xcd34, 0x4d34, 0x2d34, 0xad34, 0x3534, 0xb534, 0xd534, 0x5534,
    0xccb4, 0x4cb4, 0x2cb4, 0xacb4, 0x34b4, 0xb4b4, 0xd4b4, 0x54b4,
    0x32b4, 0xb2b4, 0xd2b4, 0x52b4, 0xcab4, 0x4ab4, 0x2ab4, 0xaab4,
    0xccd4, 0x4cd4, 0x2cd4, 0xacd4, 0x34d4, 0xb4d4, 0xd4d4, 0x54d4,
    0x32d4, 0xb2d4, 0xd2d4, 0x52d4, 0xcad4, 0x4ad4, 0x2ad4, 0xaad4,
    0x3354, 0xb354, 0xd354, 0x5354, 0xcb54, 0x4b54, 0x2b54, 0xab54,
    0xcd54, 0x4d54, 0x2d54, 0xad54, 0x3554, 0xb554, 0xd554, 0x5554,
    0x3332, 0xb332, 0xd332, 0x5332, 0xcb32, 0x4b32, 0x2b32, 0xab32,
    0xcd32, 0x4d32, 0x2d32, 0xad32, 0x3532, 0xb532, 0xd532, 0x5532,
    0xccb2, 0x4cb2, 0x2cb2, 0xacb2, 0x34b2, 0xb4b2, 0xd4b2, 0x54b2,
    0x32b2, 0xb2b2, 0xd2b2, 0x52b2, 0xcab2, 0x4ab2, 0x2ab2, 0xaab2,
    0xccd2, 0x4cd2, 0x2cd2, 0xacd2, 0x34d2, 0xb4d2, 0xd4d2, 0x54d2,
    0x32d2, 0xb2d2, 0xd2d2, 0x52d2, 0xcad2, 0x4ad2, 0x2ad2, 0xaad2,
    0x3352, 0xb352, 0xd352, 0x5352, 0xcb52, 0x4b52, 0x2b52, 0xab52,
    0xcd52, 0x4d52, 0x2d52, 0xad52, 0x3552, 0xb552, 0xd552, 0x5552,
    0xccca, 0x4cca, 0x2cca, 0xacca, 0x34ca, 0xb4ca, 0xd4ca, 0x54ca,
    0x32ca, 0xb2ca, 0xd2ca, 0x52ca, 0xcaca, 0x4aca, 0x2aca, 0xaaca,
    0x334a, 0xb34a, 0xd34a, 0x534a, 0xcb4a, 0x4b4a, 0x2b4a, 0xab4a,
    0xcd4a, 0x4d4a, 0x2d4a, 0xad4a, 0x354a, 0xb54a, 0xd54a, 0x554a,
    0x332a, 0xb32a, 0xd32a, 0x532a, 0xcb2a, 0x4b2a, 0x2b2a, 0xab2a,
    0xcd2a, 0x4d2a, 0x2d2a, 0xad2a, 0x352a, 0xb52a, 0xd52a, 0x552a,
    0xccaa, 0x4caa, 0x2caa, 0xacaa, 0x34aa, 0xb4aa, 0xd4aa, 0x54aa,
    0x32aa, 0xb2aa, 0xd2aa, 0x52aa, 0xcaaa, 0x4aaa, 0x2aaa, 0xaaaa
};

SPDIFOut::SPDIFOut(int dout_pin, int port, int dma_buf_count) {
  this->portNo = port;
  i2s_config_t i2s_config_spdif = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 88200,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = dma_buf_count,
    .dma_buf_len = DMA_BUF_SIZE_DEFAULT,
    .use_apll = true 
  };
  if (i2s_driver_install((i2s_port_t)portNo, &i2s_config_spdif, 0, NULL) != ESP_OK) return;
  i2s_zero_dma_buffer((i2s_port_t)portNo);
  SetPinout(I2S_PIN_NO_CHANGE, I2S_PIN_NO_CHANGE, dout_pin);
  rate_multiplier = 2;
  i2sOn = true;
  mono = false;
  bps = 16;
  channels = 2;
  frame_num = 0;
  SetGain(1.0);
  hertz = 0;
  SetRate(44100);
}

SPDIFOut::~SPDIFOut() {
  if (i2sOn) {
    i2s_stop((i2s_port_t)this->portNo);
    i2s_driver_uninstall((i2s_port_t)this->portNo);
  }
  i2sOn = false;
}

bool SPDIFOut::SetPinout(int bclk, int wclk, int dout) {
  i2s_pin_config_t pins = {
    .bck_io_num = bclk,
    .ws_io_num = wclk,
    .data_out_num = dout,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  return (i2s_set_pin((i2s_port_t)portNo, &pins) == ESP_OK);
}

void SPDIFOut::i2sTurnOn(){
    if (!i2sOn && i2s_start((i2s_port_t)portNo) == ESP_OK) i2sOn = true;
}

void SPDIFOut::i2sTurnOff(){
    if (i2sOn && i2s_stop((i2s_port_t)portNo) == ESP_OK) {
        i2s_zero_dma_buffer((i2s_port_t)portNo);
        i2sOn = false;
    }
}

bool SPDIFOut::SetRate(int hz) {
  if (!i2sOn || hz < 32000) return false;
  if (hz == this->hertz) return true;
  this->hertz = hz;
  int adjustedHz = AdjustI2SRate(hz);
  
  if (i2s_set_sample_rates((i2s_port_t)portNo, adjustedHz) != ESP_OK) {
      return false;
  }
  return true;
}

bool SPDIFOut::SetBitsPerSample(int bits) { this->bps = bits; return true; }
bool SPDIFOut::SetChannels(int channels) { this->channels = channels; return true; }
bool SPDIFOut::SetOutputModeMono(bool mono) { this->mono = mono; if (mono) SetChannels(1); return true; }
bool SPDIFOut::begin() { return true; }
bool SPDIFOut::stop() { i2s_zero_dma_buffer((i2s_port_t)portNo); frame_num = 0; return true; }

// УЛЬТИМАТИВНАЯ ОПТИМИЗАЦИЯ BATCH-PROCESSING
bool SPDIFOut::ConsumeSample(int16_t sample[2]) {
    if (!i2sOn) return true;

    // Внутренний кэш на 64 сэмпла (256 32-битных слов = 1024 байта)
    static uint32_t out_buf[256];
    static int out_idx = 0;
    static bool bmc_state = false;

    auto bmc_encode_byte =[](uint8_t raw, bool &st) -> uint16_t {
        uint16_t w = pgm_read_word(&spdif_bmclookup[raw]);
        bool table_expects = (w & 0x8000) == 0;
        bool invert = (st != table_expects);
        if (invert) w ^= 0xFFFF;
        st = invert;
        return w;
    };

    // === LEFT CHANNEL ===
    uint16_t sample_left = Amplify(sample[LEFTCHANNEL]);
    uint16_t p_left = sample_left;
    p_left ^= p_left >> 8; p_left ^= p_left >> 4; p_left ^= p_left >> 2; p_left ^= p_left >> 1; p_left &= 1;

    uint8_t b1_l = (sample_left << 4) & 0xFF;
    uint8_t b2_l = (sample_left >> 4) & 0xFF;
    uint8_t b3_l = ((sample_left >> 12) & 0x0F) | (p_left << 7);

    uint8_t pb_l = bmc_state ? (frame_num == 0 ? 0x17 : 0x1D) : (frame_num == 0 ? 0xE8 : 0xE2);
    bmc_state = pb_l & 1;
    uint8_t ab_l = bmc_state ? 0x33 : 0xCC;
    bmc_state = ab_l & 1;
    uint16_t w0_l = (pb_l << 8) | ab_l;

    uint16_t w1_l = bmc_encode_byte(b1_l, bmc_state);
    uint16_t w2_l = bmc_encode_byte(b2_l, bmc_state);
    uint16_t w3_l = bmc_encode_byte(b3_l, bmc_state);

    out_buf[out_idx++] = ((uint32_t)w0_l << 16) | w1_l;
    out_buf[out_idx++] = ((uint32_t)w2_l << 16) | w3_l;

    // === RIGHT CHANNEL ===
    uint16_t sample_right = Amplify(sample[RIGHTCHANNEL]);
    uint16_t p_right = sample_right;
    p_right ^= p_right >> 8; p_right ^= p_right >> 4; p_right ^= p_right >> 2; p_right ^= p_right >> 1; p_right &= 1;

    uint8_t b1_r = (sample_right << 4) & 0xFF;
    uint8_t b2_r = (sample_right >> 4) & 0xFF;
    uint8_t b3_r = ((sample_right >> 12) & 0x0F) | (p_right << 7);

    uint8_t pb_r = bmc_state ? 0x1B : 0xE4;
    bmc_state = pb_r & 1;
    uint8_t ab_r = bmc_state ? 0x33 : 0xCC;
    bmc_state = ab_r & 1;
    uint16_t w0_r = (pb_r << 8) | ab_r;

    uint16_t w1_r = bmc_encode_byte(b1_r, bmc_state);
    uint16_t w2_r = bmc_encode_byte(b2_r, bmc_state);
    uint16_t w3_r = bmc_encode_byte(b3_r, bmc_state);

    out_buf[out_idx++] = ((uint32_t)w0_r << 16) | w1_r;
    out_buf[out_idx++] = ((uint32_t)w2_r << 16) | w3_r;

    if (++frame_num > 191) frame_num = 0;

    // Скидываем данные в DMA только когда набрался полный блок (снижаем нагрузку на процессор на 98%)
    if (out_idx >= 256) { 
        size_t bytes_written;
        esp_err_t ret = i2s_write((i2s_port_t)portNo, out_buf, sizeof(out_buf), &bytes_written, portMAX_DELAY);
        out_idx = 0;
        return ret == ESP_OK;
    }

    return true;
}