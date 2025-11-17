#include <Arduino.h>
#include <DHT.h>
#include <Wire.h>
#include <U8x8lib.h>

#define LINE_WIDTH 16

#define DHTPIN 5         // DHT11 数据口接 ESP32 的 GPIO5
#define DHTTYPE DHT11    // DHT11 类型

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

Button btn1(BUTTON1_PIN, BUTTON_PULSE);
Button btn2(BUTTON2_PIN, BUTTON_PULSE);
Button btn3(BUTTON3_PIN, BUTTON_PULSE);
Button btn4(BUTTON4_PIN, BUTTON_PULSE);

U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(U8X8_PIN_NONE);

void drawStringWrap(uint8_t col, uint8_t row, const char* str);
void clearLine(uint8_t row);

DHT dht(DHTPIN, DHTTYPE);
unsigned long DHTlastUpdate = 0;
const unsigned long DHTupdateInterval = 5000;

void setup() {
  Serial.begin(115200);

  Serial.println("Button test ready.");

  Wire.begin(22, 23);  // SDA=21, SCL=22

  u8x8.begin();
  u8x8.setFont(u8x8_font_chroma48medium8_r);
  u8x8.clear();
  
  dht.begin();

  delay(50);
}

void loop() {
  bool btn1Out = btn1.update();
  bool btn2Out = btn2.update();
  bool btn3Out = btn3.update(); 
  bool btn4Out = btn4.update();

  unsigned long now = millis();
  if (now - DHTlastUpdate >= DHTupdateInterval) {
    DHTlastUpdate = now;

    float temp = dht.readTemperature();
    float hum = dht.readHumidity();

    char buffer[32];

    clearLine(0);
    if (isnan(temp)) {
      snprintf(buffer, sizeof(buffer), "Temp: NaN");
    } else {
      snprintf(buffer, sizeof(buffer), "Temp: %.1f C", temp);
    }
    u8x8.drawString(0, 0, buffer);

    clearLine(1);
    if (isnan(hum)) {
      snprintf(buffer, sizeof(buffer), "Humidity: NaN");
    } else {
      snprintf(buffer, sizeof(buffer), "Humidity: %.1f %%", hum);
    }
    u8x8.drawString(0, 1, buffer);

    Serial.print("Temperature: ");
    if (isnan(temp)) Serial.print("NaN");
    else Serial.print(temp);
    Serial.print(" ℃, Humidity: ");
    if (isnan(hum)) Serial.println("NaN");
    else Serial.print(hum); Serial.println(" %");
  }
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












