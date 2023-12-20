#include <HTTPClient.h>

#include "id.h"
#include "display.h"
#include "device.h"
#include "inet.h"

/* ************************************************************************** */

namespace WIFI {
	void initialize(void) {
		if (enable_gateway) {
			WiFi.mode(WIFI_STA);
			WiFi.begin(WIFI_SSID, WIFI_PASS);
		}
		else {
			WiFi.mode(WIFI_OFF);
		}
	}

	static class String status_message(signed int const WiFi_status) {
		switch (WiFi_status) {
		case WL_NO_SHIELD:
			return String("WiFi no shield");
		case WL_IDLE_STATUS:
			return String("WiFi idle");
		case WL_NO_SSID_AVAIL:
			return String("WiFi no SSID");
		case WL_SCAN_COMPLETED:
			return String("WiFi scan completed");
		case WL_CONNECTED:
			return String("WiFi connected");
		case WL_CONNECT_FAILED:
			return String("WiFi connect failed");
		case WL_CONNECTION_LOST:
			return String("WiFi connection lost");
		case WL_DISCONNECTED:
			return String("WiFi disconnected");
		default:
			return String("WiFi Status: ") + String(WiFi_status);
		}
	}

	bool upload(Device const device, SerialNumber const serial, struct Data const *const data) {
		signed int const WiFi_status = WiFi.status();
		if (WiFi_status != WL_CONNECTED) {
			OLED_LOCK(oled_lock);
			Display::print("No WiFi: ");
			Display::println(status_message(WiFi.status()));
			return false;
		}
		class String const time = String(data->time);
		class HTTPClient HTTP_client;
		char URL[HTTP_UPLOAD_LENGTH];
		snprintf(
			URL, sizeof URL,
			HTTP_UPLOAD_FORMAT,
			device, serial, time.c_str()
			#ifdef ENABLE_BATTERY_GAUGE
				, data->battery_voltage
				, data->battery_percentage
			#endif
			#ifdef ENABLE_DALLAS
				, data->dallas_temperature
			#endif
			#ifdef ENABLE_SHT40
				, data->sht40_temperature
				, data->sht40_humidity
			#endif
			#ifdef ENABLE_BME280
				, data->bme280_temperature
				, data->bme280_pressure
				, data->bme280_humidity
			#endif
			#ifdef ENABLE_LTR390
				, data->ltr390_ultraviolet
			#endif
		);
		COM::print("Upload to ");
		COM::println(URL);
		HTTP_client.begin(URL);
		static char const authorization_type[] = HTTP_AUTHORIZATION_TYPE;
		static char const authorization_code[] = HTTP_AUTHORIZATION_CODE;
		if (authorization_type[0] && authorization_code[0]) {
			HTTP_client.setAuthorizationType(authorization_type);
			HTTP_client.setAuthorization(authorization_code);
		}
		signed int HTTP_status = HTTP_client.GET();
		OLED_LOCK(oled_lock);
		Display::print("HTTP status: ");
		Display::println(HTTP_status);
		if (not (HTTP_status >= 200 and HTTP_status < 300)) return false;
		return true;
	}

	static bool first_WiFi = false;
	static wl_status_t last_WiFi = WL_IDLE_STATUS;

	void loop(void) {
		if (enable_gateway) {
			wl_status_t this_WiFi = WiFi.status();
			if (this_WiFi != last_WiFi) {
				COM::print("WiFi status: ");
				COM::println(status_message(WiFi.status()));
				if (this_WiFi == WL_CONNECTED && !first_WiFi) {
					first_WiFi = true;
					NTP::initialize();
				}
				last_WiFi = this_WiFi;
			}
			if (this_WiFi == WL_CONNECTED) {
				NTP::synchronize();
			}
		}
	}
}

/* ************************************************************************** */
