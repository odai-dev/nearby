#include "sharing/linux/nearby_sharing_api.h"

#include <fstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace nearby::sharing {
namespace {

bool ContainsWarning(const std::vector<std::string>& warnings,
                     const std::string& needle) {
  for (const std::string& warning : warnings) {
    if (warning.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

TEST(NearbySharingApiTest, DiagnosticsWarningsMatchUnavailableServices) {
  const NearbySharingApi::DiagnosticInfo diagnostics =
      NearbySharingApi::CollectDiagnostics();

  EXPECT_EQ(!diagnostics.dbus_available,
            ContainsWarning(diagnostics.warnings, "D-Bus"));
  EXPECT_EQ(!diagnostics.bluetooth_available,
            ContainsWarning(diagnostics.warnings, "Bluetooth"));
  EXPECT_EQ(!diagnostics.network_manager_available,
            ContainsWarning(diagnostics.warnings, "NetworkManager"));
  EXPECT_EQ(!diagnostics.avahi_available,
            ContainsWarning(diagnostics.warnings, "Avahi"));
}

TEST(NearbySharingApiTest, ValidateSendFilePathsRejectsMissingFile) {
  const std::string missing_path = testing::TempDir() + "/nearby-missing-file";

  EXPECT_EQ(NearbySharingApi::ValidateSendFilePaths({missing_path}),
            NearbySharingApi::StatusCode::kInvalidArgument);
}

TEST(NearbySharingApiTest, ValidateSendFilePathsRejectsZeroByteFile) {
  const std::string zero_byte_path = testing::TempDir() + "/nearby-empty-file";
  std::ofstream(zero_byte_path).close();

  EXPECT_EQ(NearbySharingApi::ValidateSendFilePaths({zero_byte_path}),
            NearbySharingApi::StatusCode::kInvalidArgument);
}

TEST(NearbySharingApiTest, ValidateSendFilePathsAcceptsReadableNonEmptyFile) {
  const std::string file_path = testing::TempDir() + "/nearby-valid-file";
  std::ofstream file(file_path);
  file << "hello";
  file.close();

  EXPECT_EQ(NearbySharingApi::ValidateSendFilePaths({file_path}),
            NearbySharingApi::StatusCode::kOk);
}

TEST(NearbySharingApiTest, ValidateSendTextRejectsEmptyText) {
  EXPECT_EQ(NearbySharingApi::ValidateSendText(" \t\n"),
            NearbySharingApi::StatusCode::kInvalidArgument);
}

TEST(NearbySharingApiTest, ValidateSendTextAcceptsValidText) {
  EXPECT_EQ(NearbySharingApi::ValidateSendText("hello"),
            NearbySharingApi::StatusCode::kOk);
}

TEST(NearbySharingApiTest, ValidateSendUrlRejectsInvalidUrl) {
  EXPECT_EQ(NearbySharingApi::ValidateSendUrl("ftp://example.com"),
            NearbySharingApi::StatusCode::kInvalidArgument);
  EXPECT_EQ(NearbySharingApi::ValidateSendUrl("example.com"),
            NearbySharingApi::StatusCode::kInvalidArgument);
  EXPECT_EQ(NearbySharingApi::ValidateSendUrl("https://"),
            NearbySharingApi::StatusCode::kInvalidArgument);
}

TEST(NearbySharingApiTest, ValidateSendUrlAcceptsHttpUrls) {
  EXPECT_EQ(NearbySharingApi::ValidateSendUrl("https://example.com"),
            NearbySharingApi::StatusCode::kOk);
  EXPECT_EQ(NearbySharingApi::ValidateSendUrl("http://example.com"),
            NearbySharingApi::StatusCode::kOk);
}

TEST(NearbySharingApiTest, ValidateReceiveFolderChecksFolderState) {
  const std::string missing_path = testing::TempDir() + "/nearby-missing-dir";
  const std::string file_path = testing::TempDir() + "/nearby-not-dir";
  std::ofstream(file_path) << "hello";

  EXPECT_EQ(NearbySharingApi::ValidateReceiveFolder(missing_path),
            NearbySharingApi::StatusCode::kInvalidArgument);
  EXPECT_EQ(NearbySharingApi::ValidateReceiveFolder(file_path),
            NearbySharingApi::StatusCode::kInvalidArgument);
  EXPECT_EQ(NearbySharingApi::ValidateReceiveFolder(testing::TempDir()),
            NearbySharingApi::StatusCode::kOk);
}

}  // namespace
}  // namespace nearby::sharing
