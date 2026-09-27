#include <cstddef>
#include <string>
#include <dirent.h>
#include <sys/param.h>
#include <sys/stat.h>
#include "lwip/sockets.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_mac.h"
#include "cJSON.h"

#include "server_comm.hpp"
#include "app_config.hpp"
#include "file_handler.hpp"

inline static constexpr char DISCOVERY_MSG[] = "DISCOVER_ESP_SERVER";

inline static constexpr char TAG[] = "SERVER_COMM";
// Retry configuration
inline static constexpr int MAX_DISCOVERY_RETRIES = 5;

const char * ServerComm::_get_mac_address(void)
{
	if (mac_str != nullptr) {
		return mac_str; // Return cached MAC address if already retrieved
	}
	else {
	    static char mac_buffer[18] = {0};
		uint8_t mac[6];

		esp_efuse_mac_get_default(mac);
	    snprintf(mac_buffer, sizeof(mac_buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
	             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	
		mac_str = mac_buffer;
	    ESP_LOGI(TAG, "Device MAC: %s", mac_str);
		return mac_str;
	}
}

void ServerComm::_set_http_header(const esp_http_client_handle_t client)
{
	esp_http_client_set_header(client, "x-device-mac", _get_mac_address());
}

// =========================================================================
// UDP Multicast Discovery
// =========================================================================
bool ServerComm::_discover_server(char* server_ip, int &server_port)
{
	ESP_LOGI(TAG, "Starting UDP multicast discovery on %s:%u...", AppConfig::MULTICAST_IP, AppConfig::UDP_PORT);
	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (sock < 0) {
		ESP_LOGE(TAG, "Unable to create UDP socket: errno %d", errno);
		return false;
	}
	
	// 1. BIND SOCKET TO LOCAL PORT (Required so recvfrom receives replies on UDP_PORT)
    struct sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(AppConfig::UDP_PORT);
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        ESP_LOGE(TAG, "Socket bind failed: errno %d", errno);
        close(sock);
        return false;
    }
	
	// 2. Send multicast discovery packet
	struct sockaddr_in dest_addr{};
	dest_addr.sin_family = AF_INET;
	dest_addr.sin_addr.s_addr = inet_addr(AppConfig::MULTICAST_IP);
	dest_addr.sin_port = htons(AppConfig::UDP_PORT);
	
	bool server_found = false;
	
	struct timeval timeout;
	timeout.tv_sec = 3;       // 3 seconds
	timeout.tv_usec = 0;
	
	for (int attempt = 1; attempt <= MAX_DISCOVERY_RETRIES; attempt++) {
		
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
				
		// Send multicast packet
		int err = sendto(sock, DISCOVERY_MSG, strlen(DISCOVERY_MSG), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
		if (err < 0) {
			ESP_LOGE(TAG, "[Attempt %d/%d] Send failed: errno %d", attempt, MAX_DISCOVERY_RETRIES, errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
		}
		ESP_LOGI(TAG, "[Attempt %d/%d] Sent discovery packet to %s:%u (Timeout: %ds)...",  attempt, MAX_DISCOVERY_RETRIES, AppConfig::MULTICAST_IP, AppConfig::UDP_PORT, (int)timeout.tv_sec);
		
		// Wait for response
		char rx_buffer[128];
		struct sockaddr_in source_addr;
		socklen_t socklen = sizeof(source_addr);
		int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);
		
		if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                ESP_LOGW(TAG, "[Attempt %d/%d] Discovery timed out. Retrying...", attempt, MAX_DISCOVERY_RETRIES);
            } else {
                ESP_LOGE(TAG, "[Attempt %d/%d] recvfrom failed: errno %d", attempt, MAX_DISCOVERY_RETRIES, errno);
            }
            vTaskDelay(pdMS_TO_TICKS(10000)); // delay 10s before next retry
            continue;
        }
		
		rx_buffer[len] = '\0'; // Null terminate
		ESP_LOGI(TAG, "Received %d bytes from %s:", len, inet_ntoa(source_addr.sin_addr));
		ESP_LOGI(TAG, "%s", rx_buffer);
		
		// Check for "SERVER_ACK:<PORT>"
		if (strncmp(rx_buffer, "SERVER_ACK:", 11) == 0) {
			inet_ntoa_r(source_addr.sin_addr, server_ip, 16);
			server_port = atoi(rx_buffer + 11);
			ESP_LOGI(TAG, "Discovered Server IP: %s, HTTP Port: %d", server_ip, server_port);
			server_found = true;
			break;
		}
	}
	if (!server_found) {
        ESP_LOGE(TAG, "Discovery failed after %d attempts. Server unreachable.", MAX_DISCOVERY_RETRIES);
    }
	close(sock);
	return server_found;
}

esp_err_t ServerComm::_http_event_handler(esp_http_client_event_t *evt)
{
    char **response_data = (char **)evt->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Determine current length or start fresh
                size_t old_len = (*response_data) ? strlen(*response_data) : 0;
                size_t new_len = old_len + evt->data_len;

                char *new_buf = (char *)realloc(*response_data, new_len + 1);
                if (new_buf) {
                    *response_data = new_buf;
                    memcpy(*response_data + old_len, evt->data, evt->data_len);
                    (*response_data)[new_len] = '\0';
                } else {
                    ESP_LOGE(TAG, "Failed to allocate memory for HTTP response");
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

bool ServerComm::connect_server(char* server_ip, int &server_port)
{
	if (server_ip[0] == '\0' || server_port == 0) {
	    if (_discover_server(server_ip, server_port)) {
	        ESP_LOGI(TAG, "Server discovery successful.");
			return true;
	    } else {
	        ESP_LOGW(TAG, "Server discovery failed.");
			return false;
	    }
	}
	else {
		ESP_LOGI(TAG, "Server already discovered. IP: %s, Port: %d", server_ip, server_port);
		return true;
	}
}

/*
	return true if have file changes
	false for no changes.
	Client send following JSON:
	
	{
	  "mac": "24:DC:C3:A1:B2:C3",
	  "images": [
	    { "name": "photo01.bin"},
	    { "name": "photo02.bin"}
	  ]
	}
	
	Server reply following JSON:
	
	{
	  "new": [
	    { "name": "photo03.bin", "url": "/api/image/A1B2C3D4E5/photo03.bin" }
	  ],
	  "delete": [
	    "photo01.bin"
	  ]
	}
	
*/
// ============================================================================
// Private Helper 1: HTTP Request Transmission
// ============================================================================
esp_err_t ServerComm::_send_sync_request(const char *server_ip, const int server_port, char **out_response_data)
{
    FileHandler fileHandler;

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/api/sync", server_ip, server_port);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "mac", _get_mac_address());

    cJSON *images = fileHandler.generate_files_json(AppConfig::STORAGE_PATH);
    cJSON_AddItemToObject(root, "images", images);

    char *json_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_body) {
        ESP_LOGE(TAG, "Failed to render sync JSON string");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sending Sync Payload:\n%s", json_body);

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.event_handler = _http_event_handler;
    config.user_data = out_response_data; // Pass char** directly

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    _set_http_header(client);
    esp_http_client_set_post_field(client, json_body, strlen(json_body));

    esp_err_t err = esp_http_client_perform(client);

    free(json_body);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP POST Status = %d", status_code);
        if (status_code != 200) {
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

// ============================================================================
// Private Helper 2: JSON Response Processing
// ============================================================================
bool ServerComm::_process_sync_response(const char *response_data, const char *server_ip, const int server_port, uint16_t &sleep_duration_min)
{
    bool file_changed = false;
    FileHandler fileHandler;

    cJSON *response_json = cJSON_Parse(response_data);
    if (!response_json) {
        ESP_LOGE(TAG, "Failed to parse sync response JSON");
        return file_changed;
    }

    // 1. Extract sleepDurationMin
    cJSON *sleep_item = cJSON_GetObjectItem(response_json, "sleepDurationMin");
    if (cJSON_IsNumber(sleep_item) && sleep_item->valueint > 0) {
        sleep_duration_min = static_cast<uint16_t>(sleep_item->valueint);
        ESP_LOGI(TAG, "Updated sleep duration from server: %u minutes", sleep_duration_min);
    }

    // 2. Process 'delete' array
    cJSON *delete_list = cJSON_GetObjectItem(response_json, "delete");
    if (cJSON_IsArray(delete_list)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, delete_list) {
            if (cJSON_IsString(item) && item->valuestring) {
                char path_to_del[128];
                snprintf(path_to_del, sizeof(path_to_del), "%s/%s", AppConfig::STORAGE_PATH, item->valuestring);
                ESP_LOGI(TAG, "Deleting old file: %s", path_to_del);
                remove(path_to_del);
                file_changed = true;
            }
        }
    }

	// 3. Process 'new' download list (Reverse order: from end of array to front)
    cJSON *new_list = cJSON_GetObjectItem(response_json, "new");
    if (cJSON_IsArray(new_list)) {
        int array_size = cJSON_GetArraySize(new_list);

        for (int i = array_size - 1; i >= 0; i--) {
            cJSON *item = cJSON_GetArrayItem(new_list, i);
            if (!item) continue;

            cJSON *name = cJSON_GetObjectItem(item, "name");
            cJSON *url = cJSON_GetObjectItem(item, "url");

            if (cJSON_IsString(name) && cJSON_IsString(url)) {
                ESP_LOGI(TAG, "Queued for download: %s from %s", name->valuestring, url->valuestring);
                std::string filepath = get_image(server_ip, server_port, name->valuestring, url->valuestring);
                if (!filepath.empty()) {
                    file_changed = true;
                }
            }
        }
    }

    // 4. Process server-driven 'playlist' array
    cJSON *playlist_json = cJSON_GetObjectItem(response_json, "playlist");
    if (cJSON_IsArray(playlist_json)) {
        std::vector<std::string> server_playlist;
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, playlist_json) {
            if (cJSON_IsString(item) && item->valuestring) {
                server_playlist.push_back(item->valuestring);
            }
        }

        // Only rewrite if local playlist.idx differs from server playlist
        if (!fileHandler.playlist_matches(AppConfig::STORAGE_PATH, server_playlist)) {
            fileHandler.write_playlist_index(AppConfig::STORAGE_PATH, server_playlist);
            ESP_LOGI(TAG, "Playlist changed. Rewrote playlist.idx with %zu items.", server_playlist.size());
        } else {
            ESP_LOGI(TAG, "Playlist unchanged. Skipped flash write.");
        }
    } 
    else if (file_changed) {
        // Fallback: Rebuild via local mtime scan if server playlist key is absent
        fileHandler.rebuild_playlist_index(AppConfig::STORAGE_PATH);
        ESP_LOGI(TAG, "Fallback: Rebuilt playlist index via local mtime scan");
    }

    cJSON_Delete(response_json);
    return file_changed;
}

// ============================================================================
// Public Orchestration Method
// ============================================================================
bool ServerComm::sync_image_list(const char *server_ip, const int server_port, uint16_t &sleep_duration_min)
{
    char *response_data = NULL;

    esp_err_t err = _send_sync_request(server_ip, server_port, &response_data);
    bool file_changed = false;

    if (err == ESP_OK && response_data) {
        file_changed = _process_sync_response(response_data, server_ip, server_port, sleep_duration_min);
    }

    // Free the dynamically allocated string buffer directly
    if (response_data) {
        free(response_data);
    }

    return file_changed;
}

const std::string ServerComm::get_image(const char *server_ip, const int server_port, const char *filename, const char *file_url)
{
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d%s", server_ip, server_port, file_url);

	// 1. Configure HTTP client
    esp_http_client_config_t config = {};
    config.url = url;
	config.timeout_ms = 60000;
    esp_http_client_handle_t client = esp_http_client_init(&config);
	
	// 2. Attach MAC address header
	_set_http_header(client);

	// 3. Perform request
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return "";
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0) {
        ESP_LOGE(TAG, "Invalid content length received");
        esp_http_client_cleanup(client);
        return "";
    }

	// 4. Store the incoming image data to storage (LittleFS)
	char filepath[40];
	snprintf(filepath, sizeof(filepath), "%s/%s", AppConfig::STORAGE_PATH, filename);
	ESP_LOGI(TAG, "File to store: %s", filepath);
    FILE *f = fopen(filepath, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open LittleFS file for writing");
        esp_http_client_cleanup(client);
        return "";
    }

    char buffer[1024];
    int read_bytes = 0;
    int total_read = 0;

    // Stream directly into flash
    while ((read_bytes = esp_http_client_read(client, buffer, sizeof(buffer))) > 0) {
        fwrite(buffer, 1, read_bytes, f);
        total_read += read_bytes;
    }

    fclose(f);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "Successfully saved %d bytes to %s", total_read, filepath);
    return filepath;
}