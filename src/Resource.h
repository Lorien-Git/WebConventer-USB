#pragma once
#include <string_view>
#include <unordered_map>

struct ResourceData {
    const unsigned char* data;
    size_t size;
    const char* mime_type;
};

extern const std::unordered_map<std::string_view, ResourceData> g_Resources;
