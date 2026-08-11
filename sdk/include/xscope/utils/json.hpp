#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace xscope::utils {

class JsonError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Json {
public:
    using Object = std::map<std::string, Json>;
    using Array = std::vector<Json>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    Json() : data_(nullptr) {}
    Json(std::nullptr_t) : data_(nullptr) {}
    Json(bool v) : data_(v) {}
    Json(int v) : data_(static_cast<double>(v)) {}
    Json(std::int64_t v) : data_(static_cast<double>(v)) {}
    Json(double v) : data_(v) {}
    Json(const char* v) : data_(std::string(v)) {}
    Json(std::string v) : data_(std::move(v)) {}
    Json(Array v) : data_(std::move(v)) {}
    Json(Object v) : data_(std::move(v)) {}

    static Json parse(std::string_view text);
    std::string dump(int indent = 2) const;

    bool is_null() const;
    bool is_bool() const;
    bool is_number() const;
    bool is_string() const;
    bool is_array() const;
    bool is_object() const;

    bool as_bool(bool fallback = false) const;
    double as_number(double fallback = 0) const;
    std::int64_t as_int64(std::int64_t fallback = 0) const;
    const std::string& as_string() const;
    std::string as_string(std::string_view fallback) const;
    const Array& as_array() const;
    const Object& as_object() const;
    Array& as_array();
    Object& as_object();

    bool contains(std::string_view key) const;
    const Json& at(std::string_view key) const;
    Json& operator[](const std::string& key);
    const Json& operator[](const std::string& key) const;

private:
    Storage data_;
    static const Json& null_singleton();
};

} // namespace xscope::utils
