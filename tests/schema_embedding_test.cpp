#include "project_schema.hpp"
#include <gtest/gtest.h>
#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

TEST(SchemaEmbedding, AllVersionsPreserveOriginalBytes) {
    const std::array<std::string_view, 5> embedded{
        sim::project::detail::schema_text,
        sim::project::detail::schema_conveyor_text,
        sim::project::detail::schema_ton_text,
        sim::project::detail::schema_ctu_text,
        sim::project::detail::schema_compare_text};
    for (std::size_t index = 0; index < embedded.size(); ++index) {
        const auto path = std::string(SIM_SCHEMA_DIR) + "/project-1." +
                          std::to_string(index) + ".schema.json";
        SCOPED_TRACE(path);
        std::ifstream file(path, std::ios::binary);
        ASSERT_TRUE(file.is_open());
        const std::string original{std::istreambuf_iterator<char>{file},
                                   std::istreambuf_iterator<char>{}};
        EXPECT_EQ(embedded[index], original);
    }
}
