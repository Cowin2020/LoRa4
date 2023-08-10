#include <atomic>
#include <mutex>

#include <SD.h>

#include "display.h"
#include "device.h"
#include "sdcard.h"

#define DATA_FILE_PATH "/data.csv"
#define CLEANUP_FILE_PATH "/cleanup.csv"
#include "config_device.h"

/* ************************************************************************** */

namespace SDCard {
	#if defined(ENABLE_SDCARD)
		static char data_file_path[] = DATA_FILE_PATH;
		static char cleanup_file_path[] = CLEANUP_FILE_PATH;

		void clean_up(void) {
			SD.remove(cleanup_file_path);
			if (!SD.rename(data_file_path, cleanup_file_path)) return;
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

			#if !defined(DEBUG_CLEAN_OLD_DATA)
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
			/* TODO */
		}
		bool read_data(struct Data *const data) {
			/* TODO */
		}

		void next_data(void) {
			/* TODO */
		}
	#else
		static std::mutex mutex;
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
	#endif
}

/* ************************************************************************** */
