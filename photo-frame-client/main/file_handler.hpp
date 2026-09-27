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
		
		// Rebuilds playlist.idx from a specific server-provided order.
	    // Populates current_filename with the first file in the sequence (index 0).
	    int write_playlist_index(const char* mount_point, const std::vector<std::string>& filenames, char* current_filename = nullptr);
		
		int rebuild_playlist_index(const char* mount_point, char* current_filename = nullptr);
		bool get_current_playlist_file(const char* mount_point, size_t current_index, char* current_filename, size_t* out_total_count);
		bool playlist_matches(const char* mount_point, const std::vector<std::string>& server_playlist);
	
	private:
		bool _get_file_md5(const char *filepath, char *output_hex_33byte);
		static bool _ends_with(const char *str, const char *suffix);
		const std::vector<FileInfo> _scan_local_files_sorted(const char* mount_point);
};

