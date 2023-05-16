#define THERMO_CS 8
#define SSR_PIN 2
#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST -1 // RST can be set to -1 if you tie it to Arduino's reset
// Note the X and Y pin numbers are opposite from what is printed on the TFT display. This was done to align with the screen rotation.
#define YP A0  // must be an analog pin, use "An" notation!
#define XM A1  // must be an analog pin, use "An" notation!
#define YM 7   // can be a digital pin
#define XP 6   // can be a digital pin
// This is calibration data for the raw touch data to the screen coordinates
#define TS_MINX 190
#define TS_MINY 400
#define TS_MAXX 890
#define TS_MAXY 820

#include <PID_v1.h>
#include <Adafruit_MAX31856.h>
#include <SPI.h>
#include "Adafruit_GFX.h"
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include "Adafruit_HX8357.h"
#include <stdint.h>
#include "TouchScreen.h"

// Use hardware SPI (on Uno, #13, #12, #11) and the above for CS/DC
Adafruit_HX8357 tft = Adafruit_HX8357(TFT_CS, TFT_DC, TFT_RST);
// SoftSPI - note that on some processors this might be *faster* than hardware SPI!
//Adafruit_HX8357 tft = Adafruit_HX8357(TFT_CS, TFT_DC, SOFT_MOSI, SOFT_CLK, TFT_RST, SOFT_MISO);
const int displayWidth = 480, displayHeight = 320;
const int gridSize = 80;
// For better pressure precision, we need to know the resistance
// between X+ and X- Use any multimeter to read it
// For the one we're using, its 300 ohms across the X plate
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);
TSPoint touchpoint = ts.getPoint();
bool setupMenu = false, editMenu = false, reflowMenu = false;
const int touchHoldLimit = 500;

// use hardware SPI, just pass in the CS pin
Adafruit_MAX31856 maxthermo = Adafruit_MAX31856(THERMO_CS);
// Use software SPI: CS, DI, DO, CLK
//Adafruit_MAX31856 maxthermo = Adafruit_MAX31856(THERMO_CS, SOFT_MOSI, SOFT_MISO, SOFT_CLK);

unsigned long timeSinceReflowStarted;
unsigned long timeTempCheck = 1000;
unsigned long lastTimeTempCheck = 0;
double preheatTemp = 180, soakTemp = 150, reflowTemp = 230, cooldownTemp = 25;
unsigned long preheatTime = 120000, soakTime = 60000, reflowTime = 120000, cooldownTime = 120000, totalTime = preheatTime + soakTime + reflowTime + cooldownTime;
bool preheating = false, soaking = false, reflowing = false, coolingDown = false, newState = false;
uint16_t gridColor = 0x7BEF;
uint16_t preheatColor = HX8357_RED, soakColor = 0xFBE0,   reflowColor = 0xDEE0,   cooldownColor = HX8357_BLUE; // colors for plotting
uint16_t preheatColor_d = 0xC000,   soakColor_d = 0xC2E0, reflowColor_d = 0xC600, cooldownColor_d =  0x0018; // desaturated colors

// Define Variables we'll be connecting to
double Setpoint, Input, Output;


// Specify the links and initial tuning parameters
double Kp=2, Ki=5, Kd=1;
PID myPID(&Input, &Output, &Setpoint, Kp, Ki, Kd, DIRECT);


void setup() {
  Serial.begin(115200);
  while (!Serial)
    delay(10);
  Serial.println("Solder Reflow Oven");
  delay(100);
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(HX8357_BLACK);
  tft.setCursor(0,0);
  tft.setTextSize(1);


  if (!maxthermo.begin()) {
    Serial.println("Could not initialize thermocouple.");
    while (1) delay(10);
  }

  maxthermo.setThermocoupleType(MAX31856_TCTYPE_K);

  /*
  Serial.print("Thermocouple type: ");
  switch (maxthermo.getThermocoupleType() ) {
    case MAX31856_TCTYPE_B: Serial.println("B Type"); break;
    case MAX31856_TCTYPE_E: Serial.println("E Type"); break;
    case MAX31856_TCTYPE_J: Serial.println("J Type"); break;
    case MAX31856_TCTYPE_K: Serial.println("K Type"); break;
    case MAX31856_TCTYPE_N: Serial.println("N Type"); break;
    case MAX31856_TCTYPE_R: Serial.println("R Type"); break;
    case MAX31856_TCTYPE_S: Serial.println("S Type"); break;
    case MAX31856_TCTYPE_T: Serial.println("T Type"); break;
    case MAX31856_VMODE_G8: Serial.println("Voltage x8 Gain mode"); break;
    case MAX31856_VMODE_G32: Serial.println("Voltage x8 Gain mode"); break;
    default: Serial.println("Unknown"); break;
  }
  */

  maxthermo.setConversionMode(MAX31856_ONESHOT_NOWAIT);

  Setpoint = cooldownTemp;
  // tell the PID to range between 0 and the full window size
  myPID.SetOutputLimits(0, 1);

  // turn the PID on
  myPID.SetMode(AUTOMATIC);

  pinMode(SSR_PIN, OUTPUT);
  digitalWrite(SSR_PIN,LOW);


}

void loop() {
  digitalWrite(SSR_PIN,LOW);
  ///* Setup Menu *///
  tft.fillScreen(HX8357_BLACK);
  drawSetupMenu();
  setupMenu = true;
  Serial.println("Setup Menu");
  while(setupMenu){
    touchpoint = ts.getPoint();
    if(touchpoint.z > ts.pressureThreshhold){
      int setupMenuXPos = getGridCellX(), setupMenuYPos = getGridCellY();
      Serial.print("Setup menu touch: ("); Serial.print(setupMenuXPos); Serial.print(","); Serial.print(setupMenuYPos); Serial.print(") -> ");
      if(setupMenuYPos < 3){ // Somewhere other than the start button
        editMenu = true;
        bool editingPreheat = false, editingSoak = false, editingReflow = false;
        if(setupMenuXPos < 2 ){ // Somwhere within the preheat zone
          editingPreheat = true;
          tft.fillScreen(preheatColor);
          Serial.println("Edit Preheat Menu");
          drawEditMenu("Preheat");
          centerText(2,0,1,1,HX8357_WHITE,String(int(preheatTemp)));
          centerText(5,0,1,1,HX8357_WHITE, formatTime(preheatTime));
        }
        else if(setupMenuXPos > 3 ){// Somwhere within the reflow zone
          editingReflow = true;
          tft.fillScreen(reflowColor);
          Serial.println("Edit Reflow Menu");
          drawEditMenu("Reflow");
          centerText(2,0,1,1,HX8357_WHITE,String(int(reflowTemp)));
          centerText(5,0,1,1,HX8357_WHITE, formatTime(reflowTime));
        }
        else{ // Somwhere within the soak zone
          editingSoak = true;
          tft.fillScreen(soakColor);
          Serial.println("Edit Soak Menu");
          drawEditMenu("Soak");
          centerText(2,0,1,1,HX8357_WHITE,String(int(soakTemp)));
          centerText(5,0,1,1,HX8357_WHITE, formatTime(soakTime));
        }
        while(editMenu){// Stay in this loop until the save button is pressed
          touchpoint = ts.getPoint();
          if(touchpoint.z > ts.pressureThreshhold){
            int editMenuXPos = getGridCellX(), editMenuYPos = getGridCellY();
            Serial.print("Edit menu touch at ("); Serial.print(editMenuXPos); Serial.print(","); Serial.print(editMenuYPos); Serial.print(") -> ");
            if(editMenuYPos == 1){ // One of the up arrows was pressed
              if(editMenuXPos < 3){ // The Temp up arrow was pressed
                Serial.println("Temp up arrow");
                tft.fillRoundRect(2*gridSize+2, 0*gridSize+2, gridSize-4, gridSize-4, 10, HX8357_BLACK);
                if(editingPreheat){
                  if(preheatTemp < 300);
                    preheatTemp += 10;
                  centerText(2,0,1,1,HX8357_WHITE,String(int(preheatTemp)));
                }
                if(editingSoak){
                  if(soakTemp < 300);
                    soakTemp += 10;
                  centerText(2,0,1,1,HX8357_WHITE,String(int(soakTemp)));
                }
                if(editingReflow){
                  if(reflowTemp < 300);
                    reflowTemp += 10;
                  centerText(2,0,1,1,HX8357_WHITE,String(int(reflowTemp)));
                }
              }
              else{// The Time up arrow was pressed
                Serial.println("Time up arrow");
                tft.fillRoundRect(5*gridSize+2, 0*gridSize+2, gridSize-4, gridSize-4, 10, HX8357_BLACK);
                if(editingPreheat){
                  if(preheatTime < 300000)
                    preheatTime += 10000;
                  centerText(5,0,1,1,HX8357_WHITE, formatTime(preheatTime));
                }
                if(editingSoak){
                  if(soakTime < 300000)
                    soakTime += 10000;
                  centerText(5,0,1,1,HX8357_WHITE, formatTime(soakTime));
                }
                if(editingReflow){
                  if(reflowTime < 300000)
                    reflowTime += 10000;
                  centerText(5,0,1,1,HX8357_WHITE, formatTime(reflowTime));
                }
              }
            }
            else if(editMenuYPos == 2){// One of the down arrows was pressed
              if(editMenuXPos < 3){ // The Temp down arrow was pressed
                Serial.println("Temp down arrow");
                tft.fillRoundRect(2*gridSize+2, 0*gridSize+2, gridSize-4, gridSize-4, 10, HX8357_BLACK);
                if(editingPreheat){
                  if(preheatTemp > 100)
                    preheatTemp -= 10;
                  centerText(2,0,1,1,HX8357_WHITE,String(int(preheatTemp)));
                }
                if(editingSoak){
                  if(soakTemp > 100)
                    soakTemp -= 10;
                  centerText(2,0,1,1,HX8357_WHITE,String(int(soakTemp)));
                }
                if(editingReflow){
                  if(reflowTemp > 100)
                    reflowTemp -= 10;
                  centerText(2,0,1,1,HX8357_WHITE,String(int(reflowTemp)));
                }
              }
              else{// The Time down arrow was pressed
                Serial.println("Time down arrow");
                tft.fillRoundRect(5*gridSize+2, 0*gridSize+2, gridSize-4, gridSize-4, 10, HX8357_BLACK);
                if(editingPreheat){
                  if(preheatTime > 30000)
                    preheatTime -= 10000;
                  centerText(5,0,1,1,HX8357_WHITE, formatTime(preheatTime));
                }
                else if(editingSoak){
                  if(soakTime > 30000)
                    soakTime -= 10000;
                  centerText(5,0,1,1,HX8357_WHITE, formatTime(soakTime));
                }
                else if(editingReflow){
                  if(reflowTime > 30000)
                    reflowTime -= 10000;
                  centerText(5,0,1,1,HX8357_WHITE, formatTime(reflowTime));
                }
              }
            }
            else if(editMenuYPos == 3){ // Save button was pressed
              Serial.println("Save button");
              tft.fillScreen(HX8357_BLACK);
              drawSetupMenu();
              editMenu = false;
            }
            delay(touchHoldLimit); // so holding the button down doesn't read multiple presses
          }
        }
      }
      else{// Start button was pressed
        Serial.println("Start button");
        setupMenu = false;
      }
      delay(touchHoldLimit); // so holding the button down doesn't read multiple presses
    }
  }
  ///* Reflow Menu *///
  tft.fillScreen(HX8357_BLACK);
  drawReflowMenu();
  drawButton(0,3,2,1, HX8357_GREEN, HX8357_WHITE, "Start");
  bool start = false;
  while(!start){
    touchpoint = ts.getPoint();
    if(touchpoint.z > ts.pressureThreshhold){
      if(getGridCellX() <2 && getGridCellY() == 3){
        start = true;
      }
      delay(touchHoldLimit); // so holding the button down doesn't read multiple presses
    }
  }
  drawButton(0,3,2,1, HX8357_RED, HX8357_WHITE, "Stop");
  Serial.println("Reflow Menu");
  unsigned long reflowStarted = millis();
  reflowMenu = true;
  while(reflowMenu){
    timeSinceReflowStarted = millis() - reflowStarted;
    if(timeSinceReflowStarted - lastTimeTempCheck > timeTempCheck){
      lastTimeTempCheck = timeSinceReflowStarted;
      printState();
      // check for conversion complete and read temperature
      if (maxthermo.conversionComplete()) {
        Serial.print("\tSetpoint:"); Serial.print(Setpoint);
        Input = maxthermo.readThermocoupleTemperature();
        Serial.print("\tInput:"); Serial.print(Input);
        myPID.Compute();
        if(Output < 0.5){
          digitalWrite(SSR_PIN,LOW);
        }
        if(Output > 0.5){
          digitalWrite(SSR_PIN,HIGH);
        }
        Serial.print("\tOutput:"); Serial.println(Output);
        plotDataPoint();
      }
      else {
        Serial.println("\tConversion not complete!");
      }
      // trigger a conversion, returns immediately
      maxthermo.triggerOneShot();
    }
    if(timeSinceReflowStarted > totalTime){
      reflowMenu = false;
    }
    else if(timeSinceReflowStarted > (preheatTime + soakTime + reflowTime)){ // preheat and soak and reflow are complete. Start cooldown
      if(!coolingDown){
        newState = true;
        preheating = false, soaking = false, reflowing = false, coolingDown = true;
      }
      Setpoint = cooldownTemp;
    }
    else if(timeSinceReflowStarted > (preheatTime + soakTime)){ // preheat and soak are complete. Start reflow
      if(!reflowing){
        newState = true;
        preheating = false, soaking = false, reflowing = true, coolingDown = false;
      }
      Setpoint = reflowTemp;
    }
    else if(timeSinceReflowStarted > preheatTime){ // preheat is complete. Start soak
      if(!soaking){
        newState = true;
        preheating = false, soaking = true, reflowing = false, coolingDown = false;
      }
      Setpoint = soakTemp;
    }
    else{ // cycle is starting. Start preheat
      if(!preheating){
        newState = true;
        preheating = true, soaking = false, reflowing = false, coolingDown = false;
      }
      Setpoint = preheatTemp;
    }
    touchpoint = ts.getPoint();
    if(touchpoint.z > ts.pressureThreshhold){
      if(getGridCellX() < 2 && getGridCellY() == 3){
        reflowMenu = false;
      }
      delay(touchHoldLimit); // so holding the button down doesn't read multiple presses
    }
  }
  drawButton(0,3,2,1, HX8357_GREEN, HX8357_WHITE, "Done");
  bool done = false;
  while(!done){
    touchpoint = ts.getPoint();
    if(touchpoint.z > ts.pressureThreshhold){
      if(getGridCellX() < 2 && getGridCellY() == 3){
        done = true;
      }
      delay(touchHoldLimit); // so holding the button down doesn't read multiple presses
    }
  }
}

void printState(){
  String time = formatTime(timeSinceReflowStarted);
  Serial.print("Current time: "); Serial.print(time); Serial.print("\t");
  tft.fillRoundRect(5*gridSize+2, 3*gridSize+2, gridSize-4, gridSize-4, 10, HX8357_BLACK);
  centerText(5,3,1,1,0,HX8357_WHITE,time);
  centerText(5,3,1,1,2,HX8357_WHITE,String(Input));
  String currentState;
  if(preheating){
    currentState = "Preheating";
  }
  if(soaking){
    currentState = "Soaking";
  }
  if(reflowing){
    currentState = "Reflowing";
  }
  if(coolingDown){
    currentState = "Cool Down";
  }
  Serial.print(currentState);
  if(newState){
    newState = false;
    tft.fillRoundRect(2*gridSize+2, 3*gridSize+2, 2*gridSize-4, gridSize-4, 10, HX8357_BLACK);
    centerText(2,3,2,1,HX8357_WHITE,currentState);
  }
}

void drawGrid(){
  tft.setFont();
  tft.setTextColor(HX8357_WHITE);
  tft.drawRect(0,0,displayWidth,displayHeight-gridSize,gridColor);
  for(int i=1; i<6; i++){
    tft.drawFastVLine(i*gridSize,0,displayHeight-gridSize,gridColor);
  }
  for(int j=1; j<4; j++){
    tft.drawFastHLine(0,j*gridSize,displayWidth,gridColor);
  }
  tft.setCursor(4,4); tft.print("300");
  tft.setCursor(4,1*gridSize+4); tft.print("200");
  tft.setCursor(4,2*gridSize+4); tft.print("100");

  tft.setCursor(1*gridSize+4,3*gridSize-7-4); tft.print(formatTime(totalTime/6));
  tft.setCursor(2*gridSize+4,3*gridSize-7-4); tft.print(formatTime(2*totalTime/6));
  tft.setCursor(3*gridSize+4,3*gridSize-7-4); tft.print(formatTime(3*totalTime/6));
  tft.setCursor(4*gridSize+4,3*gridSize-7-4); tft.print(formatTime(4*totalTime/6));
  tft.setCursor(5*gridSize+4,3*gridSize-7-4); tft.print(formatTime(5*totalTime/6));

  plotReflowProfile();
}

void drawButton(int x, int y, int w, int h, uint16_t backgroundColor, uint16_t textColor, String text){
  tft.setFont(&FreeMonoBold12pt7b);
  if(backgroundColor == HX8357_BLACK){
    tft.drawRoundRect(x*gridSize+2, y*gridSize+2, w*gridSize-4, h*gridSize-4, 10, HX8357_WHITE);
  }
  else{
    tft.fillRoundRect(x*gridSize+2, y*gridSize+2, w*gridSize-4, h*gridSize-4, 10, backgroundColor);
  }
  if(text == "UP_ARROW"){
    tft.fillTriangle(x*gridSize+(w*gridSize-60)/2, y*gridSize+(h*gridSize-52)/2+52, x*gridSize+(w*gridSize-60)/2+60, y*gridSize+(h*gridSize-52)/2+52, x*gridSize+w*gridSize/2, y*gridSize+(h*gridSize-52)/2, textColor);
  }
  else if(text == "DOWN_ARROW"){
    tft.fillTriangle(x*gridSize+(w*gridSize-60)/2, y*gridSize+(h*gridSize-52)/2, x*gridSize+(w*gridSize-60)/2+60, y*gridSize+(h*gridSize-52)/2, x*gridSize+w*gridSize/2, y*gridSize+(h*gridSize-52)/2+52, textColor);
  }
  else{
    int16_t textBoundX, textBoundY;
    uint16_t textBoundWidth, textBoundHeight;
    tft.getTextBounds(text,0,0,&textBoundX, &textBoundY, &textBoundWidth, &textBoundHeight);
    tft.setCursor(x*gridSize+(w*gridSize-textBoundWidth)/2, y*gridSize+(h*gridSize+textBoundHeight)/2); tft.setTextColor(textColor); tft.print(text);
  }
}

void centerText(int x, int y, int w, int h, uint16_t textColor, String text){
  tft.setFont(&FreeMonoBold12pt7b);
  int16_t textBoundX, textBoundY;
  uint16_t textBoundWidth, textBoundHeight;
  tft.getTextBounds(text,0,0,&textBoundX, &textBoundY, &textBoundWidth, &textBoundHeight);
  tft.setCursor(x*gridSize+(w*gridSize-textBoundWidth)/2, y*gridSize+(h*gridSize+textBoundHeight)/2);
  tft.setTextColor(textColor); tft.print(text);
}

void centerText(int x, int y, int w, int h, int justification, uint16_t textColor, String text){
  tft.setFont(&FreeMonoBold12pt7b);
  int16_t textBoundX, textBoundY;
  uint16_t textBoundWidth, textBoundHeight;
  tft.getTextBounds(text,0,0,&textBoundX, &textBoundY, &textBoundWidth, &textBoundHeight);
  switch(justification){
    case 0: //top justified
      tft.setCursor(x*gridSize+(w*gridSize-textBoundWidth)/2, y*gridSize+(h*gridSize/2-textBoundHeight)/2+textBoundHeight);
      break;
    case 1: //center justified
      tft.setCursor(x*gridSize+(w*gridSize-textBoundWidth)/2, y*gridSize+(h*gridSize+textBoundHeight)/2);
      break;
    case 2: //bottom justified
      tft.setCursor(x*gridSize+(w*gridSize-textBoundWidth)/2, y*gridSize+gridSize-(h*gridSize/2-textBoundHeight)/2);
      break;
  }
  tft.setTextColor(textColor); tft.print(text);
}

void drawSetupMenu(){
  tft.setFont(&FreeMonoBold12pt7b);
  drawButton(0,0,2,3, preheatColor, HX8357_WHITE, "");                  drawButton(2,0,2,3, soakColor, HX8357_WHITE, "");                 drawButton(4,0,2,3, reflowColor, HX8357_WHITE, "");
  centerText(0,0,2,1, HX8357_WHITE, "Preheat");                         centerText(2,0,2,1, HX8357_WHITE, "Soak");                        centerText(4,0,2,1, HX8357_WHITE, "Reflow");
  centerText(0,1,2,1,0, HX8357_WHITE, String(int(preheatTemp)) + " C");        centerText(2,1,2,1,0, HX8357_WHITE, String(int(soakTemp)) + " C");       centerText(4,1,2,1,0, HX8357_WHITE, String(int(reflowTemp)) + " C");
  centerText(0,1,2,1,2, HX8357_WHITE, String(formatTime(preheatTime)) + " min."); centerText(2,1,2,1,2, HX8357_WHITE, String(formatTime(soakTime)) + " min.");centerText(4,1,2,1,2, HX8357_WHITE, String(formatTime(reflowTime)) + " min.");
  drawButton(0,3,6,1, HX8357_GREEN, HX8357_WHITE, "Confirm");
  tft.drawCircle(90,95,3,HX8357_WHITE); tft.drawCircle(250,95,3,HX8357_WHITE); tft.drawCircle(410,95,3,HX8357_WHITE);
}

void drawReflowMenu(){
  tft.setFont(&FreeMonoBold12pt7b);
  drawGrid();
  centerText(4,3,1,1,0, HX8357_WHITE, "Time: ");
  centerText(4,3,1,1,2, HX8357_WHITE, "Temp: ");
  //drawButton(0,3,2,1, HX8357_RED, HX8357_WHITE, "Stop"); drawButton(0,3,2,1, HX8357_RED, HX8357_WHITE, "Start");
}

void drawEditMenu(String stage){
  tft.setFont(&FreeMonoBold12pt7b);
  centerText(0,0,2,1,0, HX8357_WHITE, stage); centerText(0,0,2,1, HX8357_WHITE, " Temp: "); drawButton(0,1,3,1, HX8357_WHITE, HX8357_BLACK, "UP_ARROW"); drawButton(0,2,3,1, HX8357_WHITE, HX8357_BLACK, "DOWN_ARROW");
  centerText(3,0,2,1,0, HX8357_WHITE, stage); centerText(3,0,2,1, HX8357_WHITE, " Time: "); drawButton(3,1,3,1, HX8357_WHITE, HX8357_BLACK, "UP_ARROW"); drawButton(3,2,3,1, HX8357_WHITE, HX8357_BLACK, "DOWN_ARROW");
  //centerText(0,1,2,1,0, HX8357_WHITE, String(int(preheatTemp)));        //centerText(2,1,2,1,0, HX8357_WHITE, String(int(soakTemp)));       centerText(4,1,2,1,0, HX8357_WHITE, String(int(reflowTemp)));
  //centerText(0,1,2,1,2, HX8357_WHITE, String(formatTime(preheatTime))); //centerText(2,1,2,1,2, HX8357_WHITE, String(formatTime(soakTime)));centerText(4,1,2,1,2, HX8357_WHITE, String(formatTime(reflowTime)));
  //drawButton(0,0,2,2, HX8357_BLACK, HX8357_WHITE, "");                  drawButton(2,0,2,2, HX8357_BLACK, HX8357_WHITE, "");              drawButton(4,0,2,2, HX8357_BLACK, HX8357_WHITE, "");

  //drawButton(0,2,1,1, HX8357_WHITE, HX8357_BLACK, "UP");drawButton(1,2,1,1, HX8357_WHITE, HX8357_BLACK, "DOWN");
  //drawButton(2,2,1,1, HX8357_WHITE, HX8357_BLACK, "UP");drawButton(3,2,1,1, HX8357_WHITE, HX8357_BLACK, "DOWN");
  //drawButton(4,2,1,1, HX8357_WHITE, HX8357_BLACK, "UP");drawButton(5,2,1,1, HX8357_WHITE, HX8357_BLACK, "DOWN");
  tft.drawCircle(90,95,3,HX8357_WHITE); tft.drawCircle(250,95,3,HX8357_WHITE); tft.drawCircle(410,95,3,HX8357_WHITE);
  drawButton(0,3,6,1, HX8357_GREEN, HX8357_WHITE, "Save");
}

int getGridCellX(){
  int xpoint = touchpoint.x;
  Serial.print("x resistance: ");Serial.print(xpoint); Serial.print(" ");
  //xpoint = map(xpoint,TS_MINX,TS_MAXX,displayWidth-1,0);
  if(xpoint > 824)
    return 0;
  else if(xpoint > 689)
    return 1;
  else if(xpoint > 546)
    return 2;
  else if(xpoint > 399)
    return 3;
  else if(xpoint > 259)
    return 4;
  else
    return 5;
}

int getGridCellY(){
  int ypoint = touchpoint.y;
  Serial.print("y resistance: ");Serial.print(ypoint); Serial.print(" ");
  //ypoint = map(ypoint,TS_MINY,TS_MAXY,0,displayHeight-1);
  if(ypoint > 800)
    return 3;
  else if(ypoint > 690)
    return 2;
  else if(ypoint > 500)
    return 1;
  else
    return 0;
}

String formatTime(unsigned long milliseconds) {
  // Calculate the number of minutes and seconds
  unsigned long totalSeconds = milliseconds / 1000;
  unsigned int minutes = totalSeconds / 60;
  unsigned int seconds = totalSeconds % 60;

  // Format the time as a string with a leading zero if necessary
  String formattedTime = (minutes < 10 ? "0" : "") + String(minutes) + ":" + (seconds < 10 ? "0" : "") + String(seconds);

  return formattedTime;
}

/*int  mapTime(int time){
  return map(time,0,totalTime,0,displayWidth);
}*/

/*int mapTemp(int temp){
  return map(temp,0,300,3*gridSize,0);
}*/

void plotDataPoint(){
  uint16_t color;
  if(preheating){
    color = preheatColor;
  }
  if(soaking){
    color = soakColor;
  }
  if(reflowing){
    color = reflowColor;
  }
  if(coolingDown){
    color = cooldownColor;
  }
  tft.fillCircle(map(timeSinceReflowStarted,0,totalTime,0,displayWidth),map(Input,0,300,3*gridSize,0),2, color);
  //tft.fillCircle(mapTime(timeSinceReflowStarted), mapTemp(Input), 2, color);
}

void plotReflowProfile(){
   int xMin, xMax, amp;
   xMin = 0;
   xMax = map(preheatTime,0,totalTime,0,displayWidth);
   amp = map(preheatTemp,0,300,3*gridSize,0) - map(cooldownTemp,0,300,3*gridSize,0);
  for(int i = 0; i <= (xMax-xMin); i++){
    tft.fillCircle(xMin+i,-amp/2*cos(PI*i/(xMax-xMin))+map(cooldownTemp,0,300,3*gridSize,0)+amp/2,2,preheatColor_d);
  }

  xMin = map(preheatTime,0,totalTime,0,displayWidth);
  xMax = map(preheatTime+soakTime,0,totalTime,0,displayWidth);
  amp = map(soakTemp,0,300,3*gridSize,0) - map(preheatTemp,0,300,3*gridSize,0);
  //amp = 80;
  for(int i = 0; i <= (xMax-xMin); i++){
    tft.fillCircle(xMin+i,-amp/2*cos(PI*i/(xMax-xMin))+map(preheatTemp,0,300,3*gridSize,0)+amp/2,2, soakColor_d);
  }

  xMin = map(preheatTime+soakTime,0,totalTime,0,displayWidth);
  xMax = map(preheatTime+soakTime+reflowTime,0,totalTime,0,displayWidth);
  amp = map(reflowTemp,0,300,3*gridSize,0) - map(soakTemp,0,300,3*gridSize,0);
  //amp = 80;
  for(int i = 0; i <= (xMax-xMin); i++){
    tft.fillCircle(xMin+i,-amp/2*cos(PI*i/(xMax-xMin))+map(soakTemp,0,300,3*gridSize,0)+amp/2,2,reflowColor_d);
  }

  xMin = map(preheatTime+soakTime+reflowTime,0,totalTime,0,displayWidth);
  xMax = map(totalTime,0,totalTime,0,displayWidth);
  amp = map(cooldownTemp,0,300,3*gridSize,0) - map(reflowTemp,0,300,3*gridSize,0);
  //amp = 80;
  for(int i = 0; i <= (xMax-xMin); i++){
    tft.fillCircle(xMin+i,-amp/2*cos(PI*i/(xMax-xMin))+map(reflowTemp,0,300,3*gridSize,0)+amp/2,2, cooldownColor_d);
  }
}