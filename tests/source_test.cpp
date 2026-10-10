#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "source.hpp"

namespace fs = std::filesystem;

class ReadSource : public ::testing::Test {
  protected:
    void SetUp() override {
        const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
        path = fs::temp_directory_path() / (std::string("thoth_") + info->name());
        fs::remove(path);
    }

    void TearDown() override { fs::remove(path); }

    void write_file(const std::string &contents) {
        std::ofstream out(path, std::ios::binary);
        out << contents;
    }

    fs::path path;
};

TEST_F(ReadSource, ReadsExistingFile) {
    const std::string contents = "int main() {\n\treturn 0;\n}\n";
    write_file(contents);

    auto result = thoth::read_source(path);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, contents);
}

TEST_F(ReadSource, ReadsEmptyFile) {
    write_file("");

    auto result = thoth::read_source(path);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "");
}

TEST_F(ReadSource, PreservesBytesExactly) {
    const std::string contents("a\0b\r\nc", 6);
    write_file(contents);

    auto result = thoth::read_source(path);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, contents);
}

TEST_F(ReadSource, MissingFileReturnsNothing) {
    EXPECT_FALSE(thoth::read_source(path).has_value());
}

TEST_F(ReadSource, DirectoryReturnsNothing) {
    EXPECT_FALSE(thoth::read_source(fs::temp_directory_path()).has_value());
}
