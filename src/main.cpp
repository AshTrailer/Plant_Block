#include <Arduino.h>

#include <LiquidCrystal.h>
#include <DHT.h>

/* --- LCD (16x2, parallel) --- */
const int rs = 12, en = 11, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

/* --- DHT11 --- */
#define DHTPIN   7
#define DHTTYPE  DHT11
DHT dht(DHTPIN, DHTTYPE);

/* --- Sensors --- */
const int SOIL1_PIN = A0;
const int SOIL2_PIN = A1;
const int LDR_PIN   = A2;

/* --- Single calibration for both soil probes (simplified) --- */
const int airVal   = 430;   // very dry
const int waterVal = 150;   // fully wet

/* --- Timing --- */
unsigned long lastDhtMs   = 0;
unsigned long lastPageMs  = 0;
const unsigned long DHT_PERIOD_MS  = 2000;
const unsigned long PAGE_PERIOD_MS = 5000;

float T = NAN, H = NAN;     // latest DHT values
bool showPageA = true;      // toggle display pages
static unsigned long lastPrint = 0; 

int soilPercentFromRaw(int raw) {
  long span = (long)airVal - (long)waterVal;   // e.g. 430-150=280
  if (span <= 0) return 0;
  long val = (long)airVal - raw;
  int pct = (int)(100L * val / span);
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  return pct;
}

void clearTail(uint8_t printed) {
  for (uint8_t i = printed; i < 16; i++) lcd.print(' ');
}

void setup() {
  Serial.begin(9600);
  dht.begin();
  lcd.begin(16, 2);
  lcd.print("Sensors Ready");
  delay(800);
}

void loop() {
  unsigned long now = millis();

  int soil1Pct = soilPercentFromRaw(analogRead(SOIL1_PIN));
  int soil2Pct = soilPercentFromRaw(analogRead(SOIL2_PIN));
  int lightPct = map(analogRead(LDR_PIN), 0, 1023, 0, 100);

  if (now - lastDhtMs >= DHT_PERIOD_MS) {
    lastDhtMs = now;
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (!isnan(h) && !isnan(t)) { H = h; T = t; }
  }

  if (now - lastPageMs >= PAGE_PERIOD_MS) {
    lastPageMs = now;
    showPageA = !showPageA;
    lcd.clear();
  }

  if (showPageA) {
    lcd.setCursor(0, 0);
    lcd.print("S1:"); lcd.print(soil1Pct); lcd.print("% ");
    lcd.print("S2:"); lcd.print(soil2Pct); lcd.print("%");


    lcd.setCursor(0, 1);
    lcd.print("L:"); lcd.print(lightPct); lcd.print("%");
    clearTail( (uint8_t) (2 + (lightPct>=100?3:(lightPct>=10?2:1)) + 1) );
  } else {
    lcd.setCursor(0, 0);
    lcd.print("T:");
    if (!isnan(T)) lcd.print(T, 1); else lcd.print("--.-");
    lcd.print("C  H:");
    if (!isnan(H)) { lcd.print((int)H); lcd.print('%'); } else lcd.print("--%");
    clearTail(16);

    lcd.setCursor(0, 1);
    lcd.print("L:"); lcd.print(lightPct); lcd.print("%");
    clearTail(16);
  }
  
  if (now - lastPrint >= 5000) {
    lastPrint = now;

    Serial.println();
    Serial.print("Temperature: ");
    if (!isnan(T)) Serial.print(T, 2); else Serial.print("N/A");
    Serial.print(" °C,  Humidity: ");
    if (!isnan(H)) Serial.print(H, 1); else Serial.print("N/A");
    Serial.print("%,  Soil1: ");
    Serial.print(soil1Pct);
    Serial.print("%,  Soil2: ");
    Serial.print(soil2Pct);
    Serial.print("%,  Light: ");
    Serial.print(lightPct);
    Serial.println("%");
  }



  delay(150);
}



