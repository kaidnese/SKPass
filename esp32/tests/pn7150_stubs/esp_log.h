#pragma once
void test_log(const char *tag, const char *format, ...);
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
#define ESP_LOGD(...) test_log(__VA_ARGS__)
#define ESP_LOG_DEBUG 0
#define ESP_LOG_ERROR 1
#define ESP_LOG_BUFFER_HEXDUMP(...) ((void)0)
