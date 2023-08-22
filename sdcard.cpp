#include <atomic>
#include <mutex>

#include <SD.h>

#include "id.h"
#include "display.h"
#include "sdcard.h"

#define DATA_FILE_PATH "/data.csv"
#define CLEANUP_FILE_PATH "/cleanup.csv"
#define LOG_FILE_PATH "/log.csv"
#include "config_device.h"

/* ************************************************************************** */

namespace SDCard {
	static std::mutex mutex;

	#if defined(ENABLE_SDCARD)
		static char const data_file_path[] = DATA_FILE_PATH;
		static char const cleanup_file_path[] = CLEANUP_FILE_PATH;
		#if defined(LOG_DATA)
			static char const log_file_path[] = LOG_FILE_PATH;
		#endif
		static class SPIClass SPI_1(HSPI);
		static off_t current_position = 0;
		static off_t next_position = 0;

		void clean_up(void) {
			if (!enable_measure) return;
			std::lock_guard<std::mutex> lock(mutex);
			if (SD.exists(cleanup_file_path))
				SD.remove(data_file_path);
			else if (!SD.rename(data_file_path, cleanup_file_path))
				return;
			class File cleanup_file = SD.open(cleanup_file_path, "r");
			if (!cleanup_file) {
				COM::println("Fail to open clean-up file");
				return;
			}
			class File data_file = SD.open(data_file_path, "w");
			if (!data_file) {
				COM::println("Fail to create data file");
				cleanup_file.close();
				return;
			}

			#if !defined(DEBUG_CLEAN_DATA)
				for (;;) {
					class String const s = cleanup_file.readStringUntil(',');
					if (!s.length()) break;
					bool const sent = s != "0";

					struct Data data;
					if (!data.readln(&cleanup_file)) {
						COM::println("Clean-up: invalid data");
						break;
					}

					if (!sent) {
						data_file.print("0,");
						data.writeln(&data_file);
					}
				}
			#endif

			cleanup_file.close();
			data_file.close();
			SD.remove(cleanup_file_path);
		}

		void add_data(struct Data const *const data) {
			if (!enable_measure) return;
			std::lock_guard<std::mutex> lock(mutex);
			class File data_file = SD.open(data_file_path, "a");
			if (!data_file) {
				Display::println("Cannot open data file");
			}
			else {
				try {
					data_file.print("0,");
					data->writeln(&data_file);
				}
				catch (...) {
					Display::println("Cannot append data file");
				}
				data_file.close();
			}
			#if defined(LOG_DATA)
				class File log_file = SD.open(log_file_path, "a");
				if (!log_file) {
					Display::println("Cannot open log file");
				}
				else {
					try {
						data->writeln(&log_file);
					}
					catch (...) {
						Display::println("Cannot append log file");
					}
					log_file.close();
				}
			#endif
		}
		bool read_data(struct Data *const data) {
			if (!enable_measure) return false;
			std::lock_guard<std::mutex> lock(mutex);
			class File file = SD.open(DATA_FILE_PATH, "r+", true);
			if (!file) {
				COM::println("ERROR: SDCard::read_data failed to open data file");
				return false;
			}
			if (!file.seek(current_position)) {
				COM::print("ERROR: SDCard::read_data could not seek to ");
				COM::println(current_position);
				file.close();
				return false;
			}
			bool success = false;
			for (;;) {
				class String const s = file.readStringUntil(',');
				if (!s.length()) break;
				bool const sent = s != "0";
				if (!data->readln(&file)) {
					COM::print("ERROR: SDCard::read_data read invalid data at ");
					COM::println(file.position());
					break;
				}
				next_position = file.position();
				if (!sent) {
					success = true;
					break;
				}
				current_position = next_position;
			}
			file.close();
			return success;
		}

		void next_data(void) {
			if (!enable_measure) return;
			std::lock_guard<std::mutex> lock(mutex);
			if (current_position == next_position) return;
			class File file = SD.open(DATA_FILE_PATH, "r+", true);
			if (!file) COM::println("ERROR: SDCard::next_data failed to open data file");
			file.write('1');
			file.close();
			current_position = next_position;
		}

		bool initialize(void) {
			if (!enable_measure) return true;
			pinMode(SD_MISO, INPUT_PULLUP);
			SPI_1.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
			if (SD.begin(SD_CS, SPI_1)) {
				Display::println("SD card initialized");
				COM::println(String("SD Card type: ") + String(SD.cardType()));
				Display::println("Cleaning up data file");
				clean_up();
				Display::println("Data file cleaned");
				OLED::display();
				return true;
			} else {
				Display::println("SD card uninitialized");
				OLED::display();
				return false;
			}
		}
	#else
		static bool filled = false;
		static struct Data last_data;

		void clean_up(void) {}

		void add_data(struct Data const *const data) {
			std::lock_guard<std::mutex> lock(mutex);
			filled = true;
			last_data = *data;
		}

		bool read_data(struct Data *const data) {
			std::lock_guard<std::mutex> lock(mutex);
			if (filled) {
				*data = last_data;
				return true;
			}
			else {
				return false;
			}
		}

		void next_data(void) {
			std::lock_guard<std::mutex> lock(mutex);
			filled = false;
		}

		bool initialize(void) {
			filled = false;
			return true;
		}
	#endif
}

/* ************************************************************************** */
