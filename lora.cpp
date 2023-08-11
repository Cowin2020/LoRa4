#include <cstring>
#include <memory>
#include <vector>
#include <thread>

#include <LoRa.h>
#include <RNG.h>
#include <AES.h>
#include <GCM.h>

#include "id.h"
#include "display.h"
#include "inet.h"
#include "daemon.h"
#include "lora.h"

/* ************************************************************************** */

/* Protocol Constants */
#define PACKET_TIME    0
#define PACKET_ASKTIME 1
#define PACKET_ACK     2
#define PACKET_SEND    3

typedef uint8_t PacketType;

/* Cipher parameters */
#define CIPHER_IV_LENGTH 12
#define CIPHER_TAG_SIZE 4

typedef GCM<AES128> AuthCipher;

static Device const router_topology[][2] = ROUTER_TOPOLOGY;
static char const secret_key[16] PROGMEM = SECRET_KEY;

template<typename TO, typename FROM>
static inline TO *pointer_offset(FROM *const from, size_t const offset) {
	return reinterpret_cast<TO *>(reinterpret_cast<char *>(from) + offset);
}

template<typename TO, typename FROM>
static inline TO const *pointer_offset(FROM const *const from, size_t const offset) {
	return reinterpret_cast<TO const *>(reinterpret_cast<char const *>(from) + offset);
}

namespace LORA {
	static Device last_receiver = 0;

	bool initialize(void) {
		SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
		LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);

		if (!enable_gateway) {
			size_t const N = sizeof router_topology / sizeof *router_topology;
			size_t i = 0;
			for (size_t i = 0;; ++i) {
				if (i >= N) {
					last_receiver = 0;
					break;
				}
				if (router_topology[i][1] == my_device_id) {
					last_receiver = router_topology[i][0];
					break;
				}
				++i;
			}
		}

		if (!LoRa.begin(LORA_BAND)) {
			Display::println("LoRa uninitialized");
			return false;
		}

		LoRa.enableCrc();
		Display::println("LoRa initialized");
		return true;
	}

	void sleep(void) {
		LoRa.sleep();
	}

	namespace Send {
		static bool payload(char const *const message, void const *const payload, size_t const size) {
			Debug::print("DEBUG: LORA::Send::payload ");
			Debug::println(message);

			uint8_t nonce[CIPHER_IV_LENGTH];
			RNG.rand(nonce, sizeof nonce);
			AuthCipher cipher;
			if (!cipher.setKey((uint8_t const *)secret_key, sizeof secret_key)) {
				COM::print("LoRa ");
				COM::print(message);
				COM::println(": unable to set key");
				#if defined(ENABLE_OLED_OUTPUT)
					OLED::set_message("Unable to set key\n");
				#endif
				return false;
			}
			if (!cipher.setIV(nonce, sizeof nonce)) {
				COM::print("LoRa ");
				COM::print(message);
				COM::println(": unable to set nonce");
				#if defined(ENABLE_OLED_OUTPUT)
					OLED::set_message("Unable to set nonce\n");
				#endif
				return false;
			}
			char ciphertext[size];
			cipher.encrypt((uint8_t *)&ciphertext, (uint8_t const *)payload, size);
			uint8_t tag[CIPHER_TAG_SIZE];
			cipher.computeTag(tag, sizeof tag);
			LoRa.write(nonce, sizeof nonce);
			LoRa.write((uint8_t const *)&ciphertext, sizeof ciphertext);
			LoRa.write((uint8_t const *)&tag, sizeof tag);

			Debug::dump("DEBUG: LoRa::Send::payload", payload, size);
			return true;
		}

		void ASKTIME(void) {
			LoRa.beginPacket();
			LoRa.write(uint8_t(PACKET_ASKTIME));
			LoRa.write(uint8_t(last_receiver));
			payload("ASKTIME", &my_device_id, sizeof my_device_id);
			LoRa.endPacket(true);
		}

		void SEND(Device const receiver, SerialNumber const serial, Data const *const data) {
			Debug::print("DEBUG: LoRa::Send::SEND receiver=");
			Debug::print(receiver);
			Debug::print(" serial=");
			Debug::println(serial);

			unsigned char content[2 * sizeof my_device_id + sizeof serial + sizeof *data];
			std::memcpy(content, &my_device_id, sizeof my_device_id);
			std::memcpy(content + sizeof my_device_id, &my_device_id, sizeof my_device_id);
			std::memcpy(content + 2 * sizeof my_device_id, &serial, sizeof serial);
			std::memcpy(content + 2 * sizeof my_device_id + sizeof serial, data, sizeof *data);
			LoRa.beginPacket();
			LoRa.write(uint8_t(PACKET_SEND));
			LoRa.write(uint8_t(receiver));
			payload("SEND", content, sizeof content);
			LoRa.endPacket(true);
		}
	}
}

namespace LORA {
	namespace Receive {
		static void TIME(Device const device, std::vector<uint8_t> const &content) {
			if (!enable_gateway) {
				if (content.size() != sizeof (struct FullTime)) return;
				struct FullTime const *const time = reinterpret_cast<struct FullTime const *>(content.data());

				if (device != Device(0)) { /* always accept TIME packet from gateway */
					size_t i = 0;
					for (;;) {
						if (i >= sizeof router_topology / sizeof *router_topology) return;
						if (router_topology[i][0] == my_device_id && router_topology[i][1] == device) break;
						++i;
					}
				}

				RTC::set(time);
				DAEMON::AskTime::synchronized();

				Debug::print("DEBUG: LoRa::Receive::TIME ");
				Debug::println(String(*time));

				LoRa.beginPacket();
				LoRa.write(PacketType(PACKET_TIME));
				LoRa.write(my_device_id);
				LORA::Send::payload("TIME+", time, sizeof *time);
				LoRa.endPacket(true);
			}
		}

		static void ASKTIME(Device const device, std::vector<uint8_t> const &content) {
			if (enable_gateway) {
				if (content.size() != sizeof (Device)) {
					COM::print("WARN: LoRa ASKTIME: incorrect packet size: ");
					COM::println(content.size());
					return;
				}

				Device receiver;
				if (LoRa.readBytes(&receiver, sizeof receiver) != sizeof receiver) return;
				if (receiver != (Device)0) return;

				Device const sender = *reinterpret_cast<Device const *>(content.data());
				if (!(sender > 0 && sender < number_of_device)) {
					COM::print("WARN: LoRa ASKTIME: incorrect device: ");
					COM::println(device);
					return;
				}

				Debug::print("DEBUG: LoRa::Receive::ASKTIME ");
				Debug::println(sender);

				DAEMON::AskTime::synchronized();
			}
		}

		static void SEND(Device const receiver, std::vector<uint8_t> const &content) {
			size_t const minimal_content_size =
				sizeof (Device)       /* terminal */
				+ sizeof (Device)       /* router list length >= 1 */
				+ sizeof (SerialNumber) /* serial code */
				+ sizeof (struct Data); /* data */
			if (enable_gateway) {
				if (!(content.size() >= minimal_content_size)) {
					COM::print("WARN: LoRa SEND: incorrect packet size: ");
					COM::println(content.size());
					return;
				}

				Device const device = *reinterpret_cast<Device const *>(content.data());
				if (!(device > 0 && device < number_of_device)) {
					COM::print("WARN: LoRa SEND: incorrect device: ");
					COM::println(device);
					return;
				}

				size_t routers_length = sizeof (Device);
				for (;;) {
					if (routers_length >= content.size()) {
						COM::println("WARN: LoRa SEND: incorrect router list");
						return;
					}
					Device const router = *reinterpret_cast<Device const *>(content.data() + routers_length);
					if (router == device) break;
					routers_length += sizeof router;
				}
				size_t const excat_content_size =
					minimal_content_size
					+ routers_length * sizeof (Device)
					- sizeof (Device);
				if (content.size() != excat_content_size) {
					COM::print("WARN: LoRa SEND: incorrect packet size or router list: ");
					COM::print(content.size());
					COM::print(" / ");
					COM::println(routers_length);
					return;
				}

				SerialNumber const serial = *reinterpret_cast<SerialNumber const *>(content.data() + sizeof (Device) + routers_length);
				size_t const router_list_size =
					sizeof (Device) * (1 + routers_length)
					+ sizeof (SerialNumber);
				struct Data const data = *reinterpret_cast<struct Data const *>(content.data() + router_list_size);

				#ifdef ENABLE_OLED_OUTPUT
					OLED::home();
					OLED::print("Receive ");
					OLED::print(device);
					OLED::print(" Serial ");
					OLED::println(serial);
					OLED::println(String(data.time));
					#ifdef ENABLE_BATTERY_GAUGE
						OLED::print("Battery:");
						OLED::print(data.battery_voltage);
						OLED::print("V ");
						OLED::print(data.battery_percentage);
						OLED::println('%');
					#endif
					#ifdef ENABLE_DALLAS
						OLED::print("Dallas temp.: ");
						OLED::println(data.dallas_temperature);
					#endif
					#ifdef ENABLE_BME280
						OLED::print("BME temp.: ");
						OLED::println(data.bme280_temperature);
						OLED::print("BME pressure: ");
						OLED::println(data.bme280_pressure, 0);
						OLED::print("BME humidity: ");
						OLED::println(data.bme280_humidity);
					#endif
					#ifdef ENABLE_LTR390
						OLED::print("LTR UV: ");
						OLED::println(data.ltr390_ultraviolet);
					#endif
					OLED::display();
				#endif

				bool const upload_success = WIFI::upload(device, serial, &data);
				OLED::display();
				if (!upload_success) return;

				Device const router = *reinterpret_cast<Device const *>(content.data() + sizeof device);
				LoRa.beginPacket();
				LoRa.write(PacketType(PACKET_ACK));
				LoRa.write(router);
				LORA::Send::payload("ACK", content.data(), router_list_size);
				LoRa.endPacket(true);
			}
			else {
				size_t const minimal_content_size =
					sizeof (Device)         /* terminal */
					+ sizeof (Device)       /* router list length >= 1 */
					+ sizeof (SerialNumber) /* serial code */
					+ sizeof (struct Data); /* data */

				if (!(content.size() >= minimal_content_size)) {
					COM::print("WARN: LoRa SEND: incorrect packet size: ");
					COM::println(content.size());
					return;
				}

				Device const receiver = *reinterpret_cast<Device const *>(content.data());
				Debug::print("DEBUG: LoRa::Receive::SEND receiver=");
				Debug::println(receiver);
				if (receiver != my_device_id) return;

				std::vector<uint8_t> bounce(content.size() + 1);
				std::memcpy(bounce.data(), content.data() + sizeof (Device), sizeof (Device));
				std::memcpy(bounce.data() + sizeof (Device), &receiver, sizeof (Device));

				Debug::print("DEBUG: LoRa::Receive::SEND last_receiver=");
				Debug::println(last_receiver);

				LoRa.beginPacket();
				LoRa.write(PacketType(PACKET_SEND));
				LoRa.write(last_receiver);
				LORA::Send::payload("SEND+", bounce.data(), bounce.size());
				LoRa.endPacket(true);
			}
		}

		static void ACK(Device const receiver, std::vector<uint8_t> const &content) {
			if (!enable_gateway) {
				size_t const minimal_content_size =
					sizeof (Device)          /* terminal */
					+ sizeof (Device)        /* router list length >= 1 */
					+ sizeof (SerialNumber); /* serial code */
				if (!(content.size() >= minimal_content_size)) {
					COM::print("WARN: LoRa ACK: incorrect packet size: ");
					COM::println(content.size());
					return;
				}

				Debug::print("DEBUG: LoRa::Receive::ACK receiver=");
				Debug::println(receiver);
				if (my_device_id != receiver) return;

				Device const terminal = *reinterpret_cast<Device const *>(content.data());
				Device const router0 = *reinterpret_cast<Device const *>(content.data() + sizeof (Device));
				Device const router1 = *reinterpret_cast<Device const *>(content.data() + 2 * sizeof (Device));
				if (my_device_id == terminal) {
					if (my_device_id != router0) {
						COM::print("WARN: LoRa ACK: dirty router list");
						return;
					}

					SerialNumber const serial = *reinterpret_cast<Device const *>(content.data() + 3 * sizeof (Device));
					Debug::print("DEBUG: LoRa::Receive::ACK serial=");
					Debug::println(serial);

					/* TODO */
					//	if (!sender_schedule.stop_ack(serial)) return;

					#ifdef ENABLE_SD_CARD
						/* TODO */
						SD_CARD::push_schedule.ack();
					#endif

					OLED::draw_received();
				}
				else {
					Debug::print("DEBUG: LoRa::Receive::ACK router=");
					Debug::print(router1);
					Debug::print(" terminal=");
					Debug::println(terminal);

					LoRa.beginPacket();
					LoRa.write(PacketType(PACKET_ACK));
					LoRa.write(router1);
					LoRa.write(terminal);
					LORA::Send::payload("ACK+", content.data() + 2 * sizeof (Device), content.size() - 2 * sizeof (Device));
					LoRa.endPacket(true);
				}
			}
		}

		static void decode(std::vector<uint8_t> packet) {
			static size_t const overhead_size = sizeof (PacketType) + sizeof (Device) + CIPHER_IV_LENGTH + CIPHER_TAG_SIZE;
			if (packet.size() < overhead_size) {
				Debug::print("DEBUG: LoRa::Receive::decode packet too short ");
				Debug::println(packet.size());
				return;
			}

			PacketType const *const packet_type = packet.data();
			Device const *const device = pointer_offset<Device>(packet_type, sizeof *packet_type);
			Debug::print("DEBUG: LoRa::Receive::decode device=");
			Debug::println(*device);

			uint8_t const *const nonce = pointer_offset<uint8_t>(device, sizeof *device);
			size_t const content_size = packet.size() - overhead_size;
			uint8_t const *const ciphertext = nonce + CIPHER_IV_LENGTH;
			uint8_t const *const tag = ciphertext + content_size;
			if (!((char *)packet.data() + sizeof (PacketType) + sizeof (Device) == (char *)nonce))
				Debug::println("DEBUG: incorrect none position");
			if (!((char *)packet.data() + packet.size() == (char *)tag + CIPHER_TAG_SIZE))
				Debug::println("DEBUG: incorrect content size");

			switch (*packet_type) {
				case PACKET_TIME:
				case PACKET_ASKTIME:
				case PACKET_SEND:
				case PACKET_ACK:
					break;
				default:
					Debug::print("DEBUG: LoRa::Receive::decode unknown packet type ");
					Debug::println(*packet_type);
					return;
			}

			if (!(*device >= 0 && * device < number_of_device)) {
				Debug::print("DEBUG:: LoRa::Receive::decode unknown device ");
				Debug::println(*device);
				return;
			}

			AuthCipher cipher;
			std::vector<uint8_t> cleantext(content_size);
			if (!cipher.setKey((uint8_t const *)secret_key, sizeof secret_key)) {
				COM::print("LoRa receive ");
				COM::print(*packet_type);
				COM::println(": fail to set cipher key");
				OLED::set_message(String("LoRa ") + *packet_type + ": fail to set cipher key\n");
				return;
			}
			if (!cipher.setIV(nonce, CIPHER_IV_LENGTH)) {
				COM::print("LoRa receive ");
				COM::print(*packet_type);
				COM::println(": fail to set cipher nonce");
				OLED::set_message(String("LoRa ") + *packet_type + ": fail to set cipher nonce\n");
				return;
			}
			cipher.decrypt(cleantext.data(), ciphertext, content_size);
			if (!cipher.checkTag(tag, sizeof tag)) {
				COM::print("LoRa receive ");
				COM::print(*packet_type);
				COM::println(": invalid cipher tag");
				OLED::set_message(String("LoRa ") + *packet_type + ": invalid cipher tag\n");
				return;
			}

			switch (*packet_type) {
			case PACKET_TIME:
				Debug::println("DEBUG: LORA::Receive::packet TIME");
				TIME(*device, cleantext);
				break;
			case PACKET_ASKTIME:
				Debug::println("DEBUG: LORA::Receive::packet ASKTIME");
				ASKTIME(*device, cleantext);
				break;
			case PACKET_SEND:
				Debug::println("DEBUG: LORA::Receive::packet SEND");
				SEND(*device, cleantext);
				break;
			case PACKET_ACK:
				Debug::println("DEBUG: LORA::Receive::packet ACK");
				ACK(*device, cleantext);
				break;
			default:
				COM::print("LoRa: incorrect packet type: ");
				COM::println(*packet_type);
			}
		}

		void packet(void) {
			signed int const parse_size = LoRa.parsePacket();
			if (parse_size < 1) return;
			Debug::print("DEBUG: LORA::Receive::packet packet_size ");
			Debug::println(parse_size);
			size_t const packet_size = static_cast<size_t>(LoRa.available());
			if (packet_size != static_cast<size_t>(parse_size)) {
				Display::println("ERROR: LoRa receive: LoRa.parsePacker != LoRa.available");
				return;
			}
			std::vector<uint8_t> packet(packet_size);
			if (LoRa.readBytes(packet.data(), packet.size()) != packet.size()) {
				Display::println("ERROR: LoRa receive: unable read data from LoRa");
				return;
			}
			RNG.stir(packet.data(), packet.size(), packet.size() << 2);
			std::thread(decode, packet).detach();
		}
	}
}

/* ************************************************************************** */