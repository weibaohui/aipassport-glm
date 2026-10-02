// main/glm_pages.h —— 用量宝的 UI 页面内容(挂到 appfw_ui 框架上)。
#pragma once

#include "appfw_ui.h"

// 主页面(用量)构建/轮询/上键回调;信息页数据行;门户应用片段与配置钩子。
void glm_pages_home_build(lv_obj_t *page);
void glm_pages_home_poll(void);
void glm_pages_home_up(void);
int glm_pages_info_rows(char (*keys)[16], char (*vals)[72], int max);
int glm_pages_config_rows(char (*keys)[24], char (*vals)[72], int max);
// 门户应用配置卡片 HTML 片段(注入框架的 <!--APP_CONFIG_HTML-->)。
// 注入契约见 appfw_portal.h 与 appfw_portal_html.inc 顶部注释。
const char *glm_pages_app_config_html(void);
bool glm_pages_app_config_save(void *cjson_root);  // 导入/保存:处理 key/org/project
void glm_pages_app_config_fill(void *cjson_obj);   // 状态/导出回显
// httpd 就绪后注册应用私有端点(POST /api/glm、POST /api/glm/discover)。
bool glm_pages_portal_register(void *httpd);
