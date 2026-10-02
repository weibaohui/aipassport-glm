// main/glm_api.c —— GLM 套餐查询应用层实现,见 glm_api.h。
#include "glm_api.h"

#include <stdio.h>
#include <string.h>

#include "appfw_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "appfw_net.h"
#include "appfw_storage.h"
#include "esp_log.h"
#include "portmacro.h"

static const char *TAG = "glm_api";

#define QUOTA_URL "https://open.bigmodel.cn/api/monitor/usage/quota/limit"
#define RESETS_URL "https://open.bigmodel.cn/api/biz/customer-package-reset/list?targetType=TEAM"

// ---- 快照与配置(自旋锁保护) ----
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static glm_usage_t s_usage;
static appfw_client_err_t s_fw_err;

bool glm_cfg_get_key(char *buf, int len) { return appfw_store_get_str("api_key", buf, (size_t)len); }
bool glm_cfg_get_org(char *buf, int len) { return appfw_store_get_str("org_id", buf, (size_t)len); }
bool glm_cfg_get_project(char *buf, int len) { return appfw_store_get_str("proj_id", buf, (size_t)len); }
void glm_cfg_set_key(const char *v) { appfw_store_set_str("api_key", v ? v : ""); }
void glm_cfg_set_org(const char *v) { appfw_store_set_str("org_id", v ? v : ""); }
void glm_cfg_set_project(const char *v) { appfw_store_set_str("proj_id", v ? v : ""); }

void glm_api_snapshot(glm_usage_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_usage;
    portEXIT_CRITICAL(&s_lock);
}

int glm_api_framework_err(void)
{
    portENTER_CRITICAL(&s_lock);
    int e = (int)s_fw_err;
    portEXIT_CRITICAL(&s_lock);
    return e;
}

int64_t glm_api_fetch_epoch(void)
{
    int64_t t = 0;
    bool ok = false;
    appfw_client_err_t e;
    appfw_client_get_state(&e, &t, &ok);
    return t;
}

bool glm_api_last_ok(void)
{
    return appfw_client_last_err() == APPFW_CLIENT_OK;
}

// ---- 团队上下文:两值成对才启用团队接口 ----
static bool team_ctx(char *org, size_t org_len, char *proj, size_t proj_len)
{
    return appfw_store_get_str("org_id", org, org_len) &&
           appfw_store_get_str("proj_id", proj, proj_len);
}

// ---- make_request:团队模式用 type=2 端点 + 组织/项目头 ----
// 注意:names/vals 是连续定长槽(n_extra 组,槽宽即传入的 name_sz/val_sz)。
static bool make_request(char *url, size_t url_len, char *auth, size_t auth_len,
                         char (*names)[64], char (*vals)[64],
                         int *n_extra, int extra_max, void *user)
{
    (void)user;
    char key[GLM_KEY_MAX], org[GLM_ORG_MAX], proj[GLM_PROJ_MAX];
    if (!appfw_store_get_str("api_key", key, sizeof(key))) {
        ESP_LOGW(TAG, "未配置 API Key,跳过本轮");
        return false;
    }
    snprintf(auth, auth_len, "%s", key);
    *n_extra = 0;
    char o[GLM_ORG_MAX], p[GLM_PROJ_MAX];
    if (team_ctx(o, sizeof(o), p, sizeof(p)) && extra_max >= 2) {
        snprintf(names[0], 64, "bigmodel-organization");
        snprintf(vals[0], 64, "%s", o);
        snprintf(names[1], 64, "bigmodel-project");
        snprintf(vals[1], 64, "%s", p);
        *n_extra = 2;
    }
    snprintf(url, url_len, "%s%s", QUOTA_URL, (*n_extra == 2) ? "?type=2" : "");
    return true;
}

// ---- on_result:解析配额;团队模式成功后追加重置次数 ----
static void on_result(appfw_client_err_t ferr, int status, int transport_err,
                      const char *body, size_t len, void *user)
{
    (void)user;
    glm_usage_t u;
    bool parsed = false;
    if (ferr == APPFW_CLIENT_OK && status == 200) {
        parsed = glm_usage_parse(body, len, &u);
        if (parsed && !u.ok) {
            ferr = glm_usage_is_auth_error(&u) ? APPFW_CLIENT_AUTH : APPFW_CLIENT_HTTP;
        } else if (!parsed) {
            ferr = APPFW_CLIENT_PARSE;
        }
    }
    // 团队模式成功:追加重置次数(独立端点;服务端偶发拒绝无 cookie 请求,
    // 实测踩坑 → 最多重试 2 次;失败沿用最近一次成功值,不清零)。
    if (ferr == APPFW_CLIENT_OK) {
        char key[GLM_KEY_MAX], org[GLM_ORG_MAX], proj[GLM_PROJ_MAX];
        if (glm_cfg_get_key(key, sizeof(key)) &&
            team_ctx(org, sizeof(org), proj, sizeof(proj))) {
            // 服务端多节点对 org/proj 头的支持不一致(间歇出现"必须传组织ID"),
            // 实测踩坑:轮换两个域名 + 重试;成功前沿用最近一次成功值。
            static const char *RESET_URLS[] = {
                "https://open.bigmodel.cn/api/biz/customer-package-reset/list?targetType=TEAM",
                "https://open.bigmodel.cn/api/biz/customer-package-reset/list?targetType=TEAM",
            };
            static int s_5h = -1, s_week = -1; // 最近一次成功值(仅本任务访问)
            int h5 = -1, week = -1;
            bool got = false;
            for (int attempt = 0; attempt < 4 && !got; attempt++) {
                char names[2][64], vals[2][64];
                snprintf(names[0], 64, "bigmodel-organization");
                snprintf(vals[0], 64, "%s", org);
                snprintf(names[1], 64, "bigmodel-project");
                snprintf(vals[1], 64, "%s", proj);
                int status = 0;
                char rbody[2048];
                size_t rlen = 0;
                if (appfw_client_fetch_once(RESET_URLS[attempt % 2], key, names, vals, 2,
                                            &status, rbody, sizeof(rbody), &rlen) == ESP_OK &&
                    status == 200 &&
                    glm_resets_parse(rbody, rlen, &h5, &week) && h5 >= 0 && week >= 0) {
                    s_5h = h5; s_week = week;
                    got = true;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(400));
                }
            }
            if (got) {
                u.five_hour_resets_left = h5;
                u.week_resets_left = week;
            } else {
                ESP_LOGW(TAG, "重置次数拉取失败(双域名 4 次),沿用上次值");
                u.five_hour_resets_left = s_5h;
                u.week_resets_left = s_week;
            }
        }
    }
    portENTER_CRITICAL(&s_lock);
    if (parsed) s_usage = u;
    s_fw_err = ferr;
    portEXIT_CRITICAL(&s_lock);
    if (ferr == APPFW_CLIENT_OK && parsed) {
        ESP_LOGI(TAG, "用量已更新:5h=%d%% 周=%d%%", u.tokens_5h_used_pct, u.tokens_week_used_pct);
    }
}

int glm_api_start(void)
{
    static appfw_client_cfg_t cfg;
    cfg.make_request = make_request;
    cfg.on_result = on_result;
    cfg.user = NULL;
    return appfw_client_start(&cfg);
}
