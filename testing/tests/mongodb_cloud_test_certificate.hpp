#pragma once
// Test-only self-signed certificate, generated inside the harness temp directory.
auto write_mongodb_cloud_test_certificate(std::string_view certificate, std::string_view key) -> bool;
