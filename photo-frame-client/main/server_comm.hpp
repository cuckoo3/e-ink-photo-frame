#pragma once

#include <string>
#include "esp_http_client.h"

class ServerComm {
	public:
		bool connect_server(char* server_ip, int &server_port);
		
		/**
		 * @brief Fetch display data from the server
		 *
		 * Makes an HTTP GET request to retrieve display data from the server.
		 */
//		static void fetch_display_data(const char *server_ip, int server_port);

		bool sync_image_list(const char *server_ip, const int server_port);
		
		const std::string get_image(const char *server_ip, const int server_port, const char *filename, const char *file_url);
		
	private:
		char *mac_str = NULL;
		
		/**
		 * @brief Discover the server on the network
		 *
		 * Sends a multicast discovery message and waits for a response from the server.
		 *
		 * @return true if the server was discovered successfully, false otherwise
		 */
		bool _discover_server(char* server_ip, int &server_port);

		static esp_err_t _http_event_handler(esp_http_client_event_t *evt);
		
		const char * _get_mac_address(void);
		
		void _set_http_header(const esp_http_client_handle_t *client_ptr);
		
};
