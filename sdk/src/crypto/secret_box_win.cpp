#include "xscope/crypto/secret_box.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

namespace fs = std::filesystem;

namespace xscope::crypto {
namespace {

constexpr ULONG kAesKeyBits = 256;
constexpr ULONG kNonceSize = 12;
constexpr ULONG kTagSize = 16;

std::vector<std::uint8_t> random_bytes(size_t n) {
    std::vector<std::uint8_t> out(n);
    NTSTATUS status = BCryptGenRandom(nullptr, out.data(), static_cast<ULONG>(out.size()),
                                      BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) {
        throw std::runtime_error("BCryptGenRandom failed");
    }
    return out;
}

std::vector<std::uint8_t> dpapi_protect(std::span<const std::uint8_t> plain) {
    DATA_BLOB in{};
    in.pbData = const_cast<BYTE*>(plain.data());
    in.cbData = static_cast<DWORD>(plain.size());
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"XScopeMasterKey", nullptr, nullptr, nullptr, 0, &out)) {
        throw std::runtime_error("CryptProtectData failed");
    }
    std::vector<std::uint8_t> sealed(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    return sealed;
}

std::vector<std::uint8_t> dpapi_unprotect(std::span<const std::uint8_t> sealed) {
    DATA_BLOB in{};
    in.pbData = const_cast<BYTE*>(sealed.data());
    in.cbData = static_cast<DWORD>(sealed.size());
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
        throw std::runtime_error("CryptUnprotectData failed");
    }
    std::vector<std::uint8_t> plain(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    return plain;
}

} // namespace

MasterKeyStore::MasterKeyStore(fs::path key_file_path) : path_(std::move(key_file_path)) {}

std::vector<std::uint8_t> MasterKeyStore::load_or_create() {
    const fs::path& p = path_;
    if (fs::exists(p)) {
        std::ifstream in(p, std::ios::binary);
        std::vector<std::uint8_t> sealed((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());
        auto key = dpapi_unprotect(sealed);
        if (key.size() != 32) {
            throw std::runtime_error("master key has unexpected size");
        }
        return key;
    }

    fs::create_directories(p.parent_path());
    auto key = random_bytes(32);
    auto sealed = dpapi_protect(key);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(sealed.data()),
              static_cast<std::streamsize>(sealed.size()));
    if (!out) {
        throw std::runtime_error("failed to write master key file");
    }
    return key;
}

SecretBox::SecretBox(std::vector<std::uint8_t> master_key) : key_(std::move(master_key)) {
    if (key_.size() != 32) {
        throw std::runtime_error("SecretBox requires 32-byte key");
    }
}

SealedSecret SecretBox::seal(std::span<const std::uint8_t> plaintext) const {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    SealedSecret sealed;
    sealed.nonce = random_bytes(kNonceSize);

    NTSTATUS status = BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (status < 0) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
    }

    status = BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                               reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                               static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_GCM) + 1) * sizeof(wchar_t)), 0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        throw std::runtime_error("BCryptSetProperty GCM failed");
    }

    status = BCryptGenerateSymmetricKey(alg, &key, nullptr, 0, const_cast<PUCHAR>(key_.data()),
                                        static_cast<ULONG>(key_.size()), 0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        throw std::runtime_error("BCryptGenerateSymmetricKey failed");
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = sealed.nonce.data();
    authInfo.cbNonce = kNonceSize;
    std::vector<std::uint8_t> tag(kTagSize);
    authInfo.pbTag = tag.data();
    authInfo.cbTag = kTagSize;

    ULONG cbResult = 0;
    sealed.ciphertext.resize(plaintext.size());
    status = BCryptEncrypt(key, const_cast<PUCHAR>(plaintext.data()),
                           static_cast<ULONG>(plaintext.size()), &authInfo, nullptr, 0,
                           sealed.ciphertext.empty() ? nullptr : sealed.ciphertext.data(),
                           static_cast<ULONG>(sealed.ciphertext.size()), &cbResult, 0);
    if (status < 0) {
        BCryptDestroyKey(key);
        BCryptCloseAlgorithmProvider(alg, 0);
        throw std::runtime_error("BCryptEncrypt failed");
    }
    sealed.ciphertext.resize(cbResult);
    sealed.ciphertext.insert(sealed.ciphertext.end(), tag.begin(), tag.end());

    BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(alg, 0);
    return sealed;
}

std::vector<std::uint8_t> SecretBox::open(const SealedSecret& sealed) const {
    if (sealed.nonce.size() != kNonceSize || sealed.ciphertext.size() < kTagSize) {
        throw std::runtime_error("sealed secret is malformed");
    }

    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;

    NTSTATUS status = BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (status < 0) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
    }
    status = BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                               reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                               static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_GCM) + 1) * sizeof(wchar_t)), 0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        throw std::runtime_error("BCryptSetProperty GCM failed");
    }
    status = BCryptGenerateSymmetricKey(alg, &key, nullptr, 0, const_cast<PUCHAR>(key_.data()),
                                        static_cast<ULONG>(key_.size()), 0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        throw std::runtime_error("BCryptGenerateSymmetricKey failed");
    }

    const size_t ct_len = sealed.ciphertext.size() - kTagSize;
    std::vector<std::uint8_t> tag(sealed.ciphertext.end() - static_cast<std::ptrdiff_t>(kTagSize),
                                  sealed.ciphertext.end());

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = const_cast<PUCHAR>(sealed.nonce.data());
    authInfo.cbNonce = kNonceSize;
    authInfo.pbTag = tag.data();
    authInfo.cbTag = kTagSize;

    std::vector<std::uint8_t> plain(ct_len);
    ULONG cbResult = 0;
    status = BCryptDecrypt(key, const_cast<PUCHAR>(sealed.ciphertext.data()),
                           static_cast<ULONG>(ct_len), &authInfo, nullptr, 0,
                           plain.empty() ? nullptr : plain.data(), static_cast<ULONG>(plain.size()),
                           &cbResult, 0);
    BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (status < 0) {
        throw std::runtime_error("BCryptDecrypt failed (auth/tag)");
    }
    plain.resize(cbResult);
    return plain;
}

SealedSecret SecretBox::seal_string(const std::string& plaintext) const {
    return seal(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(plaintext.data()),
                                              plaintext.size()));
}

std::string SecretBox::open_string(const SealedSecret& sealed) const {
    auto plain = open(sealed);
    return std::string(reinterpret_cast<const char*>(plain.data()), plain.size());
}

} // namespace xscope::crypto
