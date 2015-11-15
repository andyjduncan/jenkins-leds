#include <MemoryFree.h>

#include <HTTPClient.h>
#include <aJSON.h>

#include <Dhcp.h>
#include <Dns.h>
#include <Ethernet2.h>
#include <EthernetClient.h>

#include <SPI.h>
#include <TimerOne.h>

//#define DEBUG_LOGGING

const byte NO_ERROR = 0;
const byte HTTP_ERROR = 1;
const byte MEM_ERROR = 2;
const byte NETWORK_ERROR = 3;

byte currentError = NO_ERROR;

const byte RED = 0;
const byte GREEN = 1;
const byte BLUE = 2;
const byte WHITE = 3;

const byte PINS[] = { 3, 5, 9, 6 };

float PIN_STATES[] = { 0.0, 0.0, 0.0, 0.0 };

boolean pulsing = false;
int pulseBrightness = 255;
int fadeAmount = -5;

float dimFactor = 1.0;

const int staticDim = 5000;
boolean staticBright = true;
unsigned long staticStart = 0;

const byte ANIM_STATE = 1;
const byte GREEN_STATE = 2;
const byte RED_STATE = 4;
const byte YELLOW_STATE = 8;
const byte WHITE_STATE = 16;
const byte BLUE_STATE = 32;
const byte ERROR_STATE = 64;

const byte GOOD = GREEN_STATE;
const byte GOOD_BUILDING = GREEN_STATE | ANIM_STATE;
const byte BAD = RED_STATE;
const byte BAD_BUILDING = RED_STATE | ANIM_STATE;
const byte ERROR_BASE = ERROR_STATE | YELLOW_STATE | ANIM_STATE;
const byte ERROR_HTTP = ERROR_STATE | RED_STATE | ANIM_STATE;
const byte ERROR_MEM = ERROR_STATE | BLUE_STATE | ANIM_STATE;
const byte ERROR_NETWORK = ERROR_STATE | GREEN_STATE | ANIM_STATE;
const byte ERROR_OTHER = ERROR_STATE | WHITE_STATE | ANIM_STATE;

byte currentState = 0x00;

int lastMemory = 0;
unsigned long lastFinish = 0;

byte mac[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

byte kAddress[] = { 192, 168, 1, 1 };

// Name of the server we want to connect to
char* kHostname = "192.168.1.1";

int kPort = 8080;
// Path to download (this is the bit after the hostname in the URL
// that you want to download
char kPath[] = "/view/leds/api/json";

// Number of milliseconds to wait without receiving any data before we give up
const int kNetworkTimeout = 30 * 1000;
// Number of milliseconds to wait if no data is available before trying again
const int kNetworkDelay = 1000;

EthernetClient c;

//HTTPClient http = NULL;

char* jsonFilter[] = { "jobs", NULL };

void setup() {
  Serial.begin(115200);

  Timer1.initialize(25000);
  Timer1.attachInterrupt(setLEDs);

  pinMode(PINS[RED], OUTPUT);
  pinMode(PINS[GREEN], OUTPUT);
  pinMode(PINS[BLUE], OUTPUT);
  pinMode(PINS[WHITE], OUTPUT);

  setWhite();

  Serial.println(F("Connecting to network..."));
  if (Ethernet.begin(mac) == 0) {
    reportNetworkError(F("Failed to connect"));
    while(1);
  }

  delay(1000);
  
//  http(kHostname, kAddress, kPort);

  Serial.println(F("Connected!"));
}

void loop() {
  while (millis() - lastFinish < 1000) {
  }
  
  if (freeMemory() < lastMemory) {
    reportMemoryError(F("Memory leak "), lastMemory);
    lastFinish = millis();
    return;
  }
  
  HTTPClient http(kHostname, kAddress, kPort);

#ifdef DEBUG_LOGGING
  Serial.println(F("Connecting..."));
#endif
  
  FILE* response = http.getURI(kPath);
  
  if (response == NULL) {
    reportHttpError(F("No response "), 0);
    lastFinish = millis();
    return;
  }
  
  int responseCode = http.getLastReturnCode();
  
  if (responseCode != 200) {
    reportHttpError(F("Bad response code "), responseCode);
    lastFinish = millis();
    http.closeStream(response);
    return;
  }

  aJsonObject* json = aJson.parse(response, jsonFilter);
  
  byte thisState = findTopState(json);
  
  setState(thisState);
  
  http.closeStream(response);

  aJson.deleteItem(json);

  currentError = NO_ERROR;
  lastMemory = freeMemory();
  lastFinish = millis();
}

void setState(byte thisState) {
  if (thisState != currentState) {
#ifdef DEBUG_LOGGING
    Serial.println(F("New state"));
#endif
    if ((thisState & YELLOW_STATE) && !(currentState & YELLOW_STATE)) {
#ifdef DEBUG_LOGGING
      Serial.println(F("Yellow state"));
#endif
      setYellow();
      dimFactor = 1.0;
    } else if (thisState & RED_STATE && !(currentState & RED_STATE)) {
#ifdef DEBUG_LOGGING
      Serial.println(F("Red state"));
#endif
      setRed();
      dimFactor = 1.0;
    } else if ((thisState & GREEN_STATE) && !(currentState & GREEN_STATE)) {
#ifdef DEBUG_LOGGING
      Serial.println(F("Green state"));
#endif
      setGreen();
      dimFactor = 1.0;
    } else if ((thisState & BLUE_STATE) && !(currentState & BLUE_STATE)) {
#ifdef DEBUG_LOGGING
      Serial.println(F("Blue state"));
#endif
      setBlue();
      dimFactor = 1.0;
    } else if ((thisState & WHITE_STATE) && !(currentState & WHITE_STATE)) {
#ifdef DEBUG_LOGGING
      Serial.println(F("White state"));
#endif
      setWhite();
      dimFactor = 1.0;
    }
    if (thisState & ANIM_STATE) {
#ifdef DEBUG_LOGGING
      Serial.println(F("Anim state"));
#endif
      pulsing = true;
      pulseBrightness = 255;
      fadeAmount = -5;
    } else {
#ifdef DEBUG_LOGGING
      Serial.println(F("Static state"));
#endif
      pulsing = false;
      staticStart = millis();
      staticBright = true;
    }
    currentState = thisState;
  }
}

byte findTopState(aJsonObject* root) {
  aJsonObject* jobs = aJson.getObjectItem(root, "jobs");
  
  byte bestColour = 0x00;

  for (short i = aJson.getArraySize(jobs) - 1; i > -1; i--) {
    byte thisColour = colourValue(aJson.getObjectItem(aJson.getArrayItem(jobs, i), "color")->valuestring);
    if (thisColour > bestColour) {
      bestColour = thisColour;
    }
  }

  return bestColour;
}

byte colourValue(const char* colour) {
  if (strcmp(colour, "blue") == 0) {
    return GOOD;
  } else if (strcmp(colour, "blue_anime") == 0) {
    return GOOD_BUILDING;
  } else if (strcmp(colour, "red") == 0) {
    return BAD;
  } else if (strcmp(colour, "red_anime") == 0) {
    return BAD_BUILDING;
  } else {
    return 0;
  }
}

void reportNetworkError(String error) {
  Serial.println(error);
  if (currentError != NETWORK_ERROR) {
    setState(ERROR_BASE);
    currentError = NETWORK_ERROR;
  }
}

void reportHttpError(String error, int code) {
  Serial.print(error);
  Serial.println(code);
  if (currentError != HTTP_ERROR) {
    setState(ERROR_BASE);
    currentError = HTTP_ERROR;
  }
}

void reportMemoryError(String error, int freeMem) {
  Serial.print(error);
  Serial.println(freeMem);
  if (currentError != MEM_ERROR) {
    setState(ERROR_BASE);
    currentError = MEM_ERROR;
  }
}

void setLEDs(void) {
  if (pulsing) {
    updateLEDs(pulseBrightness);
    pulseBrightness += fadeAmount;
    if (pulseBrightness == 0 || pulseBrightness == 255) {
      fadeAmount = -fadeAmount;
    }
    if (currentError && pulseBrightness == 255) {
      if (currentState == ERROR_BASE) {
        switch(currentError) {
          case HTTP_ERROR:
            setState(ERROR_HTTP);
            break;
          case MEM_ERROR:
            setState(ERROR_MEM);
            break;
          case NETWORK_ERROR:
            setState(ERROR_NETWORK);
            break;
          default:
            setState(ERROR_OTHER);
        }
      } else {
        setState(ERROR_BASE);
      }
    }
  } else {
    updateLEDs(255);
    if (staticBright && (currentState & BLUE) && (millis() - staticStart > staticDim)) {
      staticBright = false;
      dimFactor = 0.05;
    }
  }
}

void setYellow() {
  PIN_STATES[RED] = 1.0;
  PIN_STATES[GREEN] = 0.4;
  PIN_STATES[BLUE] = 0.0;
  PIN_STATES[WHITE] = 0.0;
}

void setRed() {
  PIN_STATES[RED] = 1.0;
  PIN_STATES[GREEN] = 0.0;
  PIN_STATES[BLUE] = 0.0;
  PIN_STATES[WHITE] = 0.0;
}

void setGreen() {
  PIN_STATES[RED] = 0.0;
  PIN_STATES[GREEN] = 1.0;
  PIN_STATES[BLUE] = 0.0;
  PIN_STATES[WHITE] = 0.0;
}

void setWhite() {
  PIN_STATES[RED] = 0.0;
  PIN_STATES[GREEN] = 0.0;
  PIN_STATES[BLUE] = 0.0;
  PIN_STATES[WHITE] = 1.0;
}

void setBlue() {
  PIN_STATES[RED] = 0.0;
  PIN_STATES[GREEN] = 0.0;
  PIN_STATES[BLUE] = 1.0;
  PIN_STATES[WHITE] = 0.0;
}

void updateLEDs(int brightness) {
  for (int pin = 0; pin < 4; pin++) {
    analogWrite(PINS[pin], PIN_STATES[pin] * brightness * dimFactor);
  }
}
