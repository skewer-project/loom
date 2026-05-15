#include "core/Param.hpp"

#include "core/Log.hpp"

namespace loom::core {

namespace {

const char* typeTag(const Param::Value& v) {
    return std::visit(
        [](auto&& x) -> const char* {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, float>) return "float";
            if constexpr (std::is_same_v<T, int>) return "int";
            if constexpr (std::is_same_v<T, bool>) return "bool";
            if constexpr (std::is_same_v<T, glm::vec3>) return "vec3";
            if constexpr (std::is_same_v<T, std::string>) return "string";
        },
        v);
}

crude_json::value valueToJson(const Param::Value& v) {
    return std::visit(
        [](auto&& x) -> crude_json::value {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, float>) {
                return crude_json::value(static_cast<crude_json::number>(x));
            } else if constexpr (std::is_same_v<T, int>) {
                return crude_json::value(static_cast<crude_json::number>(x));
            } else if constexpr (std::is_same_v<T, bool>) {
                return crude_json::value(static_cast<crude_json::boolean>(x));
            } else if constexpr (std::is_same_v<T, glm::vec3>) {
                crude_json::array arr;
                arr.push_back(crude_json::value(static_cast<crude_json::number>(x.x)));
                arr.push_back(crude_json::value(static_cast<crude_json::number>(x.y)));
                arr.push_back(crude_json::value(static_cast<crude_json::number>(x.z)));
                return crude_json::value(std::move(arr));
            } else if constexpr (std::is_same_v<T, std::string>) {
                return crude_json::value(x);
            }
        },
        v);
}

}  // namespace

crude_json::value Param::toJson() const {
    crude_json::object obj;
    obj["name"] = crude_json::value(m_name);
    obj["type"] = crude_json::value(std::string(typeTag(m_value)));
    obj["value"] = valueToJson(m_value);
    if (m_range.hasBounds || m_range.step != 0.0f) {
        crude_json::object range;
        range["min"] = crude_json::value(static_cast<crude_json::number>(m_range.min));
        range["max"] = crude_json::value(static_cast<crude_json::number>(m_range.max));
        range["step"] = crude_json::value(static_cast<crude_json::number>(m_range.step));
        range["hasBounds"] = crude_json::value(static_cast<crude_json::boolean>(m_range.hasBounds));
        obj["range"] = crude_json::value(std::move(range));
    }
    return crude_json::value(std::move(obj));
}

Param Param::fromJson(const crude_json::value& j) {
    if (!j.is_object()) {
        log::warn("Param::fromJson: expected object, got non-object — returning default");
        return {};
    }

    std::string name;
    if (j.contains("name") && j["name"].is_string()) name = j["name"].get<crude_json::string>();

    std::string type;
    if (j.contains("type") && j["type"].is_string()) type = j["type"].get<crude_json::string>();

    Value value{0.0f};
    if (j.contains("value")) {
        const auto& v = j["value"];
        if (type == "float" && v.is_number()) {
            value = static_cast<float>(v.get<crude_json::number>());
        } else if (type == "int" && v.is_number()) {
            value = static_cast<int>(v.get<crude_json::number>());
        } else if (type == "bool" && v.is_boolean()) {
            value = static_cast<bool>(v.get<crude_json::boolean>());
        } else if (type == "vec3" && v.is_array()) {
            const auto& a = v.get<crude_json::array>();
            if (a.size() == 3 && a[0].is_number() && a[1].is_number() && a[2].is_number()) {
                value = glm::vec3(static_cast<float>(a[0].get<crude_json::number>()),
                                  static_cast<float>(a[1].get<crude_json::number>()),
                                  static_cast<float>(a[2].get<crude_json::number>()));
            } else {
                log::warn("Param::fromJson: malformed vec3 for '", name, "' — using zero");
                value = glm::vec3(0.0f);
            }
        } else if (type == "string" && v.is_string()) {
            value = v.get<crude_json::string>();
        } else {
            log::warn("Param::fromJson: unrecognised type '", type, "' for '", name,
                      "' — defaulting to 0.0f");
        }
    }

    ParamRange range;
    if (j.contains("range") && j["range"].is_object()) {
        const auto& r = j["range"];
        if (r.contains("min") && r["min"].is_number())
            range.min = static_cast<float>(r["min"].get<crude_json::number>());
        if (r.contains("max") && r["max"].is_number())
            range.max = static_cast<float>(r["max"].get<crude_json::number>());
        if (r.contains("step") && r["step"].is_number())
            range.step = static_cast<float>(r["step"].get<crude_json::number>());
        if (r.contains("hasBounds") && r["hasBounds"].is_boolean())
            range.hasBounds = r["hasBounds"].get<crude_json::boolean>();
    }

    return Param(std::move(name), std::move(value), range);
}

}  // namespace loom::core
