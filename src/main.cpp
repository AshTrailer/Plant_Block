#include <Arduino.h>
#include <DHT.h>
#include <Wire.h>
#include <U8x8lib.h>
#include <Ds1302.h>

#define PIN_SDA 22
#define PIN_SCL 23

#define PIN_SOILSENSOR_1 32
#define PIN_SOILSENSOR_2 33

#ifndef PIN_ENA
#define PIN_ENA 21
#define PIN_DAT 19
#define PIN_CLK 18
#endif

#define LINE_WIDTH 16

#define DHTPIN 5
#define DHTTYPE DHT11

const char* menuItems[] = {"Data", "SetTime"};
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
      Serial.println("Setting default time...");
      Ds1302::DateTime dt;
      dt.year   = 25;
      dt.month  = 11;
      dt.day    = 18;
      dt.hour   = 19;
      dt.minute = 18;
      dt.second = 30;
      dt.dow    = 4;

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

void setDateTime(const Ds1302::DateTime &dt) {
  Ds1302::DateTime temp = dt;
  rtc.setDateTime(&temp);
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

class SoilSensor {
private:
    uint8_t pin;
    float voltageSum;
    uint8_t sampleCount;
    float lastVoltage;

public:
    SoilSensor(uint8_t p) : pin(p), voltageSum(0), sampleCount(0), lastVoltage(0) {}

    void begin() {
      pinMode(pin, INPUT);
    }

    void update() {
        int raw = analogRead(pin);
        float v = raw * 3.0 / 4095.0;
        voltageSum += v;
        sampleCount++;
    }

    void calcAverage() {
        if (sampleCount > 0) {
            lastVoltage = voltageSum / sampleCount;
            voltageSum = 0;
            sampleCount = 0;
        }
    }

    float getVoltage() const {
        return lastVoltage;
    }
};

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
      snprintf(humBuf, sizeof(humBuf), "H: %.1f%", hum);
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
  SoilSensor sensor1;
  SoilSensor sensor2;
  unsigned long lastSoilUpdate = 0;
  unsigned long lastSoilDisplay = 0;

public:
  MenuSystem(U8X8_SSD1306_128X64_NONAME_HW_I2C &u8x8, DHT_Display &dht,
             const char* items[], int size)
      : display(u8x8), dhtDisplay(dht),
        currentMode(DATA_MODE), menuItems(items), 
        menuSize(size), cursorIndex(0), lastSecond(255),
        sensor1(PIN_SOILSENSOR_1), sensor2(PIN_SOILSENSOR_2) {}

  void begin() {
    sensor1.begin();
    sensor2.begin();
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
        handleSetTimeModes(btn1, btn2, btn3, btn4);
        break;
      case TEST2_MODE:
        break;
    }
  }

private:
  Ds1302::DateTime timeSnapshot;
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
      case SETTIME_MODE: display.drawString(0, 0, "    Set Time"); break;
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
        case 0: 
          currentMode = DATA_MODE; 
          break;
        case 1: 
          rtcManager.getDateTime(timeSnapshot);
          currentMode = SETTIME_MODE;
          break;
        case 2: 
          currentMode = TEST2_MODE; 
          break;
      }
      drawModeScreen(currentMode);
    }
  }

  void handleDataMode(bool btn4) {
    char buf[17];
    if (rtcManager.getFormattedMonthDayTime(buf, sizeof(buf))) {
      display.drawString(0, 1, buf);
    }

    unsigned long nowMillis = millis();

    if (nowMillis - lastSoilUpdate >= 1000) {
      lastSoilUpdate = nowMillis;
      sensor1.update();
      sensor2.update();
    }
    if (nowMillis - lastSoilDisplay >= 5000) {
      lastSoilDisplay = nowMillis;

      char buf[17];

      dhtDisplay.update(true);
      dhtDisplay.displayLast();

      sensor1.calcAverage();
      sensor2.calcAverage();

      snprintf(buf, sizeof(buf), "1: %.2fV", sensor1.getVoltage());
      display.drawString(0, 3, buf);

      snprintf(buf, sizeof(buf), "2: %.2fV", sensor2.getVoltage());
      display.drawString(0, 4, buf);
    }

    dhtDisplay.update(true);
    dhtDisplay.displayLast();

    if (btn4) {
      currentMode = MAIN_MENU;
      cursorIndex = 0;
      drawMainMenu();
    }
  }

  void handleSetTimeModes(bool btn1, bool btn2, bool btn3, bool btn4) {
    static int editIndex = 0;

    if (btn2 && editIndex > 0) editIndex--;
    if (btn3 && editIndex < 11) editIndex++;

    if (btn1) {
      uint8_t *digits[6] = {
          &timeSnapshot.year, &timeSnapshot.month, &timeSnapshot.day,
          &timeSnapshot.hour, &timeSnapshot.minute, &timeSnapshot.second
      };
      int valueIndex = editIndex / 2;
      int digitPos = editIndex % 2;
      int v = *digits[valueIndex];
      int tens = v / 10;
      int ones = v % 10;

      if (digitPos == 0) tens++;
      else ones++;

      switch (valueIndex) {
        case 0: // year (0~99)
          if (tens > 9) tens = 0;
          if (ones > 9) ones = 0;
          break;
        case 1: // month (1~12)
          {
          int month = tens * 10 + ones;
          if (month > 12) { tens = 0; ones = 1; }  // back to 01
          if (month < 1) { tens = 0; ones = 1; }
          }
          break;
        case 2: // day (1~maxDay)
          {
          int day = tens * 10 + ones;
          int year = timeSnapshot.year + 2000;
          int month = timeSnapshot.month;
          int maxDay = 31;
          if (month == 2) maxDay = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 29 : 28;
          else if (month == 4 || month == 6 || month == 9 || month == 11) maxDay = 30;

          if (day > maxDay) { tens = 0; ones = 1; } 
          if (day < 1) { tens = 0; ones = 1; }
          }
          break;
        case 3: // hour (0~23)
          if (tens * 10 + ones > 23) { tens = 0; ones = 0; }
          break;
        case 4: // minute (0~59)
        case 5: // second (0~59)
          if (tens * 10 + ones > 59) { tens = 0; ones = 0; }
          break;
      }

      *digits[valueIndex] = tens * 10 + ones;
  }

    char bufDate[11], bufTime[9];
    snprintf(bufDate, sizeof(bufDate), "20%02d/%02d/%02d",
             timeSnapshot.year, timeSnapshot.month, timeSnapshot.day);
    snprintf(bufTime, sizeof(bufTime), "%02d:%02d:%02d",
             timeSnapshot.hour, timeSnapshot.minute, timeSnapshot.second);

    display.drawString(0, 1, bufDate);
    display.drawString(0, 2, bufTime);

    if (btn4) {
        rtcManager.setDateTime(timeSnapshot);
        editIndex = 0;
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

  Wire.begin(22, 23);  // SDA=22, SCL=23

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












