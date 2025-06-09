#include "bilibili_api.h"
#include <curl/curl.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

struct bilibili_api {
    char* user_agent;
    struct curl_slist* headers;
};

struct write_data {
    char* buffer;
    size_t size;
};

static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    struct write_data* data = (struct write_data*)userdata;
    size_t new_size = data->size + size * nmemb;
    data->buffer = (char*)brealloc(data->buffer, new_size + 1);
    memcpy(data->buffer + data->size, ptr, size * nmemb);
    data->size = new_size;
    data->buffer[new_size] = '\0';
    return size * nmemb;
}

struct bilibili_api* bilibili_api_create(void) {
    struct bilibili_api* api = (struct bilibili_api*)bzalloc(sizeof(struct bilibili_api));
    api->user_agent = bstrdup("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/96.0.4664.110 Safari/537.36");
    api->headers = curl_slist_append(NULL, "accept: application/json, text/plain, */*");
    api->headers = curl_slist_append(api->headers, "accept-language: zh-CN,zh;q=0.9,en;q=0.8,en-GB;q=0.7,en-US;q=0.6");
    api->headers = curl_slist_append(api->headers, "content-type: application/x-www-form-urlencoded; charset=UTF-8");
    api->headers = curl_slist_append(api->headers, "origin: https://link.bilibili.com");
    api->headers = curl_slist_append(api->headers, "referer: https://link.bilibili.com/p/center/index");
    return api;
}

void bilibili_api_destroy(struct bilibili_api* api) {
    if (api) {
        if (api->user_agent) bfree(api->user_agent);
        if (api->headers) curl_slist_free_all(api->headers);
        bfree(api);
    }
}

cJSON* bilibili_api_get_qrcode_data(struct bilibili_api* api) {
    CURL* curl = curl_easy_init();
    struct write_data data = {0};
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://passport.bilibili.com/x/passport-login/web/qrcode/generate");
        curl_easy_setopt(curl, CURLOPT_USERAGENT, api->user_agent);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, api->headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (res != CURLE_OK) {
            obs_log(LOG_ERROR, "获取二维码失败: %s", curl_easy_strerror(res));
            if (data.buffer) bfree(data.buffer);
            return NULL;
        }
    }
    cJSON* json = cJSON_Parse(data.buffer);
    if (data.buffer) bfree(data.buffer);
    if (!json) {
        obs_log(LOG_ERROR, "JSON 解析失败");
        return NULL;
    }
    return json;
}

int bilibili_api_check_qr_login(struct bilibili_api* api, const char* qrcode_key, cJSON** cookies) {
    CURL* curl = curl_easy_init();
    struct write_data data = {0};
    char url[256];
    snprintf(url, sizeof(url), "https://passport.bilibili.com/x/passport-login/web/qrcode/poll?qrcode_key=%s", qrcode_key);
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, api->user_agent);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, api->headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (res != CURLE_OK) {
            obs_log(LOG_ERROR, "检查二维码登录失败: %s", curl_easy_strerror(res));
            if (data.buffer) bfree(data.buffer);
            return -1;
        }
    }
    cJSON* json = cJSON_Parse(data.buffer);
    if (data.buffer) bfree(data.buffer);
    if (!json) {
        obs_log(LOG_ERROR, "JSON 解析失败");
        return -1;
    }
    cJSON* code = cJSON_GetObjectItem(cJSON_GetObjectItem(json, "data"), "code");
    int status_code = code ? code->valueint : -1;
    if (status_code == 0) {
        *cookies = cJSON_CreateObject();
        cJSON* sessdata = cJSON_GetObjectItem(cJSON_GetObjectItem(json, "data"), "SESSDATA");
        cJSON_AddStringToObject(*cookies, "SESSDATA", sessdata ? sessdata->valuestring : "");
    }
    cJSON_Delete(json);
    return status_code;
}

bool bilibili_api_start_live(struct bilibili_api* api, int room_id, const char* csrf, int area_v2, const char* cookies, cJSON** result) {
    CURL* curl = curl_easy_init();
    struct write_data data = {0};
    char post_data[256];
    snprintf(post_data, sizeof(post_data), "room_id=%d&platform=android_link&area_v2=%d&csrf_token=%s&csrf=%s",
             room_id, area_v2, csrf, csrf);
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.live.bilibili.com/room/v1/Room/startLive");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
        curl_easy_setopt(curl, CURLOPT_COOKIE, cookies);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, api->user_agent);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, api->headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (res != CURLE_OK) {
            obs_log(LOG_ERROR, "开始直播失败: %s", curl_easy_strerror(res));
            if (data.buffer) bfree(data.buffer);
            return false;
        }
    }
    *result = cJSON_Parse(data.buffer);
    if (data.buffer) bfree(data.buffer);
    if (!*result) {
        obs_log(LOG_ERROR, "JSON 解析失败");
        return false;
    }
    cJSON* code = cJSON_GetObjectItem(*result, "code");
    return code && code->valueint == 0;
}

bool bilibili_api_stop_live(struct bilibili_api* api, int room_id, const char* csrf, const char* cookies) {
    CURL* curl = curl_easy_init();
    struct write_data data = {0};
    char post_data[256];
    snprintf(post_data, sizeof(post_data), "room_id=%d&platform=android_link&csrf_token=%s&csrf=%s",
             room_id, csrf, csrf);
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.live.bilibili.com/room/v1/Room/stopLive");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
        curl_easy_setopt(curl, CURLOPT_COOKIE, cookies);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, api->user_agent);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, api->headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (res != CURLE_OK) {
            obs_log(LOG_ERROR, "停止直播失败: %s", curl_easy_strerror(res));
            if (data.buffer) bfree(data.buffer);
            return false;
        }
    }
    cJSON* json = cJSON_Parse(data.buffer);
    if (data.buffer) bfree(data.buffer);
    if (!json) {
        obs_log(LOG_ERROR, "JSON 解析失败");
        return false;
    }
    cJSON* code = cJSON_GetObjectItem(json, "code");
    bool success = code && code->valueint == 0;
    cJSON_Delete(json);
    return success;
}

static void* qr_check_thread(void* arg) {
    struct qr_check_data* check_data = (struct qr_check_data*)arg;
    struct bilibili_source* source = check_data->source;
    const char* qrcode_key = check_data->qrcode_key;

    while (!source->logged_in) {
        cJSON* cookies = NULL;
        int status = bilibili_api_check_qr_login(source->api, qrcode_key, &cookies);
        if (status == 0 && cookies) {
            char* cookies_str = cJSON_PrintUnformatted(cookies);
            source->cookies = bstrdup(cookies_str);
            source->logged_in = true;
            obs_data_t* settings = obs_data_create();
            obs_data_set_string(settings, "cookies", source->cookies);
            // 保存到 OBS 配置目录
            char* config_path = obs_module_config_path("bilibili_config.json");
            obs_data_save_json(settings, config_path);
            bfree(config_path);
            obs_data_release(settings);
            bfree(cookies_str);
            cJSON_Delete(cookies);
            break;
        }
        if (cookies) cJSON_Delete(cookies);
        sleep(2); // 每 2 秒检查一次
    }
    bfree(check_data->qrcode_key);
    bfree(check_data);
    return NULL;
}

void bilibili_api_start_qr_login(struct bilibili_api* api, void* source) {
    struct bilibili_source* src = (struct bilibili_source*)source;
    cJSON* qr_data = bilibili_api_get_qrcode_data(api);
    if (qr_data && cJSON_GetObjectItem(qr_data, "data")) {
        cJSON* url = cJSON_GetObjectItem(cJSON_GetObjectItem(qr_data, "data"), "url");
        cJSON* qrcode_key = cJSON_GetObjectItem(cJSON_GetObjectItem(qr_data, "data"), "qrcode_key");
        if (url && qrcode_key) {
            // 加载二维码图片为纹理
            src->texture = load_image_texture(url->valuestring);
            struct qr_check_data* check_data = (struct qr_check_data*)bzalloc(sizeof(struct qr_check_data));
            check_data->source = src;
            check_data->qrcode_key = bstrdup(qrcode_key->valuestring);
            pthread_t thread;
            pthread_create(&thread, NULL, qr_check_thread, check_data);
            pthread_detach(thread);
        }
    }
    if (qr_data) cJSON_Delete(qr_data);
}