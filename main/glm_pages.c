// main/glm_pages.c —— 用量宝 UI 页面内容,挂到 appfw_ui 框架上(见 glm_pages.h)。
#include "glm_pages.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "appfw_client.h"
#include "appfw_portal.h"
#include "appfw_net.h"
#include "appfw_storage.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "glm_api.h"
#include "glm_parsers.h"
#include "lwip/ip4_addr.h"

static const char *TAG = "glm_pages";

// ---- 主页面(用量)控件 ----
static lv_obj_t *s_week_bar, *s_week_pct, *s_week_reset;
static lv_obj_t *s_h5_bar, *s_h5_pct, *s_h5_reset;
static lv_obj_t *s_mcp_label, *s_mcp_val, *s_foot;

static lv_font_t s_font16; // 与框架字库一致的引用(appfw_ui 初始化后可用)
static lv_font_t s_font24;

static void style_label(lv_obj_t *l, const lv_font_t *f, uint32_t color)
{
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
}

static lv_obj_t *make_bar(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x24303C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, h / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x35C26B), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, h / 2, LV_PART_INDICATOR);
    return bar;
}

static void set_bar_pct(lv_obj_t *bar, int pct)
{
    lv_bar_set_value(bar, pct > 0 ? pct : 0, LV_ANIM_OFF);
    uint32_t col = 0x35C26B;
    if (pct >= 90) col = 0xE5484D;
    else if (pct >= 70) col = 0xE5A13D;
    lv_obj_set_style_bg_color(bar, lv_color_hex(col), LV_PART_INDICATOR);
}

// 字体句柄由 appfw_ui 拷贝字库描述符;此处用 montserrat 兜底引用以保证链接。
// 实际中文字体通过 LV_FONT_DECLARE 使用与 appfw 相同的生成字体。
LV_FONT_DECLARE(app_font_16);
LV_FONT_DECLARE(app_font_24);
static lv_font_t f16, f24;
static bool s_fonts_ready;

static void ensure_fonts(void)
{
    if (s_fonts_ready) return;
    f16 = app_font_16;
    f16.fallback = &lv_font_montserrat_14;
    f24 = app_font_24;
    f24.fallback = &lv_font_montserrat_20;
    s_fonts_ready = true;
}

void glm_pages_home_build(lv_obj_t *page)
{
    lv_obj_t *p = page;
    ensure_fonts();

    lv_obj_t *l = lv_label_create(p);
    style_label(l, &f16, 0x8B98A5);
    lv_label_set_text(l, "本周额度");
    lv_obj_set_pos(l, 14, 50);
    s_week_pct = lv_label_create(p);
    style_label(s_week_pct, &f24, 0xE6E6E6);
    lv_obj_set_pos(s_week_pct, 172, 46);
    lv_label_set_text(s_week_pct, "--");
    s_week_bar = make_bar(p, 14, 82, 212, 12);
    s_week_reset = lv_label_create(p);
    style_label(s_week_reset, &f16, 0x8B98A5);
    lv_obj_set_pos(s_week_reset, 14, 100);
    lv_label_set_text(s_week_reset, "重置 --");

    l = lv_label_create(p);
    style_label(l, &f16, 0x8B98A5);
    lv_label_set_text(l, "5小时窗口");
    lv_obj_set_pos(l, 14, 126);
    s_h5_pct = lv_label_create(p);
    style_label(s_h5_pct, &f24, 0xE6E6E6);
    lv_obj_set_pos(s_h5_pct, 172, 122);
    lv_label_set_text(s_h5_pct, "--");
    s_h5_bar = make_bar(p, 14, 158, 212, 12);
    s_h5_reset = lv_label_create(p);
    style_label(s_h5_reset, &f16, 0x8B98A5);
    lv_obj_set_pos(s_h5_reset, 14, 176);
    lv_label_set_text(s_h5_reset, "重置 --");

    l = lv_label_create(p);
    style_label(l, &f16, 0x8B98A5);
    lv_label_set_text(l, "MCP 调用(每月)");
    lv_obj_set_pos(l, 14, 202);
    s_mcp_label = l;
    s_mcp_val = lv_label_create(p);
    style_label(s_mcp_val, &f24, 0xE6E6E6);
    lv_obj_set_pos(s_mcp_val, 140, 198);
    lv_label_set_text(s_mcp_val, "--");

    s_foot = lv_label_create(p);
    style_label(s_foot, &f16, 0x8B98A5);
    lv_obj_set_width(s_foot, 216);
    lv_label_set_long_mode(s_foot, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s_foot, 14, 240);
    lv_label_set_text(s_foot, "等待数据…");
}

static void set_reset_line(lv_obj_t *label, int64_t reset_ms)
{
    char buf[16];
    lv_label_set_text_fmt(label, "重置 %s",
                          glm_usage_format_reset_ms(reset_ms, buf, sizeof(buf)));
}

void glm_pages_home_poll(void)
{
    glm_usage_t u;
    glm_api_snapshot(&u);
    appfw_client_err_t err = (appfw_client_err_t)glm_api_framework_err();
    int64_t epoch = glm_api_fetch_epoch();
    bool last_ok = glm_api_last_ok();

    set_bar_pct(s_week_bar, u.tokens_week_used_pct);
    if (u.tokens_week_used_pct < 0) lv_label_set_text(s_week_pct, "--");
    else lv_label_set_text_fmt(s_week_pct, "%d%%", u.tokens_week_used_pct);
    set_reset_line(s_week_reset, u.tokens_week_reset_ms);

    set_bar_pct(s_h5_bar, u.tokens_5h_used_pct);
    if (u.tokens_5h_used_pct < 0) lv_label_set_text(s_h5_pct, "--");
    else lv_label_set_text_fmt(s_h5_pct, "%d%%", u.tokens_5h_used_pct);
    set_reset_line(s_h5_reset, u.tokens_5h_reset_ms);

    // 行 3:仅显示剩余重置次数(团队套餐);MCP 用量不再展示(用户要求,
    // 解析仍保留)。未获取到时整行留空。
    if (u.week_resets_left >= 0 && u.five_hour_resets_left >= 0) {
        lv_label_set_text(s_mcp_label, "剩余重置");
        lv_label_set_text_fmt(s_mcp_val, "周%d 5h%d", u.week_resets_left, u.five_hour_resets_left);
    } else {
        lv_label_set_text(s_mcp_label, "");
        lv_label_set_text(s_mcp_val, "");
    }

    // foot:错误优先;断网其次;正常=套餐+倒计时。
    if (!last_ok && err != APPFW_CLIENT_OK && err != APPFW_CLIENT_IDLE) {
        const char *msg;
        char buf[72];
        if (u.msg[0] && (err == APPFW_CLIENT_AUTH || err == APPFW_CLIENT_HTTP)) {
            snprintf(buf, sizeof(buf), "套餐接口:%.36s", u.msg);
            msg = buf;
        } else {
            switch (err) {
            case APPFW_CLIENT_WAIT_NET: msg = "等待网络连接…"; break;
            case APPFW_CLIENT_WAIT_TIME: msg = "正在对时…"; break;
            case APPFW_CLIENT_AUTH: msg = "Key 无效,网页可改"; break;
            case APPFW_CLIENT_PARSE: msg = "响应异常,稍后重试"; break;
            default: {
                int terr = appfw_client_transport_err();
                if (terr != 0) { snprintf(buf, sizeof(buf), "查询失败(%x)", terr); msg = buf; }
                else msg = "查询失败,稍后重试";
                break;
            }
            }
        }
        lv_label_set_text(s_foot, msg);
        lv_obj_set_style_text_color(s_foot, lv_color_hex(0xE5484D), 0);
        return;
    }
    if (err == APPFW_CLIENT_WAIT_NET) {
        lv_label_set_text(s_foot, "网络已断开,自动重连中…");
        lv_obj_set_style_text_color(s_foot, lv_color_hex(0xE5484D), 0);
        return;
    }
    const char *plan = u.level[0] ? u.level : "--";
    if (epoch > 1000000000LL) {
        uint16_t period_s = 60;
        appfw_store_get_period(&period_s);
        int64_t now = (int64_t)time(NULL);
        int left = (int)period_s - (int)(now - epoch);
        if (left < 0) left = 0;
        if (left > (int)period_s) left = period_s;
        char buf[16];
        glm_usage_format_reset_ms(epoch * 1000, buf, sizeof(buf));
        if (left >= 120) lv_label_set_text_fmt(s_foot, "套餐 %s · %d 分钟后刷新 · %s", plan, (left + 59) / 60, buf);
        else lv_label_set_text_fmt(s_foot, "套餐 %s · %ds 后刷新 · %s", plan, left, buf);
    } else {
        lv_label_set_text_fmt(s_foot, "套餐 %s · 已连接,等待数据…", plan);
    }
    lv_obj_set_style_text_color(s_foot, lv_color_hex(0x8B98A5), 0);
}

void glm_pages_home_up(void)
{
    appfw_client_refresh_now(); // 用量页上键=手动刷新
}

// ---- 信息页数据行 ----
int glm_pages_info_rows(char (*keys)[16], char (*vals)[72], int max)
{
    char key[GLM_KEY_MAX];
    appfw_net_status_t st;
    appfw_net_get_status(&st);
    int n = 0;
    if (n < max) { snprintf(keys[n], 16, "API Key"); snprintf(vals[n], 72,
        glm_cfg_get_key(key, GLM_KEY_MAX) ? "已配置" : "未设置"); n++; }
    if (n < max) { snprintf(keys[n], 16, "团队");
        snprintf(vals[n], 72, "已配置"); n++; }
    if (n < max) { snprintf(keys[n], 16, "刷新周期");
        uint16_t p = 60; appfw_store_get_period(&p);
        snprintf(vals[n], 72, "%u 分钟", (unsigned)(p / 60)); n++; }
    if (n < max) { snprintf(keys[n], 16, "熄屏");
        uint16_t s = 300; appfw_store_get_screen_off(&s);
        snprintf(vals[n], 72, s == 0 ? "永不" : "%u 分钟", (unsigned)(s / 60)); n++; }
    return n;
}

// 设备信息「配置」区:应用定义的配置项状态(框架渲染)。
int glm_pages_config_rows(char (*keys)[24], char (*vals)[72], int max)
{
    int n = 0;
    if (n < max) {
        char key[GLM_KEY_MAX];
        snprintf(keys[n], 24, "API Key");
        snprintf(vals[n], 72, glm_cfg_get_key(key, sizeof(key)) ? "已配置" : "未设置");
        n++;
    }
    if (n < max) {
        char org[GLM_ORG_MAX] = { 0 }, proj[GLM_PROJ_MAX] = { 0 };
        bool team = glm_cfg_get_org(org, sizeof(org)) && glm_cfg_get_project(proj, sizeof(proj));
        snprintf(keys[n], 24, "团队上下文");
        snprintf(vals[n], 72, team ? "已配置" : "未配置");
        n++;
    }
    return n;
}

// ---- 门户:应用配置卡片(H5)与保存/回显 ----
void glm_pages_app_config_fill(void *obj)
{
    cJSON *root = (cJSON *)obj;
    char key[GLM_KEY_MAX] = { 0 }, org[GLM_ORG_MAX] = { 0 }, proj[GLM_PROJ_MAX] = { 0 };
    glm_cfg_get_key(key, sizeof(key));
    glm_cfg_get_org(org, sizeof(org));
    glm_cfg_get_project(proj, sizeof(proj));
    cJSON_AddStringToObject(root, "key", key);
    cJSON_AddStringToObject(root, "org", org);
    cJSON_AddStringToObject(root, "project", proj);
}

bool glm_pages_app_config_save(void *obj)
{
    cJSON *root = (cJSON *)obj;
    cJSON *key = cJSON_GetObjectItemCaseSensitive(root, "key");
    cJSON *org = cJSON_GetObjectItemCaseSensitive(root, "org");
    cJSON *proj = cJSON_GetObjectItemCaseSensitive(root, "project");
    // Key 空串=不改动(仅改团队上下文);非空才覆盖。
    if (cJSON_IsString(key) && key->valuestring && key->valuestring[0])
        glm_cfg_set_key(key->valuestring);
    glm_cfg_set_org(cJSON_IsString(org) && org->valuestring ? org->valuestring : "");
    glm_cfg_set_project(cJSON_IsString(proj) && proj->valuestring ? proj->valuestring : "");
    appfw_client_refresh_now();
    return true;
}

const char *glm_pages_app_config_html(void)
{
    return ""
    "<div class=\"card\"><h2>0 · 应用配置(GLM)</h2>\n"
    "<input type=\"text\" id=\"key\" placeholder=\"粘贴智谱 API Key(bigmodel.cn)\">\n"
    "<input type=\"text\" id=\"org\" placeholder=\"组织 ID org-…(团队套餐,个人留空)\">\n"
    "<input type=\"text\" id=\"project\" placeholder=\"项目 ID proj-…(团队套餐,个人留空)\">\n"
    "<button onclick=\"saveKey()\">保存应用配置</button>\n"
    "<small>保存后设备数秒内开始查询并显示在屏幕上</small></div>\n"
    "<script>\n"
    "async function saveKey(){\n"
    "  const r=await jpost('/api/glm',{key:$('key').value.trim(),org:$('org').value.trim(),project:$('project').value.trim()});\n"
    "  if(r.ok)alert('已保存,设备数秒内开始查询');else alert('保存失败');\n"
    "}\n"
    "try{(async()=>{const s=await jget('/api/status');if(!$('key').value&&s.key)$('key').value=s.key;if(!$('org').value&&s.org)$('org').value=s.org;if(!$('project').value&&s.project)$('project').value=s.project;})()}catch(e){}\n"
    "</script>\n";
}

// 门户 HTTP 就绪后注册应用端点:POST /api/glm(保存 Key/组织/项目)。
static esp_err_t glm_glm_save_handler(httpd_req_t *req);

bool glm_pages_portal_register(void *httpd)
{
    httpd_handle_t h = (httpd_handle_t)httpd;
    static const httpd_uri_t uri = {
        .uri = "/api/glm", .method = HTTP_POST, .handler = glm_glm_save_handler,
    };
    return httpd_register_uri_handler(h, &uri) == ESP_OK;
}

// POST /api/glm 处理器(读 JSON,写应用配置,立即触发查询)。
static esp_err_t glm_glm_save_handler(httpd_req_t *req)
{
    cJSON *root = (cJSON *)appfw_prov_read_json(req);
    if (!root) return ESP_FAIL;
    cJSON *key = cJSON_GetObjectItemCaseSensitive(root, "key");
    cJSON *org = cJSON_GetObjectItemCaseSensitive(root, "org");
    cJSON *proj = cJSON_GetObjectItemCaseSensitive(root, "project");
    if (cJSON_IsString(key) && key->valuestring[0]) glm_cfg_set_key(key->valuestring);
    glm_cfg_set_org(cJSON_IsString(org) && org->valuestring ? org->valuestring : "");
    glm_cfg_set_project(cJSON_IsString(proj) && proj->valuestring ? proj->valuestring : "");
    cJSON_Delete(root);
    appfw_client_refresh_now();
    appfw_prov_send_ok(req, true);
    return ESP_OK;
}