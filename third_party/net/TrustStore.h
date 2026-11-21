#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include <bearssl.h>

// Trust anchors for BearSSL, decoded from PEM certificates (the Mozilla list
// in /app0/assets/certs/ca-bundle.pem, plus the dev test CA).
namespace dusk::net {

class TrustStore {
public:
    // Adds every CERTIFICATE of `pem`; returns how many were added.
    int addPem(const std::string& pem, std::string& error);

    const br_x509_trust_anchor* anchors() const { return anchors_.empty() ? nullptr : anchors_.data(); }
    size_t count() const { return anchors_.size(); }

private:
    bool addCertificate(const std::vector<unsigned char>& der, std::string& error);
    unsigned char* keep(const unsigned char* data, size_t size);

    std::vector<br_x509_trust_anchor> anchors_;
    std::deque<std::vector<unsigned char>> blobs_; // owns the DN and key bytes
};

} // namespace dusk::net
