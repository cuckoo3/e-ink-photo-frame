#include <string>
#include <sys/param.h>
#include "driver/dedic_gpio.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "esp_http_client.h"
#include "cJSON.h"

#include "app_config.hpp"
#include "server_comm.hpp"

inline static constexpr char DISCOVERY_MSG[] = "DISCOVER_ESP_SERVER";

inline static constexpr char TAG[] = "SERVER_COMM";
// Retry configuration
inline static constexpr int MAX_DISCOVERY_RETRIES = 5;

// =========================================================================
// UDP Multicast Discovery
// =========================================================================
bool ServerComm::discover_server(char* server_ip, int &server_port)
{
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
	
	close(sock);
	return server_found;
}

// Event handler to append incoming chunked response data
esp_err_t ServerComm::http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        std::string *response_body = static_cast<std::string *>(evt->user_data);
        if (response_body != nullptr && evt->data != nullptr) {
            response_body->append(static_cast<char *>(evt->data), evt->data_len);
        }
    }
    return ESP_OK;
}
// =========================================================================
// HTTP Client Execution
// =========================================================================
void ServerComm::fetch_display_data(const char *server_ip, int server_port)
{
	char url[128];
	snprintf(url, sizeof(url), "http://%s:%d/api/display-data", server_ip, server_port);
	
	std::string response_body = "";

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 5000;
    config.event_handler = http_event_handler;
    config.user_data = &response_body;
	
	esp_http_client_handle_t client = esp_http_client_init(&config);
	
	esp_err_t err = esp_http_client_perform(client);
	if (err == ESP_OK) {
	    int status_code = esp_http_client_get_status_code(client);
	    ESP_LOGI(TAG, "HTTP GET Status = %d, Received Bytes = %zu", status_code, response_body.length());
	
		if (status_code == 200 && !response_body.empty()) {
	        ESP_LOGI(TAG, "RAW Payload: %s", response_body.c_str());
	
	        // Parse JSON payload
	        cJSON *root = cJSON_Parse(response_body.c_str());
	        if (root) {
	            cJSON *msg = cJSON_GetObjectItem(root, "message");
	            cJSON *updated = cJSON_GetObjectItem(root, "updatedAt");
	
	            if (cJSON_IsString(msg) && (msg->valuestring != NULL)) {
	                ESP_LOGI(TAG, "Parsed Message: %s", msg->valuestring);
	            } else {
	                ESP_LOGW(TAG, "Field 'message' missing or invalid format");
	            }
	
	            if (cJSON_IsString(updated) && (updated->valuestring != NULL)) {
	                ESP_LOGI(TAG, "Parsed Timestamp: %s", updated->valuestring);
	            } else {
	                ESP_LOGW(TAG, "Field 'updatedAt' missing or invalid format");
	            }
	
	            cJSON_Delete(root);
	        } else {
	            ESP_LOGE(TAG, "Failed to parse JSON payload");
	        }
	    } else {
	        ESP_LOGW(TAG, "HTTP response status code is not 200 or body is empty");
	    }
	} else {
	    ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
	}
	
	esp_http_client_cleanup(client);
}


//void ServerComm::start_background_sync_task(void)
//{
//    // Create FreeRTOS task running the background sync entry
//    xTaskCreate(&ServerComm::sync_task_entry, "server_sync_task", 4096, this, 5, NULL);
//}

void ServerComm::connect_server(char* server_ip, int &server_port)
{
    if (discover_server(server_ip, server_port)) {
        ESP_LOGI(TAG, "Server discovery successful. Fetching display data...");
        fetch_display_data(server_ip, server_port);
    } else {
        ESP_LOGW(TAG, "Server discovery failed.");
    }
}