//*******************************************************************************************
//  ----- PROJECT: POWER METER 2.1
//*******************************************************************************************

#define btnPin 2

unsigned long timeFactor;
unsigned long energyTimer;
unsigned long writeToSDTimer;
unsigned long standbyTimer;
byte currentDay = 0;
byte currentMonth = 0;
byte currentYear = 0;
byte savedDay = 0;
byte savedMonth = 0;
byte savedYear = 0;
long totalDaily;
long totalMonthly;
long totalYearly;
int pageN = 0;
bool errToggle = false;
bool errRE = false;
bool btnPress = false;
bool btnRE = false;
bool energyRE = false;
bool standBy = false;

// ----- LCD
// LiquidCrystal I2C - Version: Latest
#include <LiquidCrystal_I2C.h>

#define lcdChars 16
#define lcdRows 2

LiquidCrystal_I2C lcd(0x3F, lcdChars, lcdRows);
char lcdText[18];

// ----- RTC
// Rtc by Makuna - Version: 2.0.2
#include <RtcDS1307.h>

RtcDS1307<TwoWire> Rtc(Wire);
RtcDateTime now;// = Rtc.GetDateTime();
char rtcDate  [11];
char rtcDateS [6];
char rtcTime  [9];

// ----- ENERGY MONITOR
// EmonLib - Version: Latest 
#include <EmonLib.h>
//Insert the voltage of the measured source
#define rede 230.0 // Italy 230.0 (V) in some countries 110.0 (V)
//Pin of the sensor SCT on A0 / A1 / A2
#define pinSct1 0
#define pinSct2 1
#define pinSct3 2
// Calibration factor
#define calibFactor1 282
#define calibFactor2 305
#define calibFactor3 308
#define currentFactor 2000

EnergyMonitor emon1;
EnergyMonitor emon2;
EnergyMonitor emon3;

int avgCount = 0;
unsigned long Wrms = 0;
unsigned long Wh = 0;
unsigned long MWh = 0;
int Irms = 0;
int Irms1 = 0;
int Irms2 = 0;
int Irms3 = 0;

// ----- SD CARD
#include "SdFat.h"

#define chipSelect 4

SdFat SD;
File myFile;
char dataFrom [25];


//*******************************************************************************************
//  ----- DECLARATIONS
//*******************************************************************************************

void formatDate(bool withYear = 0, bool longYear = 0);
void formatTime(bool withSeconds = 1, bool amPm = 0);
void writeToLcd(int posX, int row = -1, const char* text = NULL, bool fill = false, int addWhiteN = 0);

//*******************************************************************************************
//  ----- SETUP
//*******************************************************************************************

void setup() {
  // STARTING PAGE
  lcd.init();
  lcd.backlight();
  writeToLcd(0, 0, " ** OKRIM-PT **");
  writeToLcd(0, 1, " ENERGY MONITOR ");
  
  // Open serial communications and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  //__!! Serial.println(F("Serial started"));

  RTC_Init();
  
  writeToLcd(15, 0, "X");
  delay(1000);
  while(!readFromSD()){}
  writeToLcd(15, 0, "O");
  
  attachInterrupt(digitalPinToInterrupt(btnPin), buttonInterrupt, RISING);
  delay(1800);
  writeToLcd(-1);
  
  energyTimer = millis();
  writeToSDTimer = millis();
  standbyTimer = millis();
}


//*******************************************************************************************
//  ----- LOOP
//*******************************************************************************************

void loop() {
  
  // LCD Update
  if(!standBy && !errToggle){
    LCDUpdate();
    RTC_Correction(0);
  }
  
  if(millis() - writeToSDTimer >= 10000){
    writeToSDTimer = millis();
    writeToLcd(15, 0, "X");
    writeToSD();
    writeToLcd(15, 0, "O");
  }
  
  if(millis() - standbyTimer >= 2000 && errToggle){
    standBy = false;
    standbyTimer = millis();
    lcd.noBacklight();
    delay(500);
    lcd.backlight();
  }
  
  if(millis() - standbyTimer >= 300000 && !errToggle){
    standBy = true;
    pageN = 0;
    lcd.noBacklight();
  }

  if(millis() - standbyTimer >= 1000){
    btnRE = false;
  }

  if (btnPress && !btnRE) {
    btnRE = true;
    btnPress = false;
    lcd.backlight();
    
    if(!standBy){
      pageN++;
      if(pageN >= 10){
        pageN = 0;
      }
    }
    Serial.println(pageN);
    
    standBy = false;
    standbyTimer = millis();
  }
  
  if(millis() - energyTimer >= 500){
    energyTimer = millis();
    energMonitor();
    
    // Calculate the actual delivered energy based on the actual power and elapsed time
    avgCount++;
    Wrms = Irms * rede;
    Wh += Wrms / (3600000 / (millis() - timeFactor) );
    timeFactor = millis();
    
    if(Wh >= 1000000){
      MWh++;
      Wh -= 1000000;
    }
  }
}



//*******************************************************************************************
//  ----- FUNCTIONS
//*******************************************************************************************


// ----- RTC

void RTC_Init(){
  Rtc.Begin();

  /*
  
  // UNCOMMENT THIS BLOCK TO INITIALIZE RTC TIME
  
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  Rtc.SetDateTime(compiled);
  if (!Rtc.IsDateTimeValid() || updateTime) {
      // following line sets the RTC to the date & time this sketch was compiled it will also reset the valid flag internally unless the Rtc device is having an issue
      
  }
  
  now = Rtc.GetDateTime();
  if (now < compiled) {
      //__!! Serial.println(F("RTC is older than compile time!  (Updating DateTime)"));
      Rtc.SetDateTime(compiled);
  }
  
  // never assume the Rtc was last configured by you, so just clear them to your needed state
  Rtc.SetSquareWavePin(DS1307SquareWaveOut_Low);
  */
  
  if (!Rtc.GetIsRunning()) {
      Rtc.SetIsRunning(true);
  }
}

void updateCurrTime(){
  now = Rtc.GetDateTime();
  currentDay = now.Day();
  currentMonth = now.Month();
  currentYear = now.Year();
}

void RTC_Correction(int drift){
  if ( now.Hour() == 0 && now.Minute() == 0 && now.Second() == 0 && drift != 0) {
    // Save actual time 
    RtcDateTime newValue = now;
    
    // Daily drift correction set a value bigger  than 10 if the clock is running slow 
    // Daily drift correction set a value smaller than 10 if the clock is running fast 
    newValue += drift;
    
    // Pause for drift + 1 seconds to accomodate the drift correction
    if(drift < 0) {
      drift *= -1;
      delay( (drift + 1) * 1000 );
    }
    
    Rtc.SetDateTime(newValue);
  }
}

void energMonitor(){
  if(!energyRE){
    emon1.current(pinSct1, calibFactor1);
    emon2.current(pinSct2, calibFactor2);
    emon3.current(pinSct3, calibFactor3);
    energyRE = true;
  }
  
  Irms1 = emon1.calcIrms(currentFactor);
  Irms2 = emon2.calcIrms(currentFactor);
  Irms3 = emon3.calcIrms(currentFactor);
  Irms = ( (Irms1 > 0.5)?Irms1:0 ) + ( (Irms2 > 0.5)?Irms2:0 ) + ( (Irms3 > 0.5)?Irms3:0 ); // Filter out the noise (if amperage is less then 0.5 amperes)
}


// ----- LCD

void LCDUpdate(){
  updateCurrTime();
  //formatDate();
  //formatTime();
  //writeToLcd(0, 0, (char*)rtcDateS);
  //writeToLcd(6, 0, (char*)rtcTime);
  
  if(currentDay < 10){
    writeToLcd(0, 0, "0" );
    writeToLcd(1, 0, itoa(currentDay, lcdText, 10) );
  } else {
    writeToLcd(0, 0, itoa(currentDay, lcdText, 10) );
  }
  writeToLcd(2, 0, "/");
  if(currentMonth < 10){
    writeToLcd(3, 0, "0" );
    writeToLcd(4, 0, itoa(currentMonth, lcdText, 10) );
  } else {
    writeToLcd(3, 0, itoa(currentMonth, lcdText, 10) );
  }
  
  byte currentH = now.Hour();
  byte currentMin = now.Minute();
  byte currentSec = now.Second();
  
  if(currentH < 10){
    writeToLcd(6, 0, "0" );
    writeToLcd(7, 0, itoa(currentH, lcdText, 10) );
  } else {
    writeToLcd(6, 0, itoa(currentH, lcdText, 10) );
  }
  writeToLcd(8, 0, ":");
  if(currentMin < 10){
    writeToLcd(9, 0, "0" );
    writeToLcd(10, 0, itoa(currentMin, lcdText, 10) );
  } else {
    writeToLcd(9, 0, itoa(currentMin, lcdText, 10) );
  }
  writeToLcd(11, 0, ":");
  if(currentSec < 10){
    writeToLcd(12, 0, "0" );
    writeToLcd(13, 0, itoa(currentSec, lcdText, 10) );
  } else {
    writeToLcd(12, 0, itoa(currentSec, lcdText, 10) );
  }
  
  int kWrms = Wrms / 1000;
  int kWrms2 = (Wrms % 1000) / 100;
  if(kWrms < 10){
    writeToLcd(1, 1, itoa(kWrms, lcdText, 10));
  } else {
    writeToLcd(0, 1, itoa(kWrms, lcdText, 10));
  }
  writeToLcd(2, 1, ".");
  writeToLcd(3, 1, itoa(kWrms2, lcdText, 10));
  writeToLcd(4, 1, "kW");
  
  unsigned long enVal = (Wh > 1000)? Wh / 1000 : Wh;
  int space = 0;
  ultoa(enVal ,  lcdText, 10);
  if(enVal < 10){
    space = 5;
  } else if(enVal < 100) {
    space = 4;
  } else if(enVal < 1000) {
    space = 3;
  } else if(enVal < 10000) {
    space = 2;
  } else if(enVal < 100000) {
    space = 1;
  }
  writeToLcd(7, 1, lcdText, false, space);
  writeToLcd(13, 1, (Wh > 1000)? "kWh" : "Wh", false, (Wh > 1000)? 0 : 1);
}

void writeToLcd(int posX, int row, const char* text, bool fill, int addWhiteN = 0){
  if( errToggle != errRE){
    lcd.clear();
    errRE = errToggle;
  }
  
  
  if(posX == -1 && row == -1){
    lcd.clear();
    return;
  }
  
  if(posX >= 0 && posX < lcdChars && row >= 0 && row < lcdRows){
    lcd.setCursor(posX,row);
  }
  
  int i = 0, j, k;
  const char* p;
  // Iterate over each character of `text`,
  // until we reach the NUL terminator
  for (p = text; *p != '\0' && i < lcdChars - posX; p++) {
    lcdText[i] = *p;
    i++;
  }
  
  for(j = 0; j < addWhiteN && i < lcdChars - posX ; j++){
    k = i;
    while(k >= 0){
      lcdText[k + 1] = lcdText[k];
      k--;
    }
    lcdText[0] = ' ';
    i++;
  }
  
  while(fill && (i - 1) < lcdChars - posX){
    lcdText[i] = ' ';
    i++;
  }

  lcdText[i] = '\0';
  // print array to LCD
  lcd.print(lcdText);
}


// ----- SD CARD

bool readFromSD(){
  int appCD;
  int appCM;
  int appCY;
  long appTD;
  long appTm;
  long appTM;
  long appValue;
  int tmpErr = 0;

  updateCurrTime();
  
  if( SD.begin(chipSelect, SD_SCK_MHZ(50)) ){
    //__!! Serial.print(F("SD OK, opening file to read..."));
    myFile = SD.open("CurData.txt");
    
    // if the file opened okay, write to it:
    if (myFile) {
      int charNum = 0;
      char tmpChar = NULL;
      
      //__!! Serial.print(F("FILE OK, reading from it..."));
      errToggle = 0;
      
      while ( myFile.available() ) {    // Checks that a charachter is available from file stream
        tmpChar =  myFile.read();       // Reads single character from file
        
        if(tmpChar >= 32  && charNum < 25) {    // Checks that current character is valid and current word doesn't exceed 25 characters
          dataFrom [charNum] = tmpChar;
          charNum++;
          
        } else {    // If the character is not valid, terminate current character array
          dataFrom [charNum] = 0;
          
          tmpErr = elaborateDataFrom(&appCD, &appCM, &appCY, &appTD, &appTm, &appTM);
          if(tmpErr > 0 && tmpErr < 9){
            //__!! Serial.println(F("Invalid  !!!!!"));
            errToggle = 1;
            writeToLcd(0, 1, "Ins right SD! e", true);
            writeToLcd(15, 1, itoa(tmpErr, lcdText, 10), true);
            delay(5000);
            return false;
          }
          //__!! Serial.println(F("NEXT WORD..."));
          charNum = 0;
        }
      }
      
      tmpErr = elaborateDataFrom(&appCD, &appCM, &appCY, &appTD, &appTm, &appTM);
      
      // close the file:
      myFile.flush();
      myFile.close();
      
    } else {
      //__!! Serial.println(F("Error opening the file on the SD!!!"));
      errToggle = 1;
      writeToLcd(0, 1, "Err with file! 1", true);
      delay(5000);
      return false;
    }  
  } else {
    //__!!Serial.println(F("Error with the SD!!!"));
    errToggle = 1;
    writeToLcd(0, 1, "Error with SD!!!", true);
    delay(5000);
    return false;
  }
  
  if(tmpErr > 0 && tmpErr < 9 || appCD == 0 || appCM == 0 || appCY == 0 || (appTD == 0 && appTm == 0 && appTM == 0) ){
    //__!! Serial.println(F("Invalid  !!!!!"));
    errToggle = 1;
    writeToLcd(0, 1, "Ins right SD! e", true);
    writeToLcd(15, 1, itoa(tmpErr, lcdText, 10), true);
    delay(5000);
    return false;
  }
  
  /*//__!!
  Serial.print(F("Curr DAY: "));
  Serial.println(appCD);
  Serial.print(F("Curr MONTH: "));
  Serial.println(appCM);
  Serial.print(F("Curr YEAR: "));
  Serial.println(appCY);
  Serial.print(F("Daily consumption: "));
  Serial.println(appTD);
  Serial.print(F("Monthly consumption: "));
  Serial.println(appTm);
  Serial.print(F("MONTHLY consumption: "));
  Serial.print(appTM);
  /**/
  return true;
}

int elaborateDataFrom(int *a, int *b, int *c, long *d, long *e, long *f){
  long check = 0;
  int errN = 0;
  
  if(dataFrom[0] == '*'  && dataFrom[1] == 'C'  && dataFrom[2] == 'D'  && dataFrom[3] == '*'){
    ClearIndexDataFrom();
    check = atoi(dataFrom);
    
    if(check >= 1 && check <= 31){
      *a = check;
    } else {
      errN = 1;
    }
    //__!! printDataFrom();
    //__!!Serial.println(F(" - 1"));
    
  } else if(dataFrom[0] == '*'  && dataFrom[1] == 'C'  && dataFrom[2] == 'M'  && dataFrom[3] == '*'){
    ClearIndexDataFrom();
    check = atoi(dataFrom);
    
    if(check >= 1 && check <= 12){
      *b = check;
    } else {
      errN = 2;
    }
    //__!! printDataFrom();
    //__!!Serial.println(F(" - 2"));
    
  } else if(dataFrom[0] == '*'  && dataFrom[1] == 'C'  && dataFrom[2] == 'Y'  && dataFrom[3] == '*'){
    ClearIndexDataFrom();
    check = atoi(dataFrom);
    
    if(check >= 2000 && check <= 2500){
      *c = check;
    } else {
      errN = 3;
    }
    //__!! printDataFrom();
    //__!!Serial.println(F(" - 3"));
    
  } else if(dataFrom[0] == '*'  && dataFrom[1] == 'D'  && dataFrom[2] == 'c'  && dataFrom[3] == '*'){
    ClearIndexDataFrom();
    check = atol(dataFrom);
    
    if(check >= 0 && check <= 1000000){
      *d = check;
    } else {
      errN = 4;
    }
    //__!! printDataFrom();
    //__!!Serial.println(F(" - 4"));
    
  } else if(dataFrom[0] == '*'  && dataFrom[1] == 'M'  && dataFrom[2] == 'c'  && dataFrom[3] == '*'){
    ClearIndexDataFrom();
    check = atol(dataFrom);
    
    if(check >= 0 && check <= 1000000){
      *e = check;
    } else {
      errN = 5;
    }
    //__!! printDataFrom();
    //__!!Serial.println(F(" - 5"));
    
  } else if(dataFrom[0] == '*'  && dataFrom[1] == 'M'  && dataFrom[2] == 'C'  && dataFrom[3] == '*'){
    ClearIndexDataFrom();
    check = atol(dataFrom);
    
    if(check >= 0 && check <= 1000000){
      *f = check;
    } else {
      errN = 6;
    }
    //__!! printDataFrom();
    //__!!Serial.println(F(" - 6"));
    
  } else {
    errN = 9;
    //__!! printDataFrom();
    //__!!Serial.println(F(" - OTHER"));
  }
  
  return errN;
}

void ClearIndexDataFrom(){
  int i = 0;
  int j;
  
  while(dataFrom[i] > 32){
    if(dataFrom[i] < '0' || dataFrom[i] > '9'){
      j = 0;
      
      do {
        dataFrom[j] = dataFrom[j+1];
        j++;
        
      } while(dataFrom[j] > 32);
      
      dataFrom[j] = 0;
    } else {
      i++;
    }
  }
}

void printDataFrom(){
  int i = 0;
  
  while(dataFrom[i] > 32){
    Serial.print(dataFrom[i]);
    i++;
  }
}


void writeToSD(){
  updateCurrTime();
  
  if(SD.begin(chipSelect)){
    //__!! Serial.print(F("SD OK, opening file..."));
    myFile = SD.open("TestSd.CSV", FILE_WRITE);
    
    // if the file opened okay, write to it:
    if (myFile) {
      //__!! Serial.print(F("FILE OK, writing to it..."));
      errToggle = 0;
      
      myFile.print(millis());
      myFile.print(";"); 
      
      myFile.println("");
      
      // close the file:
      myFile.flush();
      myFile.close();
      
    } else {
      //__!! Serial.println(F("Error opening the file on the SD!"));
      errToggle = 1;
      writeToLcd(0, 1, "Error with file", true);
    }  
  } else {
    //__!! Serial.println(F("Error with the SD!"));
    errToggle = 1;
    writeToLcd(0, 1, "Error with SD!", true);
  }
}

//*******************************************************************************************
//  ----- INTERRUPTS
//*******************************************************************************************

void buttonInterrupt(){

  btnPress = true;

}

//*******************************************************************************************
//  ----- END!
//*******************************************************************************************