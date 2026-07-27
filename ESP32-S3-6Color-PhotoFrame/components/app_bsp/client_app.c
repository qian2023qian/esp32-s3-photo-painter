#include "client_app.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>
#include <stdlib.h>
#include <esp_wifi.h>

static const char *TAG = "client_bsp";

// Define the fixed weather API URL
#define WEATHER_API_URL "http://t.weather.sojson.com/api/weather/city/101280601"
// Maximum size of the HTTP response buffer
#define MAX_HTTP_RESPONSE_BUFFER (1024 * 8) // 8KB buffer
// HTTP request timeout in milliseconds
#define HTTP_REQUEST_TIMEOUT 10000
// Maximum number of retry attempts
#define MAX_RETRY_ATTEMPTS 1

/**
 * @brief HTTP event handler
 * @param evt HTTP client event
 * @return ESP_OK on success, other values on error
 */
static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    static int output_len = 0;
    
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP_EVENT_ERROR - Error occurred during HTTP request");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        output_len = 0;
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        // Check for content-length header
        if (strcmp(evt->header_key, "Content-Length") == 0) {
            int content_len = atoi(evt->header_value);
            ESP_LOGI(TAG, "Content-Length: %d bytes", content_len);
        }
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d, chunked=%d", 
                 evt->data_len, esp_http_client_is_chunked_response(evt->client));
        
        // Copy response data to buffer - handle both chunked and non-chunked responses
        if (evt->user_data) {
            int copy_len = evt->data_len;
            if (output_len + copy_len > MAX_HTTP_RESPONSE_BUFFER) {
                copy_len = MAX_HTTP_RESPONSE_BUFFER - output_len;
                ESP_LOGW(TAG, "Response buffer almost full, truncating data. Current length: %d, copying: %d bytes", output_len, copy_len);
            }
            
            if (copy_len > 0) {
                memcpy(evt->user_data + output_len, evt->data, copy_len);
                output_len += copy_len;
                // Add null terminator after each data chunk
                ((char*)evt->user_data)[output_len] = '\0';
            }
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH. Total data received: %d bytes", output_len);
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    default:
        break;
    }
    
    return ESP_OK;
}

/**
 * @brief Get weather data from the fixed API URL
 * @return Weather data JSON string, or NULL on failure
 * @note The caller must free the returned memory using free_weather_data()
 */
const char *get_weather_data(void) {
    ESP_LOGI(TAG, "=== Starting weather data retrieval ===");
    
    // Check WiFi connection status
    wifi_ap_record_t ap_info;
    memset(&ap_info, 0, sizeof(ap_info));
    esp_err_t wifi_err = esp_wifi_sta_get_ap_info(&ap_info);
    if (wifi_err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected to: %s", ap_info.ssid);
    } else {
        ESP_LOGE(TAG, "WiFi not connected: %s", esp_err_to_name(wifi_err));
        return NULL;
    }
    
    ESP_LOGI(TAG, "Requesting weather data from: %s", WEATHER_API_URL);
    
    char *response_buffer = NULL;
    esp_http_client_handle_t client = NULL;
    int retry_count = 0;
    esp_err_t err = ESP_FAIL;
    
    while (retry_count < MAX_RETRY_ATTEMPTS) {
        // Allocate memory for response buffer
        ESP_LOGD(TAG, "Allocating %d bytes for response buffer", MAX_HTTP_RESPONSE_BUFFER + 1);
        // Use heap_caps_malloc with SPIRAM capability for better memory management
        response_buffer = (char *)heap_caps_malloc(MAX_HTTP_RESPONSE_BUFFER + 1, MALLOC_CAP_SPIRAM);
        if (!response_buffer) {
            ESP_LOGE(TAG, "Failed to allocate memory for response buffer");
            // Try regular malloc if SPIRAM allocation fails
            response_buffer = (char *)malloc(MAX_HTTP_RESPONSE_BUFFER + 1);
            if (!response_buffer) {
                ESP_LOGE(TAG, "Failed to allocate memory using regular malloc");
                return NULL;
            } else {
                ESP_LOGW(TAG, "Successfully allocated memory using regular malloc");
            }
        } else {
            ESP_LOGD(TAG, "Successfully allocated memory from SPIRAM");
        }
        memset(response_buffer, 0, MAX_HTTP_RESPONSE_BUFFER + 1);
        
        // Configure HTTP client
        ESP_LOGD(TAG, "Configuring HTTP client with %dms timeout", HTTP_REQUEST_TIMEOUT);
        esp_http_client_config_t config = {
            .url = WEATHER_API_URL,
            .event_handler = _http_event_handler,
            .user_data = response_buffer,
            .timeout_ms = HTTP_REQUEST_TIMEOUT,
            .disable_auto_redirect = false,
        };
        
        // Initialize HTTP client
        client = esp_http_client_init(&config);
        if (client) {
            // Add User-Agent header to mimic a browser request
            esp_http_client_set_header(client, "User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
            // Add Accept header
            esp_http_client_set_header(client, "Accept", "application/json");
        } else {
            ESP_LOGE(TAG, "Failed to initialize HTTP client");
            free(response_buffer);
            response_buffer = NULL;
            retry_count++;
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        
        // Perform HTTP request
        ESP_LOGI(TAG, "Performing HTTP request... (Attempt %d/%d)", retry_count + 1, MAX_RETRY_ATTEMPTS);
        err = esp_http_client_perform(client);
        
        if (err == ESP_OK) {
            int status_code = esp_http_client_get_status_code(client);
            int content_len = esp_http_client_get_content_length(client);
            ESP_LOGI(TAG, "HTTP request completed with status: %d, content length: %d", status_code, content_len);
            
            // Always log response buffer status
            int buffer_len = response_buffer ? strlen(response_buffer) : 0;
            ESP_LOGI(TAG, "Response buffer length: %d bytes", buffer_len);
            
            if (status_code == 200) {
                // Log the first 100 characters of response for debugging
                if (buffer_len > 0) {
                    int log_len = buffer_len < 100 ? buffer_len : 100;
                    ESP_LOGI(TAG, "Response data (first %d chars): %.*s", log_len, log_len, response_buffer);
                } else {
                    ESP_LOGW(TAG, "Response buffer is empty even though status code is 200");
                }
                ESP_LOGI(TAG, "Weather data obtained successfully");
                break; // Success, exit retry loop
            } else {
                ESP_LOGE(TAG, "HTTP request failed with status code: %d", status_code);
                err = ESP_FAIL;
            }
        } else {
            ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        }
        
        // Clean up on failure
        esp_http_client_cleanup(client);
        free(response_buffer);
        response_buffer = NULL;
        client = NULL;
        
        retry_count++;
        ESP_LOGI(TAG, "Retrying (%d/%d)...", retry_count, MAX_RETRY_ATTEMPTS);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
    // Clean up and return result
    if (client) {
        esp_http_client_cleanup(client);
    }
    
    if (err != ESP_OK || !response_buffer) {
        ESP_LOGE(TAG, "Failed to get weather data after %d attempts", MAX_RETRY_ATTEMPTS);
        if (response_buffer) {
            ESP_LOGD(TAG, "Freeing response buffer due to failure");
            // Free memory using the same method that allocated it
            heap_caps_free(response_buffer);
            response_buffer = NULL;
        }
        return NULL;
    }
    
    // Final check before returning
    if (strlen(response_buffer) == 0) {
        ESP_LOGE(TAG, "Response buffer is empty even though status code is 200");
        heap_caps_free(response_buffer);
        return NULL;
    }
    
    ESP_LOGI(TAG, "=== Weather data retrieval completed successfully ===");
    
    return response_buffer;
}