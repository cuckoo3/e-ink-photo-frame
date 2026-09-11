#pragma once

#include "esp_http_client.h"

class ServerComm {
	public:
		void connect_server(char* server_ip, int &server_port);
		
		/**
		 * @brief Fetch display data from the server
		 *
		 * Makes an HTTP GET request to retrieve display data from the server.
		 */
		static void fetch_display_data(const char *server_ip, int server_port);
		
		char* get_image(const char *server_ip, int server_port);
		
	private:
		char *mac_str = NULL;
		
		/**
		 * @brief Discover the server on the network
		 *
		 * Sends a multicast discovery message and waits for a response from the server.
		 *
		 * @return true if the server was discovered successfully, false otherwise
		 */
		static bool discover_server(char* server_ip, int &server_port);

		static esp_err_t http_event_handler(esp_http_client_event_t *evt);
		
		const char * get_mac_address(void);
};
