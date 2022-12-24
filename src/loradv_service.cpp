#include "loradv_service.h"

namespace LoraDv {

std::shared_ptr<AiEsp32RotaryEncoder> Service::rotaryEncoder_;

Service::Service()
  : btnPressed_(false)
  , radioTask_(std::make_shared<RadioTask>())
  , audioTask_(std::make_shared<AudioTask>())
  , pmService_(std::make_shared<PmService>())
  , hwMonitor_(std::make_shared<HwMonitor>())
{
}

void Service::setup(std::shared_ptr<Config> config)
{
  config_ = config;

  LOG_SET_LEVEL(config_->LogLevel);
  LOG_SET_OPTION(false, false, true);  // disable file, line, enable func
  
  setupEncoder();
  setupScreen();

  LOG_INFO("PTT setup started");
  pinMode(config_->PttBtnPin_, INPUT);
  LOG_INFO("PTT setup completed");

  hwMonitor_->setup(config);
  pmService_->setup(config, display_);
  audioTask_->start(config, radioTask_, pmService_);
  radioTask_->start(config, audioTask_);
  LOG_INFO("Board setup completed");
}

void Service::setupEncoder() 
{
  LOG_INFO("Encoder setup started");
  rotaryEncoder_ = std::make_shared<AiEsp32RotaryEncoder>(config_->EncoderPinA_, config_->EncoderPinB_, 
    config_->EncoderPinBtn_, config_->EncoderPinVcc_, config_->EncoderSteps_);
  rotaryEncoder_->begin();
  rotaryEncoder_->setBoundaries(0, config_->AudioMaxVol_);
  rotaryEncoder_->setEncoderValue(config_->AudioVol);
  rotaryEncoder_->setup(isrReadEncoder);
  LOG_INFO("Encoder setup completed");
}

void Service::setupScreen() 
{
  display_ = std::make_shared<Adafruit_SSD1306>(CfgDisplayWidth, CfgDisplayHeight, &Wire, -1);
  if(display_->begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    LOG_INFO("Display setup completed");
  } else {
    LOG_ERROR("Display init failed");
  }
}

IRAM_ATTR void Service::isrReadEncoder()
{
  rotaryEncoder_->readEncoder_ISR();
}

void Service::printStatus(const String &str) const
{
  display_->clearDisplay();
  display_->setTextSize(2);
  display_->setTextColor(WHITE);
  display_->setCursor(0, 0);
  display_->print(str); display_->print(" "); 
  if (btnPressed_)
    display_->println((float)config_->LoraFreqTx / 1e6, 3);
  else
    display_->println((float)config_->LoraFreqRx / 1e6, 3);
  display_->print(audioTask_->getVolume()); display_->print("% "); 
  display_->print(hwMonitor_->getBatteryVoltage()); display_->print("V");
  display_->display();
}

void Service::processPttButton()
{
    // button 
  if (digitalRead(config_->PttBtnPin_) == LOW && !btnPressed_) {
    btnPressed_ = true;
    LOG_DEBUG("PTT pushed, start TX");
    printStatus("TX");
    audioTask_->setPtt(true);
    audioTask_->record();
  } else if (digitalRead(config_->PttBtnPin_) == HIGH && btnPressed_) {
    btnPressed_ = false;
    LOG_DEBUG("PTT released");
    printStatus("RX");
    audioTask_->setPtt(false);
  }
}

void Service::processRotaryEncoder()
{
  // rotary encoder
  if (rotaryEncoder_->encoderChanged())
  {
    LOG_INFO("Encoder changed:", rotaryEncoder_->readEncoder());
    audioTask_->setVolume(rotaryEncoder_->readEncoder());
    printStatus("RX");
    pmService_->lightSleepReset();
  }
  if (rotaryEncoder_->isEncoderButtonClicked())
  {
    LOG_INFO("Encoder button pressed", esp_get_free_heap_size());
    pmService_->lightSleepReset();
  }
  if (rotaryEncoder_->isEncoderButtonClicked(2000))
  {
    LOG_INFO("Encoder button long pressed");
    pmService_->lightSleepReset();
  }
}

void Service::loop() 
{
  processPttButton();
  processRotaryEncoder();
  if (pmService_->loop()) {
    printStatus("RX");
  }
}

} // LoraDv