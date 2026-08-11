#pragma once

#include "xscope/utils/json.hpp"

#include <string>

namespace xscope::mcp {

struct ToolRequest {
    std::string name;
    utils::Json arguments; // object
};

struct ToolResponse {
    bool ok = false;
    /// Full structured result (never summarized by the router).
    utils::Json result;
    std::string error;
};

struct ToolDescriptor {
    std::string name;
    std::string description;
    /// JSON Schema-like object describing arguments (informational).
    utils::Json input_schema;
};

} // namespace xscope::mcp
