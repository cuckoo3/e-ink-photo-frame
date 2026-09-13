/*
 * file_handler.cpp
 *
 *  Created on: Sep 12, 2026
 *      Author: Cuckoo
 */

#include <vector>
#include <algorithm>
#include <string>
#include <cstring>
#include <sys/stat.h>
#include <dirent.h>
#include "cJSON.h"
#include "mbedtls/md5.h"
#include "esp_log.h"

#include "file_handler.hpp"
#include "app_config.hpp"

inline static constexpr char TAG[] = "FILE_HANDLER";

bool FileHandler::_get_file_md5(const char *filepath, char *output_hex_33byte)
{
    FILE *f = fopen(filepath, "rb");
    if (!f) return false;

    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts(&ctx);

    unsigned char buf[512];
    size_t bytes_read;

    // Read and update MD5 state chunk-by-chunk
    while ((bytes_read = fread(buf, 1, sizeof(buf), f)) > 0) {
        mbedtls_md5_update(&ctx, buf, bytes_read);
    }

    unsigned char digest[16];
    mbedtls_md5_finish(&ctx, digest);
    mbedtls_md5_free(&ctx);
    fclose(f);

    // Convert 16-byte raw digest to 32-character Hex string
    for (int i = 0; i < 16; i++) {
        sprintf(output_hex_33byte + (i * 2), "%02x", digest[i]);
    }
    output_hex_33byte[32] = '\0';

    return true;
}

cJSON* FileHandler::generate_files_json(const char* mount_point)
{
    cJSON *file_array = cJSON_CreateArray();
	if (!file_array) {
	    ESP_LOGE(TAG, "Failed to allocate cJSON array");
	    return NULL;
	}

    DIR *dir = opendir(mount_point);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", mount_point);
        return file_array;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // ignore non .bin file
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".bin")) {
			// Check filename bounds against configuration limits
            size_t name_len = strlen(entry->d_name);
            if (name_len >= AppConfig::MAX_FILENAME_LEN) {
                ESP_LOGW(TAG, "Skipping file exceeding length limit (%zu >= %d): %s",  name_len, AppConfig::MAX_FILENAME_LEN, entry->d_name);
                continue;
            }
			
            char filepath[257];
            int ret = snprintf(filepath, sizeof(filepath), "%s/%s", mount_point, entry->d_name);
			if (ret < 0 || ret >= (int)sizeof(filepath)) {
                ESP_LOGE(TAG, "File path truncated, skipping: %s", entry->d_name);
                continue;
            }

            struct stat st;
            if (stat(filepath, &st) == 0) {
                cJSON *file_obj = cJSON_CreateObject();
				if (!file_obj) {
                    ESP_LOGE(TAG, "Failed to allocate cJSON object for file: %s", entry->d_name);
                    continue;
                }
				cJSON_AddStringToObject(file_obj, "name", entry->d_name);
				cJSON_AddNumberToObject(file_obj, "size", st.st_size);

                // calculate MD5
                char md5[33];
				if (_get_file_md5(filepath, md5)) {
					cJSON_AddStringToObject(file_obj, "md5", md5);
				} else {
				    cJSON_AddStringToObject(file_obj, "md5", "");
				}

                cJSON_AddItemToArray(file_array, file_obj);
            }
        }
    }

    closedir(dir);
    return file_array;
}

const std::vector<FileHandler::FileInfo> FileHandler::_scan_local_files_sorted(const char* mount_point) {
    std::vector<FileHandler::FileInfo> files;
    files.reserve(AppConfig::MAX_FILE_COUNT); // Pre-allocate memory on heap to avoid fragmentation

    DIR *dir = opendir(mount_point);
    if (!dir) {
        return files;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Filter for regular files containing .bin extension
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".bin") != NULL) {
			size_t name_len = strlen(entry->d_name);
            if (name_len >= AppConfig::MAX_FILENAME_LEN) {
                ESP_LOGW(TAG, "Skipping file exceeding length limit (%zu >= %d): %s", 
                         name_len, AppConfig::MAX_FILENAME_LEN, entry->d_name);
                continue;
            }
			
            char filepath[257];
            int ret =snprintf(filepath, sizeof(filepath), "%s/%s", mount_point, entry->d_name);
			if (ret < 0 || ret >= (int)sizeof(filepath)) {
                ESP_LOGE(TAG, "File path truncated, skipping: %s", entry->d_name);
                continue;
            }

            struct stat st;
            if (stat(filepath, &st) == 0) {
                files.push_back({
                    .name = entry->d_name,
                    .mtime = st.st_mtime
                });
            }
        }
    }
    closedir(dir);

    // Sort descending by mtime (newest first)
    std::sort(files.begin(), files.end(), [](const FileInfo &a, const FileInfo &b) {
        return a.mtime > b.mtime;
    });

    return files;
}

int FileHandler::rebuild_playlist_index(const char* mount_point, char* current_filename)
{
    std::vector<FileHandler::FileInfo> sorted_files = _scan_local_files_sorted(mount_point);

    // 1. Always open/create playlist.idx in "wb" mode to truncate/clear old content
    char idx_path[64];
    snprintf(idx_path, sizeof(idx_path), "%s/playlist.idx", mount_point);

    FILE* f = fopen(idx_path, "wb");
    if (!f) return -1;

    // 2. Handle empty directory state
    if (sorted_files.empty()) {
        if (current_filename) {
            current_filename[0] = '\0'; // Clear buffer safely
        }
        fclose(f); // Closes empty 0-byte file
        return 0;  // 0 files written
    }

    // 3. Copy newest filename (index 0) to output buffer
    if (current_filename) {
        strncpy(current_filename, sorted_files[0].name.c_str(), AppConfig::MAX_FILENAME_LEN - 1);
        current_filename[AppConfig::MAX_FILENAME_LEN - 1] = '\0'; // Ensure NULL safety
    }

    // 4. Write records for all found files
    for (const auto& file : sorted_files) {
        char fixed_name[AppConfig::MAX_FILENAME_LEN] = {0};
        strncpy(fixed_name, file.name.c_str(), AppConfig::MAX_FILENAME_LEN - 1);
        fwrite(fixed_name, 1, AppConfig::MAX_FILENAME_LEN, f);
    }

    fclose(f);
    
    return static_cast<int>(sorted_files.size());
}

bool FileHandler::get_current_playlist_file(const char* mount_point, size_t current_index, char* current_filename, size_t* out_total_count)
{
    if (!current_filename) return false;

    char idx_path[64];
    snprintf(idx_path, sizeof(idx_path), "%s/playlist.idx", mount_point);

    FILE* f = fopen(idx_path, "rb");
    if (!f) {
        if (out_total_count) *out_total_count = 0;
        return false;
    }

    // 1. Calculate total records by checking total file size (O(1))
    if (fseek(f, 0, SEEK_END) == 0) {
        long file_size = ftell(f);
        if (out_total_count && AppConfig::MAX_FILENAME_LEN > 0) {
            *out_total_count = (size_t)(file_size / AppConfig::MAX_FILENAME_LEN);
        }
    } else if (out_total_count) {
        *out_total_count = 0;
    }

    // 2. Calculate byte offset for target record
    long offset = (long)(current_index * AppConfig::MAX_FILENAME_LEN);

    // 3. Seek directly to target record
    if (fseek(f, offset, SEEK_SET) != 0) {
        fclose(f); // Index out of bounds or read error
        return false;
    }

    // 4. Read record
    size_t bytes_read = fread(current_filename, 1, AppConfig::MAX_FILENAME_LEN, f);
    fclose(f);

    // 5. Ensure safety null-termination
    current_filename[AppConfig::MAX_FILENAME_LEN - 1] = '\0';

    return (bytes_read == AppConfig::MAX_FILENAME_LEN);
}