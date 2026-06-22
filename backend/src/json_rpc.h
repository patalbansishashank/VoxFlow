#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct RpcRequest {
    int64_t id = 0;
    std::string method;
    json params;
};

struct RpcEvent {
    std::string event;
    json data;
};

struct RpcResponse {
    int64_t id = 0;
    json result;
    json error;
};

bool parse_rpc(const std::string& line, RpcRequest& req);

std::string make_event(const std::string& event, const json& data);
std::string make_result(int64_t id, const json& result);
std::string make_error(int64_t id, int code, const std::string& message);
