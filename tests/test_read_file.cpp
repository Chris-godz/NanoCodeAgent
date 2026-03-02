#include <gtest/gtest.h>
#include "read_file.hpp"
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <sys/stat.h>

namespace fs = std::filesystem;

class ReadFileTest : public ::testing::Test {
protected:
    std::string test_workspace;

    void SetUp() override {
        test_workspace = fs::absolute(fs::path("test_ws_read")).string();
        fs::create_directories(test_workspace);
    }

    void TearDown() override {
        fs::remove_all(test_workspace);
    }

    void create_mock_file(const std::string& rel_path, const std::string& content) {
        fs::path p = fs::path(test_workspace) / rel_path;
        fs::create_directories(p.parent_path());
        std::ofstream ofs(p, std::ios::binary);
        ofs.write(content.data(), content.size());
    }
};

TEST_F(ReadFileTest, ReadOkSmallText) {
    create_mock_file("foo.txt", "Hello Secure Read!");
    
    auto res = read_file_safe(test_workspace, "foo.txt");
    
    EXPECT_TRUE(res.ok) << "Expected read to succeed: " << res.err;
    EXPECT_FALSE(res.truncated);
    EXPECT_FALSE(res.is_binary);
    EXPECT_EQ(res.content, "Hello Secure Read!");
    EXPECT_EQ(res.bytes_read, 18);
}

TEST_F(ReadFileTest, ReadRespectsMaxBytes) {
    std::string huge(5000, 'X'); 
    create_mock_file("big.txt", huge);
    
    // Set artificial limit of 4000 bytes
    auto res = read_file_safe(test_workspace, "big.txt", 4000);
    
    EXPECT_TRUE(res.ok);
    EXPECT_TRUE(res.truncated); 
    EXPECT_EQ(res.content.size(), 4000);
    EXPECT_EQ(res.bytes_read, 4000);
    EXPECT_EQ(res.content.substr(0, 5), "XXXXX");
}

TEST_F(ReadFileTest, ReadRejectsSymlinkEscape) {
    // Setup nested sub folder
    fs::path sub_path = fs::path(test_workspace) / "nested";
    fs::create_directories(sub_path);

    // Attack: write a symlink directing completely out into system /etc/passwd
    std::string sym_target = (sub_path / "link").string();
    symlink("/etc/passwd", sym_target.c_str());

    auto res = read_file_safe(test_workspace, "nested/link");
    EXPECT_FALSE(res.ok);
    EXPECT_NE(res.err.find("securely open target"), std::string::npos);

    // Setup mid-path directory symlink
    std::string sym_dir = (fs::path(test_workspace) / "linked_dir").string();
    symlink(sub_path.c_str(), sym_dir.c_str());
    
    // Attack traversal
    auto res2 = read_file_safe(test_workspace, "linked_dir/some_file.txt");
    EXPECT_FALSE(res2.ok);
    EXPECT_NE(res2.err.find("securely traverse directory"), std::string::npos);
}

TEST_F(ReadFileTest, ReadRejectsFifoDoesNotHang) {
    // Create FIFO file
    std::string fifo_path = (fs::path(test_workspace) / "pipe").string();
    mkfifo(fifo_path.c_str(), 0666);

    auto res = read_file_safe(test_workspace, "pipe");
    
    // If defensive code failed, `read_file_safe` would hang indefinitely here.
    EXPECT_FALSE(res.ok);
    EXPECT_NE(res.err.find("statistically regular file type"), std::string::npos);
}

TEST_F(ReadFileTest, ReadBinaryPolicy) {
    // Injecting NUL character `\0` marking file implicitly as binary
    std::string binary_data("Hello \x00 World", 13);
    create_mock_file("data.bin", binary_data);

    auto res = read_file_safe(test_workspace, "data.bin");

    // Fails due to binary detection Policy 1
    EXPECT_FALSE(res.ok);
    EXPECT_TRUE(res.is_binary);
    EXPECT_TRUE(res.content.empty()); // Does not leak dirty memory
    EXPECT_NE(res.err.find("Detected binary content"), std::string::npos);
}
