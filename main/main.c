// main/main.c —— GLM 用量宝(appfw 框架版)启动装配。
//
// 应用只负责:1) 框架初始化顺序;2) 注入业务(解析/主页/门户片段/信息行)。
// 全部通用能力(WiFi 引擎/配网门户/存储/客户端/UI 骨架/熄屏/按键)来自
// components/appfw。
#include "appfw_client.h"
#include "appfw_files.h"
#include "appfw_net.h"
#include "appfw_netlist.h"
#include "appfw_portal.h"
#include "appfw_storage.h"
#include "appfw_ui.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "glm_api.h"
#include "glm_pages.h"
#include "glm_parsers.h"

static const char *TAG = "main";

// ---- 输入任务(框架提供事件规整与栈预算,应用只给回调) ----
static QueueHandle_t s_key_queue;
static volatile bool s_keys_ready;

static void on_key_from_bsp(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!s_keys_ready || !s_key_queue) return;
    const int msg = (int)btn | ((int)ev << 4); // 轻量打包:低 4 位键,高 4 位事件
    (void)xQueueSend(s_key_queue, &msg, 0);
}

static void key_task(void *arg)
{
    (void)arg;
    int msg;
    for (;;) {
        if (xQueueReceive(s_key_queue, &msg, portMAX_DELAY) == pdTRUE) {
            appfw_ui_on_key(msg & 0xF, (msg >> 4) & 0xF);
        }
    }
}

// 门户 HTTP 就绪回调:注册应用专属端点。
static bool portal_ready(void *httpd)
{
    extern bool glm_pages_portal_register(void *httpd);
    return glm_pages_portal_register(httpd);
}

// 门户秒级维护(esp_timer):门户拉活 + 熄屏判定。
static void second_tick_cb(void *arg)
{
    (void)arg;
    appfw_ui_second_tick();
}

void app_main(void)
{
    ESP_LOGI(TAG, "GLM 用量宝(appfw)启动");
    bsp_i2c_init();
    (void)bsp_battery_init(); // 失败不阻塞:电量显示降级为 --

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,应用无法继续");
        return;
    }
    bsp_display_backlight(100);
    if (appfw_files_init() != ESP_OK) {
        ESP_LOGE(TAG, "文件分区挂载失败(文件管理不可用)");
    }
    if (appfw_store_init() != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败(配置将无法保存)");
    }

    appfw_netlist_t list;
    if (!appfw_store_netlist_load(&list)) appfw_netlist_reset(&list);
    int net_err = appfw_net_init(&list, false); // 配网统一从菜单进,不再自动开门户
    // 热点名想定制?一行覆盖(框架默认 "AI-WiFi-"+MAC 尾缀,如 AI-WiFi-D22C):
    // appfw_net_set_ap_ssid("我的热点名");
    int glm_err = glm_api_start(); // 内部构造 appfw_client 配置并启动

    // UI:注入业务页面与信息行。
    const appfw_ui_cfg_t ucfg = {
        .home_title = "GLM 用量",
        .home_build = glm_pages_home_build,
        .home_poll = glm_pages_home_poll,
        .home_up = glm_pages_home_up,
        .info_rows = glm_pages_info_rows,
        .app_config_html = glm_pages_app_config_html,
        
        .app_config_fill = glm_pages_app_config_fill,
        .config_rows = glm_pages_config_rows,
    };

    // 按键输入任务(框架约定:回调转 appfw_ui_on_key)。
    s_key_queue = xQueueCreate(8, sizeof(int));
    if (s_key_queue &&
        xTaskCreate(key_task, "app_input", 6144, NULL, 5, NULL) == pdPASS &&
        bsp_button_init(on_key_from_bsp, NULL) == ESP_OK) {
        s_keys_ready = true;
    } else {
        ESP_LOGE(TAG, "按键初始化失败");
    }

    if (bsp_lvgl_lock(1000)) {
        appfw_ui_init(&ucfg);
        bsp_lvgl_unlock();
        s_keys_ready = true; // 顺序:UI 就绪后允许按键
    } else {
        ESP_LOGE(TAG, "LVGL 锁获取失败,界面未创建");
    }

    // 门户(常驻)+ 应用端点注册 + 秒级维护定时器。
    const appfw_prov_cfg_t pcfg = {
        .app_config_html = glm_pages_app_config_html,
        .on_httpd_ready = portal_ready,
    };
    appfw_prov_configure(&pcfg);
    (void)appfw_portal_start();
    esp_timer_handle_t tick;
    const esp_timer_create_args_t ta = { .callback = second_tick_cb, .name = "tick" };
    if (esp_timer_create(&ta, &tick) == ESP_OK) esp_timer_start_periodic(tick, 1000000);

    ESP_LOGI(TAG, "启动完成:net=%s glm=%s",
             net_err == 0 ? "ok" : esp_err_to_name(net_err),
             glm_err == 0 ? "ok" : esp_err_to_name(glm_err));
}
