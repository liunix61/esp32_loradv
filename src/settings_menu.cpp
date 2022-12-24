#include "settings_menu.h"

namespace LoraDv {

class SettingsLoraFreqStepItem : SettingsItem {
private:
  static const int CfgItemsCount = 7;
public:
  SettingsLoraFreqStepItem(std::shared_ptr<Config> config) 
    : SettingsItem(config)
    , items_{ 1000, 5000, 6250, 10000, 12500, 20000, 25000 } 
  {
    for (selIndex_ = 0; selIndex_ < CfgItemsCount; selIndex_++)
      if (config_->LoraFreqStep == items_[selIndex_])
        break;
  }
  void changeValue(int delta) {
    config_->LoraFreqStep = items_[(selIndex_ + delta) % CfgItemsCount];
  }
  void getName(std::stringstream &s) const { s << "Frequency Step"; }
  void getValue(std::stringstream &s) const { s << config_->LoraFreqStep << "Hz"; }
private:
  int selIndex_;
  long items_[CfgItemsCount];
};

class SettingsLoraFreqRxItem : SettingsItem {
public:
  void changeValue(int delta) { 
    long newVal = config_->LoraFreqRx + config_->LoraFreqStep * delta;
    if (newVal >= 400e6 && newVal <= 520e6) config_->LoraFreqRx = newVal;
  }
  void getName(std::stringstream &s) const { s << "RX Frequency"; }
  void getValue(std::stringstream &s) const { s << config_->LoraFreqRx << "Hz"; }
};

class SettingsLoraFreqTxItem : SettingsItem {
public:
  void changeValue(int delta) {
    long newVal = config_->LoraFreqTx + config_->LoraFreqStep * delta;
    if (newVal >= 400e6 && newVal <= 520e6) config_->LoraFreqTx = newVal;
  }
  void getName(std::stringstream &s) const { s << "TX Frequency"; }
  void getValue(std::stringstream &s) const { s << config_->LoraFreqTx << "Hz"; }
};

class SettingsLoraBwItem : SettingsItem {
private:
  static const int CfgItemsCount = 10;
public:
  SettingsLoraBwItem(std::shared_ptr<Config> config) 
    : SettingsItem(config)
    , items_{ 7800, 10400, 15600, 20800, 31250, 41700, 62500, 125000, 250000, 500000 } 
  {
    for (selIndex_ = 0; selIndex_ < CfgItemsCount; selIndex_++)
      if (config_->LoraBw == items_[selIndex_])
        break;
  }
  void changeValue(int delta) {
    config_->LoraBw = items_[(selIndex_ + delta) % CfgItemsCount];
  }
  void getName(std::stringstream &s) const { s << "Bandwidth"; }
  void getValue(std::stringstream &s) const { s << config_->LoraBw << "Hz"; }
private:
  int selIndex_;
  long items_[CfgItemsCount];
};

class SettingsLoraSfItem : SettingsItem {
public:
  void changeValue(int delta) { 
    long newVal = config_->LoraSf + delta;
    if (newVal >= 6 && newVal <= 12) config_->LoraSf = newVal;
  }
  void getName(std::stringstream &s) const { s << "Spreading Factor"; }
  void getValue(std::stringstream &s) const { s << config_->LoraSf; }
};

class SettingsLoraCrItem : SettingsItem {
public:
  void changeValue(int delta) { 
    long newVal = config_->LoraCodingRate + delta;
    if (newVal >= 5 && newVal <= 8) config_->LoraCodingRate = newVal;
  }
  void getName(std::stringstream &s) const { s << "Coding Rate"; }
  void getValue(std::stringstream &s) const { s << config_->LoraCodingRate; }
};

class SettingsLoraPowerItem : SettingsItem {
public:
  void changeValue(int delta) { 
    long newVal = config_->LoraPower + delta;
    if (newVal >= 2 && newVal <= 22) config_->LoraPower = newVal;
  }
  void getName(std::stringstream &s) const { s << "Power"; }
  void getValue(std::stringstream &s) const { s << config_->LoraPower << "dBm"; }
};

class SettingsAudioCodec2ModeItem : SettingsItem {
private:
  static const int CfgItemsCount = 8;
public:
  SettingsAudioCodec2ModeItem(std::shared_ptr<Config> config) 
    : SettingsItem(config)
    , items_{ 
      CODEC2_MODE_3200,
      CODEC2_MODE_2400,
      CODEC2_MODE_1600,
      CODEC2_MODE_1400,
      CODEC2_MODE_1300,
      CODEC2_MODE_1200,
      CODEC2_MODE_700C,
      CODEC2_MODE_450 
    }
    , map_{ 
      { CODEC2_MODE_3200, "3200" },
      { CODEC2_MODE_2400, "2400" },
      { CODEC2_MODE_1600, "1600" },
      { CODEC2_MODE_1400, "1400" },
      { CODEC2_MODE_1300, "1300" },
      { CODEC2_MODE_1200, "1200" },
      { CODEC2_MODE_700C, "700" },
      { CODEC2_MODE_450, "450" }
    }
  {
    for (selIndex_ = 0; selIndex_ < CfgItemsCount; selIndex_++)
      if (config_->LoraBw == items_[selIndex_])
        break;
  }
  void changeValue(int delta) {
    config_->AudioCodec2Mode = items_[(selIndex_ + delta) % CfgItemsCount];
  }
  void getName(std::stringstream &s) const { s << "Codec2 Mode"; }
  void getValue(std::stringstream &s) const { 
    for (int i = 0; i < CfgItemsCount; i++)
      if (config_->AudioCodec2Mode == map_[i].k) {
        s << map_[i].val << "bps"; 
        break;
      }
  }
private:
  int selIndex_;
  long items_[CfgItemsCount];
  struct MapItem { 
    int k; 
    const char *val; 
  } map_[CfgItemsCount];
};

class SettingsAudioVolItem : SettingsItem {
public:
  void changeValue(int delta) { 
    long newVal = config_->AudioVol + delta;
    if (newVal >= 0 && newVal <= 100) config_->AudioVol = newVal;
  }
  void getName(std::stringstream &s) const { s << "Default Volume"; }
  void getValue(std::stringstream &s) const { s << config_->AudioVol; }
};

class SettingsBatteryMonCalItem : SettingsItem {
public:
  void changeValue(int delta) { 
    float newVal = config_->BatteryMonCal + 0.01f * delta;
    if (newVal >= -2.0f && newVal <= 2.0f) config_->BatteryMonCal = newVal;
  }
  void getName(std::stringstream &s) const { s << "Battery Calibration"; }
  void getValue(std::stringstream &s) const { s << config_->BatteryMonCal << "V"; }
};

class SettingsPmLightSleepAfterMsItem : SettingsItem {
public:
  void changeValue(int delta) { 
    int newVal = config_->PmLightSleepAfterMs + 1000 * delta;
    if (newVal >= 10 && newVal <= 5*60) config_->PmLightSleepAfterMs = newVal * 1000;
  }
  void getName(std::stringstream &s) const { s << "Sleep After"; }
  void getValue(std::stringstream &s) const { s << config_->PmLightSleepAfterMs / 1000 << "s"; }
};

SettingsMenu::SettingsMenu() 
{
}

} // LoraDv