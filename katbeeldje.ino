#include <ArduinoOTA.h>

#include <ESP8266WiFi.h>

#include <LittleFS.h>
#include <configure.h>
#include <wifi.h>

#include <string>

#include <PubSubClient.h>

#include <ESP8266mDNS.h>

const char name[] = "KatBeeldje";

WiFiClient   wclient;
PubSubClient client(wclient);

bool updated = false;

void callback(const char topic[], byte *payload, unsigned int len) {
	Serial.println(topic);

	if (!payload || len == 0)
		return;

	// TODO: check rxpk or txpk?

	updated = true;
}

bool progress_indicator(const int nr, const int mx, const std::string & which) {
	printf("%3.2f%%: %s\r\n", nr * 100. / mx, which.c_str());

	digitalWrite(D1, !digitalRead(D1));
	digitalWrite(D2, !digitalRead(D2));

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

			if (client.connect(name)) {
				Serial.println(F("Connected"));
				break;
			}

			delay(1000);
		}

		Serial.println(F("MQTT Connected!"));

		if (client.subscribe("lorawan/uplink") == false)
			Serial.println("subscribe failed");
		else
			Serial.println("subscribed");

		client.loop();
	}
}

void enableOTA() {
	ArduinoOTA.setPort(8266);
	ArduinoOTA.setHostname(name);
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
	set_hostname(name);

	enable_wifi_debug();

	scan_access_points_start();

	if (!LittleFS.begin())
		printf("LittleFS.begin() failed\r\n");

	configure_wifi cw;

	if (cw.is_configured() == false) {
retry:
		start_wifi(name);  // enable wifi with AP (empty string for no AP)

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

	if (!MDNS.begin(name))
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

unsigned long when = 0;

void loop() {
	digitalWrite(LED_BUILTIN, !!(millis() & 512));

	ArduinoOTA.handle();

	MQTT_connect();

	unsigned long now = millis();

	if (updated) {
		updated = false;

		when = now;
	}

	if (when) {
		uint16_t l_fade_from = 1023;  // TODO: left eye rx pkts, right eye tx pkts
		uint16_t r_fade_from = 1023;
		uint16_t l_fade_to   = 0;
		uint16_t r_fade_to   = 0;

		int16_t distance = now - when;

		if (distance >= 1024)
			when = 0;
		else {
			analogWrite(D1, l_fade_from + double(l_fade_to - l_fade_from) * distance / 1023);
			analogWrite(D2, r_fade_from + double(r_fade_to - r_fade_from) * distance / 1023);
		}
	}
}
