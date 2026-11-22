#include "net/TrustStore.h"

#include <cstring>

namespace dusk::net {

namespace {

void appendBytes(void* context, const void* data, size_t size)
{
    auto* out = static_cast<std::vector<unsigned char>*>(context);
    const auto* bytes = static_cast<const unsigned char*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

} // namespace

unsigned char* TrustStore::keep(const unsigned char* data, size_t size)
{
    blobs_.emplace_back(data, data + size);
    return blobs_.back().data();
}

// Same conversion as BearSSL's tools/certs.c (certificate_to_trust_anchor).
bool TrustStore::addCertificate(const std::vector<unsigned char>& der, std::string& error)
{
    std::vector<unsigned char> dn;
    br_x509_decoder_context decoder;
    br_x509_decoder_init(&decoder, appendBytes, &dn);
    br_x509_decoder_push(&decoder, der.data(), der.size());
    const br_x509_pkey* key = br_x509_decoder_get_pkey(&decoder);
    if (!key) {
        error = "certificate not decoded (BearSSL error " + std::to_string(br_x509_decoder_last_error(&decoder)) + ")";
        return false;
    }
    br_x509_trust_anchor anchor;
    memset(&anchor, 0, sizeof(anchor));
    anchor.dn.data = keep(dn.data(), dn.size());
    anchor.dn.len = dn.size();
    anchor.flags = br_x509_decoder_isCA(&decoder) ? BR_X509_TA_CA : 0;
    switch (key->key_type) {
    case BR_KEYTYPE_RSA:
        anchor.pkey.key_type = BR_KEYTYPE_RSA;
        anchor.pkey.key.rsa.n = keep(key->key.rsa.n, key->key.rsa.nlen);
        anchor.pkey.key.rsa.nlen = key->key.rsa.nlen;
        anchor.pkey.key.rsa.e = keep(key->key.rsa.e, key->key.rsa.elen);
        anchor.pkey.key.rsa.elen = key->key.rsa.elen;
        break;
    case BR_KEYTYPE_EC:
        anchor.pkey.key_type = BR_KEYTYPE_EC;
        anchor.pkey.key.ec.curve = key->key.ec.curve;
        anchor.pkey.key.ec.q = keep(key->key.ec.q, key->key.ec.qlen);
        anchor.pkey.key.ec.qlen = key->key.ec.qlen;
        break;
    default:
        error = "unsupported public key type";
        return false;
    }
    anchors_.push_back(anchor);
    return true;
}

int TrustStore::addPem(const std::string& pem, std::string& error)
{
    br_pem_decoder_context decoder;
    br_pem_decoder_init(&decoder);
    std::vector<unsigned char> der;
    bool inCertificate = false;
    int added = 0;
    int skipped = 0;
    // BearSSL needs the last line terminated: feed a final newline.
    const std::string text = pem + "\n";
    const unsigned char* data = reinterpret_cast<const unsigned char*>(text.data());
    size_t left = text.size();
    while (left > 0) {
        const size_t used = br_pem_decoder_push(&decoder, data, left);
        data += used;
        left -= used;
        switch (br_pem_decoder_event(&decoder)) {
        case BR_PEM_BEGIN_OBJ:
            der.clear();
            inCertificate = strcmp(br_pem_decoder_name(&decoder), "CERTIFICATE") == 0;
            br_pem_decoder_setdest(&decoder, inCertificate ? appendBytes : nullptr, inCertificate ? &der : nullptr);
            break;
        case BR_PEM_END_OBJ:
            if (inCertificate) {
                std::string certError;
                if (addCertificate(der, certError)) {
                    ++added;
                } else {
                    ++skipped;
                    error = certError;
                }
            }
            inCertificate = false;
            break;
        case BR_PEM_ERROR:
            error = "invalid PEM data";
            return added;
        default:
            break;
        }
    }
    if (added == 0 && error.empty()) {
        error = "no certificate found";
    } else if (skipped > 0) {
        error = std::to_string(skipped) + " certificate(s) skipped: " + error;
    }
    return added;
}

} // namespace dusk::net
