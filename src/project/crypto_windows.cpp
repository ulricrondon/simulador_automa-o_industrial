#include "sim/project/document.hpp"
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <limits>

namespace sim::project {
namespace {
bool hex(char c) noexcept { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }
std::string encode(std::span<const unsigned char> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }
    return result;
}
void check(NTSTATUS status, const char* operation) {
    if (status < 0) { throw Error(ErrorCode::io, std::string(operation) + " failed"); }
}
struct Algorithm {
    BCRYPT_ALG_HANDLE handle{};
    Algorithm() { check(BCryptOpenAlgorithmProvider(&handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0), "SHA-256 provider"); }
    ~Algorithm() { BCryptCloseAlgorithmProvider(handle, 0); }
    Algorithm(const Algorithm&) = delete;
    Algorithm& operator=(const Algorithm&) = delete;
};
}
bool is_uuid_v4(std::string_view text) noexcept {
    if (text.size() != 36 || text[14] != '4' ||
        (text[19] != '8' && text[19] != '9' && text[19] != 'a' && text[19] != 'b')) { return false; }
    for (std::size_t i = 0; i < text.size(); ++i) {
        const bool separator = i == 8 || i == 13 || i == 18 || i == 23;
        if (separator ? text[i] != '-' : !hex(text[i])) { return false; }
    }
    return true;
}
bool is_sha256(std::string_view text) noexcept {
    return text.size() == 64 && std::all_of(text.begin(), text.end(), hex);
}
std::string generate_uuid_v4() {
    std::array<unsigned char, 16> bytes{};
    check(BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                         BCRYPT_USE_SYSTEM_PREFERRED_RNG), "UUID random generator");
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);
    const auto text = encode(bytes);
    return text.substr(0, 8) + "-" + text.substr(8, 4) + "-" + text.substr(12, 4) +
           "-" + text.substr(16, 4) + "-" + text.substr(20, 12);
}
std::string sha256(std::span<const std::byte> data) {
    if (data.size() > std::numeric_limits<ULONG>::max()) {
        throw Error(ErrorCode::limit, "SHA-256 buffer exceeds Windows single-buffer limit");
    }
    Algorithm algorithm;
    std::array<unsigned char, 32> digest{};
    // BCryptHash documents pbInput as unmodified despite its non-const signature.
    auto input = const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(data.data()));
    unsigned char empty{};
    check(BCryptHash(algorithm.handle, nullptr, 0, data.empty() ? &empty : input,
                     static_cast<ULONG>(data.size()), digest.data(), static_cast<ULONG>(digest.size())),
          "SHA-256");
    return encode(digest);
}
void verify_asset(const AssetRecord& record, std::span<const std::byte> data) {
    if (!is_uuid_v4(record.id) || !is_sha256(record.sha256) ||
        (record.media_type != "model/gltf+json" && record.media_type != "model/gltf-binary")) {
        throw Error(ErrorCode::semantic, "Invalid asset metadata");
    }
    if (record.byte_size > max_asset_bytes) { throw Error(ErrorCode::limit, "Asset exceeds size limit"); }
    if (record.byte_size != data.size()) { throw Error(ErrorCode::integrity, "Asset byte size mismatch"); }
    if (sha256(data) != record.sha256) { throw Error(ErrorCode::integrity, "Asset SHA-256 mismatch"); }
}
}
