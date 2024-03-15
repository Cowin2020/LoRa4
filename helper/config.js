(function(p){document.readyState!="loading"?p():document.addEventListener("DOMContentLoaded",p);})(function () {

var predefined_form = document.forms.predefined
var basic_form = document.forms.basic;
var basic_output = document.getElementById("basic_output");
var internal_form = document.forms.internal;
var anchor_id = document.getElementById("anchor_id");
var anchor_device = document.getElementById("anchor_device");
var anchor_log = document.getElementById("anchor_log");

var fields = [
	"site", "devices", "id",
	"battery", "bme280", "sht4x",
	"measure",
	"lora_key", "wifi_ssid", "wifi_pass",
	"upload", "auth_type", "auth_code"
];

function show_typedarray(array) {
	return Array.from(array)
		.map(x => x.toString(16).toUpperCase().padStart(4, "0x0"))
		.join(", ");
}

function string_code(string, n) {
	if (typeof length !== "number")
		n = string.length;
	var array = new Uint8Array(n);
	for (var i = 0; i < n; ++i)
		array[i] = i < string.length ? string.charCodeAt(i) & 0xFF : 0;
	return array;
}

function hex_string(string, n) {
	return show_typedarray(string_code(string, n));
}

function now_string() {
	var now = new Date();
	return (
		String(now.getFullYear()).padStart(4, "0") +
		String(now.getMonth() + 1).padStart(2, "0") +
		String(now.getDate()).padStart(2, "0") +
		String(now.getHours()).padStart(2, "0") +
		String(now.getMinutes()).padStart(2, "0")
	);
}

var text_encoder = new TextEncoder("utf-8");
var text_decoder = new TextDecoder("utf-8");

async function crypto_key(string, usage_encrypt) {
	var usages = usage_encrypt ? ["encrypt"] : ["decrypt"];
	return crypto.subtle.digest("SHA-256", text_encoder.encode(string)).then(
		hash =>
			crypto.subtle.importKey("raw", hash, "AES-GCM", false, usages)
	);
}

function encode_string(rawkey, string) {
	var iv = crypto.getRandomValues(new Uint8Array(12));
	var algorithm = {name: "AES-GCM", iv: iv};
	return (
		crypto_key(rawkey, true)
			.then(
				key => crypto.subtle.encrypt(algorithm, key, text_encoder.encode(string))
			)
			.then(
				ciphertext => [show_typedarray(iv), show_typedarray(new Uint8Array(ciphertext))]
			)
	);
}

function decode_form(rawkey, iv, ciphertext) {
	var algorithm = {name: "AES-GCM", iv: iv};
	return (
		crypto_key(rawkey, false)
			.then(key => crypto.subtle.decrypt(algorithm, key, ciphertext))
			.then(
				cleantext => {
					try {
						var string = text_decoder.decode(cleantext);
						var object = JSON.parse(string);
					}
					catch (error) {
						return Promise.reject(error);
					}
					fill_form(object);
				}
			)
			.catch(
				error => {
					if (error.name === "OperationError")
						alert("Incorrect password");
					else
						alert("Decode error: " + error);
					return Promise.reject(error);
				}
			)
	);
}

function clear_form() {
	fields.forEach(
		function (key) {
			var control = basic_form[key];
			if (control.type === "checkbox")
				control.checked = false;
			else
				control.value = "";
		}
	);
}

function collect_form() {
	var collection = new Object;
	fields.forEach(
		function (field) {
			var control = basic_form.elements[field];
			if (control.type === "checkbox")
				collection[field] = control.checked;
			else
				collection[field] = control.value;
		}
	);
	return collection;
}

function fill_form(object) {
	fields.forEach(
		function (key) {
			var control = basic_form[key];
			var value = object[key];
			if (control == undefined || value == undefined)
				;
			else if (control.type === "checkbox")
				control.checked = object[key];
			else
				control.value = object[key];
		}
	);
}

function replace_objectURL_strings(anchor, strings) {
	var blob = new Blob(strings.map(s => text_encoder.encode(s)), {type: "text/plain"});
	var href = anchor.href;
	anchor.href = URL.createObjectURL(blob);
	if (href) URL.revokeObjectURL(href);
}

function generate_config_id(form) {
	return (
`#ifndef INCLUDE_CONFIG_ID_H
#define INCLUDE_CONFIG_ID_H

/* Device identity */
#define DEVICE_ID ${form.id}
#define NUMBER_OF_DEVICES ${form.devices}
#define ${Number(form.id) ? "ENABLE_MEASURE" : "ENABLE_GATEWAY"}

#endif // INCLUDE_CONFIG_ID_H
`
	);
}

function generate_upload_URL(form) {
	var queries = ["site=" + form.site, "device=%1$u", "serial=%2$u", "time=%3$s"];
	if (form.battery) {
		queries.push("battery_voltage=%" + queries.length + "$.2F");
		queries.push("battery_percentage=%" + queries.length + "$.2F");
	}
	if (form.sht4x) {
		queries.push("sht_temperature=%" + queries.length + "$.1F");
		queries.push("sht_humidity=%" + queries.length + "$.1F");
	}
	if (form.bme280) {
		queries.push("bme_temperature=%" + queries.length + "$.1F");
		queries.push("bme_pressure=%" + queries.length + "$.1F");
		queries.push("bme_humidity=%" + queries.length + "$.1F");
	}
	return form.upload + "?" + queries.join("&");
}

function generate_config_device(form) {
	return (
`#ifndef INCLUDE_CONFIG_DEVICE_H
#define INCLUDE_CONFIG_DEVICE_H

/* Debug */
#define NDEBUG

/* Hareware */
${Number(form.id) ? "#define ENABLE_SDCARD\n" : ""}\
#define ENABLE_LOG_FILE
${form.sht4x ? "#define ENABLE_SHT40\n" : ""}\
${form.bme280 ? "#define ENABLE_BME280\n" : ""}\
${form.ltr390 ? "#define ENABLE_LTR390\n" : ""}\
${
	form.battery === "dfrobot" ? "#define ENABLE_BATTERY_GAUGE BATTERY_GAUGE_DFROBOT\n" :
	form.battery === "lc709203f" ? "#define ENABLE_BATTERY_GAUGE BATTERY_GAUGE_LC709203F\n" :
	""
}\
${Number(form.id) ? "#define ENABLE_CLOCK CLOCK_DS3231\n" : ""}\

/* LoRa */
#define SECRET_KEY [${hex_string(form.lora_key, 16)}]
#define ROUTER_TOPOLOGY {}
#define LORA_BAND 923000000 /* Hz */

/* Internet */
#define WIFI_SSID "${form.wifi_ssid}"
#define WIFI_PASS "${form.wifi_pass}"
#define HTTP_UPLOAD_LENGTH 512
#define HTTP_UPLOAD_FORMAT "${generate_upload_URL(form)}"
#define HTTP_AUTHORIZATION_TYPE ${form.auth_type}
#define HTTP_AUTHORIZATION_CODE ${form.auth_code}
#define HTTP_RESPONE_SIZE 256
#define NTP_SERVER "stdtime.gov.hk"
#define NTP_INTERVAL 1234567UL /* milliseconds */

/* Timimg */
#define START_DELAY 10000UL /* milliseconds */
#define IDLE_INTERVAL 123UL /* milliseconds */
#define ACK_TIMEOUT 1000UL /* milliseconds */
#define RESEND_TIMES 3
#define SEND_INTERVAL 6000UL /* milliseconds */ /* MUST: > ACK_TIMEOUT * (RESEND_TIMES + 1) */
#define MEASURE_INTERVAL ${form.measure}000UL /* milliseconds */ /* MUST: > SEND_INTERVAL */
#define SEND_IDLE_INTERVAL (MEASURE_INTERVAL * 10)
#define SYNCHONIZE_INTERVAL 12345678UL /* milliseconds */
#define SYNCHONIZE_MARGIN 1234UL /* milliseconds */
#define CLEANLOG_INTERVAL 21600000UL /* milliseconds */
#define SLEEP_MARGIN 1000UL /* milliseconds */

/* Display */
#define ENABLE_LED
#define ENABLE_COM_OUTPUT
#define ENABLE_OLED_OUTPUT
#define OLED_ROTATION 2

/* Power saving */
#define ENABLE_SLEEP
#define CPU_FREQUENCY 20 /* MHz */ /* MUST: >= 20MHz for LoRa, and >= 80MHz for WiFi */

#endif // INCLUDE_CONFIG_DEVICE_H
`
	);
}

var predefined_data = {
	cowin: {
		iv: new Uint8Array([0xD5, 0xF2, 0x5B, 0x01, 0xA6, 0x33, 0x94, 0x00, 0x4E, 0x26, 0x87, 0xD2]),
		data:
			new Uint8Array(
				[
					0xD5, 0x68, 0x52, 0x67, 0x27, 0x17, 0xF8, 0x99, 0x29, 0x7C, 0x7A, 0x24, 0x8D, 0x9C, 0x1D, 0x2A,
					0xB2, 0xA7, 0x59, 0x0E, 0xDF, 0x45, 0xE4, 0xEB, 0x89, 0x93, 0xA4, 0x9E, 0x68, 0xD6, 0xAD, 0x61,
					0xE9, 0xF0, 0xD2, 0x36, 0x4F, 0x97, 0x80, 0x9C, 0x46, 0x13, 0xEC, 0x7E, 0xF1, 0x13, 0x54, 0xF3,
					0x2A, 0x08, 0xF0, 0x66, 0x28, 0xD7, 0x84, 0xEA, 0xE2, 0x1F, 0xE6, 0x81, 0xE6, 0x83, 0xFF, 0xFE,
					0x87, 0x70, 0x55, 0x5D, 0x0E, 0x59, 0x77, 0xAC, 0xCF, 0x64, 0x36, 0x9A, 0xA4, 0xA9, 0x82, 0x7E,
					0xE8, 0x91, 0xC9, 0x28, 0xA9, 0xFA, 0xB4, 0x62, 0xBB, 0xB9, 0xB4, 0x4E, 0xA7, 0x56, 0x93, 0x84,
					0x2A, 0x80, 0x7C, 0xD4, 0xBF, 0xDF, 0x8F, 0x0B, 0xB4, 0x2D, 0x13, 0x3A, 0xC7, 0xE5, 0x7C, 0x64,
					0x5B, 0x80, 0x98, 0xEE, 0x34, 0x01, 0xC0, 0xD6, 0xB9, 0x52, 0xDA, 0x17, 0x6F, 0x4D, 0xDC, 0xA3,
					0xB1, 0x3B, 0x1B, 0xEC, 0x29, 0xE5, 0x9B, 0xBB, 0xFA, 0x8E, 0xDA, 0x31, 0x6E, 0x51, 0x6E, 0x35,
					0xAC, 0xB4, 0xE1, 0xCA, 0x4D, 0xA1, 0x89, 0xF5, 0xD9, 0x05, 0x24, 0x39, 0x7C, 0xFE, 0x96, 0x37,
					0x14, 0x66, 0xE0, 0xE7, 0x91, 0x40, 0xF8, 0xEF, 0xF9, 0x9B, 0x02, 0x87, 0x40, 0x94, 0x76, 0x36,
					0x4D, 0x85, 0xA8, 0x66, 0x96, 0x20, 0xF2, 0x97, 0xC6, 0xC5, 0xDE, 0x09, 0x49, 0x5B, 0xF0, 0x8E,
					0x38, 0x40, 0x81, 0xEC, 0xAA, 0xDC, 0x6E, 0x5A, 0xFB, 0x6C, 0x3C, 0x61, 0x47, 0x72, 0x14, 0xB1,
					0x24, 0x28, 0xB4, 0x2D, 0x8C, 0x08, 0x67, 0xCD, 0x86, 0x0A, 0x69, 0x2F, 0x14, 0x7D, 0xE6, 0xFC,
					0xB7, 0x1E, 0xD4, 0xBA, 0xD2, 0x88, 0xE3, 0xAE, 0x6F, 0xA3, 0x82, 0x66, 0x97, 0x16, 0x29, 0x61,
					0x23, 0xD2, 0x41, 0x5B, 0x60, 0xAE, 0x0A, 0xF7, 0xD9, 0x86, 0xA2, 0x5F, 0x11, 0xA9, 0xF0, 0x93,
					0x6C, 0x4E, 0xF1, 0x87, 0xEC, 0x61, 0x1E, 0x64, 0xC6, 0xDB
				]
			)
	}
};

var golden_finger = "iddqd";
var golden_finger_count = 0;
predefined_form.elements.select.addEventListener(
	"keyup",
	function keyup(event) {
		if (typeof event.key === "string" && event.key.length === 1) {
			var c = event.key;
			if (c === golden_finger[golden_finger_count]) {
				golden_finger_count += 1;
				if (golden_finger_count !== golden_finger.length)
					return;
				internal_form.hidden = false;
			}
		}
		golden_finger_count = 0;
	}
);

predefined_form.elements.select.addEventListener(
	"change",
	function change() {
		predefined_form.elements.key.hidden = this.value === "";
	}
);

predefined_form.addEventListener(
	"submit",
	function click(event) {
		event.preventDefault();
		if (predefined_form.elements.select.value === "") {
			return clear_form();
		} else {
			var fill = predefined_data[predefined_form.elements.select.value];
			if (fill != undefined) {
				var key = predefined_form.elements.key.value;
				return decode_form(key, fill.iv, fill.data);
			}
		}
	}
);

basic_form.addEventListener(
	"input",
	function input(event) {
		basic_output.hidden = true;
	}
);

basic_form.addEventListener(
	"submit",
	function submit(event) {
		event.preventDefault();
		basic_form.elements.generate.disabled = true;
		setTimeout(
			function () {
				basic_form.elements.generate.disabled = false;
			},
			3000
		);
		var form = collect_form();
		basic_output.hidden = false;
		var config_id = generate_config_id(form);
		var config_device = generate_config_device(form);
		this.elements["config_id.h"].value = config_id;
		this.elements["config_device.h"].value = config_device;
		replace_objectURL_strings(anchor_id, [config_id]);
		replace_objectURL_strings(anchor_device, [config_device]);
		anchor_log.download = form.site + "_" + now_string() + "_config.json";
		replace_objectURL_strings(anchor_log, [config_id, "\n\n", config_device]);
		anchor_log.click();
		anchor_id.click();
		anchor_device.click();
	}
);

internal_form.elements.grab.addEventListener(
	"click",
	function click(event) {
		internal_form.elements.data.value = JSON.stringify(collect_form());
	}
);

internal_form.elements.encrypt.addEventListener(
	"click",
	function click(event) {
		var key = internal_form.elements.key.value;
		var data = internal_form.elements.data.value;
		return (
			encode_string(key, data)
				.then(
					c => {
						internal_form.elements.nonce.value = c[0];
						internal_form.elements.ciphertext.value = c[1];
					}
				)
		);
	}
);

});
