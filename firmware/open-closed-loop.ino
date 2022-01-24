/**
    This is a library written for the TODO:  title

    https://github.com/JenertsA/open-closed-loop

    Written by Andris Jenerts @ ViA 2021

    Code handle software serial commands from Nextion display and
    generates STEP and DIR signals for two stepper motors. 
    Optical encoder is used to measure position of one stepper, this position
    is used as feedback to close the loop for this motor contorl. 

    Development environment specifics:
    Arduino IDE 1.8.15

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY

    Copyright (c) 2021 ViA. All rights reserved.

    This work is licensed under the terms of the MIT license.  
    For a copy, see <https://opensource.org/licenses/MIT>.

*/

/**
 * Atmega328P used on Arduino Nano has only 
 * one hardware UART interface. This intrface by default
 * is used to program the board. 
 * Nextion display also uses UART interface to communicate.
 * To maintain possibility to easily reprogram the board
 * software serial library is used to generate bit-banged
 * signal for UART communication.
 */
#include <SoftwareSerial.h>
SoftwareSerial nextionSerial(A5, A4); //RX pin and TX pin for Nextion

/**
 * Encoder phases are connected to pins
 * with hardware interrupts 
 */
const byte pinA = 3;
const byte pinB = 2;

// Quadrature Encoder Matrix
// Here is a good tutorial:
// https://cdn.sparkfun.com/datasheets/Robotics/How%20to%20use%20a%20quadrature%20encoder.pdf
const int8_t encoderStates[] = {0, -1, 1, 2, 1, 0, -2, -1, -1, -2, 0, 1, 2, 1, -1};
// 3:4 bit current state AB, 0:1 bit old state AB
volatile uint8_t state = B0000;

/**
 * constants for the motor
 * steps per mm takes into account microstepping and
 * belt reduction 
 */
const byte stepsPerMM = 40;     //how many steps need to be made to move 1mm
const float ticksPerStep = 2.5; //how many encoder ticks registered for 1mm

/**
 * Motor driver control pin
 * and end switch pin 
 * definitions
 */
const byte STEP_1 = 9;
const byte DIR_1 = 8;
const byte ENDSW_1 = A1;

const byte STEP_2 = 12;
const byte DIR_2 = 11;
const byte ENDSW_2 = A0;

/**
 * how many steps are needed to turn one degree
 * motor has 200 steps and we use 1/8 microstepping
 */
const float stepDeg = (float)1600 / 360.0;

/**
 * speed of rotation
 */
const uint16_t degS = 500;                    // absolute speed
const float stepsS = stepDeg * degS;          // how many steps we need to do in 1s
const uint16_t stepPeriod = 1000000 / stepsS; // what is the period of the steps in us

// variable used for measuring and enforcing step period
uint32_t periodTimer;

/**
 * position information 
 */
uint16_t currentPos = 0; // current position of motor in step count
int stepsToMove = 0;     // how many steps we need to move
uint16_t stepsMoved = 0; // how many steps we have moved

volatile int16_t encoderPos = 0; // what is the position of encoder
int16_t ticksToMove = 0;         // how many encoder ticks to reach destination
int16_t moveToTicks = 0;         // what is the final postion in encoder ticks

/**
 *  buffer that holds commands from Nextion display
 */
byte buffer[4];

/**
 * setup is run on time
 * when device is powered on
 */
void setup()
{
  /**
   * I/O configuration 
   */
  pinMode(13, OUTPUT); // inbuilt led

  pinMode(STEP_1, OUTPUT);
  pinMode(DIR_1, OUTPUT);

  pinMode(STEP_2, OUTPUT);
  pinMode(DIR_2, OUTPUT);

  pinMode(ENDSW_1, INPUT);
  pinMode(ENDSW_2, INPUT);

  /**
   * effective encoder reading requires 
   * use of hardware interrputs
   * interrupts and according ISR (interrupt service rotines)
   * are defined here
   */
  pinMode(pinA, INPUT);
  pinMode(pinB, INPUT);
  attachInterrupt(digitalPinToInterrupt(pinA), read_encoder_ISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinB), read_encoder_ISR, CHANGE);

  delay(10);

  //initial state for encoder based on current readings
  if (digitalRead(pinA))
    state = state | B0010;
  if (digitalRead(pinB))
    state = state | B0001;

  Serial.begin(115200);      // Serial port with computer
  nextionSerial.begin(9600); // Software serial port with Nextion display

  /**
   * motors are mounted in mirror 
   * that is why
   * 1st motors DIR is LOW
   * 2nd motors DIR is HIGH
   * indicator on both axes moves in the same direction. 
   */
  digitalWrite(DIR_1, LOW);
  digitalWrite(DIR_2, HIGH);

  // home all axis on power on
  homeAll();

  // just to be safe
  digitalWrite(DIR_1, LOW);
  digitalWrite(DIR_2, HIGH);
}

/**
 * loop is running continiously
 * until device is powered down
 */
void loop()
{
  // there is nextion display command available
  if (nextionSerial.available() == 2)
  {

    for (int i = 1; i >= 0; i--)
    {
      // read received two bytes into out buffer
      buffer[i] = nextionSerial.read();

      // print out received bytes
      Serial.println("Received from Nextion");
      Serial.print(i);
      Serial.print(": ");
      Serial.println(buffer[i], DEC);
    }

    /**
     * save information from buffer 
     * to next position variable for each axis
     */
    uint8_t nextPos_1 = buffer[0];
    uint8_t nextPos_2 = buffer[1];

    /**
     * if value less than 0xFF is received
     * we received location where we need to go
     */
    if (nextPos_1 < 255 && nextPos_2 < 255)
    {
      /**
       * closed loop motor is moved based on encoder ticks
       * once received next destination we: 
       *  - calculate what will be tick count once reached destination
       *  - calculate how many ticks will be needed to reach destination
       */
      moveToTicks = mmToTicks(nextPos_1);
      ticksToMove = moveToTicks - encoderPos;

      /**
       * open loop motor is moved specific
       * count of steps that is calculated before movement
       * we need to: 
       *  - calculate how many steps we need to move
       *  - set that we have moved 0 steps
       */
      stepsToMove = (int16_t)mmToSteps(nextPos_2) - currentPos;
      stepsMoved = 0;

      // serial output for debuging
      Serial.print("steps to move: ");
      Serial.print(stepsToMove);
      Serial.print(" current pos: ");
      Serial.print(currentPos);

      Serial.print(" ticks to move ");
      Serial.print(ticksToMove);
      Serial.print("current encoder pos: ");
      Serial.println(encoderPos);
    }
    else
    {
      //somthing else should be in the serial buffer read it in
      for (int i = 2; i >= 0; i--)
      {
        buffer[i] = nextionSerial.read();
        Serial.println("Received from Nextion");
        Serial.print(i);
        Serial.print(": ");
        Serial.println(buffer[i], DEC);
      }

      //homing should be performed
      if (buffer[0] == 0xFF && buffer[1] == 0xFF && buffer[2] == 0xFF)
      {
        Serial.println("HOMING");
        homeAll();
      }
    }
  }

  //we we have nothing in out buffer to process we move motors
  moveMotors();
}

/**
 * based on current position and 
 * desired position move motors by one step
 * if necessary
 */
void moveMotors()
{
  // this is used to time our step
  // digital output 13 can be used to debug timing
  periodTimer = micros();
  digitalWrite(13, LOW);

  //closed loop
  //is endswitch is not pressed and soft limit is not reached
  if (digitalRead(ENDSW_1) && encoderPos < mmToTicks(200))
  {
    //we need to move forward
    if (encoderPos < (moveToTicks - 10))
    {
      digitalWrite(DIR_1, LOW);
      digitalWrite(STEP_1, HIGH);
      digitalWrite(STEP_1, LOW);
    }

    //we need to move backward
    else if (encoderPos > (moveToTicks + 10))
    {
      digitalWrite(DIR_1, HIGH);
      digitalWrite(STEP_1, HIGH);
      digitalWrite(STEP_1, LOW);
    }
  }

  //open loop
  //is endswitch is not pressed and soft limit is not reached
  if (digitalRead(ENDSW_2) && currentPos < mmToSteps(200))
  {
    //we need to move forward
    if (stepsToMove > 0)
    {
      digitalWrite(DIR_2, HIGH);

      //we are not at the destination
      if (stepsMoved < stepsToMove)
      {
        digitalWrite(STEP_2, HIGH);
        digitalWrite(STEP_2, LOW);
        //save that we moved
        stepsMoved++;
        currentPos++;
      }
    }

    //we need to move backward
    if (stepsToMove < 0)
    {
      digitalWrite(DIR_2, LOW);
      //we are not at the destination
      if (stepsMoved < (stepsToMove * (-1)))
      {
        digitalWrite(STEP_2, HIGH);
        digitalWrite(STEP_2, LOW);
        //save that we moved
        stepsMoved++;
        currentPos--;
      }
    }
  }

  // limit our period
  while (micros() - periodTimer < stepPeriod)
  {
    digitalWrite(13, HIGH);

    // we have extra time here
  }
}


//FUNCTIONS//

/**
 * @brief calcaulate how many steps needed 
 * to go specified distance
 * 
 * @param distance  distance in mm
 * @return step count
 */
uint16_t mmToSteps(uint8_t distance)
{
  return (stepsPerMM * distance);
}

/**
 * @brief calcaulate how many encoder ticks needed 
 * to go specified distance
 * 
 * @param distance  distance in mm
 * @return encoder tick count
 */
uint16_t mmToTicks(uint8_t distance)
{
  return (mmToSteps(distance) * ticksPerStep);
}

/**
 * @brief ISR for the encoder interrupts
 * each time encoder produces a tick, it is registered and
 * current encoder position is updated
 */
void read_encoder_ISR()
{
  volatile uint8_t oldAB = state & B0011; // save old state
  if (digitalRead(pinA))
    state = state | B1000;
  if (digitalRead(pinB))
    state = state | B0100;
  encoderPos += encoderStates[state];
  state = state >> 2; //this position is now old
}

/**
 * @brief Function that performes homing for
 * both motors
 */
void homeAll()
{
  //HOMING SERVO
  digitalWrite(DIR_1, HIGH);
  while (true)
  {
    digitalWrite(STEP_1, HIGH);
    digitalWrite(STEP_1, LOW);
    delayMicroseconds(250);
    //END SWITCH found
    if (!digitalRead(ENDSW_1))
    {
      //go back 5mm
      digitalWrite(DIR_1, LOW);
      for (int i = 0; i < 400; i++)
      {
        digitalWrite(STEP_1, HIGH);
        digitalWrite(STEP_1, LOW);
        delayMicroseconds(500);
      }
      //to end switch slower
      digitalWrite(DIR_1, HIGH);
      while (digitalRead(ENDSW_1))
      {
        digitalWrite(STEP_1, HIGH);
        digitalWrite(STEP_1, LOW);
        delayMicroseconds(1000);
      }
      digitalWrite(DIR_1, LOW);
      while (!digitalRead(ENDSW_1))
      {
        digitalWrite(STEP_1, HIGH);
        digitalWrite(STEP_1, LOW);
        delayMicroseconds(2000);
      }
      encoderPos = 0;
      ticksToMove = 0;
      moveToTicks = 0;
      break;
    }
  }

  //HOMING STEPPER
  digitalWrite(DIR_2, LOW);
  while (true)
  {
    digitalWrite(STEP_2, HIGH);
    digitalWrite(STEP_2, LOW);
    delayMicroseconds(250);
    //END SWITCH found
    if (!digitalRead(ENDSW_2))
    {
      //go back 5mm
      digitalWrite(DIR_2, HIGH);
      for (int i = 0; i < 400; i++)
      {
        digitalWrite(STEP_2, HIGH);
        digitalWrite(STEP_2, LOW);
        delayMicroseconds(500);
      }
      //to end switch slower
      digitalWrite(DIR_2, LOW);
      while (digitalRead(ENDSW_2))
      {
        digitalWrite(STEP_2, HIGH);
        digitalWrite(STEP_2, LOW);
        delayMicroseconds(1000);
      }
      digitalWrite(DIR_2, HIGH);
      while (!digitalRead(ENDSW_2))
      {
        digitalWrite(STEP_2, HIGH);
        digitalWrite(STEP_2, LOW);
        delayMicroseconds(2000);
      }
      currentPos = 0;
      stepsToMove = 0;
      break;
    }
  }
}
