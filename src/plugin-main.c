/*
Plugin Name
Copyright (C) <Year> <Developer> <Email Address>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include "bilibili_api.h"
#include <pthread.h>
#include <string.h>
#include <stdlib.h>

// stb_image 头文件（需下载到 external/stb/stb_image.h）
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define PLUGIN_NAME "MyBilibiliPlugin"
#define PLUGIN_VERSION "1.0.0"

// 自定义源数据结构
struct bilibili_source {
    obs_source_t* source;
    gs_texture_t* texture; // 用于渲染二维码
    bool logged_in;
    char* cookies;
    struct bilibili_api* api;
};

// 线程参数，用于二维码登录检查
struct qr_check_data {
    struct bilibili_source* source;
    char* qrcode_key;
};

// 加载图片为 OBS 纹理
static gs_texture_t* load_image_texture(const char* url) {
    CURL* curl = curl_easy_init();
    struct write_data data = {0};
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (res != CURLE_OK) {
            obs_log(LOG_ERROR, "下载二维码图片失败: %s", curl_easy_strerror(res));
            if (data.buffer) bfree(data.buffer);
            return NULL;
        }
    }

    int width, height, channels;
    unsigned char* image_data = stbi_load_from_memory((unsigned char*)data.buffer, data.size, &width, &height, &channels, 4);
    if (data.buffer) bfree(data.buffer);
    if (!image_data) {
        obs_log(LOG_ERROR, "无法加载二维码图片");
        return NULL;
    }

    gs_texture_t* texture = gs_texture_create(width, height, GS_RGBA, 1, (const uint8_t**)&image_data, 0);
    stbi_image_free(image_data);
    return texture;
}

// 源回调函数
static const char* bilibili_source_get_name(void* unused) {
    UNUSED_PARAMETER(unused);
    return obs_module_text("BilibiliLoginSource");
}

static void* bilibili_source_create(obs_data_t* settings, obs_source_t* source) {
    struct bilibili_source* data = (struct bilibili_source*)bzalloc(sizeof(struct bilibili_source));
    data->source = source;
    data->api = bilibili_api_create();

    // 检查 OBS 配置中的 Cookies
    const char* cookies = obs_data_get_string(settings, "cookies");
    if (cookies && strlen(cookies) > 0) {
        data->cookies = bstrdup(cookies);
        data->logged_in = true;
    } else {
        // 启动二维码登录流程
        bilibili_api_start_qr_login(data->api, data);
    }
    return data;
}

static void bilibili_source_destroy(void* data) {
    struct bilibili_source* source = (struct bilibili_source*)data;
    if (source->texture) {
        gs_texture_destroy(source->texture);
    }
    if (source->cookies) {
        bfree(source->cookies);
    }
    bilibili_api_destroy(source->api);
    bfree(data);
}

static void bilibili_source_video_render(void* data, gs_effect_t* effect) {
    struct bilibili_source* source = (struct bilibili_source*)data;
    if (source->texture) {
        gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), source->texture);
        gs_draw_sprite(source->texture, 0, 0, 0);
    }
}

static void bilibili_source_update(void* data, obs_data_t* settings) {
    struct bilibili_source* source = (struct bilibili_source*)data;
    const char* cookies = obs_data_get_string(settings, "cookies");
    if (cookies && strlen(cookies) > 0) {
        if (source->cookies) {
            bfree(source->cookies);
        }
        source->cookies = bstrdup(cookies);
        source->logged_in = true;
    }
}

// 源定义
struct obs_source_info bilibili_source_info = {
    .id = "bilibili_login_source",
    .type = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_ASYNC_VIDEO,
    .get_name = bilibili_source_get_name,
    .create = bilibili_source_create,
    .destroy = bilibili_source_destroy,
    .video_render = bilibili_source_video_render,
    .update = bilibili_source_update,
};

// 菜单回调
static void start_live_callback(void* data) {
    struct bilibili_source* source = (struct bilibili_source*)data;
    if (source->logged_in) {
        int room_id = 12345; // 替换为实际房间 ID（可从配置读取）
        int area_v2 = 1; // 替换为实际分区 ID
        const char* csrf = "your_csrf_token"; // 从 Cookies 提取
        cJSON* result = NULL;
        bool success = bilibili_api_start_live(source->api, room_id, csrf, area_v2, source->cookies, &result);
        if (success) {
            obs_log(LOG_INFO, "直播开始成功");
        } else {
            char* result_str = cJSON_PrintUnformatted(result);
            obs_log(LOG_ERROR, "直播开始失败: %s", result_str ? result_str : "未知错误");
            if (result_str) bfree(result_str);
        }
        if (result) cJSON_Delete(result);
    } else {
        obs_log(LOG_WARNING, "未登录，无法开始直播");
    }
}

static void stop_live_callback(void* data) {
    struct bilibili_source* source = (struct bilibili_source*)data;
    if (source->logged_in) {
        int room_id = 12345; // 替换为实际房间 ID
        const char* csrf = "your_csrf_token"; // 从 Cookies 提取
        bool success = bilibili_api_stop_live(source->api, room_id, csrf, source->cookies);
        if (success) {
            obs_log(LOG_INFO, "直播停止成功");
        } else {
            obs_log(LOG_ERROR, "直播停止失败");
        }
    } else {
        obs_log(LOG_WARNING, "未登录，无法停止直播");
    }
}

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void) {
    obs_register_source(&bilibili_source_info);

    // 添加菜单项到 OBS 工具菜单
    obs_frontend_add_menu_item(obs_module_text("StartLive"), start_live_callback);
    obs_frontend_add_menu_item(obs_module_text("StopLive"), stop_live_callback);

    obs_log(LOG_INFO, "插件加载成功 (版本 %s)", PLUGIN_VERSION);
    return true;
}

void obs_module_unload(void) {
    obs_log(LOG_INFO, "插件卸载");
}
