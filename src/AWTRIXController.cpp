// AWTRIX Controller
// Copyright (C) 2020
// by Blueforcer & Mazze2000

#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <FastLED.h>
#include <FastLED_NeoMatrix.h>
#include <Fonts/TomThumb.h>
#include <Wire.h>
#include "SoftwareSerial.h"

#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <ESP8266HTTPClient.h>

#include <WiFiManager.h>
#include <DoubleResetDetect.h>
#include <Wire.h>
#include <BME280_t.h>
#include "Adafruit_HTU21DF.h"
#include <Adafruit_BMP280.h>

#include <DFMiniMp3.h>

#include "MenueControl/MenueControl.h"

// instantiate temp sensor
BME280<> BMESensor;
Adafruit_BMP280 BMPSensor; // use I2C interface
Adafruit_HTU21DF htu = Adafruit_HTU21DF();

enum MsgType
{
	MsgType_Wifi,
	MsgType_Host,
	MsgType_Temp,
	MsgType_Audio,
	MsgType_Gest,
	MsgType_LDR,
	MsgType_Other
};
enum TempSensor
{
	TempSensor_None,
	TempSensor_BME280,
	TempSensor_HTU21D,
	TempSensor_BMP280
}; // None = 0

TempSensor tempState = TempSensor_None;

int ldrState = 0;		// 0 = None
bool USBConnection = false; // true = usb...
bool WIFIConnection = false;
bool notify = false;
int connectionTimout;
int matrixTempCorrection = 0;

bool dbg_updateMatrix = false;

String version = "0.46";
char awtrix_server[16] = "0.0.0.0";
char Port[6] = "7001"; // AWTRIX Host Port, default = 7001
int matrixType = 0;

IPAddress Server;
WiFiClient espClient;
PubSubClient client(espClient);

WiFiManager wifiManager;

MenueControl myMenue;

//update
ESP8266WebServer server(80);
//const char *serverIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 3600000); /* interval 60 mins */


//resetdetector
#define DRD_TIMEOUT 5.0
#define DRD_ADDRESS 0x00
DoubleResetDetect drd(DRD_TIMEOUT, DRD_ADDRESS);

bool firstStart = true;
int myTime;	 //need for loop
int myTime2; //need for loop
int myTime3; //need for loop3
int myCounter;
int myCounter2;
//boolean getLength = true;
//int prefix = -5;

bool ignoreServer = false;
int menuePointer;
bool appRun = true;
bool appRunClock = true;
bool appRunAlert = true;
bool appAlertFlag = false;

//Taster_mid
int tasterPin[] = {D0, D4, D8};
int tasterCount = 3;

int timeoutTaster[] = {0, 0, 0, 0};
bool pushed[] = {false, false, false, false};
int blockTimeTaster[] = {0, 0, 0, 0};
bool blockTaster[] = {false, false, false, false};
bool blockTaster2[] = {false, false, false, false};
bool tasterState[3];
bool allowTasterSendToServer = true;
int pressedTaster = 0;

//Reset time (Touch Taster)
int resetTime = 6000; //in milliseconds

boolean awtrixFound = false;
int myPointer[14];
uint32_t messageLength = 0;
uint32_t SavemMessageLength = 0;

//USB Connection:
byte myBytes[1000];
int bufferpointer;

//Zum speichern...
int cfgStart = 0;

//flag for saving data
bool shouldSaveConfig = false;

/// LDR Config
#define LDR_PIN A0
int LDRvalue = 0;
int minBrightness = 5;
int maxBrightness = 100;
int newBri;
static unsigned long lastTimeLDRCheck = 0;
bool autoBrightness;

bool inFade = false;

#define I2C_SDA D3
#define I2C_SCL D1

#ifndef ICACHE_RAM_ATTR
#define ICACHE_RAM_ATTR IRAM_ATTR
#endif

bool updating = false;

void appClock(int input);

// Audio

class Mp3Notify;
SoftwareSerial mySoftwareSerial(D7, D5); // RX, TX
typedef DFMiniMp3<SoftwareSerial, Mp3Notify> DfMp3;
DfMp3 dfmp3(mySoftwareSerial);

class Mp3Notify
{
};

// Matrix Settings
CRGB leds[256];
FastLED_NeoMatrix *matrix;

static byte c1; // Last character buffer
byte utf8ascii(byte ascii)
{
	if (ascii < 128) // Standard ASCII-set 0..0x7F handling
	{
		c1 = 0;
		return (ascii);
	}
	// get previous input
	byte last = c1; // get last char
	c1 = ascii;		// remember actual character
	switch (last)	// conversion depending on first UTF8-character
	{
	case 0xC2:
		return (ascii)-34;
		break;
	case 0xC3:
		return (ascii | 0xC0) - 34;
		break;
	case 0x82:
		if (ascii == 0xAC)
			return (0xEA);
	}
	return (0);
}

bool saveConfig()
{
	DynamicJsonBuffer jsonBuffer;
	JsonObject &json = jsonBuffer.createObject();
	json["awtrix_server"] = awtrix_server;
	json["matrixType"] = matrixType;
	json["matrixCorrection"] = matrixTempCorrection;
	json["Port"] = Port;

	json["minBri"] = minBrightness;
	json["maxBri"] = maxBrightness;
	json["ldr"] = autoBrightness;

	File configFile = LittleFS.open("/awtrix.json", "w");

	if (!configFile)
	{
		if (!USBConnection)
		{
			Serial.println("failed to open config file for writing");
		}

		return false;
	}

	json.printTo(configFile);
	configFile.close();
	return true;
}

void debuggingWithMatrix(String text)
{
	matrix->setCursor(7, 6);
	matrix->clear();
	matrix->print(text);
	matrix->show();
}

void sendToServer(String s)
{
	if (USBConnection)
	{
		uint32_t laenge = s.length();
		Serial.printf("%c%c%c%c%s", (laenge & 0xFF000000) >> 24, (laenge & 0x00FF0000) >> 16, (laenge & 0x0000FF00) >> 8, (laenge & 0x000000FF), s.c_str());
	}
	else
	{
		client.publish("matrixClient", s.c_str());
	}
}

void logToServer(String s)
{
	StaticJsonBuffer<400> jsonBuffer;
	JsonObject &root = jsonBuffer.createObject();
	root["type"] = "log";
	root["msg"] = s;
	String JS;
	root.printTo(JS);
	sendToServer(JS);
}

int checkTaster(int nr)
{
	tasterState[0] = !digitalRead(tasterPin[0]);
	tasterState[1] = digitalRead(tasterPin[1]);
	tasterState[2] = !digitalRead(tasterPin[2]);

	switch (nr)
	{
	case 0:
		if (tasterState[0] == LOW && !pushed[nr] && !blockTaster2[nr] && tasterState[1] && tasterState[2])
		{
			pushed[nr] = true;
			timeoutTaster[nr] = millis();
		}
		break;
	case 1:
		if (tasterState[1] == LOW && !pushed[nr] && !blockTaster2[nr] && tasterState[0] && tasterState[2])
		{
			pushed[nr] = true;
			timeoutTaster[nr] = millis();
		}
		break;
	case 2:
		if (tasterState[2] == LOW && !pushed[nr] && !blockTaster2[nr] && tasterState[0] && tasterState[1])
		{
			pushed[nr] = true;
			timeoutTaster[nr] = millis();
		}
		break;
	case 3:
		if (tasterState[0] == LOW && tasterState[2] == LOW && !pushed[nr] && !blockTaster2[nr] && tasterState[1])
		{
			pushed[nr] = true;
			timeoutTaster[nr] = millis();
		}
		break;
	}

	if (pushed[nr] && (millis() - timeoutTaster[nr] < 2000) && tasterState[nr] == HIGH)
	{
		if (!blockTaster2[nr])
		{
			StaticJsonBuffer<400> jsonBuffer;
			JsonObject &root = jsonBuffer.createObject();
			root["type"] = "button";

			switch (nr)
			{
			case 0:
				root["left"] = "short";
				pressedTaster = 1;
				//Serial.println("LEFT: normaler Tastendruck");
				break;
			case 1:
				root["middle"] = "short";
				pressedTaster = 2;
				//Serial.println("MID: normaler Tastendruck");
				break;
			case 2:
				root["right"] = "short";
				pressedTaster = 3;
				//Serial.println("RIGHT: normaler Tastendruck");
				break;
			}

			String JS;
			root.printTo(JS);
			if (allowTasterSendToServer)
			{
				sendToServer(JS);
			}

			if (appRun)
			{
				switch (nr)
				{
					case 0:
					case 2:
						//dfmp3.playMp3FolderTrack(3); /* click */
						appClock(1);
						break;
				}
			}

			pushed[nr] = false;
			return 1;
		}
	}

	if (pushed[nr] && (millis() - timeoutTaster[nr] > 2000))
	{
		if (!blockTaster2[nr])
		{
			StaticJsonBuffer<400> jsonBuffer;
			JsonObject &root = jsonBuffer.createObject();
			root["type"] = "button";
			switch (nr)
			{
			case 0:
				root["left"] = "long";
				//Serial.println("LEFT: langer Tastendruck");
				break;
			case 1:
				root["middle"] = "long";
				//Serial.println("MID: langer Tastendruck");
				break;
			case 2:
				root["right"] = "long";
				//Serial.println("RIGHT: langer Tastendruck");
				break;
			case 3:
				if (allowTasterSendToServer)
				{
					allowTasterSendToServer = false;
					ignoreServer = true;
				}
				else
				{
					allowTasterSendToServer = true;
					ignoreServer = false;
					menuePointer = 0;
				}
				break;
			}
			String JS;
			root.printTo(JS);
			if (allowTasterSendToServer)
			{
				sendToServer(JS);
			}

			if (appRun)
			{
				switch (nr)
				{
					case 1:
						dfmp3.playMp3FolderTrack(4); /* enter */
						break;
				}
			}
			blockTaster[nr] = true;
			blockTaster2[nr] = true;
			pushed[nr] = false;
			return 2;
		}
	}
	if (nr == 3)
	{
		if (blockTaster[nr] && tasterState[0] == HIGH && tasterState[2] == HIGH)
		{
			blockTaster[nr] = false;
			blockTimeTaster[nr] = millis();
		}
	}
	else
	{
		if (blockTaster[nr] && tasterState[nr] == HIGH)
		{
			blockTaster[nr] = false;
			blockTimeTaster[nr] = millis();
		}
	}

	if (!blockTaster[nr] && (millis() - blockTimeTaster[nr] > 500))
	{
		blockTaster2[nr] = false;
	}
	return 0;
}

String utf8ascii(String s)
{
	String r = "";
	char c;
	for (unsigned int i = 0; i < s.length(); i++)
	{
		c = utf8ascii(s.charAt(i));
		if (c != 0)
			r += c;
	}
	return r;
}

void hardwareAnimatedUncheck(int typ, int x, int y)
{
	int wifiCheckTime = millis();
	int wifiCheckPoints = 0;
	while (millis() - wifiCheckTime < 2000)
	{
		while (wifiCheckPoints < 10)
		{
			matrix->clear();
			switch (typ)
			{
			case 0:
				matrix->setCursor(7, 6);
				matrix->print("WiFi");
				break;
			case 1:
				matrix->setCursor(1, 6);
				matrix->print("Server");
				break;
			case 2:
				matrix->setCursor(7, 6);
				matrix->print("Temp");
				break;
			case 4:
				matrix->setCursor(3, 6);
				matrix->print("Gest.");
				break;
			}

			switch (wifiCheckPoints)
			{
			case 9:
				matrix->drawPixel(x, y + 4, 0xF800);
			case 8:
				matrix->drawPixel(x - 1, y + 3, 0xF800);
			case 7:
				matrix->drawPixel(x - 2, y + 2, 0xF800);
			case 6:
				matrix->drawPixel(x - 3, y + 1, 0xF800);
			case 5:
				matrix->drawPixel(x - 4, y, 0xF800);
			case 4:
				matrix->drawPixel(x - 4, y + 4, 0xF800);
			case 3:
				matrix->drawPixel(x - 3, y + 3, 0xF800);
			case 2:
				matrix->drawPixel(x - 2, y + 2, 0xF800);
			case 1:
				matrix->drawPixel(x - 1, y + 1, 0xF800);
			case 0:
				matrix->drawPixel(x, y, 0xF800);
				break;
			}
			wifiCheckPoints++;
			matrix->show();
			delay(100);
		}
	}
}

void hardwareAnimatedCheck(MsgType typ, int x, int y)
{
	int wifiCheckTime = millis();
	int wifiCheckPoints = 0;
	while (millis() - wifiCheckTime < 2000)
	{
		while (wifiCheckPoints < 7)
		{
			matrix->clear();
			switch (typ)
			{
			case MsgType_Wifi:
				matrix->setCursor(7, 6);
				matrix->print("WiFi");
				break;
			case MsgType_Host:
				matrix->setCursor(5, 6);
				matrix->print("Host");
				break;
			case MsgType_Temp:
				matrix->setCursor(7, 6);
				matrix->print("Temp");
				break;
			case MsgType_Audio:
				matrix->setCursor(3, 6);
				matrix->print("Audio");
				break;
			case MsgType_Gest:
				matrix->setCursor(3, 6);
				matrix->print("Gest.");
				break;
			case MsgType_LDR:
				matrix->setCursor(7, 6);
				matrix->print("LDR");
				break;
			}

			switch (wifiCheckPoints)
			{
			case 6:
				matrix->drawPixel(x, y, 0x07E0);
			case 5:
				matrix->drawPixel(x - 1, y + 1, 0x07E0);
			case 4:
				matrix->drawPixel(x - 2, y + 2, 0x07E0);
			case 3:
				matrix->drawPixel(x - 3, y + 3, 0x07E0);
			case 2:
				matrix->drawPixel(x - 4, y + 4, 0x07E0);
			case 1:
				matrix->drawPixel(x - 5, y + 3, 0x07E0);
			case 0:
				matrix->drawPixel(x - 6, y + 2, 0x07E0);
				break;
			}
			wifiCheckPoints++;
			matrix->show();
			delay(100);
		}
	}
}

void serverSearch(int rounds, int typ, int x, int y)
{
	matrix->clear();
	matrix->setTextColor(0xFFFF);
	matrix->setCursor(5, 6);
	matrix->print("Host");

	if (typ == 0)
	{
		switch (rounds)
		{
		case 3:
			matrix->drawPixel(x, y, 0x22ff);
			matrix->drawPixel(x + 1, y + 1, 0x22ff);
			matrix->drawPixel(x + 2, y + 2, 0x22ff);
			matrix->drawPixel(x + 3, y + 3, 0x22ff);
			matrix->drawPixel(x + 2, y + 4, 0x22ff);
			matrix->drawPixel(x + 1, y + 5, 0x22ff);
			matrix->drawPixel(x, y + 6, 0x22ff);
		case 2:
			matrix->drawPixel(x - 1, y + 2, 0x22ff);
			matrix->drawPixel(x, y + 3, 0x22ff);
			matrix->drawPixel(x - 1, y + 4, 0x22ff);
		case 1:
			matrix->drawPixel(x - 3, y + 3, 0x22ff);
		case 0:
			break;
		}
	}
	else if (typ == 1)
	{

		switch (rounds)
		{
		case 12:
			//matrix->drawPixel(x+3, y+2, 0x22ff);
			matrix->drawPixel(x + 3, y + 3, 0x22ff);
			//matrix->drawPixel(x+3, y+4, 0x22ff);
			matrix->drawPixel(x + 3, y + 5, 0x22ff);
			//matrix->drawPixel(x+3, y+6, 0x22ff);
		case 11:
			matrix->drawPixel(x + 2, y + 2, 0x22ff);
			matrix->drawPixel(x + 2, y + 3, 0x22ff);
			matrix->drawPixel(x + 2, y + 4, 0x22ff);
			matrix->drawPixel(x + 2, y + 5, 0x22ff);
			matrix->drawPixel(x + 2, y + 6, 0x22ff);
		case 10:
			matrix->drawPixel(x + 1, y + 3, 0x22ff);
			matrix->drawPixel(x + 1, y + 4, 0x22ff);
			matrix->drawPixel(x + 1, y + 5, 0x22ff);
		case 9:
			matrix->drawPixel(x, y + 4, 0x22ff);
		case 8:
			matrix->drawPixel(x - 1, y + 4, 0x22ff);
		case 7:
			matrix->drawPixel(x - 2, y + 4, 0x22ff);
		case 6:
			matrix->drawPixel(x - 3, y + 4, 0x22ff);
		case 5:
			matrix->drawPixel(x - 3, y + 5, 0x22ff);
		case 4:
			matrix->drawPixel(x - 3, y + 6, 0x22ff);
		case 3:
			matrix->drawPixel(x - 3, y + 7, 0x22ff);
		case 2:
			matrix->drawPixel(x - 4, y + 7, 0x22ff);
		case 1:
			matrix->drawPixel(x - 5, y + 7, 0x22ff);
		case 0:
			break;
		}
	}
	matrix->show();
}

void hardwareAnimatedSearch(int typ, int x, int y)
{
	for (int i = 0; i < 4; i++)
	{
		matrix->clear();
		matrix->setTextColor(0xFFFF);
		if (typ == 0)
		{
			matrix->setCursor(7, 6);
			matrix->print("WiFi");
		}
		else if (typ == 1)
		{
			matrix->setCursor(5, 6);
			matrix->print("Host");
		}
		switch (i)
		{
		case 3:
			matrix->drawPixel(x, y, 0x22ff);
			matrix->drawPixel(x + 1, y + 1, 0x22ff);
			matrix->drawPixel(x + 2, y + 2, 0x22ff);
			matrix->drawPixel(x + 3, y + 3, 0x22ff);
			matrix->drawPixel(x + 2, y + 4, 0x22ff);
			matrix->drawPixel(x + 1, y + 5, 0x22ff);
			matrix->drawPixel(x, y + 6, 0x22ff);
		case 2:
			matrix->drawPixel(x - 1, y + 2, 0x22ff);
			matrix->drawPixel(x, y + 3, 0x22ff);
			matrix->drawPixel(x - 1, y + 4, 0x22ff);
		case 1:
			matrix->drawPixel(x - 3, y + 3, 0x22ff);
		case 0:
			break;
		}
		matrix->show();
		delay(100);
	}
}

void utf8ascii(char *s)
{
	int k = 0;
	char c;
	for (unsigned int i = 0; i < strlen(s); i++)
	{
		c = utf8ascii(s[i]);
		if (c != 0)
			s[k++] = c;
	}
	s[k] = 0;
}

String GetChipID()
{
	return String(ESP.getChipId());
}

int hexcolorToInt(char upper, char lower)
{
	int uVal = (int)upper;
	int lVal = (int)lower;
	uVal = uVal > 64 ? uVal - 55 : uVal - 48;
	uVal = uVal << 4;
	lVal = lVal > 64 ? lVal - 55 : lVal - 48;
	//  Serial.println(uVal+lVal);
	return uVal + lVal;
}

int GetRSSIasQuality(int rssi)
{
	int quality = 0;

	if (rssi <= -100)
	{
		quality = 0;
	}
	else if (rssi >= -50)
	{
		quality = 100;
	}
	else
	{
		quality = 2 * (rssi + 100);
	}
	return quality;
}

void updateMatrix(byte payload[], int length)
{
	if (!ignoreServer)
	{
		int y_offset = 5;
		if (firstStart)
		{
			//hardwareAnimatedCheck(1, 30, 2);
			firstStart = false;
		}

		connectionTimout = millis();

// KENDBG
//if ((payload[0] != 0) && (payload[0] != 6))Serial.println(String(__FILE__)+"("+String(__LINE__)+") payload0 = "+String(payload[0]));

		switch (payload[0])
		{
		case 0:
		{
			//Command 0: DrawText

			//Prepare the coordinates
			uint16_t x_coordinate = int(payload[1] << 8) + int(payload[2]);
			uint16_t y_coordinate = int(payload[3] << 8) + int(payload[4]);

			matrix->setCursor(x_coordinate + 1, y_coordinate + y_offset);
			matrix->setTextColor(matrix->Color(payload[5], payload[6], payload[7]));
			String myText = "";
			for (int i = 8; i < length; i++)
			{
				char c = payload[i];
				myText += c;
			}

			matrix->print(utf8ascii(myText));
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") draw text, ("+String(x_coordinate + 1)+","+String(y_coordinate + y_offset)+")"); /*KENDBG*/
			break;
		}
		case 1:
		{
			//Command 1: DrawBMP
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") draw bmp"); /*KENDBG*/
			//Prepare the coordinates
			uint16_t x_coordinate = int(payload[1] << 8) + int(payload[2]);
			uint16_t y_coordinate = int(payload[3] << 8) + int(payload[4]);

			int16_t width = payload[5];
			int16_t height = payload[6];

			unsigned short colorData[width * height];

			for (int i = 0; i < width * height * 2; i++)
			{
				colorData[i / 2] = (payload[i + 7] << 8) + payload[i + 1 + 7];
				i++;
			}

			for (int16_t j = 0; j < height; j++, y_coordinate++)
			{
				for (int16_t i = 0; i < width; i++)
				{
					matrix->drawPixel(x_coordinate + i, y_coordinate, (uint16_t)colorData[j * width + i]);
				}
			}
			break;
		}

		case 2:
		{
			//Command 2: DrawCircle
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") draw circle"); /*KENDBG*/
			//Prepare the coordinates
			uint16_t x0_coordinate = int(payload[1] << 8) + int(payload[2]);
			uint16_t y0_coordinate = int(payload[3] << 8) + int(payload[4]);
			uint16_t radius = payload[5];
			matrix->drawCircle(x0_coordinate, y0_coordinate, radius, matrix->Color(payload[6], payload[7], payload[8]));
			break;
		}
		case 3:
		{
			//Command 3: FillCircle
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") fill circle"); /*KENDBG*/
			//Prepare the coordinates
			uint16_t x0_coordinate = int(payload[1] << 8) + int(payload[2]);
			uint16_t y0_coordinate = int(payload[3] << 8) + int(payload[4]);
			uint16_t radius = payload[5];
			matrix->fillCircle(x0_coordinate, y0_coordinate, radius, matrix->Color(payload[6], payload[7], payload[8]));
			break;
		}
		case 4:
		{
			//Command 4: DrawPixel
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") draw pixel"); /*KENDBG*/
			//Prepare the coordinates
			uint16_t x0_coordinate = int(payload[1] << 8) + int(payload[2]);
			uint16_t y0_coordinate = int(payload[3] << 8) + int(payload[4]);
			matrix->drawPixel(x0_coordinate, y0_coordinate, matrix->Color(payload[5], payload[6], payload[7]));
			break;
		}
		case 5:
		{
			//Command 5: DrawRect
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") draw rect"); /*KENDBG*/
			//Prepare the coordinates
			uint16_t x0_coordinate = int(payload[1] << 8) + int(payload[2]);
			uint16_t y0_coordinate = int(payload[3] << 8) + int(payload[4]);
			int16_t width = payload[5];
			int16_t height = payload[6];
			matrix->drawRect(x0_coordinate, y0_coordinate, width, height, matrix->Color(payload[7], payload[8], payload[9]));
			break;
		}
		case 6:
		{
			//Command 6: DrawLine
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") draw line - "+String(payload[9])+","+String(payload[10])+","+String(payload[11])); /*KENDBG*/
			//Prepare the coordinates
			uint16_t x0_coordinate = int(payload[1] << 8) + int(payload[2]);
			uint16_t y0_coordinate = int(payload[3] << 8) + int(payload[4]);
			uint16_t x1_coordinate = int(payload[5] << 8) + int(payload[6]);
			uint16_t y1_coordinate = int(payload[7] << 8) + int(payload[8]);
			matrix->drawLine(x0_coordinate, y0_coordinate, x1_coordinate, y1_coordinate, matrix->Color(payload[9], payload[10], payload[11]));
			break;
		}

		case 7:
		{
			//Command 7: FillMatrix
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") fill matrix"); /*KENDBG*/
			matrix->fillScreen(matrix->Color(payload[1], payload[2], payload[3]));
			break;
		}

		case 8:
		{
			//Command 8: Show
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") show"); /*KENDBG*/
			if (notify)
			{
				matrix->drawPixel(31, 0, matrix->Color(200, 0, 0));
			}
			matrix->show();
			break;
		}
		case 9:
		{
			//Command 9: Clear
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") clear"); /*KENDBG*/
			matrix->clear();
			break;
		}
		case 10:
		{
			//deprecated
			//Command 10: Play
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") [deprecated] set volume and play mp3 folder track"); /*KENDBG*/
			dfmp3.setVolume(payload[2]);
			delay(10);
			dfmp3.playMp3FolderTrack(payload[1]);

			break;
		}
		case 11:
		{
			//Command 11: reset
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") reset "); /*KENDBG*/
			ESP.reset();
			break;
		}
		case 12:
		{
			//Command 12: GetMatrixInfo
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") get Matrix info "); /*KENDBG*/
			StaticJsonBuffer<400> jsonBuffer;
			JsonObject &root = jsonBuffer.createObject();
			root["type"] = "MatrixInfo";
			root["version"] = version;
			root["wifirssi"] = String(WiFi.RSSI());
			root["wifiquality"] = GetRSSIasQuality(WiFi.RSSI());
			root["wifissid"] = WiFi.SSID();
			root["serial"] = USBConnection;
			root["IP"] = WiFi.localIP().toString();
			LDRvalue = analogRead(LDR_PIN);
			root["LDR"] = LDRvalue;
			root["LUX"] = 0;
			switch (tempState)
			{
			case TempSensor_BME280:
				BMESensor.refresh();
				root["Temp"] = BMESensor.temperature;
				root["Hum"] = BMESensor.humidity;
				root["hPa"] = BMESensor.pressure;
				break;
			case TempSensor_HTU21D:
				root["Temp"] = htu.readTemperature();
				root["Hum"] = htu.readHumidity();
				root["hPa"] = 0;
				break;
			case TempSensor_BMP280:
				sensors_event_t temp_event, pressure_event;
				BMPSensor.getTemperatureSensor()->getEvent(&temp_event);
				BMPSensor.getPressureSensor()->getEvent(&pressure_event);

				root["Temp"] = temp_event.temperature;
				root["Hum"] = 0;
				root["hPa"] = pressure_event.pressure;
				break;
			default:
				root["Temp"] = 0;
				root["Hum"] = 0;
				root["hPa"] = 0;
				break;
			}

			String JS;
			root.printTo(JS);
			sendToServer(JS);
			break;
		}
		case 13:
		{
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") set brightness : "+String(payload[1])); /*KENDBG*/
			if (autoBrightness)
			{
				int bri = payload[1];
				int d = min(bri, newBri);
				matrix->setBrightness(d);
			}
			else
			{
				matrix->setBrightness(payload[1]);
			}

			break;
		}
		case 14:
		{
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") save "); /*KENDBG*/
			bool reset = false;
			autoBrightness = int(payload[1]);
			minBrightness = int(payload[2]);
			maxBrightness = int(payload[3]);

			if (matrixTempCorrection != (int)payload[4])
			{
				reset = true;
				matrixTempCorrection = (int)payload[4];
				Serial.println(matrixTempCorrection);
			}

			if (reset)
			{
			saveConfig();
				matrix->clear();
				matrix->setCursor(6, 6);
				matrix->setTextColor(matrix->Color(0, 255, 50));
				matrix->print("SAVED!");
				matrix->show();
				delay(2000);
				ESP.reset();
			}
			saveConfig();
			break;
		}
		case 15:
		{
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") reset "); /*KENDBG*/
			matrix->clear();
			matrix->setTextColor(matrix->Color(255, 0, 0));
			matrix->setCursor(6, 6);
			matrix->print("RESET!");
			matrix->show();
			delay(1000);
			if (LittleFS.begin())
			{
				delay(1000);
				LittleFS.remove("/awtrix.json");

				LittleFS.end();
				delay(1000);
			}
			wifiManager.resetSettings();
			ESP.reset();
			break;
		}
		case 16:
		{
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") send to server "); /*KENDBG*/
			sendToServer("ping");
			break;
		}
		case 17:
		{

			//Command 17: Volume
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") set Volume: "+String(payload[1])); /*KENDBG*/
			dfmp3.setVolume(payload[1]);
			break;
		}
		case 18:
		{
			//Command 18: Play
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") play mp3 folder track: "+String(payload[1])); /*KENDBG*/
			dfmp3.playMp3FolderTrack(payload[1]);
			break;
		}
		case 19:
		{
			//Command 18: Stop
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") stop Advertisement"); /*KENDBG*/
			dfmp3.stopAdvertisement();
			delay(50);
			dfmp3.stop();
			break;
		}
		case 20:
		{
			//change the connection...
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") change the connection..."); /*KENDBG*/
			USBConnection = false;
			WIFIConnection = false;
			firstStart = true;
			break;
		}
		case 21:
		{
			//multicolor...
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") multi-color"); /*KENDBG*/
			uint16_t x_coordinate = int(payload[1] << 8) + int(payload[2]);
			uint16_t y_coordinate = int(payload[3] << 8) + int(payload[4]);
			matrix->setCursor(x_coordinate + 1, y_coordinate + y_offset);

			String myJSON = "";
			for (int i = 5; i < length; i++)
			{
				myJSON += (char)payload[i];
			}
			//Serial.println("myJSON: " + myJSON + " ENDE");
			DynamicJsonBuffer jsonBuffer;
			JsonArray &array = jsonBuffer.parseArray(myJSON);
			if (array.success())
			{
				//Serial.println("Array erfolgreich geöffnet... =)");
				for (int i = 0; i < (int)array.size(); i++)
				{
					String tempString = array[i]["t"];
					String colorString = array[i]["c"];
					JsonArray &color = jsonBuffer.parseArray(colorString);
					if (color.success())
					{
						//Serial.println("Color erfolgreich geöffnet... =)");
						String myText = "";
						int r = color[0];
						int g = color[1];
						int b = color[2];
						//Serial.println("Test: " + tempString + " / Color: " + r + "/" + g + "/" + b);
						matrix->setTextColor(matrix->Color(r, g, b));
						for (int y = 0; y < (int)tempString.length(); y++)
						{
							myText += (char)tempString[y];
						}
						matrix->print(utf8ascii(myText));
					}
				}
			}
			break;
		}
		case 22:
		{
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") print scroll text"); /*KENDBG*/
			String myJSON = "";
			for (int i = 1; i < length; i++)
			{
				myJSON += (char)payload[i];
			}
			DynamicJsonBuffer jsonBuffer;
			JsonObject &json = jsonBuffer.parseObject(myJSON);

			String tempString = json["text"];
			String colorString = json["color"];

			JsonArray &color = jsonBuffer.parseArray(colorString);
			int r = color[0];
			int g = color[1];
			int b = color[2];
			int scrollSpeed = (int)json["scrollSpeed"];
			int textlaenge;
			while (true)
			{
				matrix->setCursor(32, 6);
				matrix->print(utf8ascii(tempString));
				textlaenge = (int)matrix->getCursorX() - 32;
				for (int i = 31; i > (-textlaenge); i--)
				{
					int starzeit = millis();
					matrix->clear();
					matrix->setCursor(i, 6);
					matrix->setTextColor(matrix->Color(r, g, b));
					matrix->print(utf8ascii(tempString));
					matrix->show();
					client.loop();
					int endzeit = millis();
					if ((scrollSpeed + starzeit - endzeit) > 0)
					{
						delay(scrollSpeed + starzeit - endzeit);
					}
				}
				connectionTimout = millis();
				break;
			}
			break;
		}
		case 23:
		{
			//Command 23: DrawFilledRect
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") Draw Filled Rect"); /*KENDBG*/
			//Prepare the coordinates
			uint16_t x0_coordinate = int(payload[1] << 8) + int(payload[2]);
			uint16_t y0_coordinate = int(payload[3] << 8) + int(payload[4]);
			int16_t width = payload[5];
			int16_t height = payload[6];
			matrix->fillRect(x0_coordinate, y0_coordinate, width, height, matrix->Color(payload[7], payload[8], payload[9]));
			break;
		}
		case 24:
		{
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") loop Blobal Track: "+String(payload[1])); /*KENDBG*/
			dfmp3.loopGlobalTrack(payload[1]);
			break;
		}
		case 25:
		{
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") play advertisement: "+String(payload[1])); /*KENDBG*/
			dfmp3.playAdvertisement(payload[1]);
			break;
		}
		case 26:
		{
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") set notify: "+String(payload[1])); /*KENDBG*/
			notify = payload[1];
			break;
		}
		case 27:
		{
			if(dbg_updateMatrix) Serial.println("["+String(connectionTimout)+"] "+String(__FUNCTION__)+"("+String(payload[0])+") set Brightness value "); /*KENDBG*/
			newBri = map(LDRvalue, 0, 1023, minBrightness, maxBrightness);
			if (newBri < 5) newBri = 0;
			matrix->setBrightness(newBri);
		}
		}
	}
}

void callback(char *topic, byte *payload, unsigned int length)
{
	WIFIConnection = true;
	updateMatrix(payload, length);
}

void reconnect()
{
	//Serial.println("reconnecting to " + String(awtrix_server));
	String clientId = "AWTRIXController-";
	clientId += String(random(0xffff), HEX);
	hardwareAnimatedSearch(1, 28, 0);

	if (client.connect(clientId.c_str(), "matrixDisconnect", 1, 0, WiFi.localIP().toString().c_str()))
	{
		//Serial.println("connected to server!");
		client.subscribe("awtrixmatrix/#");

		client.publish("matrixClient", "connected");
		matrix->fillScreen(matrix->Color(0, 0, 0));
		matrix->show();
	}
}

uint32_t Wheel(byte WheelPos, int pos)
{
	if (WheelPos < 85)
	{
		return matrix->Color((WheelPos * 3) - pos, (255 - WheelPos * 3) - pos, 0);
	}
	else if (WheelPos < 170)
	{
		WheelPos -= 85;
		return matrix->Color((255 - WheelPos * 3) - pos, 0, (WheelPos * 3) - pos);
	}
	else
	{
		WheelPos -= 170;
		return matrix->Color(0, (WheelPos * 3) - pos, (255 - WheelPos * 3) - pos);
	}
}

void flashProgress(unsigned int progress, unsigned int total)
{
	matrix->setBrightness(80);
	long num = 32 * 8 * progress / total;
	for (unsigned char y = 0; y < 8; y++)
	{
		for (unsigned char x = 0; x < 32; x++)
		{
			if (num-- > 0)
				matrix->drawPixel(x, 8 - y - 1, Wheel((num * 16) & 255, 0));
		}
	}
	matrix->setCursor(1, 6);
	matrix->setTextColor(matrix->Color(200, 200, 200));
	matrix->print("FLASHING");
	matrix->show();
}

void saveConfigCallback()
{
	if (!USBConnection)
	{
		Serial.println("Should save config");
	}
	shouldSaveConfig = true;
}

void configModeCallback(WiFiManager *myWiFiManager)
{

	if (!USBConnection)
	{
		Serial.println("Entered config mode");
		Serial.println(WiFi.softAPIP());
		Serial.println(myWiFiManager->getConfigPortalSSID());
	}
	matrix->clear();
	matrix->setCursor(3, 6);
	matrix->setTextColor(matrix->Color(0, 255, 50));
	matrix->print("Hotspot");
	matrix->show();
}

int getTimeOffset() {
	HTTPClient http;
	int httpCode;
	long ret = 0;
	http.begin("http://worldtimeapi.org/api/ip/");
	httpCode = http.GET();
	if (httpCode == HTTP_CODE_OK) {
		DynamicJsonBuffer jsonBuffer;
		JsonObject &json = jsonBuffer.parseObject(http.getString());
		if (json.success())
		{
			int year, month, day, hour, minute, second;
			ret = json["raw_offset"].as<int>();
			sscanf(json["datetime"].asString(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second);
			setTime(hour, minute, second, day, month, year);
		}
	}
	http.end();
	return ret;
}

String handleRoot() {
  String modeInfo;
  String ipAddress;

  // Check current WiFi mode
  if (WiFi.getMode() == WIFI_STA) {
    modeInfo = "Station Mode";
    ipAddress = "Station IP address: " + WiFi.localIP().toString();
  } else {
    modeInfo = "Access Point Mode";
    ipAddress = "Access Point IP address: " + WiFi.softAPIP().toString();
  }

  // HTML response
  String html = "<html><body>";
  html += "<p>WiFi Mode: " + modeInfo + "</p>";
  html += "<p>" + ipAddress + "</p>";
  html += "<form action='/setup' method='POST'>SSID: <input type='text' name='ssid'><br>Password: <input type='password' name='password'><br><input type='submit' value='Submit'></form>";
  html += "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";
  html += "</body></html>";

  server.send(200, "text/html", html);
  return html;
}

void checkLDR()
{
	static bool first = true;
	static unsigned long timeStamp = 0;
	if (first)
	{
		newBri = 50;
		first = false;
	}

	if (!timeStamp) timeStamp = millis();

	if ((millis() - timeStamp) > 5000)
	{
		int ldr = analogRead(LDR_PIN);
		if (ldr >= 170) ldr=170;
		if (ldr < 10) ldr = 0;
		else ldr -= 10;
		ldr = ldr * 50 / 160 + 2;
		newBri = ldr;
		timeStamp = 0;
		if(!inFade)
		{
			matrix->setBrightness(newBri);
			matrix->show();
		}
	}
}

void appAlert()
{
	//appRunAlert;
	if (appAlertFlag)
	{
		appRunClock = false;
		appClock(1);
		dfmp3.setRepeatPlayCurrentTrack(true);
		dfmp3.playMp3FolderTrack(10);
		appAlertFlag = !appAlertFlag;
	}
}

void appClock(int input)
{
	int i,j,k;
	char buffer[50];
	static bool colon = true;
	static bool fade = true;
	static unsigned long timeStamp = 0;
	static int counter = 1;
	static int process = 0;
	//newBri = 50;
	if (input)
	{
		dfmp3.playMp3FolderTrack(3); /* click */
		process = 3;
	}

	if (!timeStamp)
	{
		timeStamp = millis();
		matrix->setBrightness(0);
	}

	switch (process)
	{
		case 0: /* time show */
			if ((millis() - timeStamp) > 1000)
			{
				timeStamp = millis();
				counter++;
				matrix->clear();
				matrix->setTextColor(matrix->Color(0, 0, 255));
				matrix->setCursor(8, 6);
				if (colon) sprintf(buffer, "%02d:%02d", hour(), minute());
				else sprintf(buffer, "%02d %02d", hour(), minute());
				matrix->print(buffer);
				matrix->drawLine( 0, 7, 31, 7, matrix->Color(00, 00, 00));
				inFade = false;

				for (j=1; j<8; j++)
				{
					k = weekday() - 1;
					uint16_t color = matrix->Color(80, 80, 80);
					if (k==0) k=7;
					if (j==k) color = matrix->Color(255, 255, 255);
					matrix->drawLine( 4*j-1, 7, 4*j+1, 7, color);
				}
				// fade in
				if (fade)
				{
					for (k=1; k<=10; k++)
					{
						int b = newBri * (0.1 * k);
						matrix->setBrightness(b);
						matrix->show();
						delay(20);
					}
					fade = false;
				}
				matrix->show();
				colon = !colon;
				//delay(1000);
				if (counter >= 10)
				{
					process = 1;
					counter = 1;
				}
			}
			break;
		case 1: /* [animation] switch to date */
			for (i=1; i<8; i++)
			{
				matrix->clear();
				matrix->setTextColor(matrix->Color(0, 0, 255));
				sprintf(buffer, "%02d.%02d.", month(), day());
				matrix->setCursor(8, i-1);
				matrix->print(buffer);
				if (colon) sprintf(buffer, "%02d:%02d", hour(), minute());
				else sprintf(buffer, "%02d %02d", hour(), minute());
				matrix->setCursor(8, i+5);
				matrix->print(buffer);
				matrix->drawLine( 0, 7, 31, 7, matrix->Color(00, 00, 00));
				for (j=1; j<8; j++)
				{
					k = weekday() - 1;
					uint16_t color = matrix->Color(80, 80, 80);
					if (k==0) k=7;
					if (j==k) color = matrix->Color(255, 255, 255);
					matrix->drawLine( 4*j-1, 7, 4*j+1, 7, color);
				}
				matrix->show();
				delay(60);
			}
			process = 2;
			break;
		case 2: /* date show */
			if ((millis() - timeStamp) > 1000)
			{
				timeStamp = millis();
				counter++;

				matrix->clear();
				sprintf(buffer, "%02d.%02d.", month(), day());
				matrix->setTextColor(matrix->Color(0, 0, 255));
				matrix->setCursor(8, 6);
				matrix->print(buffer);
				matrix->drawLine( 0, 7, 31, 7, matrix->Color(00, 00, 00));
				for (j=1; j<8; j++)
				{
					k = weekday() - 1;
					uint16_t color = matrix->Color(80, 80, 80);
					if (k==0) k=7;
					if (j==k) color = matrix->Color(255, 255, 255);
					matrix->drawLine( 4*j-1, 7, 4*j+1, 7, color);
				}
				matrix->show();
				//delay(1000);
				if (counter >= 10)
				{
					process = 3;
					counter = 1;
				}
			}
			break;
		case 3: /* fade out */
			for (i=1; i<=10; i++)
			{
				int b = newBri * (1 - (0.1 * i));
				matrix->setBrightness(b);
				matrix->show();
				delay(20);
			}

			process = 0;
			counter = 1;
			timeStamp = 0;
			colon = true;
			fade = true;
			inFade = true;
			break;
	}
}

void setup()
{
	delay(2000);

	for (int i = 0; i < tasterCount; i++)
	{
		pinMode(tasterPin[i], INPUT_PULLUP);
	}

	Serial.setRxBufferSize(1024);
	Serial.begin(115200);
	mySoftwareSerial.begin(9600);

	if (LittleFS.begin())
	{
		//if file not exists
		if (!(LittleFS.exists("/awtrix.json")))
		{
			LittleFS.open("/awtrix.json", "w+");
		}

		File configFile = LittleFS.open("/awtrix.json", "r");
		if (configFile)
		{
			size_t size = configFile.size();
			// Allocate a buffer to store contents of the file.
			std::unique_ptr<char[]> buf(new char[size]);
			configFile.readBytes(buf.get(), size);
			DynamicJsonBuffer jsonBuffer;
			JsonObject &json = jsonBuffer.parseObject(buf.get());
			if (json.success())
			{

				strcpy(awtrix_server, json["awtrix_server"]);

				matrixTempCorrection = json["matrixCorrection"].as<int>();

				if (json.containsKey("matrixType"))
				{
					matrixType = json["matrixType"].as<int>();
				}

				if (json.containsKey("Port"))
				{
					strcpy(Port, json["Port"]);
				}

				if (json.containsKey("ldr"))
				{
					autoBrightness = json["ldr"].as<int>();
				}

				if (json.containsKey("minBri"))
				{
					minBrightness = json["minBri"].as<int>();
				}

				if (json.containsKey("maxBri"))
				{
					maxBrightness = json["maxBri"].as<int>();
				}
			}
			configFile.close();
		}
	}
	else
	{
		//error
	}
	Serial.println("matrixType");
	Serial.println(matrixType);
	switch (matrixType)
	{
	case 0:
		matrix = new FastLED_NeoMatrix(leds, 32, 8, NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG);
		break;
	case 1:
		matrix = new FastLED_NeoMatrix(leds, 8, 8, 4, 1, NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_ROWS + NEO_MATRIX_PROGRESSIVE);
		break;
	case 2:
		matrix = new FastLED_NeoMatrix(leds, 32, 8, NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG);
		break;
	default:
		matrix = new FastLED_NeoMatrix(leds, 32, 8, NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG);
		break;
	}

	switch (matrixTempCorrection)
	{
	case 0:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setCorrection(TypicalLEDStrip);
		break;
	case 1:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(Candle);
		break;
	case 2:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(Tungsten40W);
		break;
	case 3:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(Tungsten100W);
		break;
	case 4:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(Halogen);
		break;
	case 5:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(CarbonArc);
		break;
	case 6:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(HighNoonSun);
		break;
	case 7:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(DirectSunlight);
		break;
	case 8:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(OvercastSky);
		break;
	case 9:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(ClearBlueSky);
		break;
	case 10:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(WarmFluorescent);
		break;
	case 11:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(StandardFluorescent);
		break;
	case 12:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(CoolWhiteFluorescent);
		break;
	case 13:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(FullSpectrumFluorescent);
		break;
	case 14:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(GrowLightFluorescent);
		break;
	case 15:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(BlackLightFluorescent);
		break;
	case 16:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(MercuryVapor);
		break;
	case 17:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(SodiumVapor);
		break;
	case 18:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(MetalHalide);
		break;
	case 19:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(HighPressureSodium);
		break;
	case 20:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setTemperature(UncorrectedTemperature);
		break;
	default:
		FastLED.addLeds<NEOPIXEL, D2>(leds, 256).setCorrection(TypicalLEDStrip);
		break;
	}

	matrix->begin();
	matrix->setTextWrap(false);
	matrix->setBrightness(30);
	matrix->setFont(&TomThumb);
	//Reset with Tasters...
	int zeit = millis();
	int zahl = 5;
	int zahlAlt = 6;
	matrix->clear();
	matrix->setTextColor(matrix->Color(255, 0, 255));
	matrix->setCursor(9, 6);
	matrix->print("BOOT");
	matrix->show();
	delay(2000);
	while (!digitalRead(D4))
	{
		if (zahl != zahlAlt)
		{
			matrix->clear();
			matrix->setTextColor(matrix->Color(255, 0, 0));
			matrix->setCursor(6, 6);
			matrix->print("RESET ");
			matrix->print(zahl);
			matrix->show();
			zahlAlt = zahl;
		}
		zahl = 5 - ((millis() - zeit) / 1000);
		if (zahl == 0)
		{
			matrix->clear();
			matrix->setTextColor(matrix->Color(255, 0, 0));
			matrix->setCursor(6, 6);
			matrix->print("RESET!");
			matrix->show();
			delay(1000);
			if (LittleFS.begin())
			{
				delay(1000);
				LittleFS.remove("/awtrix.json");

				LittleFS.end();
				delay(1000);
			}
			wifiManager.resetSettings();
			ESP.reset();
		}
	}
	/*
		if (drd.detect())
		{
			//Serial.println("** Double reset boot **");
			matrix->clear();
			matrix->setTextColor(matrix->Color(255, 0, 0));
			matrix->setCursor(6, 6);
			matrix->print("RESET!");
			matrix->show();
			delay(1000);
			if (LittleFS.begin())
			{
				delay(1000);
				LittleFS.remove("/awtrix.json");

				LittleFS.end();
				delay(1000);
			}
			wifiManager.resetSettings();
			ESP.reset();
		}
		*/

	wifiManager.setAPStaticIPConfig(IPAddress(172, 217, 28, 1), IPAddress(172, 217, 28, 1), IPAddress(255, 255, 255, 0));
	WiFiManagerParameter custom_awtrix_server("server", "AWTRIX Host", awtrix_server, 16);
	WiFiManagerParameter custom_port("Port", "Matrix Port", Port, 6);
	WiFiManagerParameter custom_matrix_type("matrixType", "MatrixType", "0", 1);
	// Just a quick hint
	WiFiManagerParameter host_hint("<small>AWTRIX Host IP (without Port)<br></small><br><br>");
	WiFiManagerParameter port_hint("<small>Communication Port (default: 7001)<br></small><br><br>");
	WiFiManagerParameter matrix_hint("<small>0: Columns; 1: Tiles; 2: Rows <br></small><br><br>");
	WiFiManagerParameter p_lineBreak_notext("<p></p>");

	wifiManager.setSaveConfigCallback(saveConfigCallback);
	wifiManager.setAPCallback(configModeCallback);

	wifiManager.addParameter(&p_lineBreak_notext);
	wifiManager.addParameter(&host_hint);
	wifiManager.addParameter(&custom_awtrix_server);
	wifiManager.addParameter(&port_hint);
	wifiManager.addParameter(&custom_port);
	wifiManager.addParameter(&matrix_hint);
	wifiManager.addParameter(&custom_matrix_type);
	wifiManager.addParameter(&p_lineBreak_notext);

	//wifiManager.setCustomHeadElement("<style>html{ background-color: #607D8B;}</style>");

	hardwareAnimatedSearch(0, 24, 0);

	if (!wifiManager.autoConnect("AWTRIX Controller", "awtrixxx"))
	{
		//reset and try again, or maybe put it to deep sleep
		ESP.reset();
		delay(5000);
	}

	//is needed for only one hotpsot!
	WiFi.mode(WIFI_STA);

	server.on("/", HTTP_GET, []()
			  {
				  server.sendHeader("Connection", "close");
				  //server.send(200, "text/html", serverIndex);
				  server.send(200, "text/html", handleRoot());
			  });

	server.on("/reset", HTTP_GET, []()
			  {
				  wifiManager.resetSettings();
				  ESP.reset();
				  //server.send(200, "text/html", serverIndex);
				  server.send(200, "text/html", handleRoot());
			  });
	server.on(
		"/update", HTTP_POST, []()
		{
			server.sendHeader("Connection", "close");
			server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
			ESP.restart();
		},
		[]()
		{
			HTTPUpload &upload = server.upload();

			if (upload.status == UPLOAD_FILE_START)
			{
				Serial.setDebugOutput(true);

				uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
				if (!Update.begin(maxSketchSpace))
				{ //start with max available size
					Update.printError(Serial);
				}
			}
			else if (upload.status == UPLOAD_FILE_WRITE)
			{
				matrix->clear();
				flashProgress((int)upload.currentSize, (int)upload.buf);
				if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
				{
					Update.printError(Serial);
				}
			}
			else if (upload.status == UPLOAD_FILE_END)
			{
				if (Update.end(true))
				{ //true to set the size to the current progress
					server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
				}
				else
				{
					Update.printError(Serial);
				}
				Serial.setDebugOutput(false);
			}
			yield();
		});

	server.on("/setup", HTTP_POST, [&]() {
		String newSSID = server.arg("ssid");
		String newPassword = server.arg("password");

		// Process the new credentials as needed

		server.send(200, "text/plain", "Setup complete. Restarting...");
		delay(1000);
		ESP.restart();
		});

	server.begin();

	if (shouldSaveConfig)
	{

		strcpy(awtrix_server, custom_awtrix_server.getValue());
		matrixType = atoi(custom_matrix_type.getValue());
		strcpy(Port, custom_port.getValue());
		saveConfig();
		ESP.reset();
	}

	hardwareAnimatedCheck(MsgType_Wifi, 27, 2);

	delay(1000); //is needed for the dfplayer to startup

	//Checking periphery
	Wire.begin(I2C_SDA, I2C_SCL);
	if (BMESensor.begin())
	{
		//temp OK
		tempState = TempSensor_BME280;
		hardwareAnimatedCheck(MsgType_Temp, 29, 2);
	}
	else if (htu.begin())
	{
		tempState = TempSensor_HTU21D;
		hardwareAnimatedCheck(MsgType_Temp, 29, 2);
	}
	else if (BMPSensor.begin(BMP280_ADDRESS_ALT) || BMPSensor.begin(BMP280_ADDRESS))
	{

		/* Default settings from datasheet. */
		BMPSensor.setSampling(Adafruit_BMP280::MODE_NORMAL,		/* Operating Mode. */
							  Adafruit_BMP280::SAMPLING_X2,		/* Temp. oversampling */
							  Adafruit_BMP280::SAMPLING_X16,	/* Pressure oversampling */
							  Adafruit_BMP280::FILTER_X16,		/* Filtering. */
							  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */
		tempState = TempSensor_BMP280;
		hardwareAnimatedCheck(MsgType_Temp, 29, 2);
	}

	dfmp3.begin();

	if (dfmp3.isOnline())
	{
		hardwareAnimatedCheck(MsgType_Audio, 29, 2);
	}
	dfmp3.reset();

	if (analogRead(LDR_PIN) > 1)
	{
		hardwareAnimatedCheck(MsgType_LDR, 29, 2);
	}

	ArduinoOTA.onStart([&]()
					   {
						   updating = true;
						   matrix->clear();
					   });

	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
						  { flashProgress(progress, total); });

	ArduinoOTA.begin();

	matrix->clear();
	matrix->setCursor(7, 6);

	bufferpointer = 0;

	myTime = millis() - 500;
	myTime2 = millis() - 1000;
	myTime3 = millis() - 500;
	myCounter = 0;
	myCounter2 = 0;

	//if (!appRun)
	{
	for (int x = 32; x >= -90; x--)
	{
		matrix->clear();
		matrix->setCursor(x, 6);
		matrix->print("Host-IP: " + String(awtrix_server) + ":" + String(Port));
		matrix->setTextColor(matrix->Color(0, 255, 50));
		matrix->show();
		delay(20);
	}

	client.setServer(awtrix_server, atoi(Port));
	client.setCallback(callback);
	}

    int offset = getTimeOffset();
	timeClient.setTimeOffset(offset);
    timeClient.begin();

	ignoreServer = false;

	connectionTimout = millis();
}

void loop()
{
	server.handleClient();
	ArduinoOTA.handle();

	//is needed for the server search animation
	if (!appRun)
	{
	if (firstStart && !ignoreServer)
	{
		if (millis() - myTime > 500)
		{
			serverSearch(myCounter, 0, 28, 0);
			myCounter++;
			if (myCounter == 4)
			{
				myCounter = 0;
			}
			myTime = millis();
		}
	}
	}
	//not during the falsh process
	if (!updating)
	{
		if (USBConnection || firstStart)
		{
			int x = 100;
			while (x >= 0)
			{
				x--;
				//USB
				if (Serial.available() > 0)
				{
					//read and fill in ringbuffer
					myBytes[bufferpointer] = Serial.read();
					messageLength--;
					for (int i = 0; i < 14; i++)
					{
						if ((bufferpointer - i) < 0)
						{
							myPointer[i] = 1000 + bufferpointer - i;
						}
						else
						{
							myPointer[i] = bufferpointer - i;
						}
					}
					//prefix from "awtrix" == 6?
					if (myBytes[myPointer[13]] == 0 && myBytes[myPointer[12]] == 0 && myBytes[myPointer[11]] == 0 && myBytes[myPointer[10]] == 6)
					{
						//"awtrix" ?
						if (myBytes[myPointer[9]] == 97 && myBytes[myPointer[8]] == 119 && myBytes[myPointer[7]] == 116 && myBytes[myPointer[6]] == 114 && myBytes[myPointer[5]] == 105 && myBytes[myPointer[4]] == 120)
						{
							messageLength = (int(myBytes[myPointer[3]]) << 24) + (int(myBytes[myPointer[2]]) << 16) + (int(myBytes[myPointer[1]]) << 8) + int(myBytes[myPointer[0]]);
							SavemMessageLength = messageLength;
							awtrixFound = true;
						}
					}

					if (awtrixFound && messageLength == 0)
					{
						byte tempData[SavemMessageLength];
						int up = 0;
						for (int i = SavemMessageLength - 1; i >= 0; i--)
						{
							if ((bufferpointer - i) >= 0)
							{
								tempData[up] = myBytes[bufferpointer - i];
							}
							else
							{
								tempData[up] = myBytes[1000 + bufferpointer - i];
							}
							up++;
						}
						USBConnection = true;
						updateMatrix(tempData, SavemMessageLength);
						awtrixFound = false;
					}
					bufferpointer++;
					if (bufferpointer == 1000)
					{
						bufferpointer = 0;
					}
				}
				else
				{
					break;
				}
			}
		}
		//Wifi
		if (WIFIConnection || firstStart)
		{
			if (!appRun)
			{
			//Serial.println("wifi oder first...");
			if (!client.connected())
			{
				//Serial.println("nicht verbunden...");
				reconnect();
				if (WIFIConnection)
				{
					USBConnection = false;
					WIFIConnection = false;
					firstStart = true;
				}
			}
			else
			{
				client.loop();
			}
			}
			timeClient.update();
		}

		if (millis() - connectionTimout > 20000)
		{
			USBConnection = false;
			WIFIConnection = false;
			firstStart = true;
		}
	}

	checkTaster(0);
	checkTaster(1);
	checkTaster(2);
	//checkTaster(3);

	//is needed for the menue...
	if (!appRun)
	{
	if (ignoreServer)
	{
		if (pressedTaster > 0)
		{
			matrix->clear();
			matrix->setCursor(0, 6);
			matrix->setTextColor(matrix->Color(0, 255, 50));
			//matrix->print(myMenue.getMenueString(&menuePointer, &pressedTaster, &minBrightness, &maxBrightness));
			matrix->show();
		}

		//get data and ignore
		if (Serial.available() > 0)
		{
			Serial.read();
		}
	}
	}


	if (appRun)
	{
		checkLDR();
		if (appRunClock) appClock(0);
		if (appRunAlert) appAlert();
	}

	if (Serial.available() > 0)
	{
		char buff[50];
		int input = Serial.read();
		switch (input)
		{
			case '1': // enable/disable debug
				dbg_updateMatrix = !dbg_updateMatrix;
				break;

			case '2': // Clock APP
				appClock(0);
				break;

			case '3':
				Serial.println("Station IP address: " + WiFi.localIP().toString());
				break;

			case '4':
				{
				HTTPClient http;
				int httpCode;
				//http.begin(espClient,"http://worldtimeapi.org/api/ip/");
				http.begin("http://worldtimeapi.org/api/ip/");
				httpCode = http.GET();
				if (httpCode == HTTP_CODE_OK) {
					//DynamicJsonDocument jsonDocOffset(1024);
					//error = deserializeJson(jsonDocOffset, );
					DynamicJsonBuffer jsonBuffer;
					JsonObject &json = jsonBuffer.parseObject(http.getString());

					if (json.success())
					{
						// Extract the time offset
						//json["raw_offset"].as<int>();
						String tempString = json["raw_offset"];
						Serial.println("Time offset = "+tempString);
					} else {
						Serial.println("Failed to parse JSON for time offset");
					}
				} else {
					Serial.println("Failed to fetch time offset");
				}
				http.end();
				}
				break;

			case '5':
				ignoreServer = !ignoreServer;
				break;

			case '6':
				appRun = !appRun;
				break;
			case '7':
				dfmp3.playMp3FolderTrack(4); /* enter */
				//dfmp3.loopFolder(1);
				break;
			case '8':
			    dfmp3.playMp3FolderTrack(3); /* click */
				//dfmp3.stop();
				break;
			case '9':
				{
					float temp=0, hum=0, hpa=0;
					switch (tempState)
					{
						case TempSensor_BME280:
							BMESensor.refresh();
							temp = BMESensor.temperature;
							hum = BMESensor.humidity;
							hpa = BMESensor.pressure;
							break;
						case TempSensor_HTU21D:
							temp = htu.readTemperature();
							hum = htu.readHumidity();
							hpa = 0;
							break;
						case TempSensor_BMP280:
							sensors_event_t temp_event, pressure_event;
							BMPSensor.getTemperatureSensor()->getEvent(&temp_event);
							BMPSensor.getPressureSensor()->getEvent(&pressure_event);
							temp = temp_event.temperature;
							hum = 0;
							hpa = pressure_event.pressure;
							break;
					}
					sprintf(buff,"TempState(%d): temp(%f), hum(%f), hpa(%f)",tempState, temp, hum, hpa);
					Serial.println(buff);
				}
				break;

			case '0':
				{
					tasterState[0] = !digitalRead(tasterPin[0]);
					tasterState[1] = digitalRead(tasterPin[1]);
					tasterState[2] = !digitalRead(tasterPin[2]);
					sprintf(buff,"PIN0(%d) PIN1(%d) PIN2(%d)", tasterState[0], tasterState[1], tasterState[2]);
					Serial.println(buff);
				}
				break;

			case 'a':
				{
					dfmp3.setRepeatPlayCurrentTrack(true);
					dfmp3.playMp3FolderTrack(10);
				}
				break;

			case 'b':
				{
					dfmp3.setRepeatPlayCurrentTrack(false);
					dfmp3.stop();
				}
				break;

			case 'c':
				{

				}
				break;

			case 'd':
				{
					dfmp3.decreaseVolume();
				}
				break;

			case 'e':
				{
					dfmp3.increaseVolume();
				}
				break;

			case 'f':
				{
					appAlertFlag = !appAlertFlag;
				}
				break;
			// Alart APP
			// Weather APP
			default:
				break;
		}
	}
}


