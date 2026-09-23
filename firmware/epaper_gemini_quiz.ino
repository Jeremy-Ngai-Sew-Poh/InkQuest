#include <GxEPD2_BW.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Adafruit_NeoPixel.h>
#include "esp_sleep.h"
#include <vector>
#include <WiFiManager.h>

constexpr uint8_t csPin = 2;
constexpr uint8_t dcPin = 7;
constexpr uint8_t rstPin = 5;
constexpr uint8_t busyPin = 6;
constexpr uint8_t batteryAdcPin = 3;
constexpr uint8_t s1Pin = 21;
constexpr uint8_t s2Pin = 9;
constexpr uint8_t keyPin = 4;
constexpr uint8_t ledPin = 20;

const int ledCount = 1;

GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display(GxEPD2_290_BS(csPin, dcPin, rstPin, busyPin));
Adafruit_NeoPixel strip(ledCount, ledPin, NEO_GRB + NEO_KHZ800);
WiFiManager wm;

// Wifi info
volatile bool shouldResetWiFi = false;
RTC_DATA_ATTR String wifiSSID = "name";

// Gemini API
const char* apikey = "AQ.Ab8RN6Jij4TnXulrIYHlXcuD4izzFZjHuZxj-fB_yFsUPvgc_Q";
const char* url = "https://generativelanguage.googleapis.com/v1/models/gemini-3.1-flash-lite:generateContent?key=";

char* geminiPrompt = "You are the game master for a text adventure game."
                     "1. Write a trilling around 70 words story segment in second person perspective ('you')"
                     "2. Do NOT separate paragraph with a new line. Strictly use '\n'. For example: 'Old Paragraph. \nNew Paragraph'"
                     "3. End the writing with exactly '\nPress to continue...'"
                     "4. Provide EXACTLY 4 distinct, 8 - 10 words action choice for the player"
                     "5. Return ONLY valid JSON with this exact schema, no markdown formatting or extra text"
                     "{\"story\":\"...\", \"choiceA\":\"...\", \"choiceB\":\"...\", \"choiceC\":\"...\", \"choiceD\":\"...\"}";

String longText = "";

int choiceSelection = 1;
String choiceName[] = { "", "Choice A", "Choice B", "Choice C", "Choice D" };
String choiceText[] = { "", "This is choice A. ", "This is choice B.", "This is choice C.", "This is choice D." };

volatile int screen = 0;
volatile bool update = true;
volatile int userSelection = 1;

int batteryPercent = 0;
float batteryVoltage = 0;
bool lowBattery = false;
int error = 0;

volatile int counter = 0;
int currentStateCLK;
int lastStateCLK;
String currentDir = "";

int scrollLines = 5;
static std::vector<String> wrappedLines;
static String lastRenderedText = "";
static int topLine = 0;

uint8_t targetR = 0;
uint8_t targetG = 0;
uint8_t targetB = 0;
uint8_t currentR = 0;
uint8_t currentG = 0;
uint8_t currentB = 0;
bool isFading = false;

unsigned long lastUpdate = 0;
const unsigned long fadeInterval = 10;

unsigned long lastInputTime = 0;
const unsigned long timeoutMillis = 180000;  // 2 minutes
bool isBusy = false;

void readRotary();
void checkBattery();
void initDisplay();
void setupPin();
void connectWiFi();
void enterDeepSleep();
void displayError(int type);
void printScrollableTextBox(String text, int x, int y, int w, int h, int lineHeight);
std::vector<String> wrapTextToWidth(const String& text, int maxWidth);

void setup() {
  Serial.begin(115200);

  setupPin();
  checkBattery();
  initDisplay();

  strip.begin();
  strip.setBrightness(25);
  strip.show();

  lastInputTime = millis();
}

void loop() {
  updateFade();
  if (error > 0) {
    displayError(error);

  } else {
    switch (screen) {
      case 0:  // WIFI PROMPT
        isBusy = false;
        wifiPrompt();
        setTargetColor(150, 0, 200);
        break;
      case 1:  // CONNECT WIFI SCREEN
        isBusy = true;
        connectWiFiManager();
        setTargetColor(20, 20, 255);
        break;
      case 2:  // GEMINI LOADING
        isBusy = true;
        setTargetColor(255, 20, 20);
        showGeminiLoading();
        fetchGeminiAPI();
        screen++;
        break;
      case 3:
        isBusy = false;
        printScrollableTextBox(longText, 10, 8, 270, 114, 21);
        setTargetColor(20, 255, 20);
        if (digitalRead(keyPin) == LOW) {
          lastInputTime = millis();
          screen++;
          update = true;
          counter = 0;
          Serial.println("screen 4");
        }
        break;
      case 4:
        setTargetColor(255, 255, 0);
        isBusy = false;

        if (counter > 0) {
          choiceSelection = (choiceSelection < 4) ? choiceSelection + 1 : 1;
          update = true;
        } else if (counter < 0) {
          choiceSelection = (choiceSelection > 1) ? choiceSelection - 1 : 4;
          update = true;
        }

        if (update) {
          update = false;
          display.setPartialWindow(0, 0, display.width(), display.height());
          display.firstPage();
          do {
            display.fillScreen(GxEPD_WHITE);
            display.setFont(&FreeSerif9pt7b);
            printCenteredText("What would you do?", 22);
            printCenteredText(choiceName[choiceSelection], 112);
            display.drawRoundRect(95, 90, 107, 31, 12, GxEPD_BLACK);
            printCenteredWrappedText(choiceText[choiceSelection], 53);

            if (choiceSelection != 1) {
              display.drawRoundRect(58, 90, 31, 31, 12, GxEPD_BLACK);
              display.drawLine(68, 105, 75, 98, GxEPD_BLACK);
              display.drawLine(68, 105, 75, 112, GxEPD_BLACK);
            }

            if (choiceSelection != 4) {
              display.drawRoundRect(208, 90, 31, 31, 12, GxEPD_BLACK);
              display.drawLine(221, 98, 228, 105, GxEPD_BLACK);
              display.drawLine(221, 112, 228, 105, GxEPD_BLACK);
            }
          } while (display.nextPage());
        }

        if (digitalRead(keyPin) == LOW && (millis() - lastInputTime > 300)) {
          lastInputTime = millis();
          addTurnToHistory(longText, choiceText[choiceSelection]);

          choiceSelection = 1;
          topLine = 0;
          lastRenderedText = "";

          screen = 2;
          update = true;
        }
        break;
      default:
        break;
    }
  }
  counter = 0;

  if (millis() - lastInputTime >= timeoutMillis && isBusy == false) {
    enterDeepSleep();
  }
}

struct GameTurn {
  String story;
  String playerChoice;
};

const int MAX_HISTORY = 5;
GameTurn gameHistory[MAX_HISTORY];
int historyCount = 0;

void addTurnToHistory(String storySegment, String chosenOption) {
  if (historyCount < MAX_HISTORY) {
    gameHistory[historyCount].story = storySegment;
    gameHistory[historyCount].playerChoice = chosenOption;
    historyCount++;
  } else {
    // Shift older turns left to drop the oldest turn
    for (int i = 0; i < MAX_HISTORY - 1; i++) {
      gameHistory[i] = gameHistory[i + 1];
    }
    gameHistory[MAX_HISTORY - 1].story = storySegment;
    gameHistory[MAX_HISTORY - 1].playerChoice = chosenOption;
  }
}

void fetchGeminiAPI() {
  Serial.println("Fetching data from Gemini...");

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String fullUrl = String(url) + String(apikey);

  if (http.begin(client, fullUrl)) {
    http.addHeader("Content-Type", "application/json");

    // Dynamic document sized to hold the prompt + history
    DynamicJsonDocument doc(4096);
    JsonArray contents = doc.createNestedArray("contents");

    // 1. Add history turns as user/model dialogue
    for (int i = 0; i < historyCount; i++) {
      // Previous Gemini output
      JsonObject modelTurn = contents.createNestedObject();
      modelTurn["role"] = "model";
      JsonObject modelPart = modelTurn.createNestedArray("parts").createNestedObject();
      modelPart["text"] = gameHistory[i].story;

      // Previous player action
      JsonObject userTurn = contents.createNestedObject();
      userTurn["role"] = "user";
      JsonObject userPart = userTurn.createNestedArray("parts").createNestedObject();
      userPart["text"] = "Player chose: " + gameHistory[i].playerChoice;
    }

    // 2. Add current turn instruction
    JsonObject currentTurn = contents.createNestedObject();
    currentTurn["role"] = "user";
    JsonObject currentPart = currentTurn.createNestedArray("parts").createNestedObject();
    
    // System instruction injected into the latest turn
    String systemPrompt = String(geminiPrompt);
    if (historyCount > 0) {
      systemPrompt += "\nContinue the story based on the player's last action: \"" + choiceText[choiceSelection] + "\"";
    }
    currentPart["text"] = systemPrompt;

    String requestBody;
    serializeJson(doc, requestBody);

    int httpResponseCode = http.POST(requestBody);

    if (httpResponseCode == 200) {
      String response = http.getString();

      DynamicJsonDocument responseDoc(4096);
      DeserializationError error = deserializeJson(responseDoc, response);

      if (!error) {
        String rawJson = responseDoc["candidates"][0]["content"]["parts"][0]["text"];

        // Clean markdown fencing if Gemini includes it
        rawJson.replace("```json", "");
        rawJson.replace("```", "");
        rawJson.trim();

        DynamicJsonDocument storyDoc(2048);
        DeserializationError innerError = deserializeJson(storyDoc, rawJson);

        if (!innerError) {
          longText = storyDoc["story"].as<String>();
          choiceText[1] = storyDoc["choiceA"].as<String>();
          choiceText[2] = storyDoc["choiceB"].as<String>();
          choiceText[3] = storyDoc["choiceC"].as<String>();
          choiceText[4] = storyDoc["choiceD"].as<String>();

          Serial.println("--- NEW STORY SEGMENT RECEIVED ---");
          Serial.println(longText);
        } else {
          Serial.println("INNER JSON PARSING FAILED");
          Serial.println(innerError.f_str());
        }
      } else {
        Serial.println("JSON PARSING FAILED");
        Serial.println(error.f_str());
      }
    } else {
      Serial.println("HTTP REQUEST FAILED");
      Serial.println(httpResponseCode);
    }

    http.end();
  } else {
    Serial.println("Error connecting to endpoint");
  }
}

void showGeminiLoading() {
  if (update) {
    update = false;

    display.setPartialWindow(0, 0, display.width(), display.height());
    display.firstPage();
    do {
      display.setFont(&FreeSerif9pt7b);
      printCenteredText("Gemini is thinking...", 33);
    } while (display.nextPage());
  }
}

void wifiPrompt() {
  if (wm.getWiFiIsSaved() == false) {
    screen = 1;
  }

  if (digitalRead(keyPin) == LOW) {
    lastInputTime = millis();
    if (userSelection == 2) {
      shouldResetWiFi = true;
    }
    screen = 1;
  }

  if (counter > 0) {
    noInterrupts();
    if (userSelection == 1) {
      userSelection = 2;
    } else if (userSelection == 2) {
      userSelection = 1;
    }
    counter = 0;
    update = true;
  } else if (counter < 0) {
    noInterrupts();
    if (userSelection == 1) {
      userSelection = 2;
    } else if (userSelection == 2) {
      userSelection = 1;
    }
    counter = 0;
    update = true;
  }

  if (update) {
    update = false;

    display.setPartialWindow(0, 0, display.width(), display.height());
    display.firstPage();
    do {
      display.setFont(&FreeSerif12pt7b);
      printCenteredText("Connect to saved WiFi?", 36);
      display.setFont(&FreeSerif9pt7b);
      printCenteredText("OK", 78);
      printCenteredText("Switch WiFi", 112);
      if (userSelection == 1) {
        display.drawRoundRect(100, 56, 96, 31, 12, GxEPD_BLACK);
      } else if (userSelection == 2) {
        display.drawRoundRect(85, 90, 126, 31, 12, GxEPD_BLACK);
      }
      interrupts();
    } while (display.nextPage());
  }
}

void connectWiFiManager() {
  if (shouldResetWiFi == true) {
    display.firstPage();
    do {
      display.setFont(&FreeSerif9pt7b);
      printCenteredText("Erasing WiFi...", 33);
    } while (display.nextPage());
    wm.resetSettings();
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.mode(WIFI_STA);
    shouldResetWiFi = false;
  }

  wm.setAPCallback(configModeCallback);
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(180);

  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.setFont(&FreeSerif9pt7b);
    printCenteredText("Connecting to WiFi...", 33);
  } while (display.nextPage());

  if (!wm.autoConnect("ESP32_Setup")) {
    Serial.println("Failed to connect or portal timed out.");
    error = 2;
    update = true;
    return;
  }

  Serial.println("WiFi connected successfully");
  wifiSSID = wm.getWiFiSSID();
  screen = 2;
  update = true;
}

void configModeCallback(WiFiManager* myWiFiManager) {
  Serial.println("Entered config mode.");

  String apSSID = myWiFiManager->getConfigPortalSSID();
  String qrContent = "WIFI:S:" + apSSID + ";T:nopass;P:;;";

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    display.setFont(&FreeSerif12pt7b);
    display.setCursor(17, 33);
    display.print("Configure WiFi");

    display.setFont(&FreeSerif9pt7b);
    display.setCursor(17, 56);
    display.print("Connect to hotspot to setup WiFi");
    display.setCursor(17, 79);
    display.print("Hotspot: ESP32_WiFi");

  } while (display.nextPage());
}

std::vector<String> wrapTextToWidth(const String& text, int maxWidth) {
  std::vector<String> lines;
  String currentLine = "";
  int i = 0;
  int n = text.length();

  display.setFont(&FreeSerif9pt7b);

  while (i < n) {
    if (text.charAt(i) == '\n') {
      lines.push_back(currentLine);
      lines.push_back("");
      currentLine = "";
      i++;
      continue;
    }

    int spaceIndex = text.indexOf(' ', i);
    int nextNewline = text.indexOf('\n', i);

    if (nextNewline != -1 && (spaceIndex == -1 || nextNewline < spaceIndex)) {
      spaceIndex = nextNewline;
    }

    String word;
    if (spaceIndex == -1) {
      word = text.substring(i);
      i = n;
    } else {
      word = text.substring(i, spaceIndex);
      i = spaceIndex;
      if (text.charAt(i) == ' ') i++;
    }

    if (word.length() == 0) continue;

    String testLine = (currentLine.length() == 0) ? word : currentLine + " " + word;

    int16_t x1, y1;
    uint16_t w1, h1;
    display.getTextBounds(testLine, 0, 0, &x1, &y1, &w1, &h1);

    if (w1 > (uint16_t)maxWidth) {
      if (currentLine.length() > 0) {
        lines.push_back(currentLine);
        currentLine = word;
      } else {
        lines.push_back(word);
        currentLine = "";
      }
    } else {
      currentLine = testLine;
    }
  }

  if (currentLine.length() > 0) lines.push_back(currentLine);
  return lines;
}

void printScrollableTextBox(String text, int x, int y, int w, int h, int lineHeight) {
  display.setFont(&FreeSerif9pt7b);

  if (text != lastRenderedText) {
    wrappedLines = wrapTextToWidth(text, w);
    lastRenderedText = text;
    topLine = 0;
    update = true;
  }

  int maxVisibleLines = h / lineHeight;
  int maxTopLine = max(0, (int)wrappedLines.size() - maxVisibleLines);

  if (counter != 0) {
    noInterrupts();
    if (counter > 0) {
      topLine += scrollLines;
      if (topLine > maxTopLine) topLine = maxTopLine;
      update = true;
    } else if (counter < 0) {
      topLine -= scrollLines;
      if (topLine < 0) topLine = 0;
      update = true;
    }
    counter = 0;
  }

  if (!update) return;

  display.setPartialWindow(x, y, w + 9, h);
  display.firstPage();
  do {
    display.fillRect(x, y, w, h, GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    int scrollbarWidth = 1;
    int scrollbarX = x + w - scrollbarWidth + 9;

    int scrollMinY = 15;
    int scrollMaxY = 112;
    int allowedTrackHeight = scrollMaxY - scrollMinY;

    int totalLines = wrappedLines.size();

    if (totalLines > maxVisibleLines) {
      int thumbHeight = (maxVisibleLines * allowedTrackHeight) / totalLines;
      if (thumbHeight < 10) thumbHeight = 10;

      int thumbY = y + ((topLine * (allowedTrackHeight - thumbHeight)) / maxTopLine) + 6;

      display.fillRect(scrollbarX, thumbY, scrollbarWidth, thumbHeight, GxEPD_BLACK);
    }

    for (int i = 0; i < maxVisibleLines; i++) {
      int lineIndex = topLine + i;
      if (lineIndex >= (int)wrappedLines.size()) break;

      display.setCursor(x, y + (lineHeight * i) + (lineHeight - 4));
      display.print(wrappedLines[lineIndex]);
    }
    interrupts();
  } while (display.nextPage());
  update = false;
}

void printCenteredWrappedText(String text, int16_t startY) {
  uint16_t screenWidth = display.width();
  int16_t margin = 10;
  uint16_t maxLineWidth = screenWidth - (margin * 2);

  int16_t x1, y1;
  uint16_t w, h;

  display.getTextBounds("A", 0, 0, &x1, &y1, &w, &h);
  uint16_t lineHeight = h + 6;

  int startIdx = 0;
  int16_t currentY = startY;

  while (startIdx < text.length()) {
    String currentLine = "";
    int nextIdx = startIdx;

    while (nextIdx < text.length()) {
      int spaceIdx = text.indexOf(' ', nextIdx);
      if (spaceIdx == -1) spaceIdx = text.length();

      String word = text.substring(nextIdx, spaceIdx);
      String testLine = (currentLine.length() == 0) ? word : currentLine + " " + word;

      display.getTextBounds(testLine, 0, 0, &x1, &y1, &w, &h);

      if (w > maxLineWidth && currentLine.length() > 0) {
        break;
      }

      currentLine = testLine;
      nextIdx = spaceIdx + 1;
    }

    display.getTextBounds(currentLine, 0, currentY, &x1, &y1, &w, &h);
    int16_t startX = (screenWidth - w) / 2;

    display.setCursor(startX, currentY);
    display.print(currentLine);

    currentY += lineHeight;
    startIdx = nextIdx;
  }
}

void printCenteredText(String text, int yPosition) {
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);
  int xCentered = (display.width() - tbw) / 2;
  display.setCursor(xCentered, yPosition);
  display.print(text);
}

void setTargetColor(uint8_t r, uint8_t g, uint8_t b) {
  targetR = r;
  targetG = g;
  targetB = b;
}

void updateFade() {
  if (millis() - lastUpdate >= fadeInterval) {
    lastUpdate = millis();

    bool needsUpdate = false;

    if (currentR < targetR) {
      currentR++;
      needsUpdate = true;
    } else if (currentR > targetR) {
      currentR--;
      needsUpdate = true;
    }

    if (currentG < targetG) {
      currentG++;
      needsUpdate = true;
    } else if (currentG > targetG) {
      currentG--;
      needsUpdate = true;
    }

    if (currentB < targetB) {
      currentB++;
      needsUpdate = true;
    } else if (currentB > targetB) {
      currentB--;
      needsUpdate = true;
    }

    if (needsUpdate) {
      isFading = true;
      for (int i = 0; i < strip.numPixels(); i++) {
        strip.setPixelColor(i, strip.Color(currentR, currentG, currentB));
      }
      strip.show();
    } else {
      isFading = false;
    }
  }
}

void checkBattery() {
  uint32_t Vbatt = 0;
  for (int i = 0; i < 32; i++) {
    Vbatt = Vbatt + analogReadMilliVolts(batteryAdcPin);
    delayMicroseconds(50);
  }
  float Vbattf = 2 * Vbatt / 32 / 1000.0;
  batteryVoltage = Vbattf;
  float result = (Vbattf - 3.4) * (99.0 / (4.2 - 3.4));
  result = constrain(result, 0, 99);
  batteryPercent = result;

  if (result == 0) {
    error = 1;
    update = true;
  }
}

void displayError(int type) {
  setTargetColor(255, 0, 0);

  if (digitalRead(keyPin) == LOW) {
    enterDeepSleep();
  }
  if (update == true) {
    update = false;
    display.firstPage();
    do {
      switch (type) {
        case 1:
          display.setFont(&FreeSerif12pt7b);
          printCenteredText("Low Battery", 33);
          display.setFont(&FreeSerif9pt7b);
          printCenteredText("Voltage: " + String(batteryVoltage) + " V", 67);
          display.drawRoundRect(100, 84, 96, 31, 12, GxEPD_BLACK);
          printCenteredText("OK", 106);
          break;
        case 2:
          display.setFont(&FreeSerif12pt7b);
          printCenteredText("WiFi Problem", 33);
          display.setFont(&FreeSerif9pt7b);
          printCenteredText("Voltage: " + String(batteryVoltage) + " V", 67);
          display.drawRoundRect(100, 84, 96, 31, 12, GxEPD_BLACK);
          printCenteredText("OK", 106);
          break;
        default:
          break;
      }
    } while (display.nextPage());
  }
}

void enterDeepSleep() {
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);

  for (int i = strip.getBrightness(); i > 0; i--) {
    strip.setBrightness(i - 1);
    strip.show();
    delay(10);
  }

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeSerif9pt7b);
    printCenteredText("Press to start!", 33);
  } while (display.nextPage());

  esp_deep_sleep_enable_gpio_wakeup(1ULL << keyPin, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
}

void initDisplay() {
  display.init(115200, true, 50, false);
  display.setRotation(3);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeSerif9pt7b);
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
  } while (display.nextPage());
}

void setupPin() {
  pinMode(batteryAdcPin, INPUT);
  pinMode(s1Pin, INPUT_PULLUP);
  pinMode(s2Pin, INPUT_PULLUP);
  pinMode(keyPin, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(s1Pin), readRotary, FALLING);
}

void readRotary() {
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = millis();

  if (interruptTime - lastInterruptTime > 4) {
    if (digitalRead(s2Pin) == HIGH) {
      lastInputTime = millis();
      counter = -1;
    } else {
      lastInputTime = millis();
      counter = 1;
    }
  }
  lastInterruptTime = interruptTime;
}