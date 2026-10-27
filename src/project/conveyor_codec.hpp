#pragma once
#include "sim/project/document.hpp"
#include <nlohmann/json.hpp>
namespace sim::project::detail {
nlohmann::json encode_conveyor(const Document& document);
void decode_conveyor(const nlohmann::json& json, Document& document);
}
