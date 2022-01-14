#include <Servo.h>
#include <TM1637Display.h>

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/Picopixel.h>

#include "dashb-lcd.h"

String property = "";
String value = "";
boolean prop_val = false;
boolean stringComplete = false;


Servo servo_speed;
Servo servo_rpm;

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


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

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  oled.clearDisplay();

  oled.drawBitmap(0, 0, background_data, SCREEN_WIDTH, SCREEN_HEIGHT, 1);
  oled.display();

  oled.setCursor(7, 10);
  oled.setTextSize(1);
  oled.setTextColor(WHITE);
  oled.print("_");
  oled.display();

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

      oled.fillRect(75, 36, 50, 10, SSD1306_BLACK);
      oled.setTextSize(1);
      oled.setCursor(5, 36);
      oled.print("SPEED");
      oled.setCursor(75, 36);
      oled.print(value);
      oled.display();
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
        oled.fillRect(6, 8, 7, 10, SSD1306_BLACK);
        oled.fillRect(17, 6, 24, 16, SSD1306_BLACK);

        oled.setCursor(7, 10);
        oled.setTextSize(1);
        oled.print("A");

        oled.setCursor(17, 6);
        oled.setTextSize(2);
        oled.print(v);

        oled.display();
      } else if (v == 0) {
        oled.fillRect(6, 8, 7, 10, SSD1306_BLACK);
        oled.fillRect(17, 6, 24, 16, SSD1306_BLACK);

        oled.setCursor(7, 10);
        oled.setTextSize(1);
        oled.print("N");

        oled.display();
      } else {
        oled.fillRect(6, 8, 7, 10, SSD1306_BLACK);
        oled.fillRect(17, 6, 24, 16, SSD1306_BLACK);

        oled.setCursor(7, 10);
        oled.setTextSize(1);
        oled.print("R");

        oled.setCursor(17, 6);
        oled.setTextSize(2);
        oled.print(v*-1);

        oled.display();
      }
    } else if (property == "game.time") {
      String t = String(value[0]);
      t.concat(value[1]);
      t.concat(":");
      t.concat(value[2]);
      t.concat(value[3]);
      oled.fillRect(3, 54, 30, 8, SSD1306_BLACK);
      oled.setTextSize(1);
      oled.setCursor(3, 54);
      oled.print(t);
      oled.display();
    } else {
      ; //Serial.println("NOPE");
    }

    property = "";
    value = "";
    stringComplete = false;
  }
}
