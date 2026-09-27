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

    char idx_path[64];
    snprintf(idx_path, sizeof(idx_path), "%s/playlist.idx", mount_point);

    FILE* f = fopen(idx_path, "rb");
    
    // Fallback if playlist.idx does not exist yet
    if (!f) {
        ESP_LOGW(TAG, "playlist.idx missing for sync payload. Falling back to directory scan.");
        std::vector<FileHandler::FileInfo> sorted_files = _scan_local_files_sorted(mount_point);
        for (const auto& file : sorted_files) {
            cJSON *file_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(file_obj, "name", file.name.c_str());
            cJSON_AddItemToArray(file_array, file_obj);
        }
        return file_array;
    }

    // Direct read from playlist.idx (no MD5 calculation)
    char filename_buf[AppConfig::MAX_FILENAME_LEN] = {0};
    while (fread(filename_buf, 1, AppConfig::MAX_FILENAME_LEN, f) == AppConfig::MAX_FILENAME_LEN) {
        filename_buf[AppConfig::MAX_FILENAME_LEN - 1] = '\0'; // Safety null termination

        cJSON *file_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(file_obj, "name", filename_buf);

        cJSON_AddItemToArray(file_array, file_obj);
    }

    fclose(f);
    return file_array;
}

// Helper function to check suffix
bool FileHandler::_ends_with(const char *str, const char *suffix)
{
    if (!str || !suffix) return false;
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len) return false;
    return strcmp(str + str_len - suffix_len, suffix) == 0;
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
	    // 1. Check if filename ends with ".bin" (avoids matching .bin.tmp or .bin.bak)
	    if (_ends_with(entry->d_name, ".bin")) {
	        size_t name_len = strlen(entry->d_name);
	        if (name_len >= AppConfig::MAX_FILENAME_LEN) {
	            ESP_LOGW(TAG, "Skipping file exceeding length limit (%zu >= %d): %s", 
	                     name_len, AppConfig::MAX_FILENAME_LEN, entry->d_name);
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
	            // 2. Safely verify it is a regular file using stat mode
	            if (S_ISREG(st.st_mode)) {
	                files.push_back({
	                    .name = entry->d_name,
	                    .mtime = st.st_mtime
	                });
	            }
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

int FileHandler::write_playlist_index(const char* mount_point, const std::vector<std::string>& filenames, char* current_filename)
{
    char idx_path[64];
    snprintf(idx_path, sizeof(idx_path), "%s/playlist.idx", mount_point);

    // 1. Open/create playlist.idx in "wb" mode to overwrite old index
    FILE* f = fopen(idx_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open playlist index for writing: %s", idx_path);
        return -1;
    }

    // 2. Handle empty list
    if (filenames.empty()) {
        if (current_filename) {
            current_filename[0] = '\0';
        }
        fclose(f);
        return 0;
    }

    // 3. Copy index 0 filename to current_filename buffer if provided
    if (current_filename) {
        strncpy(current_filename, filenames[0].c_str(), AppConfig::MAX_FILENAME_LEN - 1);
        current_filename[AppConfig::MAX_FILENAME_LEN - 1] = '\0';
    }

    // 4. Write records sequentially in the exact order received from server
    for (const auto& name : filenames) {
        char fixed_name[AppConfig::MAX_FILENAME_LEN] = {0};
        strncpy(fixed_name, name.c_str(), AppConfig::MAX_FILENAME_LEN - 1);
        fwrite(fixed_name, 1, AppConfig::MAX_FILENAME_LEN, f);
    }

    fclose(f);
    ESP_LOGI(TAG, "Successfully wrote %zu items to playlist.idx", filenames.size());

    return static_cast<int>(filenames.size());
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

bool FileHandler::playlist_matches(const char* mount_point, const std::vector<std::string>& server_playlist)
{
    char idx_path[64];
    snprintf(idx_path, sizeof(idx_path), "%s/playlist.idx", mount_point);

    FILE* f = fopen(idx_path, "rb");
    if (!f) return false; // File doesn't exist yet, must write

    // Check size match first
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size != (long)(server_playlist.size() * AppConfig::MAX_FILENAME_LEN)) {
        fclose(f);
        return false; // Length mismatch
    }

    // Compare each record
    char record[AppConfig::MAX_FILENAME_LEN];
    for (const auto& name : server_playlist) {
        if (fread(record, 1, AppConfig::MAX_FILENAME_LEN, f) != AppConfig::MAX_FILENAME_LEN) {
            fclose(f);
            return false;
        }
        if (strncmp(record, name.c_str(), AppConfig::MAX_FILENAME_LEN - 1) != 0) {
            fclose(f);
            return false; // Filename or sequence order mismatch
        }
    }

    fclose(f);
    return true; // Playlist is identical, skip rewrite!
}