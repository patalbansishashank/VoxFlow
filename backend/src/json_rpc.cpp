#include "json_rpc.h"

bool parse_rpc(const std::string& line, RpcRequest& req) {
    json j;
    try {
        j = json::parse(line);
    } catch (...) {
        return false;
    }

    if (!j.contains("method"))
        return false;

    req.id = j.value("id", int64_t(0));
    req.method = j["method"].get<std::string>();
    req.params = j.value("params", json::object());
    return true;
}

std::string make_event(const std::string& event, const json& data) {
    json j = {
        {"event", event},
        {"data", data}
    };
    return j.dump() + "\n";
}

std::string make_result(int64_t id, const json& result) {
    json j = {
        {"id", id},
        {"result", result}
    };
    return j.dump() + "\n";
}

std::string make_error(int64_t id, int code, const std::string& message) {
    json j = {
        {"id", id},
        {"error", {
            {"code", code},
            {"message", message}
        }}
    };
    return j.dump() + "\n";
}
