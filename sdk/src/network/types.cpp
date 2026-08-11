#include "xscope/network/types.hpp"

namespace xscope::network {

const char* method_name(HttpMethod method) {
    switch (method) {
    case HttpMethod::Get:
        return "GET";
    case HttpMethod::Post:
        return "POST";
    case HttpMethod::Put:
        return "PUT";
    case HttpMethod::Patch:
        return "PATCH";
    case HttpMethod::Delete:
        return "DELETE";
    case HttpMethod::Head:
        return "HEAD";
    }
    return "GET";
}

} // namespace xscope::network
