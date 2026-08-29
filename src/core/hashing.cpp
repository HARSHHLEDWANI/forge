#include "core/hashing.hpp"

#include <openssl/evp.h>

#include "core/error.hpp"

namespace forge::core {

struct Sha256Hasher::Impl {
    EVP_MD_CTX* ctx = nullptr;
};

Sha256Hasher::Sha256Hasher() : impl_(std::make_unique<Impl>()) {
    impl_->ctx = EVP_MD_CTX_new();
    if (impl_->ctx == nullptr || EVP_DigestInit_ex(impl_->ctx, EVP_sha256(), nullptr) != 1) {
        if (impl_->ctx != nullptr) {
            EVP_MD_CTX_free(impl_->ctx);
        }
        throw ForgeError("failed to initialize SHA-256 context");
    }
}

Sha256Hasher::~Sha256Hasher() {
    if (impl_ && impl_->ctx != nullptr) {
        EVP_MD_CTX_free(impl_->ctx);
    }
}

Sha256Hasher::Sha256Hasher(Sha256Hasher&&) noexcept = default;
Sha256Hasher& Sha256Hasher::operator=(Sha256Hasher&&) noexcept = default;

void Sha256Hasher::update(std::string_view data) {
    if (EVP_DigestUpdate(impl_->ctx, data.data(), data.size()) != 1) {
        throw ForgeError("SHA-256 update failed");
    }
}

Sha256Digest Sha256Hasher::finish() {
    Sha256Digest digest{};
    unsigned int digest_len = 0;
    if (EVP_DigestFinal_ex(impl_->ctx, reinterpret_cast<unsigned char*>(digest.data()), &digest_len) !=
            1 ||
        digest_len != digest.size()) {
        throw ForgeError("SHA-256 finalize failed");
    }
    return digest;
}

Sha256Digest sha256(std::string_view data) {
    Sha256Hasher hasher;
    hasher.update(data);
    return hasher.finish();
}

} // namespace forge::core
