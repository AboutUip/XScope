#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace xscope::crypto {

/// AES-256-GCM sealed blob: nonce || ciphertext || tag (tag appended by WinCNG layout we store separately).
struct SealedSecret {
    std::vector<std::uint8_t> nonce;
    std::vector<std::uint8_t> ciphertext; // includes auth tag at end (16 bytes)
};

/// Protects the workspace master key with the OS secret store (DPAPI on Windows).
class MasterKeyStore {
public:
    explicit MasterKeyStore(std::filesystem::path key_file_path);

    /// Loads or creates a 32-byte master key.
    std::vector<std::uint8_t> load_or_create();

private:
    std::filesystem::path path_;
};

class SecretBox {
public:
    explicit SecretBox(std::vector<std::uint8_t> master_key);

    SealedSecret seal(std::span<const std::uint8_t> plaintext) const;
    std::vector<std::uint8_t> open(const SealedSecret& sealed) const;

    SealedSecret seal_string(const std::string& plaintext) const;
    std::string open_string(const SealedSecret& sealed) const;

private:
    std::vector<std::uint8_t> key_;
};

} // namespace xscope::crypto
