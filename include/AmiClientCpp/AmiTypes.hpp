#pragma once
#include <variant>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace ami {
    using AmiValue = std::variant<
        std::nullptr_t,
        bool,
        int,
        long long,
        long,
        float,
        double,
        std::string,
        std::vector<uint8_t>,
        nlohmann::json>;

}// AMI_TYPES_HPP
