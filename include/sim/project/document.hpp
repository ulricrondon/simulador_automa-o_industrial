#pragma once
#include "sim/core/simulation.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace sim::project {
inline constexpr std::size_t max_document_bytes = 16 * 1024 * 1024;
inline constexpr std::uint64_t max_asset_bytes = 256 * 1024 * 1024;
enum class ErrorCode { invalid_json, schema, unsupported_version, semantic, integrity, io, limit };
class Error : public std::runtime_error {
public:
    Error(ErrorCode code, std::string message) : std::runtime_error(std::move(message)), code_(code) {}
    ErrorCode code() const noexcept { return code_; }
private:
    ErrorCode code_;
};
struct AssetRecord {
    std::string id;
    std::string sha256;
    std::uint64_t byte_size{};
    std::string media_type;
    bool operator==(const AssetRecord&) const = default;
};
struct Document {
    std::string project_id;
    std::string title;
    std::string axis_id;
    std::vector<std::string> rung_ids;
    core::RunSpec simulation;
    std::vector<AssetRecord> assets;
};
// Format 1.0, model 1, engine discrete-v1. No implicit migrations.
Document parse(std::string_view utf8);
std::string serialize(const Document& document);
Document load(const std::filesystem::path& path);
Document new_demo(std::string title);
void validate(const Document& document);
bool is_uuid_v4(std::string_view text) noexcept;
bool is_sha256(std::string_view text) noexcept;
std::string generate_uuid_v4();
std::string sha256(std::span<const std::byte> data);
void verify_asset(const AssetRecord& record, std::span<const std::byte> data);
}
