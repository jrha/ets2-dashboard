#include <Servo.h>
#include <TM1637Display.h>


String property = "";
String value = "";
boolean prop_val = false;
boolean stringComplete = false;


Servo servo_speed;
Servo servo_rpm;

TM1637Display display(2, 3); //clk, dio
static uint8_t BLANK[] = { 0x0, 0x0, 0x0, 0x0 };
static uint8_t INIT[] = { 0x0, SEG_G, SEG_G, 0x0};
//                             .GFEDCBA
static uint8_t NEUTRAL[] = { 0b00110111, 0x0, 0x0, 0x0 };
static uint8_t FORWARD[] = { 0b01110001, 0x0, 0x0, 0x0 };
static uint8_t REVERSE[] = { 0b00110011, 0x0, 0x0, 0x0 };
static uint8_t T_COLON[] = { 0x0, 0b10000000, 0x0, 0x0 };


void serialEvent() {
  while (Serial.available()) {
    // get the new byte:
    char inChar = (char)Serial.read();
    // if the incoming character is a newline, set a flag
    // so the main loop can do something about it:
    if (inChar == '=') {
      prop_val = true;
    } else if (inChar == '\n') {
      prop_val = false;
      stringComplete = true;
    } else {
      if (!prop_val) {
        property += inChar;
      } else {
        value += inChar;
      }
    }
  }
}


void setup() {
  servo_speed.attach(9);
  servo_rpm.attach(10);

  servo_speed.write(180);
  servo_rpm.write(180);
  delay(500);
  servo_speed.write(0);
  servo_rpm.write(0);
  delay(500);
  servo_speed.write(180);
  servo_rpm.write(180);

  display.setSegments(BLANK);
  display.setBrightness(0x1);
  display.setSegments(INIT);

  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for Leonardo only
  }
}


void pin_bool(int pin, String v) {
  if (v == "1") {
    digitalWrite(pin, HIGH);
  } else {
    digitalWrite(pin, LOW);
  }
}


void loop() {
  if (stringComplete) {
    /*
    Serial.print("[");
    Serial.print(property);
    Serial.print("|");
    Serial.print(value);
    Serial.println("]");
    */

    if (property == "truck.speed") {
      servo_speed.write(180 - value.toInt());
    } else if (property == "truck.engine.rpm") {
      servo_rpm.write(180 - value.toInt());
    } else if (property == "truck.light.rblinker") {
      pin_bool(11, value);
    } else if (property == "truck.light.lblinker") {
      pin_bool(12, value);
    } else if (property == "truck.light.brake") {
      pin_bool(8, value);
    } else if (property == "truck.light.beam.low") {
      pin_bool(7, value);
    } else if (property == "truck.light.beam.high") {
      pin_bool(4, value);
    } else if (property == "truck.displayed.gear") {
      int v = value.toInt();
      if (v > 0) {
        display.setSegments(FORWARD);
        display.showNumberDec(v, false, 2, 1);
      } else if (v == 0) {
        display.setSegments(NEUTRAL);
        display.showNumberDec(v, false, 2, 1);
      } else {
        display.setSegments(REVERSE);
        display.showNumberDec(v*-1, false, 2, 1);
      }
    } else if (property == "game.time") {
      int v = value.toInt();
      display.showNumberDecEx(v, 0b01000000, true, 4, 0);
    } else {
      ; //Serial.println("NOPE");
    }

    property = "";
    value = "";
    stringComplete = false;
  }
}
