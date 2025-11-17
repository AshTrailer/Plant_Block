#include <Arduino.h>
#include <DHT.h>
#include <Wire.h>
#include <U8x8lib.h>
#include <Ds1302.h>

#ifndef PIN_ENA
#define PIN_ENA 21
#define PIN_DAT 19
#define PIN_CLK 18
#endif

#define LINE_WIDTH 16

#define DHTPIN 5
#define DHTTYPE DHT11

const char* menuItems[] = {"Data", "SetTime", "test2"};
int menuSize = sizeof(menuItems) / sizeof(menuItems[0]);

const int BUTTON1_PIN = 17;
const int BUTTON2_PIN = 16;
const int BUTTON3_PIN = 4;
const int BUTTON4_PIN = 15;
const unsigned long BUTTON4_DEBOUNCE_MS = 50;
const unsigned long BUTTON4_REPEAT_MS   = 200;

enum ButtonMode {
  BUTTON_TOGGLE,
  BUTTON_HOLD,
  BUTTON_PULSE,
  BUTTON_REPEAT
};

U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(U8X8_PIN_NONE);

class RTCManager {
private:
  Ds1302 rtc;
  uint8_t lastSecond;

  static const char* WeekDays[7];

public:
  RTCManager(uint8_t pinCE, uint8_t pinCLK, uint8_t pinDAT)
    : rtc(pinCE, pinCLK, pinDAT), lastSecond(255) {}

  void begin() {
    rtc.init();

    if (rtc.isHalted()) {
      Serial.println("RTC halted. Setting default time...");
      Ds1302::DateTime dt;
      dt.year   = 25;
      dt.month  = 1;
      dt.day    = 1;
      dt.hour   = 1;
      dt.minute = 1;
      dt.second = 1;
      dt.dow    = 3;

      rtc.setDateTime(&dt);
    }
}

void printIfSecondChanged() {
  Ds1302::DateTime now;
  rtc.getDateTime(&now);

  if (now.second != lastSecond) {
    lastSecond = now.second;
    printTime(now);
  }
}

void getDateTime(Ds1302::DateTime &dt) {
        rtc.getDateTime(&dt);
}

bool getFormattedDate(char *buf, size_t size) {
  Ds1302::DateTime now;
  rtc.getDateTime(&now);
  if (now.second != lastSecond) {
    lastSecond = now.second;
    snprintf(buf, size, "20%02d/%02d/%02d", now.year, now.month, now.day);
    return true; 
  }
  return false;
}

bool getFormattedDateTime(char *bufDate, size_t sizeDate,
                          char *bufTime, size_t sizeTime) {
  Ds1302::DateTime now;
  rtc.getDateTime(&now);

  if (now.second != lastSecond) {
    lastSecond = now.second;
    snprintf(bufDate, sizeDate, "20%02d/%02d/%02d", now.year, now.month, now.day);
    snprintf(bufTime, sizeTime, "%02d:%02d:%02d", now.hour, now.minute, now.second);
    return true;
  }
  return false;
}

bool getFormattedMonthDayTime(char *buf, size_t size) {
  Ds1302::DateTime now;
  rtc.getDateTime(&now);
  if (now.second != lastSecond) {
    lastSecond = now.second;
    snprintf(buf, size, "%02d/%02d %02d:%02d:%02d",
              now.month, now.day, now.hour, now.minute, now.second);
    return true;
  }
  return false;
}

private:
  void printTime(const Ds1302::DateTime& now) {
    Serial.print("20");
    if (now.year < 10) Serial.print('0');
    Serial.print(now.year);
    Serial.print('-');
    if (now.month < 10) Serial.print('0');
    Serial.print(now.month);
    Serial.print('-');
    if (now.day < 10) Serial.print('0');
    Serial.print(now.day);
    Serial.print(' ');
    Serial.print(WeekDays[now.dow - 1]);
    Serial.print(' ');
    if (now.hour < 10) Serial.print('0');
    Serial.print(now.hour);
    Serial.print(':');
    if (now.minute < 10) Serial.print('0');
    Serial.print(now.minute);
    Serial.print(':');
    if (now.second < 10) Serial.print('0');
    Serial.print(now.second);
    Serial.println();
  }
};
const char* RTCManager::WeekDays[7] = {
    "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"
};
RTCManager rtcManager(PIN_ENA, PIN_CLK, PIN_DAT);

class Button {
  public:
    Button(int pin, ButtonMode mode, unsigned long debounce = 20, unsigned long repeatInterval = 500)
      : _pin(pin), _mode(mode), _debounce(debounce), _repeatInterval(repeatInterval),
        _lastState(HIGH), _buttonState(HIGH), _lastDebounceTime(0),
        _output(false), _lastOutputTime(0) {
      pinMode(pin, INPUT_PULLUP);
    }

    bool update() {
      bool reading = digitalRead(_pin);

      debounceCheck(reading);

      if ((millis() - _lastDebounceTime) > _debounce) {
        bool lastLogic = _buttonState;
        _buttonState = reading;

        switch (_mode) {
          case BUTTON_TOGGLE:
            handleToggle(lastLogic);
            break;

          case BUTTON_HOLD:
            handleHold();
            break;

          case BUTTON_PULSE:
            handlePulse(lastLogic);
            break;

          case BUTTON_REPEAT:
            handleRepeat();
            break;
        }
      }

      _lastState = reading;
      return _output;
    }

  private:
    int _pin;
    ButtonMode _mode;
    unsigned long _debounce;
    unsigned long _repeatInterval;

    bool _lastState;
    bool _buttonState;
    unsigned long _lastDebounceTime;

    bool _output;
    unsigned long _lastOutputTime;

    void debounceCheck(bool reading) {
      if (reading != _lastState) {
        _lastDebounceTime = millis();
      }
    }

    void handleToggle(bool lastLogic) {
      if (lastLogic == HIGH && _buttonState == LOW) {
        _output = !_output;
      }
    }

    void handleHold() {
      _output = (_buttonState == LOW);
    }

    void handlePulse(bool lastLogic) {
      _output = (lastLogic == HIGH && _buttonState == LOW);
    }

    void handleRepeat() {
      if (_buttonState == LOW) {
        if (millis() - _lastOutputTime >= _repeatInterval) {
          _output = true;
          _lastOutputTime = millis();
        } else {
          _output = false;
        }
      } else {
        _output = false;
      }
    }
};

class DHT_Display {
private:
  DHT dht;
  uint8_t row;
  float lastTemp;
  float lastHum;
  unsigned long lastSampleTime;
  unsigned long lastUpdateTime;
  float tempSum;
  float humSum;
  int sampleCount;

  bool fastMode;
  const unsigned long sampleIntervalSlow = 2500; // 2.5s
  const unsigned long sampleIntervalFast = 1000; // 1s
  const unsigned long updateInterval = 5000;     // 5s
  const float tempThreshold = 3.0;               // ℃
  const float humThreshold  = 20.0;              // %

public:
  DHT_Display(uint8_t pin, uint8_t r) : dht(pin, DHTTYPE), row(r),
                                        lastTemp(NAN), lastHum(NAN),
                                        lastSampleTime(0), lastUpdateTime(0),
                                        tempSum(0), humSum(0), sampleCount(0),
                                        fastMode(false) {}

  void begin() {
    dht.begin();
  }

  // now takes a flag: if showOnOLED==true then draws to OLED; otherwise only samples + Serial
  void update(bool showOnOLED) {
    unsigned long now = millis();

    unsigned long interval = fastMode ? sampleIntervalFast : sampleIntervalSlow;
    if (now - lastSampleTime >= interval) {
      lastSampleTime = now;
      float t = dht.readTemperature();
      float h = dht.readHumidity();
      if (!isnan(t)) tempSum += t;
      if (!isnan(h)) humSum += h;
      sampleCount++;
    }

    if (now - lastUpdateTime >= updateInterval) {
      lastUpdateTime = now;

      float avgTemp = NAN;
      float avgHum  = NAN;

      if (sampleCount > 0) {
        avgTemp = tempSum / sampleCount;
        avgHum  = humSum  / sampleCount;
      }

      char buf[40];
      snprintf(buf, sizeof(buf), "T=%.1f H=%.1f", avgTemp, avgHum);
      Serial.println(buf);

      if (showOnOLED) {
        display(avgTemp, avgHum);
      }

      if (!isnan(avgTemp) && !isnan(avgHum) && !isnan(lastTemp) && !isnan(lastHum)) {
        if (!fastMode) {
          if (abs(avgTemp - lastTemp) > tempThreshold || abs(avgHum - lastHum) > humThreshold) {
            fastMode = true;
          }
        } else {
          if (abs(avgTemp - lastTemp) <= tempThreshold && abs(avgHum - lastHum) <= humThreshold) {
            fastMode = false;
          }
        }
      }

      if (!isnan(avgTemp)) lastTemp = avgTemp;
      if (!isnan(avgHum))  lastHum  = avgHum;

      tempSum = 0;
      humSum = 0;
      sampleCount = 0;
    }
  }

  void display(float temp, float hum) {
    char buffer[32];
    if (isnan(temp)) {
      snprintf(buffer, sizeof(buffer), "T: NaN H: ");
    } else {
      snprintf(buffer, sizeof(buffer), "T: %.1fC", temp);
    }
    if (isnan(hum)) {
      strcat(buffer, "NaN%");
    } else {
      char humBuf[16];
      snprintf(humBuf, sizeof(humBuf), " %.1f%", hum);
      strcat(buffer, humBuf);
    }
    u8x8.drawString(0, row, buffer);
  }

  void displayLast() {
    display(lastTemp, lastHum);
  }
};
DHT_Display dhtDisplay(DHTPIN, 2);

class MenuSystem {  
public:
  enum Mode {
    MAIN_MENU,
    DATA_MODE,
    SETTIME_MODE,
    TEST2_MODE
  };

private:
  U8X8_SSD1306_128X64_NONAME_HW_I2C &display;
  DHT_Display &dhtDisplay;
  Mode currentMode;
  const char **menuItems;
  int menuSize;
  int cursorIndex;
  uint8_t lastSecond;

public:
  MenuSystem(U8X8_SSD1306_128X64_NONAME_HW_I2C &u8x8, DHT_Display &dht,
             const char* items[], int size)
      : display(u8x8), dhtDisplay(dht),
        currentMode(DATA_MODE), menuItems(items), 
        menuSize(size), cursorIndex(0), lastSecond(255) {}

  void begin() {
    if (currentMode == DATA_MODE) {
      drawModeScreen(DATA_MODE);
    } else {
      drawMainMenu();
    }
  }

  void update(bool btn1, bool btn2, bool btn3, bool btn4) {
    switch (currentMode) {
      case MAIN_MENU:
        handleMainMenu(btn1, btn2, btn3);
        break;
      case DATA_MODE:
        handleDataMode(btn4);
        break;
      case SETTIME_MODE:
        handleSetTimeModes(btn4);
        break;
      case TEST2_MODE:
        break;
    }
  }

private:
  void drawMainMenu() {
    display.clear();
    display.drawString(0, 0, "   Main Menu");
    for (int i = 0; i < menuSize; i++) {
      char buffer[20];
      snprintf(buffer, sizeof(buffer), "%-12s%s", menuItems[i], 
              (i == cursorIndex) ? "<-" : "");
      display.drawString(0, i + 1, buffer);
    }
  }

  void drawModeScreen(Mode m) {
    display.clear();
    switch (m) {
      case DATA_MODE: display.drawString(0, 0, "      Data"); break;
      case SETTIME_MODE: display.drawString(0, 0, "   Set Time"); break;
      case TEST2_MODE: display.drawString(0, 0, "    Test2"); break;
      default: break;
    }
  }

  void handleMainMenu(bool btn1, bool btn2, bool btn3) {
    dhtDisplay.update(false);
    Ds1302::DateTime now;
    rtcManager.getDateTime(now);
    char buf[11];
    snprintf(buf, sizeof(buf), "20%02d/%02d/%02d", now.year, now.month, now.day);
    display.drawString(16, 7, buf);

    if (btn2 && cursorIndex > 0) {
      cursorIndex--;
      drawMainMenu();
    }
    if (btn3 && cursorIndex < menuSize - 1) {
      cursorIndex++;
      drawMainMenu();
    }
    if (btn1) {
      switch (cursorIndex) {
        case 0: currentMode = DATA_MODE; break;
        case 1: currentMode = SETTIME_MODE; break;
        case 2: currentMode = TEST2_MODE; break;
      }
      drawModeScreen(currentMode);
    }
  }

  void handleDataMode(bool btn4) {
    char buf[17];
    if (rtcManager.getFormattedMonthDayTime(buf, sizeof(buf))) {
      display.drawString(0, 1, buf);
    }

    dhtDisplay.update(true);
    dhtDisplay.displayLast();

    if (btn4) {
      currentMode = MAIN_MENU;
      cursorIndex = 0;
      drawMainMenu();
    }
  }

  void handleSetTimeModes(bool btn4) {
    char bufDate[11], bufTime[9];
    if (rtcManager.getFormattedDateTime(bufDate, sizeof(bufDate), bufTime, sizeof(bufTime))) {
      display.drawString(0, 1, bufDate);
      display.drawString(0, 2, bufTime);
    }

    if (btn4) {
      currentMode = MAIN_MENU;
      cursorIndex = 0;
      drawMainMenu();
    }
  }
};

Button btn1(BUTTON1_PIN, BUTTON_PULSE);
Button btn2(BUTTON2_PIN, BUTTON_PULSE);
Button btn3(BUTTON3_PIN, BUTTON_PULSE);
Button btn4(BUTTON4_PIN, BUTTON_PULSE);

MenuSystem menu(u8x8, dhtDisplay, menuItems, menuSize);

void clearLine(uint8_t row);


void setup() {
  Serial.begin(115200);

  Wire.begin(22, 23);  // SDA=21, SCL=22

  u8x8.begin();
  u8x8.setFont(u8x8_font_chroma48medium8_r);
  u8x8.clear();
  
  rtcManager.begin();
  Serial.println("RTC ready");
  delay(100);
  dhtDisplay.begin();
  Serial.println("RTC ready");
  delay(100);
  menu.begin();
  Serial.println("OLED Menu ready");
  delay(100);

  Serial.println("All Setup ready");
  delay(1000);
}

void loop() {
  bool b1 = btn1.update();
  bool b2 = btn2.update();
  bool b3 = btn3.update();
  bool b4 = btn4.update();

  menu.update(b1, b2, b3, b4);
}



void clearLine(uint8_t row) {
  char empty[LINE_WIDTH + 1];
  memset(empty, ' ', LINE_WIDTH);
  empty[LINE_WIDTH] = '\0';
  u8x8.drawString(0, row, empty);
}












