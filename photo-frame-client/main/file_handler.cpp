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

#include "file_handler.hpp"
#include "app_config.hpp"

constexpr size_t MAX_FILENAME_LEN = 32;

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
    DIR *dir = opendir(AppConfig::STORAGE_PATH);
    if (!dir) {
        printf("Failed to open directory: %s\n", mount_point);
        return file_array;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // ignore non .bin file
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".bin")) {
            char filepath[257];
            snprintf(filepath, sizeof(filepath), "%s/%s", mount_point, entry->d_name);

            struct stat st;
            if (stat(filepath, &st) == 0) {
                cJSON *file_obj = cJSON_CreateObject();
                cJSON_AddStringToObject(file_obj, "name", entry->d_name);
				cJSON_AddNumberToObject(file_obj, "size", st.st_size);

                // calculate MD5
                 char md5[33];
                 _get_file_md5(filepath, md5);
                 cJSON_AddStringToObject(file_obj, "md5", md5);

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
        // Filter for files containing .bin
        if (strstr(entry->d_name, ".bin") != NULL) {
            char filepath[300];
            snprintf(filepath, sizeof(filepath), "%s/%s", mount_point, entry->d_name);

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

bool FileHandler::get_current_playlist_file(const char* mount_point, size_t current_index, char* current_filename)
{
    if (!current_filename) return false;

    char idx_path[64];
    snprintf(idx_path, sizeof(idx_path), "%s/playlist.idx", mount_point);

    FILE* f = fopen(idx_path, "rb"); // Open in binary read mode
    if (!f) {
        return false;
    }

    // 1. Calculate byte offset for current_index
    long offset = (long)(current_index * AppConfig::MAX_FILENAME_LEN);

    // 2. Seek directly to the target record (O(1) direct access)
    if (fseek(f, offset, SEEK_SET) != 0) {
        fclose(f); // Index out of bounds or read error
        return false;
    }

    // 3. Read the 32-byte record into out_filename
    size_t bytes_read = fread(current_filename, 1, AppConfig::MAX_FILENAME_LEN, f);
    fclose(f);

    // 4. Ensure safety null-termination
    current_filename[AppConfig::MAX_FILENAME_LEN - 1] = '\0';

    return (bytes_read == AppConfig::MAX_FILENAME_LEN);
}