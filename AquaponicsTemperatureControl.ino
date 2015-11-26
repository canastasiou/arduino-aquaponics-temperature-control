#include <Wire.h>
#include "RTClib.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <dht11.h>
//#include <SD.h>

/*!! ANTREA DE TA TOUTA !!*/
//Intervals in seconds
#define CIRCULATION_PUMP_ON_INTERVAL 1000
#define CIRCULATION_PUMP_OFF_INTERVAL 1000
#define WATERING_PUMP_INTERVAL 1000
#define FAN_INTERVAL 1000

#define FAN_TEMPERATURE_THRESHOLD 20
#define CIRCULATION_PUMP_TEMPERATURE_THRESHOLD 20

#define ON 1
#define OFF 0

//Data wire for the water temperature sensor
#define WATER_TEMP1_PIN A4
#define WATER_TEMP2_PIN A5
//Pins for the air sensors
#define INDOOR_AIR_SENSOR_PIN1 7
#define INDOOR_AIR_SENSOR_PIN2 6
#define INDOOR_AIR_SENSOR_PIN3 5
//Pins for float sensors
#define LOW_FLOAT_PIN 8
#define HIGH_FLOAT_PIN 9
//Pins for water pumps
#define CIRCULATION_PUMP_PIN 4
#define WATERING_PUMP_PIN 5
//Pins for override switches
#define CIRCULATION_PUMP_SWITCH_PIN 10
#define WATERING_PUMP_SWITCH_PIN 10
#define FAN_SWITCH_PIN 10
//Defines the number of measurements that are stored and used to calculate the final value.
#define CACHEDVALUES 10

// Setup a oneWire instance to communicate with OneWire devices; in our case the water temperature sensor
OneWire oneWire1(WATER_TEMP1_PIN);
OneWire oneWire2(WATER_TEMP2_PIN);

// Create sensors object. Can retrieve values for multiple sensors on the same BUS. Will be used to retrieve temperature.
DallasTemperature waterSensor1(&oneWire1);
DallasTemperature waterSensor2(&oneWire2);

//Set the LCD address to 0x27. Our LCD has 20x4 characters.
//Set the pins on the I2C chip used for LCD connections:
//                         addr, en,rw,rs,d4,d5,d6,d7,bl,blpol
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);

//DHT-11 sensor. Measures air temperature and humidity. Returns only integer values but the precision is acceptable.
dht11 IndoorAirSensor1;
dht11 IndoorAirSensor2;
dht11 IndoorAirSensor3;

//We initialize it with 10 values. 20 C is a good temperature to start. It will be fixed quickly.
double indoorTemperatureValues[CACHEDVALUES] = {20,20,20,20,20,20,20,20,20,20};
double waterTemperatureValues[CACHEDVALUES] = {20,20,20,20,20,20,20,20,20,20};

//Defines the current index in the measurements arrays.
int measurementIndex;

//File object representing file on SD
//File myFile;

//Real time clock (RTC). Will be used to measure pump activation intervals.
RTC_DS1307 rtc;

long circulationPumpStartTime = 0;
long circulationPumpEndTime = -100000;
int circulationPumpState = OFF;
int checkCirculationPump(double waterTemperature, int floatSensor, long timeInSeconds);
long wateringPumpStartTime = 0;
long wateringPumpState = OFF;
int checkWateringPump(int floatSensor);
long fanStartTime;
int checkFan();
int checkInsectLights();

void setup () {
	Serial.begin(57600);
	
	//Blink lcd to indicate initialization
	lcd.begin(20,4);
	for(int i = 0; i< 3; i++)
	{
		lcd.backlight();
		delay(250);
		lcd.noBacklight();
		delay(250);
	}
	lcd.backlight();

#ifdef AVR
	Wire.begin();
#else
	Wire1.begin(); // Shield I2C pins connect to alt I2C bus on Arduino Due
#endif
	rtc.begin();

	if (! rtc.isrunning()) {
		Serial.println("RTC is NOT running!");
		//Following line sets the RTC to the date & time this sketch was compiled
		rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
		//This line sets the RTC with an explicit date & time, for example to set
		//January 21, 2014 at 3am you would call:
		//rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
	}

	pinMode(LOW_FLOAT_PIN, INPUT);
	pinMode(HIGH_FLOAT_PIN, INPUT);

	/*if (!SD.begin()) {
		Serial.println("SD initialization failed!");
		return;
	}*/
	
	measurementIndex = 0;
}

void loop () {
	float waterTemperature1;
	float waterTemperature2;
	float avgWaterTemperatureMeasurement;
	float airTemperatureIndoors1;
	float airTemperatureIndoors2;
	float airTemperatureIndoors3;
	float avgIndoorTemperatureMeasurement; //The average of the values from the 3 sensors
	float airHumidity1;
	float airHumidity2;
	float airHumidity3;
	int highFloatState;
	int lowFloatState;
	DateTime currentTime;

	/* Get measurements from sensors, clock etc. */
	//Float sensors
	highFloatState = digitalRead(HIGH_FLOAT_PIN);
	lowFloatState = digitalRead(LOW_FLOAT_PIN);

	//Water temperature
	waterSensor1.requestTemperatures();
	waterTemperature1 = waterSensor1.getTempCByIndex(0);

	waterSensor2.requestTemperatures();
	waterTemperature2 = waterSensor2.getTempCByIndex(0);

	//Indoor Air Temperature
	IndoorAirSensor1.read(INDOOR_AIR_SENSOR_PIN1);
	airTemperatureIndoors1 = (float)IndoorAirSensor1.temperature;
	airHumidity1 = (float)IndoorAirSensor1.humidity;

	IndoorAirSensor2.read(INDOOR_AIR_SENSOR_PIN2);
	airTemperatureIndoors2 = (float)IndoorAirSensor2.temperature;
	airHumidity2 = (float)IndoorAirSensor2.humidity;

	IndoorAirSensor3.read(INDOOR_AIR_SENSOR_PIN3);
	airTemperatureIndoors3 = (float)IndoorAirSensor3.temperature;
	airHumidity3 = (float)IndoorAirSensor3.humidity;

	//Date-Time
	currentTime = rtc.now();
	/* End Get measurements */

	avgIndoorTemperatureMeasurement = (airTemperatureIndoors1 + airTemperatureIndoors2 + airTemperatureIndoors3) / 3;
	avgWaterTemperatureMeasurement = (waterTemperature2 + waterTemperature2) / 2;

	//We store the average of the measurements received by the sensors
	indoorTemperatureValues[measurementIndex] = avgIndoorTemperatureMeasurement;
	waterTemperatureValues[measurementIndex] = avgWaterTemperatureMeasurement;

	//Set the new index
	measurementIndex = (measurementIndex + 1) % CACHEDVALUES;

	//Print date-time on serial output
	/*Serial.print(currentTime.year(), DEC);
	Serial.print('/');
	Serial.print(currentTime.month(), DEC);
	Serial.print('/');
	Serial.print(currentTime.day(), DEC);
	Serial.print(' ');
	Serial.print(currentTime.hour(), DEC);
	Serial.print(':');
	Serial.print(currentTime.minute(), DEC);
	Serial.print(':');
	Serial.print(currentTime.second(), DEC);*/
	// Print measuerements on serial output
	//Serial.print("Temperature: ");
	//Serial.print(waterTemperature);
	//Serial.print("C");
	//Serial.println();
	//Serial.print("Humidity (%): ");
	//Serial.println(airHumidity, 2);

	//Serial.print("Temperature (°C): ");
	//Serial.println(airTemperatureIndoors, 2);
	
	//Print measurements on lcd
	//lcd.clear();
	//lcd.setCursor(0,0);
	//lcd.print("Temperature: ");
	//lcd.print(waterTemperature);
	//lcd.print("C");
	//lcd.setCursor(0,1);

	//Open file on SD and write date-time
	/*myFile = SD.open("time.txt", FILE_WRITE);
	if (myFile) {
		Serial.println("Writting on SD");
		myFile.print(currentTime.year(), DEC);
		myFile.print('/');
		myFile.print(currentTime.month(), DEC);
		myFile.print('/');
		myFile.print(currentTime.day(), DEC);
		myFile.print(' ');
		myFile.print(currentTime.hour(), DEC);
		myFile.print(':');
		myFile.print(currentTime.minute(), DEC);
		myFile.print(':');
		myFile.print(currentTime.second(), DEC);
		myFile.print(' Temperature: ');
		myFile.print(waterTemperature);
		myFile.print('C');
		myFile.println();
		myFile.close();
	} else {
		// if the file didn't open, print an error:
		Serial.println("error opening test.txt");
	}*/
	
	if(checkCirculationPump(avgWaterTemperatureMeasurement, lowFloatState, currentTime.secondstime())){
		digitalWrite(CIRCULATION_PUMP_PIN, HIGH);
	}else{
		digitalWrite(CIRCULATION_PUMP_PIN, LOW);
	}

	Serial.println();
	delay(3000);
}

int checkCirculationPump(double waterTemperature, int floatSensor, long timeInSeconds){
	int pumpState = OFF;
	int isTemperatureLow = false;
	long timeSinceLastTurnedOn = timeInSeconds - circulationPumpStartTime;
	long timeSinceLastTurnedOff = timeInSeconds - circulationPumpEndTime;
	
	//If water temperature is low
	if(waterTemperature <= CIRCULATION_PUMP_TEMPERATURE_THRESHOLD){
		// If:
		// (1) Currently pump is on and not enough time has passed or,
		// (2) Currently pump is off and enoug time has passed
		// turn pump on
		if(
			circulationPumpState == ON && timeSinceLastTurnedOn < CIRCULATION_PUMP_ON_INTERVAL ||
			circulationPumpState == OFF && timeSinceLastTurnedOff >= CIRCULATION_PUMP_OFF_INTERVAL
		){
			pumpState = ON;
		}else{
			//Pump was on for long enough or, pump was off but not long enough
			pumpState = OFF;
		}
	}else{
		pumpState = OFF;
	}
	
	//If override switch is on, turn on
	if(digitalRead(CIRCULATION_PUMP_SWITCH_PIN) == HIGH){
		pumpState = ON;
	}
	
	//If we don't have enough water, always turn off
	if(floatSensor == LOW){
		pumpState = OFF;
	}
	
	//Store when we turned off
	if(circulationPumpState == ON && pumpState == OFF){
		circulationPumpEndTime = timeInSeconds;
	}
	
	//Store when we turned on
	if(circulationPumpState == OFF && pumpState == ON){
		circulationPumpStartTime = timeInSeconds;
	}
	
	circulationPumpState = pumpState;
	
	return circulationPumpState;
}

int checkWateringPump(int floatSensor, long timeInSeconds){
	int pumpState = OFF;
	long timeSinceLastTurnedOn = timeInSeconds - wateringPumpStartTime;
	
	// If we have overflow always turn on
	if(floatSensor == HIGH){
		pumpState = ON;
	}else if(wateringPumpState == ON && timeSinceLastTurnedOn < WATERING_PUMP_INTERVAL){
		// If we don't have overflow but the pump is started
		// we give it some time to work, so that we don't have
		// continuous starts and stops when water level is close
		// to sensor level.
		pumpState = ON;
	}else{
		pumpState = OFF;
	}
	
	//If override switch is on, turn on
	if(digitalRead(WATERING_PUMP_SWITCH_PIN) == HIGH){
		pumpState = ON;
	}
	
	if(wateringPumpState == OFF && pumpState == ON){
		wateringPumpStartTime = timeInSeconds;
	}
	
	wateringPumpState = pumpState;
	
	return wateringPumpState;
}