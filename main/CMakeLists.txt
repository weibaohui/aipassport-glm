# GLM 用量宝 应用组件(appfw 框架版)。
#
# 应用层只保留业务:GLM 解析(纯逻辑)、API 回调、页面内容与门户片段。
# 通用能力(WiFi/门户/存储/客户端/UI 骨架/熄屏/按键)全部来自 components/appfw。
idf_component_register(
    SRCS "main.c"
         "glm_parsers.c"
         "glm_api.c"
         "glm_pages.c"
    INCLUDE_DIRS "."
    REQUIRES appfw bsp esp_timer nvs_flash
)

# 应用中文字体子集(生成物,来源/命令/字符清单见 assets/fonts/README.zh_CN.md)。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/app_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/app_font_24.c"
)
