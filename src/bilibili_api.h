#ifndef BILIBILI_API_H
#define BILIBILI_API_H

#include <obs-module.h>
#include <cJSON.h>

struct bilibili_api;

// Structure for libcurl write callback
struct write_data {
    char* buffer;
    size_t size;
};

// libcurl write callback function
size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);

struct bilibili_api* bilibili_api_create(void);
void bilibili_api_destroy(struct bilibili_api* api);
cJSON* bilibili_api_get_qrcode_data(struct bilibili_api* api);
int bilibili_api_check_qr_login(struct bilibili_api* api, const char* qrcode_key, cJSON** cookies);
bool bilibili_api_start_live(struct bilibili_api* api, int room_id, const char* csrf, int area_v2, const char* cookies, cJSON** result);
bool bilibili_api_stop_live(struct bilibili_api* api, int room_id, const char* csrf, const char* cookies);
void bilibili_api_start_qr_login(struct bilibili_api* api, void* source);

#endif // BILIBILI_API_H