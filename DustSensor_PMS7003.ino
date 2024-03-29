#include <Arduino.h>
#include <DHT.h>
#include <ESP8266WiFi.h>
#include <PMS.h>
#include "my_secret.h"

// To use Deep Sleep connect RST to GPIO16 (D0) and uncomment below line.
// #define DEEP_SLEEP

// GPIO2 (D4 pin on ESP-12E Development Board).
#define DEBUG_OUT Serial1

// PMS_READ_INTERVAL (4:30 min) and PMS_READ_DELAY (30 sec) CAN'T BE EQUAL! Values are also used to detect sensor state.
static const uint32_t PMS_READ_INTERVAL = 265000;
//static const uint32_t PMS_READ_INTERVAL = 10000;
static const uint32_t PMS_READ_DELAY = 30000;

// Default sensor state.
uint32_t timerInterval = PMS_READ_DELAY;

PMS pms(Serial);

/*
 * Wifi Stuff
 */
// Your WiFi credentials.
// Set password to "" for open networks.
char ssid[] = MY_SECRET_SSID;
char pass[] = MY_SECRET_PASSWORD;

/*
 * DHT stuff
 */
#define DHTPIN D1
#define DHTTYPE DHT22 // DHT 22 Change this if you have a DHT11

DHT dht(DHTPIN, DHTTYPE);
float d, fd, c, t, h;
unsigned long previousMillis = 0; // will store last temp was read
const long interval = 2000;       // interval at which to read sensor

/*
 * thingspeak stuff
 */
WiFiClient client;
String thingSpeakApiKey = MY_SECRET_THING_SPEAK_WRITE_API_KEY;
const char *server = "api.thingspeak.com";

/*
 * Common stuff
 */
#define SENSOR_GET_ITERATION_NUMBER 10

void setup()
{
	// GPIO1, GPIO3 (TX/RX pin on ESP-12E Development Board)
	Serial.begin(PMS::BAUD_RATE);
	DEBUG_OUT.begin(9600);
	while (!DEBUG_OUT)
		;

	thingSpeakSetup();
	dht.begin();

	// Switch to passive mode.
	pms.passiveMode();

	// Default state after sensor power, but undefined after ESP restart e.g. by OTA flash, so we have to manually wake up the sensor for sure.
	// Some logs from bootloader is sent via Serial port to the sensor after power up. This can cause invalid first read or wake up so be patient and wait for next read cycle.
	pms.wakeUp();

#ifdef DEEP_SLEEP
	delay(PMS_READ_DELAY);
	readData();
	pms.sleep();
	ESP.deepSleep(PMS_READ_INTERVAL * 1000);
#endif // DEEP_SLEEP
}

void loop()
{
#ifndef DEEP_SLEEP
	static uint32_t timerLast = 0;

	uint32_t timerNow = millis();
	if (timerNow - timerLast >= timerInterval)
	{
		timerLast = timerNow;
		timerCallback();
		timerInterval = timerInterval == PMS_READ_DELAY ? PMS_READ_INTERVAL : PMS_READ_DELAY;
	}

	// Do other stuff...
#endif // DEEP_SLEEP
}

#ifndef DEEP_SLEEP
void timerCallback()
{
	if (timerInterval == PMS_READ_DELAY)
	{
		readData(SENSOR_GET_ITERATION_NUMBER);
		DEBUG_OUT.println("Going to PMS sleep.");
		pms.sleep();
		getTempHumidity(SENSOR_GET_ITERATION_NUMBER);
		sendDataToThingSpeak(d, fd, 0.0, t, h);

		DEBUG_OUT.print("Dust Level: ");
		DEBUG_OUT.print(d);
		DEBUG_OUT.println("㎍/㎥");

		DEBUG_OUT.print("Fine Dust Level: ");
		DEBUG_OUT.print(fd);
		DEBUG_OUT.println("㎍/㎥");

		DEBUG_OUT.print("Temperature: ");
		DEBUG_OUT.print(t);
		DEBUG_OUT.println(" Celsius");

		DEBUG_OUT.print("Humidity: ");
		DEBUG_OUT.print(h);
		DEBUG_OUT.println(" %");
	}
	else
	{
		DEBUG_OUT.println("Waking up.");
		pms.wakeUp();
	}
}
#endif // DEEP_SLEEP

void readData(int iter)
{
	PMS::DATA data;
	int i;
	float temp_fd = 0.0f;
	float temp_d = 0.0f;
	int read_count = 0;

	// Clear buffer (removes potentially old data) before read. Some data could have been also sent before switching to passive mode.
	while (Serial.available())
	{
		Serial.read();
	}

	for (i = 0; i < iter; i++)
	{
		DEBUG_OUT.println("Send read request...");
		pms.requestRead();

		DEBUG_OUT.println("Reading data...");
		if (pms.readUntil(data))
		{
			DEBUG_OUT.println();

			DEBUG_OUT.print("PM 1.0 (ug/m3): ");
			DEBUG_OUT.println(data.PM_AE_UG_1_0);

			DEBUG_OUT.print("PM 2.5 (ug/m3): ");
			DEBUG_OUT.println(data.PM_AE_UG_2_5);
			temp_fd += data.PM_AE_UG_2_5;

			DEBUG_OUT.print("PM 10.0 (ug/m3): ");
			DEBUG_OUT.println(data.PM_AE_UG_10_0);
			temp_d += data.PM_AE_UG_10_0;

			DEBUG_OUT.println();
			read_count++;
		}
		else
		{
			DEBUG_OUT.println("No data.");
		}
		delay(1000);
	}

	fd = temp_fd / read_count;
	d = temp_d / read_count;
}

/*************************************************************************/

/*
 * DHT stuff
 */
void getTempHumidity(int iter)
{
	// Wait at least 2 seconds seconds between measurements.
	// if the difference between the current time and last time you read
	// the sensor is bigger than the interval you set, read the sensor
	// Works better than delay for things happening elsewhere also
	int i;
	float temp_h = 0.0f;
	float temp_t = 0.0f;
	float sum_h = 0.0f;
	float sum_t = 0.0f;
	int read_count = 0;

	for (i = 0; i < iter; i++)
	{
		unsigned long currentMillis = millis();

		if (currentMillis - previousMillis >= interval)
		{
			// save the last time you read the sensor
			previousMillis = currentMillis;

			// Reading temperature for humidity takes about 250 milliseconds!
			// Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
			temp_h = dht.readHumidity();    // Read humidity (percent)
			temp_t = dht.readTemperature(); // Read temperature as
			// Check if any reads failed and exit early (to try again).
			if (isnan(temp_h) || isnan(temp_t))
			{
				DEBUG_OUT.println("Failed to read from DHT sensor!");
				continue;
			}

			DEBUG_OUT.print("humidity : ");
			DEBUG_OUT.print(temp_h, 1);
			DEBUG_OUT.print("\t\t");
			DEBUG_OUT.print("Temperature : ");
			DEBUG_OUT.println(temp_t, 1);

			sum_h += temp_h;
			sum_t += temp_t;

			read_count++;
		}
		delay(2000);
	}

	t = sum_t / read_count;
	h = sum_h / read_count;
}
/*************************************************************************/

/*
 * thingspeak stuff
 */

void thingSpeakSetup()
{
	WiFi.begin(ssid, pass);

	DEBUG_OUT.println("WiFi connecting ");
	while (WiFi.status() != WL_CONNECTED)
	{
		delay(1000);
		DEBUG_OUT.print(".");
	}
	DEBUG_OUT.println("");
	DEBUG_OUT.println("WiFi connected");
}

void sendDataToThingSpeak(float dustLevel, float fineDustLevel, float co2, float temperature, float humidity)
{
	DEBUG_OUT.println("Sending Result Data to IoT Cloud");
	if (client.connect(server, 80))
	{
		//"184.106.153.149" or api.thingspeak.com
		String postStr = thingSpeakApiKey;
		postStr += "&field1=";
		postStr += String(dustLevel);
		postStr += "&field2=";
		postStr += String(fineDustLevel);
		postStr += "&field3=";
		postStr += String(co2);
		postStr += "&field4=";
		postStr += String(temperature);
		postStr += "&field5=";
		postStr += String(humidity);

		client.print("POST /update HTTP/1.1\n");
		client.print("Host: api.thingspeak.com\n");
		client.print("Connection: close\n");
		client.print("X-THINGSPEAKAPIKEY: " + thingSpeakApiKey + "\n");
		client.print("Content-Type: application/x-www-form-urlencoded\n");
		client.print("Content-Length: ");
		client.print(postStr.length());
		client.print("\n\n");
		client.print(postStr);
	}
	else
	{
		DEBUG_OUT.println("Failed to connect to thingspeak");
	}

	client.stop();
	DEBUG_OUT.println("Done to send");
}