#include <Arduino.h>
#include <DHT.h>
#include <Wire.h>
#include <U8x8lib.h>

#define LINE_WIDTH 16

#define DHTPIN 5
#define DHTTYPE DHT11

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

    void update() {
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

            display(avgTemp, avgHum);

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
            snprintf(buffer, sizeof(buffer), "T: %.1f", temp);
        }
        if (isnan(hum)) {
            strcat(buffer, "NaN%%");
        } else {
            char humBuf[16];
            snprintf(humBuf, sizeof(humBuf), " %.1f%%", hum);
            strcat(buffer, humBuf);
        }

        char empty[21];
        memset(empty, ' ', 20); empty[20] = '\0';
        u8x8.drawString(0, row, empty);
        u8x8.drawString(0, row, buffer);

        Serial.println(buffer);
    }
};

Button btn1(BUTTON1_PIN, BUTTON_PULSE);
Button btn2(BUTTON2_PIN, BUTTON_PULSE);
Button btn3(BUTTON3_PIN, BUTTON_PULSE);
Button btn4(BUTTON4_PIN, BUTTON_PULSE);

DHT_Display dhtDisplay(DHTPIN, 0);

void drawStringWrap(uint8_t col, uint8_t row, const char* str);
void clearLine(uint8_t row);



void setup() {
  Serial.begin(115200);

  Serial.println("Button test ready.");

  Wire.begin(22, 23);  // SDA=21, SCL=22

  u8x8.begin();
  u8x8.setFont(u8x8_font_chroma48medium8_r);
  u8x8.clear();
  
  dhtDisplay.begin();

  delay(50);
}

void loop() {
  bool btn1Out = btn1.update();
  bool btn2Out = btn2.update();
  bool btn3Out = btn3.update(); 
  bool btn4Out = btn4.update();

  dhtDisplay.update();

}


void drawStringWrap(uint8_t col, uint8_t row, const char* str)
{
    const uint8_t maxCols = 16; 
    uint8_t line = row;
    uint8_t colIndex = col;

    for (int i = 0; str[i] != '\0'; i++) {
        if (colIndex >= maxCols) {
            colIndex = 0;
            line++;
        }
        u8x8.setCursor(colIndex, line);
        u8x8.write(str[i]);
        colIndex++;
    }
}

void clearLine(uint8_t row) {
  char empty[LINE_WIDTH + 1];
  memset(empty, ' ', LINE_WIDTH);
  empty[LINE_WIDTH] = '\0';
  u8x8.drawString(0, row, empty);
}












