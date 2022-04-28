#include <Arduino.h>

#include <MPU6050_light.h>
#include "Wire.h"

#define PITCHBEND_THRESHOLD 5

struct double_char
{
  char firstByte;
  char secondByte;
};

MPU6050 mpu(Wire);

// D5 - GPIO pin 14
// D6 - GPIO pin 12
// D7 - GPIO pin 13
// A0 - Analog 0 pin
int d5 = 5;
int d6 = 6;
int d7 = 7;
int a0 = A0;
int a3 = A3;

byte lastNote = 0xFF; // No buttons are pressed
byte volumeControl = 0x07; // Takes one paramter
byte lastVolume = 0x00;

int analogVolumeOffset = 0; // default analog value of volume control

uint16_t lastPitchBend = 0x0000;

bool button1Pressed = false;
bool button2Pressed = false;
bool button3Pressed = false;

// Ordered in         C  D  E  F  G  A  B   C
// int numberToNote[] = {0, 2, 4, 5, 7, 9, 11, 12};
int numberToNote[] = {0, 0, 2, 4, 5, 7, 9, 11};

// Converts an integer into a character array
double_char intToChar(uint16_t value)
{
  // Separate integer into two bytes
  byte firstByte = value & 0b011111111;
  byte secondByte = (value >> 7);

  // Store the bytes in a char array
  double_char duple;
  duple.firstByte = firstByte;
  duple.secondByte = secondByte;

  return duple;
}

// Returns an unsigned integer containing
// pitchbend data based on MPU6050 rotation
uint16_t getMpuPitchBend()
{
  float xRot = mpu.getAngleX();
  float yRot = mpu.getAngleY();

  // Maximum distance of 90 deg
  float dist = sqrt(pow(xRot, 2) + pow(yRot, 2));
  if (xRot < 0)
  {
    dist *= -1.0f;
  }

  float percent = dist / 90.0f;
  uint16_t intValue = (int)(8192.0f * percent) + 8192;

  #ifdef DEBUG
    Serial.print("X rotation: ");
    Serial.println(xRot);
    Serial.print("\nY Rotation: ");
    Serial.print(yRot);
    Serial.print("\n\n");
  #endif
  
  return intValue;
}


#define VOLUME_DEADZONE 10
#define VOLUME_VARIANCE 40.0f
// Returns an byte integer with the second and 
// third byte of the pitchbend command
byte getVolume()
{
  // Get the analog value of the pitchbend potentiometer
  float analogValue = float(analogRead(a3) - analogVolumeOffset);
  
  // [0 - 126]
  float float_value = 0.0f;
  // [440 - 510] 
  if (analogValue < VOLUME_DEADZONE)
  {
    float_value = 0;
  }
  else
  {
    float_value = (analogValue/ VOLUME_VARIANCE) * 126.0f;
    // Cap float value at 126
    float_value = min(float_value, 126);  

    float_value = (float(int(lastVolume)) + float_value) / 2.0f;
    float_value = floor(float_value);
  }
  int value = (int)(float_value);
  byte output = (byte)(value);
  return output;
}

// Returns the octave selected by the slider
int getOctave()
{
  float maxVoltage = 3.3f;
  float minVoltage = 0.0f;

  float voltRange = maxVoltage - minVoltage;

  delay(10);
  float analogValue = float(analogRead(a0));
  delay(10);

  // [355 - 720] analog range with linear potentiometer
  /*
   * 0% - 20%   :  1st Octave  (bottom)
   * 20% - 40%  :  2nd Octave
   * 40% - 60%  :  3rd Octave  (middle)
   * 60% - 80%  :  4th Octave
   * 80% - 100% :  5th octabe  (top)
   * 
   */


  // [18 - 20]
  if (analogValue < 20)
  {
    return 1;
  }
  // [20 - 26]
  else if (analogValue < 26)
  {
    return 2;
  }
  // [26 - 49] Middle
  else if (analogValue < 49)
  {
    return 3;
  }
  // [49 - 165]
  else if (analogValue < 165)
  {
    return 4;
  }
  // [165 - 720]
  else if (analogValue < 720)
  {
    return 5;
  }
  
  return 1;
}

// Returns the current note based on octave and buttons pressed
byte getNote()
{  
  byte pressedButtons = 0b00000000;
  
  if (digitalRead(d5) == HIGH)
  {
    pressedButtons = pressedButtons | 0b00000001;
  }
  if (digitalRead(d6) == HIGH)
  {
    pressedButtons = pressedButtons | 0b00000010;
  }
  if (digitalRead(d7) == HIGH)
  {
    pressedButtons = pressedButtons | 0b00000100;
  }
  if (pressedButtons == 0)
  {
    return 0xFF;
  }
  
  // Convert button press combination to whole note value
  int wholeNoteValue = numberToNote[pressedButtons];
  int octaveNum = (getOctave() + 2) * 12;           
  byte note = octaveNum + wholeNoteValue;

  return note;
}


int noteON = 0x90;  // Note ON command code for MIDI
int noteOFF = 0x80; // Note OFF command code for MIDI

const uint8_t velocity = 0x7F; // 0x59

// Writes a MIDI command to serial
void MIDImessage(byte command, byte MIDInote, byte MIDIvelocity)
{
  command |= 0b10000000;
  MIDInote &= 0b01111111;
  MIDIvelocity &= 0b01111111;
  
  Serial.write(command);
  Serial.write(MIDInote);
  Serial.write(MIDIvelocity);
  delay(10);
}

void setup() {
  // Setup serial MIDI stream 
  delay(1000);
  Serial.begin(38400, SERIAL_8N1);
  Wire.begin();
  pinMode(d5, INPUT);
  pinMode(d6, INPUT);
  pinMode(d7, INPUT);

  analogVolumeOffset = analogRead(a3);

  byte mpu_status = mpu.begin();
  while (mpu_status != 0) {}; // Wait for the mpu 6050 to calibrate
  delay(1000);
  mpu.calcOffsets();
}

void loop() {
  delay(5);
  mpu.update();  
  byte note = getNote();
  uint16_t pitchBend = getMpuPitchBend();

  byte volumeByte = getVolume();
  
  if (volumeByte == 0)
  {
    note = 0xFF;
  }
  
  if (lastVolume != volumeByte)
  {
    lastVolume = volumeByte;
    MIDImessage(0b10110000, volumeControl, volumeByte);
  }
  
  if (note != lastNote)
  {
    MIDImessage(noteOFF, lastNote, 0xFF);    

    if (note != 0xFF)                          // Plays no sound if no buttons are pressed
    {
      MIDImessage(noteON, note, 0xFF);  
    }
    lastNote = note;
  }

  if (note == 0xFF)
  {
    lastNote = 0xFF;
  }

  if (abs(pitchBend - lastPitchBend) > 100)
  {
    lastPitchBend = pitchBend;
    double_char bend_data = intToChar(pitchBend);
    byte firstByte = bend_data.firstByte & 0b01111111;
    byte secondByte = bend_data.secondByte & 0b01111111;

    MIDImessage(0xE0, firstByte, secondByte);
  }
}
