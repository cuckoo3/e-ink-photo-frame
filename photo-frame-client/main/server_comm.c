/*
 * server_comm.c
 *
 *  Created on: Sep 8, 2026
 *      Author: Cuckoo
 */
 #include <string.h>
 #include <sys/param.h>
 #include "esp_system.h"
 #include "esp_event.h"
 #include "esp_log.h"
 #include "nvs_flash.h"
 #include "lwip/err.h"
 #include "lwip/sockets.h"
 #include "esp_http_client.h"
 #include "cJSON.h"
 
 #include "server_comm.h"
 
 static EventGroupHandle_t s_wifi_event_group;
 static char server_ip_str[16] = {0};
 static int server_http_port = 0;
 
 // =========================================================================
 // UDP Multicast Discovery
 // =========================================================================
 bool discover_server(void)
 {
     int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
     if (sock < 0) {
         ESP_LOGE(TAG_SERVER_COMM, "Unable to create UDP socket: errno %d", errno);
         return false;
     }

     // Set timeout on receive operations (2 seconds)
     struct timeval tv;
     tv.tv_sec = 2;
     tv.tv_usec = 0;
     setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

     struct sockaddr_in dest_addr;
     dest_addr.sin_addr.s_addr = inet_addr(MULTICAST_IPV4);
     dest_addr.sin_family = AF_INET;
     dest_addr.sin_port = htons(UDP_PORT);

     // Send multicast packet
     int err = sendto(sock, DISCOVERY_MSG, strlen(DISCOVERY_MSG), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
     if (err < 0) {
         ESP_LOGE(TAG_SERVER_COMM, "Error occurred during sending: errno %d", errno);
         close(sock);
         return false;
     }
     ESP_LOGI(TAG_SERVER_COMM, "Sent UDP discovery packet...");

     // Wait for response
     char rx_buffer[128];
     struct sockaddr_in source_addr;
     socklen_t socklen = sizeof(source_addr);
     int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

     if (len < 0) {
         ESP_LOGE(TAG_SERVER_COMM, "recvfrom failed or timed out: errno %d", errno);
         close(sock);
         return false;
     }

     rx_buffer[len] = '\0'; // Null terminate
     ESP_LOGI(TAG_SERVER_COMM, "Received %d bytes from %s:", len, inet_ntoa(source_addr.sin_addr));
     ESP_LOGI(TAG_SERVER_COMM, "%s", rx_buffer);

     // Check for "SERVER_ACK:<PORT>"
     if (strncmp(rx_buffer, "SERVER_ACK:", 11) == 0) {
         inet_ntoa_r(source_addr.sin_addr, server_ip_str, sizeof(server_ip_str));
         server_http_port = atoi(rx_buffer + 11);
         ESP_LOGI(TAG_SERVER_COMM, "Discovered Server IP: %s, HTTP Port: %d", server_ip_str, server_http_port);
         close(sock);
         return true;
     }

     close(sock);
     return false;
 }

 // =========================================================================
 // HTTP Client Execution
 // =========================================================================
 void fetch_display_data(void)
 {
     char url[128];
     snprintf(url, sizeof(url), "http://%s:%d/api/display-data", server_ip_str, server_http_port);

     esp_http_client_config_t config = {
         .url = url,
         .timeout_ms = 5000,
     };
     esp_http_client_handle_t client = esp_http_client_init(&config);

     esp_err_t err = esp_http_client_perform(client);
     if (err == ESP_OK) {
         int status_code = esp_http_client_get_status_code(client);
         int content_length = esp_http_client_get_content_length(client);
         ESP_LOGI(TAG_SERVER_COMM, "HTTP GET Status = %d, content_length = %d", status_code, content_length);

         if (status_code == 200) {
             char buffer[512] = {0};
             int read_len = esp_http_client_read(client, buffer, sizeof(buffer) - 1);
             if (read_len > 0) {
                 buffer[read_len] = '\0';
                 
                 // Parse JSON response using cJSON
                 cJSON *root = cJSON_Parse(buffer);
                 if (root) {
                     cJSON *msg = cJSON_GetObjectItem(root, "message");
                     cJSON *updated = cJSON_GetObjectItem(root, "updatedAt");

                     if (cJSON_IsString(msg) && cJSON_IsString(updated)) {
                         ESP_LOGI(TAG_SERVER_COMM, "Parsed Message: %s", msg->valuestring);
                         ESP_LOGI(TAG_SERVER_COMM, "Parsed Timestamp: %s", updated->valuestring);
                         
                         // TODO: Pass text string or image renderer to E-Paper driver here
                     }
                     cJSON_Delete(root);
                 }
             }
         }
     } else {
         ESP_LOGE(TAG_SERVER_COMM, "HTTP GET request failed: %s", esp_err_to_name(err));
     }

     esp_http_client_cleanup(client);
 }
