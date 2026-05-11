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

}  // namespace
}  // namespace nearby::sharing
