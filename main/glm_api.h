// main/glm_api.h —— GLM 套餐查询的应用层:客户端回调、快照与配置项。
//
// 框架(appfw_client)负责周期/联网/对时/HTTPS;本模块只提供:
// make_request(选配额端点,团队模式带组织/项目头)、on_result(解析并追加重置
// 列表)、应用配置(api_key/org/project 的存取与门户注入)。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "glm_parsers.h"

#define GLM_KEY_MAX 129
#define GLM_ORG_MAX 64
#define GLM_PROJ_MAX 64

// 启动轮询客户端(接 appfw_client 框架)。
int glm_api_start(void);

// 最近一次框架状态(用量页 foot 显示用)。
int glm_api_framework_err(void);      // appfw_client_err_t 值
int64_t glm_api_fetch_epoch(void);
bool glm_api_last_ok(void);

// 应用配置(api_key/org_id/proj_id,存于 appfw_store 通用键)。
bool glm_cfg_get_key(char *buf, int len);
bool glm_cfg_get_org(char *buf, int len);
bool glm_cfg_get_project(char *buf, int len);
void glm_cfg_set_key(const char *v);
void glm_cfg_set_org(const char *v);
void glm_cfg_set_project(const char *v);

// 最近一次用量快照(线程安全拷贝;解析失败时字段为 -1/false)。
void glm_api_snapshot(glm_usage_t *out);

// 门户 HTML 片段(阶段二「应用配置」卡片:API Key + 团队上下文)。
const char *glm_api_portal_fragment(void);
