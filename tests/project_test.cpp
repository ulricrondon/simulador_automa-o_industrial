#include "sim/project/document.hpp"
#include "sim/runtime/runner.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <array>
#include <fstream>
#include <limits>
#include <set>
#include <thread>

namespace {
using namespace sim::project;
using Json = nlohmann::json;
const std::filesystem::path fixture = std::filesystem::path(SIM_FIXTURES_DIR) / "project-v1.json";
Json json_fixture() {
    std::ifstream stream(fixture);
    return Json::parse(stream);
}
std::span<const std::byte> bytes(std::string_view text) {
    return std::as_bytes(std::span(text.data(), text.size()));
}
void expect_error(const std::string& text, ErrorCode expected) {
    try { const auto result = parse(text); (void)result; FAIL() << "Expected rejection"; }
    catch (const Error& error) { EXPECT_EQ(error.code(), expected) << error.what(); }
}
TEST(Project, FixtureRoundTripPreservesIdentityAndEntireTrace) {
    const auto original = load(fixture);
    const auto written = serialize(original);
    const auto restored = parse(written);
    EXPECT_EQ(restored.project_id, original.project_id);
    EXPECT_EQ(restored.axis_id, original.axis_id);
    EXPECT_EQ(restored.rung_ids, original.rung_ids);
    EXPECT_EQ(restored.assets, original.assets);
    EXPECT_EQ(serialize(restored), written);
    sim::runtime::Runner runner;
    EXPECT_EQ(*runner.submit(restored.simulation).get(), sim::core::simulate(sim::core::demo_scenario()));
}
TEST(Project, NewDocumentsHaveFreshIdentityButSameSimulation) {
    const auto a = new_demo("Primeiro");
    const auto b = new_demo("Segundo");
    EXPECT_NE(a.project_id, b.project_id);
    EXPECT_NE(a.axis_id, b.axis_id);
    EXPECT_NE(a.rung_ids, b.rung_ids);
    EXPECT_EQ(sim::core::simulate(a.simulation), sim::core::simulate(b.simulation));
}
TEST(Project, UnicodeTitleSurvivesRoundTrip) {
    auto doc = load(fixture);
    doc.title = "Simula\xc3\xa7\xc3\xa3o industrial";
    EXPECT_EQ(parse(serialize(doc)).title, doc.title);
}
TEST(Project, RejectsMissingAndUnknownFieldsAtEveryLevel) {
    auto json = json_fixture();
    json.erase("project_id");
    expect_error(json.dump(), ErrorCode::schema);
    json = json_fixture();
    json["unexpected"] = true;
    expect_error(json.dump(), ErrorCode::schema);
    json = json_fixture();
    json["simulation"]["axis"]["unexpected"] = true;
    expect_error(json.dump(), ErrorCode::schema);
    json = json_fixture();
    json["$schema"] = "https://example.invalid/untrusted-schema";
    expect_error(json.dump(), ErrorCode::schema);
}
TEST(Project, VersionsRequireExplicitMigration) {
    for (const auto major : {0, 2}) {
        auto json = json_fixture();
        json["format_version"]["major"] = major;
        expect_error(json.dump(), ErrorCode::unsupported_version);
    }
    auto json = json_fixture();
    json["format_version"]["minor"] = 1;
    expect_error(json.dump(), ErrorCode::unsupported_version);
    json = json_fixture();
    json["model_version"] = 2;
    expect_error(json.dump(), ErrorCode::unsupported_version);
    json = json_fixture();
    json["engine"] = "future-engine";
    expect_error(json.dump(), ErrorCode::unsupported_version);
}
TEST(Project, InvalidEnvelopeAndWrongFormatAreRejected) {
    expect_error("[]", ErrorCode::schema);
    auto json = json_fixture();
    json["format_version"] = "1.0";
    expect_error(json.dump(), ErrorCode::schema);
    json = json_fixture();
    json["format"] = "another-product";
    expect_error(json.dump(), ErrorCode::schema);
}
TEST(Project, DuplicateKeysIncludingEscapedKeysAreRejected) {
    expect_error("{\"format\":1,\"format\":2}", ErrorCode::invalid_json);
    expect_error("{\"nested\":{\"x\":1,\"\\u0078\":2}}", ErrorCode::invalid_json);
}
TEST(Project, InvalidJsonTrailingDataAndInvalidUtf8AreRejected) {
    expect_error("{", ErrorCode::invalid_json);
    expect_error(json_fixture().dump() + " true", ErrorCode::invalid_json);
    auto text = json_fixture().dump();
    const auto position = text.find("Contator");
    ASSERT_NE(position, std::string::npos);
    text[position] = static_cast<char>(0xff);
    expect_error(text, ErrorCode::invalid_json);
}
TEST(Project, SizeAndNestingAreBoundedBeforeSchemaValidation) {
    expect_error(std::string(max_document_bytes + 1, ' '), ErrorCode::limit);
    expect_error(std::string(70, '[') + "0" + std::string(70, ']'), ErrorCode::limit);
}
TEST(Project, FloatingPointAndHugeNumericTokensAreRejected) {
    auto json = json_fixture();
    json["simulation"]["ticks"] = 8.5;
    expect_error(json.dump(), ErrorCode::invalid_json);
    auto text = json_fixture().dump();
    const auto where = text.find("\"ticks\":8");
    ASSERT_NE(where, std::string::npos);
    text.replace(where, std::string("\"ticks\":8").size(), "\"ticks\":18446744073709551616");
    expect_error(text, ErrorCode::invalid_json);
}
TEST(Project, SignedIntegerOverflowCannotWrapDuringDecode) {
    auto json = json_fixture();
    json["simulation"]["axis"]["maximum_um"] = std::numeric_limits<std::uint64_t>::max();
    expect_error(json.dump(), ErrorCode::semantic);
}
TEST(Project, LargeSignedDomainSequenceIsPreservedExactly) {
    auto json = json_fixture();
    json["simulation"]["events"][1]["sequence"] = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const auto doc = parse(json.dump());
    EXPECT_EQ(doc.simulation.events[1].sequence, static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
    EXPECT_EQ(parse(serialize(doc)).simulation.events[1].sequence, static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
}
TEST(Project, CoreSemanticErrorsAreRejectedBeforeExecution) {
    auto json = json_fixture();
    json["simulation"]["events"][1]["sequence"] = 0;
    expect_error(json.dump(), ErrorCode::semantic);
    json = json_fixture();
    json["simulation"]["axis"]["drive"] = 1;
    expect_error(json.dump(), ErrorCode::semantic);
    json = json_fixture();
    json["simulation"]["events"][1]["tick"] = 8;
    expect_error(json.dump(), ErrorCode::semantic);
    json = json_fixture();
    json["simulation"]["tick_period_us"] = std::numeric_limits<std::int64_t>::max();
    expect_error(json.dump(), ErrorCode::semantic);
}
TEST(Project, CrossEntityUuidDuplicatesAreRejected) {
    auto json = json_fixture();
    json["simulation"]["axis"]["id"] = json["project_id"];
    expect_error(json.dump(), ErrorCode::semantic);
    auto doc = load(fixture);
    doc.rung_ids[0] = doc.project_id;
    EXPECT_THROW(serialize(doc), Error);
}
TEST(Project, InvalidInMemoryDocumentsCannotBeSerialized) {
    auto doc = load(fixture);
    doc.rung_ids.clear();
    EXPECT_THROW(serialize(doc), Error);
    doc = load(fixture);
    doc.title = std::string(161, 'a');
    EXPECT_THROW(serialize(doc), Error);
    doc = load(fixture);
    doc.title = std::string(1, static_cast<char>(0xff));
    EXPECT_THROW(validate(doc), Error);
    EXPECT_THROW(serialize(doc), Error);
    doc = load(fixture);
    doc.simulation.events[0].sequence = std::numeric_limits<std::uint64_t>::max();
    EXPECT_THROW(serialize(doc), Error);
    doc = load(fixture);
    doc.simulation.program.rungs[0].branches[0][0].bank = static_cast<sim::core::Bank>(7);
    EXPECT_THROW(serialize(doc), Error);
}
TEST(Project, AssetsRoundTripButConflictingCatalogMetadataIsRejected) {
    auto doc = load(fixture);
    const auto digest = sha256(bytes("abc"));
    doc.assets = {{generate_uuid_v4(), digest, 3, "model/gltf+json"}};
    EXPECT_EQ(parse(serialize(doc)).assets, doc.assets);
    auto second = doc.assets[0];
    second.id = generate_uuid_v4();
    doc.assets.push_back(second); // Same content can have distinct entity identities.
    EXPECT_NO_THROW(serialize(doc));
    doc.assets[1].byte_size = 4;
    EXPECT_THROW(serialize(doc), Error);
}
TEST(Project, UnicodeFilePathLoadsWithoutLocaleConversion) {
    auto path = std::filesystem::temp_directory_path() / std::filesystem::path(u"projeto-a\u00e7\u00e3o-");
    path += generate_uuid_v4() + ".json";
    ASSERT_FALSE(std::filesystem::exists(path));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{path};
    {
        std::ofstream file(path, std::ios::binary);
        ASSERT_TRUE(file);
        file << json_fixture().dump();
        ASSERT_TRUE(file);
    }
    EXPECT_EQ(load(path).project_id, load(fixture).project_id);
}
TEST(Project, MissingFileIsReportedAsIoError) {
    try { const auto doc = load(fixture.parent_path() / "does-not-exist.json"); (void)doc; FAIL(); }
    catch (const Error& error) { EXPECT_EQ(error.code(), ErrorCode::io); }
}
TEST(Project, IndependentThreadsValidateAndReplayTheSameDocument) {
    const auto text = json_fixture().dump();
    const auto expected = sim::core::simulate(sim::core::demo_scenario());
    std::vector<std::jthread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&] {
            for (int n = 0; n < 10; ++n) {
                EXPECT_EQ(sim::core::simulate(parse(text).simulation), expected);
            }
        });
    }
}
TEST(Identity, UuidGeneratorSetsVersionVariantAndProducesDistinctValues) {
    std::set<std::string> ids;
    for (int i = 0; i < 1000; ++i) {
        const auto id = generate_uuid_v4();
        EXPECT_TRUE(is_uuid_v4(id));
        EXPECT_TRUE(ids.insert(id).second);
    }
}
TEST(Identity, RejectsMalformedNonV4AndUppercaseUuids) {
    EXPECT_TRUE(is_uuid_v4("d53c2a81-681a-4e38-9bd7-2e329936ee47"));
    EXPECT_FALSE(is_uuid_v4("D53c2a81-681a-4e38-9bd7-2e329936ee47"));
    EXPECT_FALSE(is_uuid_v4("d53c2a81-681a-1e38-9bd7-2e329936ee47"));
    EXPECT_FALSE(is_uuid_v4("d53c2a81-681a-4e38-7bd7-2e329936ee47"));
    EXPECT_FALSE(is_uuid_v4("00000000-0000-0000-0000-000000000000"));
    EXPECT_FALSE(is_uuid_v4("invalid"));
}
TEST(Identity, Sha256MatchesKnownVectors) {
    EXPECT_EQ(sha256(bytes("")), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256(bytes("abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string million(1000000, 'a');
    EXPECT_EQ(sha256(bytes(million)), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    const std::array<std::byte, 3> binary{std::byte{0}, std::byte{1}, std::byte{255}};
    EXPECT_EQ(sha256(binary), "26a66b061e8f48f39927c312f25293959729eee95978e2892d49d3512a5cc092");
}
TEST(Identity, DigestsMustBeCanonicalLowercaseHex) {
    EXPECT_TRUE(is_sha256(sha256(bytes("abc"))));
    EXPECT_FALSE(is_sha256(std::string(64, 'A')));
    EXPECT_FALSE(is_sha256(std::string(63, 'a')));
}
TEST(Integrity, ChecksBothSizeAndDigestBeforeAcceptance) {
    AssetRecord asset{generate_uuid_v4(), sha256(bytes("abc")), 3, "model/gltf+json"};
    EXPECT_NO_THROW(verify_asset(asset, bytes("abc")));
    try { verify_asset(asset, bytes("abd")); FAIL(); }
    catch (const Error& error) { EXPECT_EQ(error.code(), ErrorCode::integrity); }
    try { verify_asset(asset, bytes("ab")); FAIL(); }
    catch (const Error& error) { EXPECT_EQ(error.code(), ErrorCode::integrity); }
    asset.byte_size = max_asset_bytes + 1;
    EXPECT_THROW(verify_asset(asset, bytes("abc")), Error);
}
}
