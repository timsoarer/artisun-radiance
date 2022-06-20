#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2); // The I2C ardess of the display is hexadecimal 0x27, the display is 16 characters long and 2 lines wide

bool
  leftA, leftAPrev,
  leftB, leftBPrev,
  leftSW, leftSWPrev,
  rightA, rightAPrev,
  rightB, rightBPrev,
  rightSW, rightSWPrev; // The values at the rotary encoder pins, as well as values from previous run of the loop() function

byte lastInputTimer = 0; // Time since last input (encoder rotation, button press), in seconds
byte debugSwitchTimer = 0; // Time for which both encoder buttons were held, in seconds
byte wakeupIncrementTimer = 0; // Time since the LED brightness was last incremented, in seconds
bool backlightOn; // Whether or not the LCD backlight is on

constexpr byte
  CLOCK_SECONDS = 0x00,
  CLOCK_MINUTES = 0x01,
  CLOCK_HOURS = 0x02; // Register addresses of the DS1307 RTC module for seconds, minutes and hours.

byte clockChar[] = { // Special character representing the clock symbol on the display
  0x00,
  0x0E,
  0x15,
  0x15,
  0x13,
  0x0E,
  0x00,
  0x00
};

byte sunChar[] = { // Special character representing the sun symbol on the display
  0x00,
  0x15,
  0x0E,
  0x1F,
  0x0E,
  0x15,
  0x00,
  0x1F
};

byte shortDash[] = { // A shorter version of the - character, for better readability
  0x00,
  0x00,
  0x00,
  0x0E,
  0x00,
  0x00,
  0x00,
  0x00
};

byte globalHours, globalMinutes, globalSeconds; // The current time in hours, minutes and seconds. These values are updated regularly through the readData() function.
byte wakeupHours = 7, wakeupMinutes = 0; // Time, by which the brightness of the LEDs should become naximum.
byte setHours = 0, setMinutes = 0; // When new time is being set on the device (either clock time or wakeup time), these variables decide, what will be the new value for the hours and minutes
byte debugColorChannel = 0; //Used entirely for the device's debug mode. 0 - red, 1 - green, 2 - blue, 3 - white
byte redValue = 0, greenValue = 0, blueValue = 0, whiteValue = 0; // The PWM values between 0 and 255 for each LED strip

bool hourUpdate = true, minuteUpdate = true, secondUpdate = true,
  setHourUpdate = true, setMinuteUpdate = true,
  ledValueUpdate = true, debugColorChannelUpdate = true;
// Since writing new characters on screen takes a lot of time, we instead update the screen only when those variables are true

byte screenState = 0; // 0 - default screen state, 1 - set clock tine, 2 - set wakeup time, 3 - debug
// DEFAULT - the default state the device starts in. Here you can see the current and wakeup time, as well as adjust hue and brightness of the LEDs.
// SET CLOCK - here you can set the device clock time to be the same as your local time. The device remembers the time even after it is turned off
// SET WAKEUP - here you can set the device wakeup tine. 30 minutes before the set wakeup time, the device will start to gradually increase the brightness of the LED strips. Is defaulted to 7:00
// DEBUG - you access this mode by holding both buttons at once. Here you can change the individual PWM value of each of the strips.

short hue = 0; // 0 = red, 100 = green, 200 = blue
//After 299 it loops back at 0, any value in between is a color between those two colors
// 0-100 is red-green transition, 100-200 is green-blue transition and 200-300 is blue-red transition

const short WHITE_THRESHOLD = 50; //When the brightness is below the threshold, only the RGB strip will be used. Once brightness value reaches this threshold the white LED strip will turn on.
short brightness = 0; // The brightness of the LED strips. The value goes between 0 and WHITE_THRESHOLD + 100.

const byte
  LEFT_SW = 2,
  LEFT_A = 7,
  LEFT_B = 4,
  RIGHT_SW = 11, 
  RIGHT_A = 10,
  RIGHT_B = 8,
  LED_RED = 3,
  LED_GREEN = 5,
  LED_BLUE = 6,
  LED_WHITE = 9; // Arduino pins that are used for this project, constant names correspond to the pins' roles.

bool WakeupSoon() { // checks is the current time is exacly 30 minutes behind wakeup time or less.
  short currentTime = globalMinutes + globalHours * 60;
  short wakeupTime = wakeupMinutes + wakeupHours * 60;
  if (currentTime > wakeupTime) {
    wakeupTime += 1440;
  }
  return (wakeupTime - currentTime) <= 30;
}

//Converts HB (hue-brightness) RGBW (red-green-blue-white) for the LED strips
void ConvertHBToRGBW() {
  short redPercent = 0, greenPercent = 0, bluePercent = 0; // Goes between 0 and 100
  // At 100 the resulting value will be equal to rgbBrightness, at 50 it will be equal to rgbBrightness / 2, etc.
  if (hue >= 0 && hue < 100) {
    redPercent = 100 - hue;
    greenPercent = hue;
  } else if (hue >= 100 && hue < 200) {
    greenPercent = 200 - hue;
    bluePercent = hue - 100;
  } else {
    bluePercent = 300 - hue;
    redPercent = hue - 200;
  }

  short rgbBrightness, whiteBrightness; // Goes between 0 and 255
  // rgbBrightness only reaches max when brightness reaches WHITE_THRESHOLD and whiteBrighness goes above 0 when brightness surpasses WHITE_THRESHOLD
  if (brightness > WHITE_THRESHOLD) { // 
    rgbBrightness = 255;
    whiteBrightness = ((brightness - WHITE_THRESHOLD) * 255) / 100; // since the maximum value for brighness is WHITE_THRESHOLD + 100, the maximum value for (brighness - WHITE_THRESHOLD) is 100
  } else {
    rgbBrightness = (brightness * 255) / WHITE_THRESHOLD;
    whiteBrightness = 0;
  }

  redValue = (rgbBrightness * redPercent) / 100;
  greenValue = (rgbBrightness * greenPercent) / 100;
  blueValue = (rgbBrightness * bluePercent) / 100;
  whiteValue = whiteBrightness;
}

// Converts an integer to boolean
bool IntToBool(int input) {return input > 0;}

// Checks whether or not the encoders were rotated, as well as direction, prioritizing accuracy first. If the input value is true, checks the right encoder, if false, checks the left encoder
int AccuracyFirstCheck(bool right) {
  if (right) {
    if ((!rightA && !rightB) && (!rightAPrev && rightBPrev)) {
      return -1;
    } else if ((!rightA && !rightB) && (rightAPrev && !rightBPrev)) {
      return 1;
    }
  } else {
    if ((!leftA && !leftB) && (!leftAPrev && leftBPrev)) {
      return -1;
    } else if ((!leftA && !leftB) && (leftAPrev && !leftBPrev)) {
      return 1;
    }
  }
  return 0;
}

//The clock's I2C adress is binary 1101000. Arduino first sends the clock register it wants to read from via I2C, then, without releasing the bus, reads the incoming byte and then releases
byte readClockRegister(byte registerAddress) {
  Wire.beginTransmission(0b1101000);
  Wire.write(registerAddress);
  Wire.endTransmission(false);
  Wire.requestFrom(0b1101000, 1, true);
  if (Wire.available()) {
    byte rcvData = Wire.read();
    return rcvData;
  }
  return 0;
}

//Arduino first send the clock register it wants to write to, then the value it wants to put in.
void writeClockRegister(byte address, byte value) {
  Wire.beginTransmission(0b1101000);
  Wire.write(address);
  Wire.write(value);
  Wire.endTransmission();
}

// The last bit of RTC's s 0x00 (CLOCK_SECONDS) register determines whether or not the oscillator is enabled
// If that bit is 0, the oscillator is enabled and the clock is ticking 
bool clockIsTicking() {
  return !(readClockRegister(CLOCK_SECONDS) & 0b10000000);
}

//Numbers on the clock module are stored in binary-coded decimal (BCD) format. To use these numbers, we first have to convert them to regular binary format
int decodeBCD(byte value) { 
  return 10 * ((value & 0b11110000) >> 4) + (value & 0b00001111);
}

//To write the time on the clock, we have to convert regular binary back into BCD
byte encodeBCDByte(int value) {
  return ((value / 10) << 4) | (value % 10);
}

void readData() { //read the hours, minutes and seconds of the clock and updates the global variables
  byte hours = decodeBCD(readClockRegister(CLOCK_HOURS));
  byte minutes = decodeBCD(readClockRegister(CLOCK_MINUTES));
  byte seconds = decodeBCD(readClockRegister(CLOCK_SECONDS));
  if (hours != globalHours) {
    globalHours = hours;
    hourUpdate = true;
  }
  if (minutes != globalMinutes) {
    globalMinutes = minutes;
    minuteUpdate = true;
  }
  if (seconds != globalSeconds) {
    globalSeconds = seconds;
    secondUpdate = true;
    if (lastInputTimer < 10) {
      lastInputTimer++;
    }
    if (leftSW && rightSW && debugSwitchTimer < 3) {
      debugSwitchTimer++;
    }
    if(WakeupSoon() && wakeupIncrementTimer < 12) {
      wakeupIncrementTimer++;
    } else if (!WakeupSoon()) {
      wakeupIncrementTimer = 0;
    }
  }
}

void setCurrentTime(int hour, int minute, int second) { //Sets the time on the clock based on the time given
  writeClockRegister(CLOCK_SECONDS, encodeBCDByte(second));
  writeClockRegister(CLOCK_MINUTES, encodeBCDByte(minute));
  writeClockRegister(CLOCK_HOURS, encodeBCDByte(hour));
}

// Writes the text in the DEFAULT screen state that won't change
void DefaultScreenSetup() {
  lcd.setCursor(0, 0);
  lcd.write(0);
  lcd.setCursor(3, 0);
  lcd.print(":");
  lcd.setCursor(6, 0);
  lcd.print(":");
  lcd.setCursor(9, 0);
  lcd.print(" ");
  lcd.write(1);
  lcd.setCursor(13, 0);
  lcd.print(":");
  lcd.setCursor(0, 1);
  lcd.print("LB");
  lcd.write(2);
  lcd.print("Set");
  lcd.write(0);
  lcd.setCursor(7, 1);
  lcd.print(" RB");
  lcd.write(2);
  lcd.print("Set");
  lcd.write(1);
  hourUpdate = true;
  minuteUpdate = true;
  secondUpdate = true;

  lcd.setCursor(11, 0);
  if (wakeupHours < 10) lcd.print("0");
  lcd.print(wakeupHours);
  lcd.setCursor(14, 0);
  if (wakeupMinutes < 10) lcd.print("0");
  lcd.print(wakeupMinutes);
}

// Updates the text in the DEFAULT screen state in case the values printed are changed
void DefaultScreenLoop() {
  if (hourUpdate) {
    lcd.setCursor(1, 0);
    if (globalHours < 10) lcd.print("0");
    lcd.print(globalHours);
    hourUpdate = false;
  }
  if (minuteUpdate) {
    lcd.setCursor(4, 0);
    if (globalMinutes < 10) lcd.print("0");
    lcd.print(globalMinutes);
    minuteUpdate = false;
  }
  if (secondUpdate) {
    lcd.setCursor(7, 0);
    if (globalSeconds < 10) lcd.print("0");
    lcd.print(globalSeconds);
    secondUpdate = false;
  }
}

// Writes the text in the SET CLOCK screen state that won't change
void ClockSetScreenSetup() {
  lcd.setCursor(0, 0);
  lcd.write(0);
  lcd.setCursor(3, 0);
  lcd.print(":");
  lcd.setCursor(6, 0);
  lcd.print(":");
  lcd.setCursor(9, 0);
  lcd.print("->");
  lcd.setCursor(13, 0);
  lcd.print(":");
  lcd.setCursor(0, 1);
  lcd.print("L");
  lcd.write(2);
  lcd.print("Hour R");
  lcd.write(2);
  lcd.print("Minute  ");
  hourUpdate = true;
  minuteUpdate = true;
  secondUpdate = true;
  setHourUpdate = true;
  setMinuteUpdate = true;
}

// Updates the text in the SET CLOCK screen state in case the values printed are changed
void ClockSetScreenLoop() {
  if (hourUpdate) {
    lcd.setCursor(1, 0);
    if (globalHours < 10) lcd.print("0");
    lcd.print(globalHours);
    hourUpdate = false;
  }
  if (minuteUpdate) {
    lcd.setCursor(4, 0);
    if (globalMinutes < 10) lcd.print("0");
    lcd.print(globalMinutes);
    minuteUpdate = false;
  }
  if (secondUpdate) {
    lcd.setCursor(7, 0);
    if (globalSeconds < 10) lcd.print("0");
    lcd.print(globalSeconds);
    secondUpdate = false;
  }
  if (setHourUpdate) {
    lcd.setCursor(11, 0);
    if (setHours < 10) lcd.print("0");
    lcd.print(setHours);
    setHourUpdate = false;
  }
  if (setMinuteUpdate) {
    lcd.setCursor(14, 0);
    if (setMinutes < 10) lcd.print("0");
    lcd.print(setMinutes);
    setMinuteUpdate = false;
  }
}

// Writes the text in the SET WAKEUP screen state that won't change
void WakeSetScreenSetup() {
  lcd.setCursor(0, 0);
  lcd.write(1);
  lcd.setCursor(3, 0);
  lcd.print(":");
  lcd.setCursor(6, 0);
  lcd.print("->");
  lcd.setCursor(10, 0);
  lcd.print(":");
  lcd.setCursor(13, 0);
  lcd.print("   ");
  lcd.setCursor(0, 1);
  lcd.print("L");
  lcd.write(2);
  lcd.print("Hour R");
  lcd.write(2);
  lcd.print("Minute  ");
  hourUpdate = true;
  minuteUpdate = true;
  secondUpdate = true;
  setHourUpdate = true;
  setMinuteUpdate = true;

  lcd.setCursor(1, 0);
  if (wakeupHours < 10) lcd.print("0");
  lcd.print(wakeupHours);
  lcd.setCursor(4, 0);
  if (wakeupMinutes < 10) lcd.print("0");
  lcd.print(wakeupMinutes);
}

// Updates the text in the SET WAKEUP screen state in case the values printed are changed
void WakeSetScreenLoop() {
  if (setHourUpdate) {
    lcd.setCursor(8, 0);
    if (setHours < 10) lcd.print("0");
    lcd.print(setHours);
    setHourUpdate = false;
  }
  if (setMinuteUpdate) {
    lcd.setCursor(11, 0);
    if (setMinutes < 10) lcd.print("0");
    lcd.print(setMinutes);
    setMinuteUpdate = false;
  }
}

// Writes the text in the DEBUG screen state that won't change
void DebugScreenSetup() {
  lcd.setCursor(0, 0);
  lcd.print(" R  G  B  W ");
  lcd.setCursor(15, 0);
  lcd.print(" ");
  lcd.setCursor(0, 1);
  lcd.print("L");
  lcd.write(2);
  lcd.print("sel R");
  lcd.write(2);
  lcd.print("adj     ");
  ledValueUpdate = true;
  debugColorChannelUpdate = true;
}

// Updates the text in the DEBUG screen state in case the values printed are changed
void DebugScreenLoop() {
  byte printedValue = 0;

  if (debugColorChannelUpdate) {
    lcd.setCursor(0, 0);
    if (debugColorChannel == 0) {lcd.print(">");} else {lcd.print(" ");}
    lcd.setCursor(3, 0);
    if (debugColorChannel == 1) {lcd.print(">");} else {lcd.print(" ");}
    lcd.setCursor(6, 0);
    if (debugColorChannel == 2) {lcd.print(">");} else {lcd.print(" ");}
    lcd.setCursor(9, 0);
    if (debugColorChannel == 3) {lcd.print(">");} else {lcd.print(" ");}

    debugColorChannelUpdate = false;
    ledValueUpdate = true;
  }

  if (debugColorChannel == 0) {printedValue = redValue;}
  if (debugColorChannel == 1) {printedValue = greenValue;}
  if (debugColorChannel == 2) {printedValue = blueValue;}
  if (debugColorChannel == 3) {printedValue = whiteValue;}

  if (ledValueUpdate) {
    lcd.setCursor(12, 0);
    if (printedValue < 100) {lcd.print("0");}
    if (printedValue < 10) {lcd.print("0");}
    lcd.print(printedValue);

    ledValueUpdate = false;
  }
}

//Adds an unsigned integer byte and a signed integer short together. input is an out parameter. If input + additive is larger than boundsLoop, the value of input is looped back to 0. If additive < 0 and input + additive < 0, input loops back to boundsLoop
//Returns an integer value between -1 and 1 depending on whether or not the value looped, and in which direction it was looped
short AdditionBounds(byte *input, short additive, byte boundsLoop) {
  if (*input < (-additive) && additive < 0) {
    *input = boundsLoop;
    return -1;
  }

  if (*input + additive > boundsLoop) {
    *input = 0;
    return 1;
  }

  *input = *input + additive;
  return 0;
}

//Does the same thing but with two integer shorts
short AdditionBounds(short *input, short additive, short boundsLoop) {
  if (*input < (-additive) && additive < 0) {
    *input = boundsLoop;
    return -1;
  }

  if (*input + additive > boundsLoop) {
    *input = 0;
    return 1;
  }

  *input = *input + additive;
  return 0;
}

//Adds an unsigned integer byte and a signed integer short together. input is an out parameter. Clamps the value of input between 0 and max.
void AdditionMinMax(byte *input, short additive, byte max) {
  if (*input + additive > max) {
    *input = max;
    return;
  }

  if (*input < (-additive) && additive < 0) {
    *input = 0;
    return;
  }

  *input = *input + additive;
  return;
}

//Does the same thing but with integer shorts
void AdditionMinMax(short *input, short additive, short max) {
  if (*input + additive > max) {
    *input = max;
    return;
  }

  if (*input < (-additive) && additive < 0) {
    *input = 0;
    return;
  }

  *input = *input + additive;
  return;
}

void setup() {
  // put your setup code here, to run once:
  Wire.begin();

  //Initializes the required pins as input or output
  pinMode(LEFT_SW, INPUT);
  pinMode(LEFT_A, INPUT);
  pinMode(LEFT_B, INPUT);
  pinMode(RIGHT_SW, INPUT);
  pinMode(RIGHT_A, INPUT);
  pinMode(RIGHT_B, INPUT);
  
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(LED_WHITE, OUTPUT);
  
  //Sets an initial value for the previous cycle variables
  //Due to resistors pulling the input pin up to +3.3V by default, the values are essentially inverted
  //For the rotation that isn't a problem, but for the switches/buttons it's easier to invert those values so it is true when the button in pressed
  leftAPrev = IntToBool(digitalRead(LEFT_A));
  leftBPrev = IntToBool(digitalRead(LEFT_B));
  leftSWPrev = !IntToBool(digitalRead(LEFT_SW));
  rightAPrev = IntToBool(digitalRead(RIGHT_A));
  rightBPrev = IntToBool(digitalRead(RIGHT_B));
  rightSWPrev = !IntToBool(digitalRead(RIGHT_SW));

  // If the oscillator wasn't enabled, enable and reset time to default value
  if (!clockIsTicking()) {
    writeClockRegister(CLOCK_SECONDS, 0);
    writeClockRegister(CLOCK_HOURS, 0);
  }
  
  //Initializes the LCD display and creates the special characters
  lcd.init();
  lcd.createChar(0, clockChar);
  lcd.createChar(1, sunChar);
  lcd.createChar(2, shortDash);
  lcd.backlight();
  backlightOn = true;
  DefaultScreenSetup();
}

void loop() {
  // put your main code here, to run repeatedly:
  //Reads the data from the RTC module and encoder pins
  readData();
  leftA = IntToBool(digitalRead(LEFT_A));
  leftB = IntToBool(digitalRead(LEFT_B));
  leftSW = !IntToBool(digitalRead(LEFT_SW));
  rightA = IntToBool(digitalRead(RIGHT_A));
  rightB = IntToBool(digitalRead(RIGHT_B));
  rightSW = !IntToBool(digitalRead(RIGHT_SW));

  //lastInputTimer increments by 1 each second. If any input by the user is detected (rotating, pressing buttons) it is reset back to 0
  if (leftA != leftAPrev || leftB != leftBPrev || leftSW != leftSWPrev || rightA != rightAPrev || rightB != rightBPrev || rightSW != rightSWPrev) {
    lastInputTimer = 0;
  }
  //If no input is detected for 10 seconds and the backlight is on, turn off the backlight, otherwise keep it on
  if (lastInputTimer == 10 && backlightOn) {
    lcd.noBacklight();
    backlightOn = false;
  } else if (lastInputTimer < 10 && !backlightOn) {
    lcd.backlight();
    backlightOn = true;
  }
  
  //debugSwitchTimer increments by 1 each second for as long as both switches are held. If any of the two switches are released, the debugSwitchTimer is reset to 0 and the switch to debug mode is cancelled
  if (!leftSW || !rightSW) {
    debugSwitchTimer = 0;
  }

  //If both switches are held for 3 seconds, switch to/from debug mode
  if (debugSwitchTimer == 3) {
    debugSwitchTimer = 0;
    if (screenState != 3) {
      screenState = 3;
      debugColorChannel = 0;
      DebugScreenSetup();
    } else {
      DefaultScreenSetup();
      screenState = 0;
    }
  }
  //wakeupIncrementTimer increases by 1 every second when WakeupSoon() is true. Every 12 seconds increase the brightness and reset the timer
  if (wakeupIncrementTimer == 12) {
    brightness++;
    wakeupIncrementTimer = 0;
  }

  if (screenState == 0) {
    //DEFAULT screen state
    AdditionBounds(&hue, AccuracyFirstCheck(false) * 6, 299);
    AdditionMinMax(&brightness, AccuracyFirstCheck(true) * 3, WHITE_THRESHOLD + 100);
    ConvertHBToRGBW();
    DefaultScreenLoop();
    
    //If the left encoder button is pressed, change to the SET CLOCK screen state
    if (leftSW && !leftSWPrev) {
      ClockSetScreenSetup();
      screenState = 1;
      setHours = globalHours;
      setMinutes = globalMinutes;
    }

    //If the right encoder button is pressed, change to the SET WAKEUP screen state
    if (rightSW && !rightSWPrev) {
      WakeSetScreenSetup();
      screenState = 2;
      setHours = wakeupHours;
      setMinutes = wakeupMinutes;
    }
  } else if (screenState == 1) {
    //SET CLOCK screen state
    short minuteChange = AccuracyFirstCheck(true);
    if (minuteChange != 0) {
      setMinuteUpdate = true;
    }
    short hourChange = AdditionBounds(&setMinutes, minuteChange, 59);
    hourChange += AccuracyFirstCheck(false);
    if (hourChange != 0) {
      setHourUpdate = true;
    }
    AdditionBounds(&setHours, hourChange, 23);

    ClockSetScreenLoop();

    // If either button is pressed and both of them aren't being held at the same time, return to DEFAULT screen state and update the clock time
    if ((!leftSWPrev && !rightSWPrev) && (leftSW || rightSW)) {
      DefaultScreenSetup();
      screenState = 0;
      setCurrentTime(setHours, setMinutes, 0);
    }
  } else if (screenState == 2) {
    //SET WAKEUP screen state
    short minuteChange = AccuracyFirstCheck(true);
    if (minuteChange != 0) {
      setMinuteUpdate = true;
    }
    short hourChange = AdditionBounds(&setMinutes, minuteChange, 59);
    hourChange += AccuracyFirstCheck(false);
    if (hourChange != 0) {
      setHourUpdate = true;
    }
    AdditionBounds(&setHours, hourChange, 23);

    WakeSetScreenLoop();

    // If either button is pressed and both of them aren't being held at the same time, return to DEFAULT screen state and update the wakeup time
    if ((!leftSWPrev && !rightSWPrev) && (leftSW || rightSW)) {
      wakeupHours = setHours;
      wakeupMinutes = setMinutes;
      DefaultScreenSetup();
      screenState = 0;
    }
  } else if (screenState == 3) {
    //DEBUG screen state
    short channelAdd = AccuracyFirstCheck(false);
    if (channelAdd != 0) {
      debugColorChannelUpdate = true;
    }
    short valueAdd = AccuracyFirstCheck(true);
    if (valueAdd != 0) {
      ledValueUpdate = true;
    }
    AdditionBounds(&debugColorChannel, channelAdd, 3);

    if (debugColorChannel == 0) {
      AdditionMinMax(&redValue, valueAdd, 255);
    } else if (debugColorChannel == 1) {
      AdditionMinMax(&greenValue, valueAdd, 255);
    } else if (debugColorChannel == 2) {
      AdditionMinMax(&blueValue, valueAdd, 255);
    } else {
      AdditionMinMax(&whiteValue, valueAdd, 255);
    }

    DebugScreenLoop();
  }

  analogWrite(LED_RED, redValue);
  analogWrite(LED_GREEN, greenValue);
  analogWrite(LED_BLUE, blueValue);
  analogWrite(LED_WHITE, whiteValue);

  leftAPrev = leftA;
  leftBPrev = leftB;
  leftSWPrev = leftSW;
  rightAPrev = rightA;
  rightBPrev = rightB;
  rightSWPrev = rightSW;
}