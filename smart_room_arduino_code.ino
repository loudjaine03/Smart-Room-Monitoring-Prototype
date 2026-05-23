// Smart Room Project
// Prepared by: Bensalem Loudjaine, Bouiche Dounia, Zaouia Salsabil, Groupe 3

#include "DHT.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

//LCD 
LiquidCrystal_I2C lcd(0x27, 16, 2);

//  Pins
const int lightPin = 5;

const int led1 = 2;
const int led2 = 3;
const int led3 = 4;

#define DHTPIN 6
#define DHTTYPE DHT11

const int heaterPin = 7;

//  Motion sensors
const int externalSensor = 8;
const int internalSensor = 9;

//  Servo door 
Servo doorServo;
const int servoPin = 11;

const int closedAngle = 0;
const int openAngle = 90;

// Door state
bool doorOpen = false;
bool doorFullyOpen = false;

int currentDoorAngle = closedAngle;
int targetDoorAngle = closedAngle;

unsigned long lastServoMoveTime = 0;
const unsigned long servoStepDelay = 25; // bigger = slower

unsigned long doorOpenStartTime = 0;
const unsigned long doorOpenTime = 6000; 

//  DHT 
DHT dht(DHTPIN, DHTTYPE);

//  Temperature limits
float tempLow = 25.0;
float tempHigh = 28.0;

bool heaterState = false;

// People counter 
int peopleCount = 0;
const int maxPeople = 4;   // maximum room capacity e.g. 4

// Movement sequence 
int sequenceState = 0;
// 0 = waiting
// 1 = external detected first, waiting for internal
// 2 = internal detected first, waiting for external
// 3 = cooldown

unsigned long sequenceStartTime = 0;
const unsigned long maxSequenceTime = 8000;

unsigned long cooldownStartTime = 0;
const unsigned long cooldownTime = 2500;

int lastExternalState = LOW;
int lastInternalState = LOW;

// Strong PIR filtering (for better door control)
const unsigned long motionConfirmTime = 1100;

unsigned long externalHighStart = 0;
unsigned long internalHighStart = 0;

bool externalAlreadyConfirmed = false;
bool internalAlreadyConfirmed = false;

// Prevents the system from becoming ready again too quickly.
unsigned long bothLowStartTime = 0;
const unsigned long bothLowConfirmTime = 1500;

// DHT reading 
unsigned long lastDHTRead = 0;
const unsigned long dhtInterval = 2500;

float temperature = 0;
float humidity = 0;
bool dhtOK = false;

// LCD update 
unsigned long lastLCDUpdate = 0;
const unsigned long lcdInterval = 1000;

// Function prototypes 
void openDoor();
void closeDoor();
void updateDoor(unsigned long currentTime);
bool confirmedMotion(int sensorState, unsigned long currentTime, unsigned long &highStartTime, bool &alreadyConfirmed);
bool sensorsStableLow(int externalState, int internalState, unsigned long currentTime);

void setup() {
  Serial.begin(9600);

  pinMode(lightPin, INPUT);

  pinMode(led1, OUTPUT);
  pinMode(led2, OUTPUT);
  pinMode(led3, OUTPUT);

  pinMode(heaterPin, OUTPUT);

  pinMode(externalSensor, INPUT);
  pinMode(internalSensor, INPUT);

  dht.begin();

  doorServo.attach(servoPin);
  doorServo.write(closedAngle);

  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("Smart Room");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  delay(2000);
  lcd.clear();

  Serial.println("System started...");
}

void loop() {
  unsigned long currentTime = millis();

  // Light sensor
  int lightState = digitalRead(lightPin);

  if (lightState == HIGH) {
    digitalWrite(led1, HIGH);
    digitalWrite(led2, HIGH);
    digitalWrite(led3, HIGH);
  } else {
    digitalWrite(led1, LOW);
    digitalWrite(led2, LOW);
    digitalWrite(led3, LOW);
  }

  // Motion sensors 
  int externalState = digitalRead(externalSensor);
  int internalState = digitalRead(internalSensor);

  // Strong filtered detection
  bool externalDetectedNow = confirmedMotion(
    externalState,
    currentTime,
    externalHighStart,
    externalAlreadyConfirmed
  );

  bool internalDetectedNow = confirmedMotion(
    internalState,
    currentTime,
    internalHighStart,
    internalAlreadyConfirmed
  );

  // People + Door logic
  if (sequenceState == 0) {

    // External first = someone wants to ENTER
    if (externalDetectedNow && internalState == LOW) {

      // If room is full, block entry
      if (peopleCount >= maxPeople) {
        closeDoor();

        Serial.println("ROOM FULL. Entry blocked.");

        sequenceState = 3;
        cooldownStartTime = currentTime;
      }

      // If room is not full, allow entry
      else {
        openDoor();

        sequenceState = 1;
        sequenceStartTime = currentTime;

        Serial.println("External first: waiting for internal...");
      }
    }

    // Internal first = someone wants to LEAVE
    else if (internalDetectedNow && externalState == LOW) {
      openDoor();

      sequenceState = 2;
      sequenceStartTime = currentTime;

      Serial.println("Internal first: waiting for external...");
    }
  }

  // External then Internal = ENTER
  else if (sequenceState == 1) {

    if (internalDetectedNow) {

      if (peopleCount < maxPeople) {
        peopleCount++;
      }

      Serial.print("ENTER detected. People: ");
      Serial.println(peopleCount);

      sequenceState = 3;
      cooldownStartTime = currentTime;
    }

    else if (currentTime - sequenceStartTime > maxSequenceTime) {
      Serial.println("Only external / too late. Door opened but no count.");

      sequenceState = 3;
      cooldownStartTime = currentTime;
    }
  }

  // Internal then External = LEAVE
  else if (sequenceState == 2) {

    if (externalDetectedNow) {
      if (peopleCount > 0) {
        peopleCount--;
      }

      Serial.print("LEAVE detected. People: ");
      Serial.println(peopleCount);

      sequenceState = 3;
      cooldownStartTime = currentTime;
    }

    else if (currentTime - sequenceStartTime > maxSequenceTime) {
      Serial.println("Only internal / too late. Door opened but no count.");

      sequenceState = 3;
      cooldownStartTime = currentTime;
    }
  }

  // Cooldown
  else if (sequenceState == 3) {
    if (currentTime - cooldownStartTime >= cooldownTime &&
        sensorsStableLow(externalState, internalState, currentTime)) {

      sequenceState = 0;
      Serial.println("Ready for next movement.");
    }
  }

  lastExternalState = externalState;
  lastInternalState = internalState;

  // slow door movement
  updateDoor(currentTime);

  // Close door automatically
  if (doorFullyOpen == true &&
      currentTime - doorOpenStartTime >= doorOpenTime &&
      externalState == LOW &&
      internalState == LOW) {
    closeDoor();
  }

  // DHT: Read temperature and humidity 
  if (currentTime - lastDHTRead >= dhtInterval) {
    lastDHTRead = currentTime;

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (isnan(t) || isnan(h)) {
      dhtOK = false;
      Serial.println("DHT read failed");
    } else {
      dhtOK = true;
      temperature = t;
      humidity = h;

      if (temperature < tempLow) {
        heaterState = true;
      } 
      else if (temperature > tempHigh) {
        heaterState = false;
      }
    }

    digitalWrite(heaterPin, heaterState ? HIGH : LOW);

    // Serial
    Serial.print("External: ");
    Serial.print(externalState);

    Serial.print(" | Internal: ");
    Serial.print(internalState);

    Serial.print(" | People: ");
    Serial.print(peopleCount);

    if (peopleCount >= maxPeople) {
      Serial.print(" FULL");
    }

    if (dhtOK) {
      Serial.print(" | Temperature: ");
      Serial.print(temperature);
      Serial.print(" C");

      Serial.print(" | Humidity: ");
      Serial.print(humidity);
      Serial.print(" %");
    } else {
      Serial.print(" | DHT failed");
    }

    Serial.print(" | Heater: ");
    Serial.print(heaterState ? "ON" : "OFF");

    Serial.print(" | Door: ");
    if (doorFullyOpen) {
      Serial.println("OPEN");
    } else if (doorOpen) {
      Serial.println("MOVING");
    } else {
      Serial.println("CLOSED");
    }

    // for Python and database 
    if (dhtOK) {
      Serial.print("DATA,");
      Serial.print(peopleCount);
      Serial.print(",");
      Serial.print(temperature);
      Serial.print(",");
      Serial.print(humidity);
      Serial.print(",");
      Serial.print(heaterState ? "ON" : "OFF");
      Serial.print(",");

      if (doorFullyOpen) {
        Serial.print("OPEN");
      } else if (doorOpen) {
        Serial.print("MOVING");
      } else {
        Serial.print("CLOSED");
      }

      Serial.print(",");

      if (peopleCount >= maxPeople) {
        Serial.println("FULL");
      } else {
        Serial.println("AVAILABLE");
      }
    }
  }

  // LCD display
  if (currentTime - lastLCDUpdate >= lcdInterval) {
    lastLCDUpdate = currentTime;

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("People: ");
    lcd.print(peopleCount);

    if (peopleCount >= maxPeople) {
      lcd.print(" FULL");
    }

    lcd.setCursor(0, 1);

    if (dhtOK) {
      lcd.print("T:");
      lcd.print(temperature, 1);
      lcd.print("C H:");
      lcd.print(humidity, 0);
      lcd.print("%");
    } else {
      lcd.print("DHT Error");
    }
  }
}

// Door functions
void openDoor() {

  if (targetDoorAngle == openAngle) {
    return;
  }

  targetDoorAngle = openAngle;
  doorOpen = true;
  doorFullyOpen = false;

  Serial.println("Door opening slowly...");
}

void closeDoor() {

  if (targetDoorAngle == closedAngle) {
    return;
  }

  targetDoorAngle = closedAngle;
  doorFullyOpen = false;

  Serial.println("Door closing slowly...");
}

void updateDoor(unsigned long currentTime) {
  if (currentTime - lastServoMoveTime >= servoStepDelay) {
    lastServoMoveTime = currentTime;

    if (currentDoorAngle < targetDoorAngle) {
      currentDoorAngle++;
      doorServo.write(currentDoorAngle);

      if (currentDoorAngle == openAngle) {
        doorFullyOpen = true;
        doorOpenStartTime = currentTime;
        Serial.println("Door fully OPEN");
      }
    }

    else if (currentDoorAngle > targetDoorAngle) {
      currentDoorAngle--;
      doorServo.write(currentDoorAngle);

      if (currentDoorAngle == closedAngle) {
        doorOpen = false;
        doorFullyOpen = false;
        Serial.println("Door fully CLOSED");
      }
    }
  }
}

bool confirmedMotion(int sensorState, unsigned long currentTime, unsigned long &highStartTime, bool &alreadyConfirmed) {
  if (sensorState == HIGH) {
    if (highStartTime == 0) {
      highStartTime = currentTime;
    }

    if (!alreadyConfirmed && currentTime - highStartTime >= motionConfirmTime) {
      alreadyConfirmed = true;
      return true;
    }
  } else {
    highStartTime = 0;
    alreadyConfirmed = false;
  }

  return false;
}

bool sensorsStableLow(int externalState, int internalState, unsigned long currentTime) {
  if (externalState == LOW && internalState == LOW) {
    if (bothLowStartTime == 0) {
      bothLowStartTime = currentTime;
    }

    if (currentTime - bothLowStartTime >= bothLowConfirmTime) {
      return true;
    }
  } else {
    bothLowStartTime = 0;
  }

  return false;
}