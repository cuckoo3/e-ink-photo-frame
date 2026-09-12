#pragma once
#include <vector>
#include <string>
#include <cstring>
#include <sys/stat.h>
#include <dirent.h>
#include "cJSON.h"

class FileHandler {
	public:	
		struct FileInfo {
		    std::string name;
		    time_t mtime;
		};

		cJSON* generate_files_json(const char* mount_point);
		int rebuild_playlist_index(const char* mount_point, char* current_filename);
		bool get_current_playlist_file(const char* mount_point, size_t current_index, char* current_filename);
	
	private:
		bool _get_file_md5(const char *filepath, char *output_hex_33byte);
		const std::vector<FileInfo> _scan_local_files_sorted(const char* mount_point);
};

