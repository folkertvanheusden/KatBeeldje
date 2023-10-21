#include <ArduinoOTA.h>

#include <ESP8266WiFi.h>

#include <LittleFS.h>
#include <configure.h>
#include <wifi.h>

#include <string>

#include <PubSubClient.h>

#include <ESP8266mDNS.h>

WiFiClient wclient;
PubSubClient client(wclient);

double price     = 0;
double new_price = 0;
bool   updated   = false;

void callback(const char topic[], byte *payload, unsigned int len) {
	Serial.println(topic);

	if (!payload || len == 0)
		return;

	std::string pls(reinterpret_cast<const char *>(payload), len);  // workaround for not 0x00-terminated payload

	new_price = atoi(pls.c_str());

	printf("new price: %.2f\r\n", new_price);

	updated = true;
}

bool progress_indicator(const int nr, const int mx, const std::string & which) {
	printf("%3.2f%%: %s\r\n", nr * 100. / mx, which.c_str());

	digitalWrite(D1, !analogRead(D1));
	digitalWrite(D2, !analogRead(D2));

	return true;
}

void reboot() {
	Serial.println(F("Reboot"));
	Serial.flush();
	delay(100);
	ESP.restart();
	delay(1000);
}

void MQTT_connect() {
	client.loop();

	if (!client.connected()) {
		while (!client.connected()) {
			Serial.println("Attempting MQTT connection... ");

			if (client.connect("KatBeeldje")) {
				Serial.println(F("Connected"));
				break;
			}

			delay(1000);
		}

		Serial.println(F("MQTT Connected!"));

		if (client.subscribe("vanheusden/bitcoin/bitstamp_usd") == false)
			Serial.println("subscribe failed");
		else
			Serial.println("subscribed");

		client.loop();
	}
}

void enableOTA() {
	ArduinoOTA.setPort(8266);
	ArduinoOTA.setHostname("KatBeeldje");
	ArduinoOTA.setPassword("iojasdsjiasd");

	ArduinoOTA.onStart([]() {
			Serial.println(F("OTA start"));
			});
	ArduinoOTA.onEnd([]() {
			Serial.println(F("OTA end"));
			});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
			Serial.printf("OTA progress: %u%%\r", (progress / (total / 100)));
			});
	ArduinoOTA.onError([](ota_error_t error) {
			Serial.printf("Error[%u]: ", error);
			if (error == OTA_AUTH_ERROR) Serial.println(F("Auth Failed"));
			else if (error == OTA_BEGIN_ERROR) Serial.println(F("Begin Failed"));
			else if (error == OTA_CONNECT_ERROR) Serial.println(F("Connect Failed"));
			else if (error == OTA_RECEIVE_ERROR) Serial.println(F("Receive Failed"));
			else if (error == OTA_END_ERROR) Serial.println(F("End Failed"));
			});
	ArduinoOTA.begin();

	Serial.println(F("Ready"));
}

void setup_wifi() {
	set_hostname("KatBeeldje");

	enable_wifi_debug();

	scan_access_points_start();

	if (!LittleFS.begin())
		printf("LittleFS.begin() failed\r\n");

	configure_wifi cw;

	if (cw.is_configured() == false) {
retry:
		start_wifi("KatBeeldje");  // enable wifi with AP (empty string for no AP)

		digitalWrite(D1, HIGH);
		digitalWrite(D2, HIGH);

		cw.configure_aps();

		digitalWrite(D1, LOW);
		digitalWrite(D2, LOW);
	}
	else {
		start_wifi("");
	}

	// see what we can see
	printf("scan\r\n");
	scan_access_points_start();

	while(scan_access_points_wait() == false)
		delay(100);

	auto available_access_points = scan_access_points_get();

	// try to connect
	printf("connect\r\n");

	auto state = try_connect_init(cw.get_targets(), available_access_points, 300, progress_indicator);

	connect_status_t cs = CS_IDLE;

	for(;;) {
		cs = try_connect_tick(state);

		if (cs != CS_IDLE)
			break;

		delay(100);
	}

	// could not connect, restart esp
	// you could also re-run the portal
	if (cs == CS_FAILURE) {
		Serial.println(F("Failed to connect."));

		goto retry;

#if defined(ESP32)
		ESP.restart();
#else
		ESP.reset();
#endif
	}

	// connected!
	Serial.println(F("Connected!"));

	WiFi.setSleep(false);

	enableOTA();

	if (!MDNS.begin("KatBeeldje"))
		Serial.println(F("Error setting up MDNS responder!"));
}

void setup() {
	Serial.begin(115200);

	Serial.setDebugOutput(true);

	Serial.println(F("Init"));

	pinMode(D1, OUTPUT);
	digitalWrite(D1, HIGH);

	pinMode(D2, OUTPUT);
	digitalWrite(D2, HIGH);

	delay(1);

	pinMode(LED_BUILTIN, OUTPUT);

	setup_wifi();

	digitalWrite(D1, LOW);
	digitalWrite(D2, LOW);

	Serial.printf("Now at IP %s\r\n", WiFi.localIP().toString().c_str());

	client.setServer("vps001.vanheusden.com", 1883);
	client.setCallback(callback);
}

void loop() {
	digitalWrite(LED_BUILTIN, !!(millis() & 512));

	ArduinoOTA.handle();

	MQTT_connect();

	if (updated) {
		Serial.println(millis());

		bool left_eye  = new_price >= price;
		bool right_eye = new_price >  price;

		uint16_t l_fade_from = digitalRead(D1) ? 1023 : 0;
		uint16_t r_fade_from = digitalRead(D2) ? 1023 : 0;
		uint16_t l_fade_to   = left_eye  ? 1023 : 0;
		uint16_t r_fade_to   = right_eye ? 1023 : 0;

		for(int i=0; i<1024; i++) {
			analogWrite(D1, l_fade_from + double(l_fade_to - l_fade_from) * i / 1023);
			analogWrite(D2, r_fade_from + double(r_fade_to - r_fade_from) * i / 1023);
			delay(1);
		}

		digitalWrite(D1, left_eye);
		digitalWrite(D2, right_eye);

		price = new_price;

		updated = false;
	}
}
