#include <SoftwareSerial.h>

SoftwareSerial mySerial(A5, A4); // RX, TX

const byte pinA = 3;
const byte pinB = 2;

const int8_t encoderStates[] = {0, -1, 1, 2, 1, 0, -2, -1, -1, -2, 0, 1, 2, 1, -1};


volatile uint8_t state = B0000; // 3:4 bit current state AB, 0:1 bit old state AB



///

const byte stepsPerMM = 40;
const float ticksPerStep = 2.5;

const byte STEP_1 = 9;
const byte DIR_1 = 8; 
const byte ENDSW_1 = A1;

const byte STEP_2 = 12;
const byte DIR_2 = 11;
const byte ENDSW_2 = A0;

const float stepDeg = (float)1600 / 360.0;
uint16_t degS = 500;

float stepsS = stepDeg * degS;
uint16_t stepPeriod = 1000000 / stepsS;


uint32_t periodTimer;

uint16_t stepsMoved = 0;
volatile int16_t encoderPos = 0;
//int stepsToMove = stepsPerMM * 150;
int stepsToMove = 0 ;
uint16_t currentPos  = 0;

long ticksToMove = 0;
int16_t moveToTicks = 0;



void setup() {
  pinMode(13, OUTPUT);

  pinMode(pinA, INPUT);
  pinMode(pinB, INPUT);
  attachInterrupt(digitalPinToInterrupt(pinA), read_encoder_ISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinB), read_encoder_ISR, CHANGE);

  delay(10);
  //initial state for encoder
  if (digitalRead(pinA)) state = state | B0010;
  if (digitalRead(pinB)) state = state | B0001;



  pinMode(STEP_1, OUTPUT);
  pinMode(DIR_1, OUTPUT);

  pinMode(STEP_2, OUTPUT);
  pinMode(DIR_2, OUTPUT);

  pinMode(ENDSW_1, INPUT);
  pinMode(ENDSW_2, INPUT);



  Serial.begin(115200);
  mySerial.begin(9600);

  homeAll();

  digitalWrite(DIR_1, HIGH);
  digitalWrite(DIR_2, LOW);

}




byte buffer[4];
void loop() {
  if (mySerial.available() == 2) {
    for (int i = 1; i >= 0; i--) {
      buffer[i] = mySerial.read();
      Serial.print(i);
      Serial.print(": ");
      Serial.println(buffer[i], DEC);
    }
    uint8_t nextPos_1 = buffer[0];
    uint8_t nextPos_2 = buffer[1];

    if (nextPos_1 < 255 && nextPos_2 < 255) {

      ticksToMove =  (int16_t)mmToTicks(nextPos_1) - encoderPos;
      moveToTicks = encoderPos + ticksToMove;

      stepsToMove =  (int16_t)mmToSteps(nextPos_2) - currentPos;
      stepsMoved = 0;
      
      Serial.print(stepsToMove);
      Serial.print(" ");
      Serial.print(stepsMoved);
      Serial.print(" ");
      Serial.print(ticksToMove);
      Serial.print(" ");
      Serial.println(encoderPos);
    }
    else
    {
      for (int i = 2; i >= 0; i--) {
        buffer[i] = mySerial.read();
        Serial.print(i);
        Serial.print(": ");
        Serial.println(buffer[i], DEC);
      }
      if (buffer[0] == 0xFF && buffer[1] == 0xFF && buffer[2] == 0xFF) {
        Serial.println("HOMING");
        homeAll();
      }
    }
  }

  moveMotors();

}








void moveMotors() {

  periodTimer = micros();
  digitalWrite(13, LOW);

  if (ENDSW_1 && encoderPos < mmToTicks(200)) {

    if (encoderPos < (moveToTicks - 10)) {
      digitalWrite(DIR_1, HIGH);
      digitalWrite(STEP_1, HIGH);
      digitalWrite(STEP_1, LOW);
    }

    else if (encoderPos > (moveToTicks + 10))
    {
      digitalWrite(DIR_1, LOW);
      digitalWrite(STEP_1, HIGH);
      digitalWrite(STEP_1, LOW);
    }
  }
  if (ENDSW_2 && currentPos < mmToSteps(200)) {
    //    Serial.print(currentPos);
    //    Serial.print(" ");
    //    Serial.print(stepsToMove);
    //    Serial.print(" ");
    //    Serial.println(stepsMoved);
    if (stepsToMove > 0) {
      digitalWrite(DIR_2, LOW);
      if (stepsMoved < stepsToMove) {
        digitalWrite(STEP_2, HIGH);
        digitalWrite(STEP_2, LOW);
        stepsMoved++;
        currentPos++;
      }
    }
    if (stepsToMove < 0) {
      digitalWrite(DIR_2, HIGH);
      if (stepsMoved < (stepsToMove * (-1))) {
        digitalWrite(STEP_2, HIGH);
        digitalWrite(STEP_2, LOW);
        stepsMoved++;
        currentPos--;
      }
    }

  }
  while (micros() - periodTimer < stepPeriod) {
    digitalWrite(13, HIGH);
    //we have extra time
  }
}


uint16_t mmToSteps(uint8_t distance) {
  return (stepsPerMM * distance);
}

uint16_t mmToTicks(uint8_t distance) {
  return (mmToSteps(distance) * ticksPerStep);
}

void read_encoder_ISR()
{
  volatile uint8_t oldAB = state & B0011; // save old state
  if (digitalRead(pinA)) state = state | B1000;
  if (digitalRead(pinB)) state = state | B0100;
  encoderPos += encoderStates[state];
  state = state >> 2; //this position is now old
}

void homeAll() {
  //HOMING SERVO
  digitalWrite(DIR_1, LOW);
  while (true) {
    digitalWrite(STEP_1, HIGH);
    digitalWrite(STEP_1, LOW);
    delayMicroseconds(250);
    //END SWITCH found
    if (!digitalRead(ENDSW_1)) {
      //go back 5mm
      digitalWrite(DIR_1, HIGH);
      for (int i = 0; i < 400; i++) {
        digitalWrite(STEP_1, HIGH);
        digitalWrite(STEP_1, LOW);
        delayMicroseconds(500);
      }
      //to end switch slower
      digitalWrite(DIR_1, LOW);
      while (digitalRead(ENDSW_1)) {
        digitalWrite(STEP_1, HIGH);
        digitalWrite(STEP_1, LOW);
        delayMicroseconds(1000);
      }
      digitalWrite(DIR_1, HIGH);
      while (!digitalRead(ENDSW_1)) {
        digitalWrite(STEP_1, HIGH);
        digitalWrite(STEP_1, LOW);
        delayMicroseconds(2000);
      }
      encoderPos = 0 ;
      ticksToMove = 0;
      moveToTicks = 0;
      break;
    }
  }

  //HOMING STEPPER
  digitalWrite(DIR_2, HIGH);
  while (true) {
    digitalWrite(STEP_2, HIGH);
    digitalWrite(STEP_2, LOW);
    delayMicroseconds(250);
    //END SWITCH found
    if (!digitalRead(ENDSW_2)) {
      //go back 5mm
      digitalWrite(DIR_2, LOW);
      for (int i = 0; i < 400; i++) {
        digitalWrite(STEP_2, HIGH);
        digitalWrite(STEP_2, LOW);
        delayMicroseconds(500);
      }
      //to end switch slower
      digitalWrite(DIR_2, HIGH);
      while (digitalRead(ENDSW_2)) {
        digitalWrite(STEP_2, HIGH);
        digitalWrite(STEP_2, LOW);
        delayMicroseconds(1000);
      }
      digitalWrite(DIR_2, LOW);
      while (!digitalRead(ENDSW_2)) {
        digitalWrite(STEP_2, HIGH);
        digitalWrite(STEP_2, LOW);
        delayMicroseconds(2000);
      }
      currentPos  = 0;
      stepsToMove = 0 ;
      break;
    }
  }
}
