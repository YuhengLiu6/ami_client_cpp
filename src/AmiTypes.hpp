#ifndef AMI_TYPES_HPP
#define AMI_TYPES_HPP

#include <variant>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

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

#endif // AMI_TYPES_HPP
