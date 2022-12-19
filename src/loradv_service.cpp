#include "loradv_service.h"

namespace LoraDv {

volatile bool Service::loraIsrEnabled_ = true;
std::shared_ptr<AiEsp32RotaryEncoder> Service::rotaryEncoder_;
TaskHandle_t Service::loraTaskHandle_;

Service::Service()
  : btnPressed_(false)
  , codecVolume_(CfgAudioMaxVolume)
  , isIsrInstalled_(false)
{
}

void Service::setup(const Config &config)
{
  config_ = config;

  // setup logging
  LOG_SET_OPTION(false, false, true);  // disable file, line, enable func

  // oled screen
  display_ = std::shared_ptr<Adafruit_SSD1306>(new Adafruit_SSD1306(CfgDisplayWidth, CfgDisplayHeight, &Wire, -1));
  if(display_->begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    LOG_INFO("Display setup completed");
  } else {
    LOG_ERROR("Display init failed");
  }

  // ptt button
  LOG_INFO("PTT setup started");
  pinMode(config_.PttBtnPin, INPUT);
  LOG_INFO("PTT setup completed");

  // rotary encoder
  LOG_INFO("Encoder setup started");
  rotaryEncoder_ = std::shared_ptr<AiEsp32RotaryEncoder>(new AiEsp32RotaryEncoder(config_.EncoderPinA, config_.EncoderPinB, 
    config_.EncoderPinBtn, config_.EncoderPinVcc, config_.EncoderSteps));
  rotaryEncoder_->begin();
  rotaryEncoder_->setBoundaries(0, CfgAudioMaxVolume);
  rotaryEncoder_->setEncoderValue(CfgAudioMaxVolume);
  rotaryEncoder_->setup(isrReadEncoder);
  LOG_INFO("Encoder setup completed");

  // start codec2 playback task
  xTaskCreate(&audioTask, "audio_task", 32000, NULL, 5, &audioTaskHandle_);

  // start lora task
  xTaskCreate(&loraRadioTask, "lora_task", 8000, NULL, 5, &loraTaskHandle_);

  // sleep
  LOG_INFO("Light sleep is enabled");
  lightSleepReset();
  printStatus("RX");

  LOG_INFO("Board setup completed");
}

void Service::setupRig(long loraFreq, long bw, int sf, int cr, int pwr, int sync, int crcBytes, bool isExplicit)
{
  rigIsImplicitMode_ = !isExplicit;
  rigIsImplicitMode_ = sf == 6;      // must be implicit for SF6
  int loraSpeed = (int)(sf * (4.0 / cr) / (pow(2.0, sf) / bw));

  LOG_INFO("Initializing LoRa");
  LOG_INFO("Frequency:", loraFreq, "Hz");
  LOG_INFO("Bandwidth:", bw, "Hz");
  LOG_INFO("Spreading:", sf);
  LOG_INFO("Coding rate:", cr);
  LOG_INFO("Power:", pwr, "dBm");
  LOG_INFO("Sync:", "0x" + String(sync, HEX));
  LOG_INFO("CRC:", crcBytes);
  LOG_INFO("Header:", rigIsImplicitMode_ ? "implicit" : "explicit");
  LOG_INFO("Speed:", loraSpeed, "bps");
  float snrLimit = -7;
  switch (sf) {
    case 7:
        snrLimit = -7.5;
        break;
    case 8:
        snrLimit = -10.0;
        break;
    case 9:
        snrLimit = -12.6;
        break;
    case 10:
        snrLimit = -15.0;
        break;
    case 11:
        snrLimit = -17.5;
        break;
    case 12:
        snrLimit = -20.0;
        break;
  }
  LOG_INFO("Min level:", -174 + 10 * log10(bw) + 6 + snrLimit, "dBm");
  rig_ = std::make_shared<MODULE_NAME>(new Module(config_.LoraPinSs, config_.LoraPinA, config_.LoraPinRst, config_.LoraPinB));
  int state = rig_->begin((float)loraFreq / 1e6, (float)bw / 1e3, sf, cr, sync, pwr);
  if (state != RADIOLIB_ERR_NONE) {
    LOG_ERROR("Radio start error:", state);
  }
  rig_->setCRC(crcBytes);
#ifdef USE_SX126X
    #pragma message("Using SX126X")
    LOG_INFO("Using SX126X module");
    rig_->setRfSwitchPins(config_.LoraPinSwitchRx, config_.LoraPinSwitchTx);
    if (isIsrInstalled_) rig_->clearDio1Action();
    rig_->setDio1Action(onRigIsrRxPacket);
    isIsrInstalled_ = true;
#else
    #pragma message("Using SX127X")
    LOG_INFO("Using SX127X module");
    if (isIsrInstalled_) radio_->clearDio0Action();
    radio_->setDio0Action(onRigIsrRxPacket);
    isIsrInstalled_ = true;
#endif

  if (rigIsImplicitMode_) {
    rig_->implicitHeader(0xff);
  } else {
    rig_->explicitHeader();
  }
  
  state = rig_->startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    LOG_ERROR("Receive start error:", state);
  }

  LOG_INFO("LoRa initialized");
}

void Service::setupAudio(int bytesPerSample) 
{
  // speaker
  i2s_config_t i2s_speaker_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = CfgAudioSampleRate,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = bytesPerSample,
    .use_apll=0,
    .tx_desc_auto_clear= true, 
    .fixed_mclk=-1    
  };
  i2s_pin_config_t i2s_speaker_pin_config = {
    .bck_io_num = config_.AudioSpkPinBclk,
    .ws_io_num = config_.AudioSpkPinLrc,
    .data_out_num = config_.AudioSpkPinDin,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  if (i2s_driver_install(CfgAudioI2sSpkId, &i2s_speaker_config, 0, NULL) != ESP_OK) {
    LOG_ERROR("Failed to install i2s speaker driver");
  }
  if (i2s_set_pin(CfgAudioI2sSpkId, &i2s_speaker_pin_config) != ESP_OK) {
    LOG_ERROR("Failed to set i2s speaker pins");
  }

  // mic
  i2s_config_t i2sMicConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = CfgAudioSampleRate,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = bytesPerSample,
    .use_apll=0,
    .tx_desc_auto_clear= true,
    .fixed_mclk=-1
  };
  i2s_pin_config_t i2sMicPinConfig = {
    .bck_io_num = config_.AudioMicPinSck,
    .ws_io_num = config_.AudioMicPinWs,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = config_.AudioMicPinSd 
  };
  if (i2s_driver_install(CfgAudioI2sMicId, &i2sMicConfig, 0, NULL) != ESP_OK) {
    LOG_ERROR("Failed to install i2s mic driver");
  }
  if (i2s_set_pin(CfgAudioI2sMicId, &i2sMicPinConfig) != ESP_OK) {
    LOG_ERROR("Failed to set i2s mic pins");
  }
}

void Service::setFreq(long loraFreq) const 
{
  rig_->setFrequency((float)config_.LoraFreqRx / 1e6);
  int state = rig_->startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    LOG_ERROR("Start receive error:", state);
  }
}

void IRAM_ATTR Service::isrReadEncoder()
{
  rotaryEncoder_->readEncoder_ISR();
}

ICACHE_RAM_ATTR void Service::onRigIsrRxPacket() 
{
  if (!loraIsrEnabled_) return;
  BaseType_t xHigherPriorityTaskWoken;
  uint32_t lora_rx_bit = CfgRadioRxBit;
  xTaskNotifyFromISR(loraTaskHandle_, lora_rx_bit, eSetBits, &xHigherPriorityTaskWoken);
}

float Service::getBatteryVoltage() 
{
  int bat_value = analogRead(config_.BatteryMonPin);
  return 2 * bat_value * (3.3 / 4096.0) + config_.BatteryMonCal;
}

void Service::printStatus(const String &str)
{
    display_->clearDisplay();
    display_->setTextSize(2);
    display_->setTextColor(WHITE);
    display_->setCursor(0, 0);
    display_->print(str); display_->print(" "); 
    if (btnPressed_)
      display_->println((float)config_.LoraFreqTx / 1e6);
    else
      display_->println((float)config_.LoraFreqRx / 1e6);
    display_->print(codecVolume_); display_->print("% "); display_->print(getBatteryVoltage()); display_->print("V");
    display_->display();
}

void Service::lightSleepReset() {
  LOG_DEBUG("Reset light sleep");
  if (lightSleepTimerTask_ != NULL) lightSleepTimer_.cancel(lightSleepTimerTask_);
  lightSleepTimerTask_ = lightSleepTimer_.in(config_.PmLightSleepAfterMs, lightSleepEnterTimer);
}

bool Service::lightSleepEnterTimer(void *param) {
  static_cast<Service*>(param)->lightSleepEnter();
  return false;
}

void Service::lightSleepEnter(void) {
  LOG_INFO("Entering light sleep");
  display_->clearDisplay();
  display_->display();

  esp_sleep_wakeup_cause_t wakeupCause = ESP_SLEEP_WAKEUP_UNDEFINED;
  while (true) {
    wakeupCause = lightSleepWait(config_.PmLightSleepDurationMs * 1000UL);
    if (wakeupCause != ESP_SLEEP_WAKEUP_TIMER) break;
    delay(config_.PmLightSleepAwakeMs);
  }

  LOG_INFO("Exiting light sleep");
  lightSleepReset();
  printStatus("RX");
}

esp_sleep_wakeup_cause_t Service::lightSleepWait(uint64_t sleepTimeUs) {
  esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(config_.PttBtnPin), LOW);
  uint64_t bitMask = (uint64_t)(1 << config_.LoraPinA) | (uint64_t)(1 << config_.LoraPinB);
  esp_sleep_enable_ext1_wakeup(bitMask, ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_sleep_enable_timer_wakeup(sleepTimeUs);
  esp_light_sleep_start();
  return esp_sleep_get_wakeup_cause();
}

void Service::loraRadioTask(void *param)
{
  reinterpret_cast<Service*>(param)->loraRadioRxTx();
}

void Service::loraRadioRxTx() 
{
  LOG_INFO("Lora task started");

  // setup radio
  setupRig(config_.LoraFreqRx, config_.LoraBw, config_.LoraSf, 
    config_.LoraCodingRate, config_.LoraPower, config_.LoraSync, config_.LoraCrc, config_.LoraExplicit);

  int state = rig_->startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    LOG_ERROR("Receive start error:", state);
  }

  // wait for ISR notification, read data and send for audio processing
  while (true) {
    uint32_t lora_status_bits = 0;
    xTaskNotifyWaitIndexed(0, 0x00, ULONG_MAX, &lora_status_bits, portMAX_DELAY);

    LOG_DEBUG("Lora task bits", lora_status_bits);

    // lora rx
    if (lora_status_bits & CfgRadioRxBit) {
      int packet_size = rig_->getPacketLength();
      if (packet_size > 0) {
        int state = rig_->readData(loraRadioRxBuf_, packet_size);
        if (state == RADIOLIB_ERR_NONE) {
          // process packet
          LOG_DEBUG("Received packet, size", packet_size);
          if (packet_size % codecBytesPerFrame_ == 0) {
            for (int i = 0; i < packet_size; i++) {
                loraRadioRxQueue_.push(loraRadioRxBuf_[i]);
            }
            loraRadioRxQueueIndex_.push(packet_size);
            uint32_t audio_play_bit = CfgAudioPlayBit;
            xTaskNotify(audioTaskHandle_, audio_play_bit, eSetBits);
          } else {
            LOG_ERROR("Audio packet of wrong size, expected mod", codecBytesPerFrame_);
          }
        } else {
          LOG_ERROR("Read data error: ", state);
        }
        // probably not needed, still in receive
        state = rig_->startReceive();
        if (state != RADIOLIB_ERR_NONE) {
          LOG_ERROR("Start receive error: ", state);
        }
        lightSleepReset();
      } // packet size > 0
    } // lora rx
    // lora tx data
    else if (lora_status_bits & CfgRadioTxBit) {
      loraIsrEnabled_ = false;
      // take packet by packet
      while (loraRadioTxQueueIndex_.size() > 0) {
        // take packet size and read it
        int tx_bytes_cnt = loraRadioTxQueueIndex_.shift();
        for (int i = 0; i < tx_bytes_cnt; i++) {
          loraRadioTxBuf_[i] = loraRadioTxQueue_.shift();
        }
        // transmit packet
        int lora_radio_state = rig_->transmit(loraRadioTxBuf_, tx_bytes_cnt);
        if (lora_radio_state != RADIOLIB_ERR_NONE) {
          LOG_ERROR("Lora radio transmit failed:", lora_radio_state);
        }
        LOG_DEBUG("Transmitted packet", tx_bytes_cnt);
        vTaskDelay(1);
        lightSleepReset();
      } // packet transmit loop
      
      // switch to receive after all transmitted
      int lora_radio_state = rig_->startReceive();
      if (lora_radio_state != RADIOLIB_ERR_NONE) {
        LOG_ERROR("Start receive error: ", lora_radio_state);
      }
      loraIsrEnabled_ = true;
    } // lora tx
  }  // task loop
}

void Service::audioTask(void *param) {
  static_cast<Service*>(param)->audioPlayRecord();
}

void Service::audioPlayRecord()
{
  LOG_INFO("Audio task started");

  // construct codec2
  codec_ = codec2_create(config_.AudioCodec2Mode);
  if (codec_ == NULL) {
    LOG_ERROR("Failed to create codec2");
    return;
  } else {
    codecSamplesPerFrame_ = codec2_samples_per_frame(codec_);
    codecBytesPerFrame_ = codec2_bytes_per_frame(codec_);
    codecSamples_ = (int16_t*)malloc(sizeof(int16_t) * codecSamplesPerFrame_);
    codecBits_ = (uint8_t*)malloc(sizeof(uint8_t) * codecBytesPerFrame_);
    LOG_INFO("C2 initialized", codecSamplesPerFrame_, codecBytesPerFrame_);
    setupAudio(codecSamplesPerFrame_);
  }

  // wait for data notification, decode frames and playback
  size_t bytes_read, bytes_written;
  while(true) {
    uint32_t audio_bits = 0;
    xTaskNotifyWaitIndexed(0, 0x00, ULONG_MAX, &audio_bits, portMAX_DELAY);

    LOG_DEBUG("Audio task bits", audio_bits);

    // audio rx-decode-playback
    if (audio_bits & CfgAudioPlayBit) {
      LOG_DEBUG("Playing audio");
      double vol = (double)codecVolume_ / (double)CfgAudioMaxVolume;
      LOG_DEBUG("Volume is", vol);
      // while rx frames are available and button is not pressed
      while (!btnPressed_ && loraRadioRxQueueIndex_.size() > 0) {
        int packet_size = loraRadioRxQueueIndex_.shift();
        LOG_DEBUG("Playing packet", packet_size);
        // split by frame, decode and play
        for (int i = 0; i < packet_size; i++) {
          codecBits_[i % codecBytesPerFrame_] = loraRadioRxQueue_.shift();
          if (i % codecBytesPerFrame_ == codecBytesPerFrame_ - 1) {
            codec2_decode(codec_, codecSamples_, codecBits_);
            for (int j = 0; j < codecSamplesPerFrame_; j++) {
              codecSamples_[j] *= vol;
            }
            i2s_write(CfgAudioI2sSpkId, codecSamples_, sizeof(uint16_t) * codecSamplesPerFrame_, &bytes_written, portMAX_DELAY);
            vTaskDelay(1);
          }
        }
      } // while rx data available
    } // audio decode playback
    // audio record-encode-tx
    else if (audio_bits & CfgAudioRecBit) {
      LOG_DEBUG("Recording audio");
      int packet_size = 0;
      // record while button is pressed
      i2s_start(CfgAudioI2sMicId);
      while (btnPressed_) {
        // send packet if enough audio encoded frames are accumulated
        if (packet_size + codecBytesPerFrame_ > config_.AudioMaxPktSize) {
          LOG_DEBUG("Recorded packet", packet_size);
          loraRadioTxQueueIndex_.push(packet_size);
          uint32_t lora_tx_bits = CfgRadioTxBit;
          xTaskNotify(loraTaskHandle_, lora_tx_bits, eSetBits);
          packet_size = 0;
        }
        // read and encode one sample
        if (!btnPressed_) break;
        size_t bytes_read;
        i2s_read(CfgAudioI2sMicId, codecSamples_, sizeof(uint16_t) * codecSamplesPerFrame_, &bytes_read, portMAX_DELAY);
        if (!btnPressed_) break;
        codec2_encode(codec_, codecBits_, codecSamples_);
        if (!btnPressed_) break;
        for (int i = 0; i < codecBytesPerFrame_; i++) {
          loraRadioTxQueue_.push(codecBits_[i]);
        }
        packet_size += codecBytesPerFrame_;
        vTaskDelay(1);
      } // btn_pressed_
      // send remaining tail audio encoded samples
      if (packet_size > 0) {
          LOG_DEBUG("Recorded packet", packet_size);
          loraRadioTxQueueIndex_.push(packet_size);
          uint32_t lora_tx_bits = CfgRadioTxBit;
          xTaskNotify(loraTaskHandle_, lora_tx_bits, eSetBits);        
          packet_size = 0;
      }
      vTaskDelay(1);
      i2s_stop(CfgAudioI2sMicId);
    } // task bit
  }
}

void Service::loop() 
{
  // button 
  if (digitalRead(config_.PttBtnPin) == LOW && !btnPressed_) {
    LOG_DEBUG("PTT pushed, start TX");
    btnPressed_ = true;
    printStatus("TX");
    // notify to start recording
    uint32_t audio_bits = CfgAudioRecBit;
    xTaskNotify(audioTaskHandle_, audio_bits, eSetBits);
  } else if (digitalRead(config_.PttBtnPin) == HIGH && btnPressed_) {
    LOG_DEBUG("PTT released");
    printStatus("RX");
    btnPressed_ = false;
  }
  // rotary encoder
  if (rotaryEncoder_->encoderChanged())
  {
    LOG_INFO("Encoder changed:", rotaryEncoder_->readEncoder());
    codecVolume_ = rotaryEncoder_->readEncoder();
    printStatus("RX");
    lightSleepReset();
  }
  if (rotaryEncoder_->isEncoderButtonClicked())
  {
    LOG_INFO("Encoder button pressed");
    lightSleepReset();
  }
  if (rotaryEncoder_->isEncoderButtonClicked(2000))
  {
    LOG_INFO("Encoder button long pressed");
    lightSleepReset();
  }
  lightSleepTimer_.tick();
}

} // LoraDv