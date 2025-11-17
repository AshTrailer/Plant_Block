#include <Arduino.h>
#include <DHT.h>
#include <Wire.h>
#include <U8x8lib.h>

const int BUTTON1_PIN = 16;
const int BUTTON2_PIN = 4;
const int BUTTON3_PIN = 2;
const int BUTTON4_PIN = 15;

const int LED_PIN = 23;

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
      if (lastLogic == HIGH && _buttonState == LOW) {
        _output = true;
      } else {
          _output = false;
      }
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

Button btn1(BUTTON1_PIN, BUTTON_TOGGLE);
Button btn2(BUTTON2_PIN, BUTTON_HOLD);
Button btn3(BUTTON3_PIN, BUTTON_PULSE);
Button btn4(BUTTON4_PIN, BUTTON_REPEAT, BUTTON4_DEBOUNCE_MS, BUTTON4_REPEAT_MS);

U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(U8X8_PIN_NONE);

void drawStringWrap(uint8_t col, uint8_t row, const char* str);

uint32_t btn3Counter = 0;

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("Button test ready.");

  Wire.begin(21, 22);  // SDA=21, SCL=22

  u8x8.begin();
  u8x8.setFont(u8x8_font_chroma48medium8_r);
  u8x8.clear();

  pinMode(LED_PIN, OUTPUT);

  delay(50);
}

void loop() {
  bool btn1Out = btn1.update();
  bool btn2Out = btn2.update();
  bool btn3Out = btn3.update(); // <-- Pulse mode
  bool btn4Out = btn4.update();

  if (btn1Out) Serial.println("Button 1 TOGGLE");
  if (btn2Out) Serial.println("Button 2 HOLD");
  if (btn3Out) Serial.println("Button 3 PULSE");
  if (btn4Out) Serial.println("Button 4 REPEAT");

  if (btn3Out) {
    btn3Counter++;                  // 每次按下 +1
    digitalWrite(LED_PIN, HIGH);    // 亮一下，表示被触发
  } else {
    digitalWrite(LED_PIN, LOW);
  }

  // ----------- OLED 显示 Button3 计数 -----------
  u8x8.setCursor(0, 0);
  u8x8.print("Btn3 pressed:");

  u8x8.setCursor(0, 1);
  u8x8.print(btn3Counter);
}

void drawStringWrap(uint8_t col, uint8_t row, const char* str)
{
    const uint8_t maxCols = 16; // 128px / 8px
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
