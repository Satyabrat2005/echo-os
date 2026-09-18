#include "echo/crypto/sealed_box.hpp"

namespace echo::crypto {

Mac seal_in_place(const Key256& key, const Block& iv, std::vector<std::uint8_t>& buf,
                   std::size_t plaintext_offset) noexcept {
    if (plaintext_offset < buf.size()) {
        ctr_xcrypt(key, iv, buf.data() + plaintext_offset, buf.size() - plaintext_offset);
    }
    // MAC covers the whole buffer as it now stands: the untouched header plus the
    // just-encrypted region, exactly what open_in_place() below re-derives and compares.
    return cmac(key, buf.data(), buf.size());
}

bool open_in_place(const Key256& key, const Block& iv, const Mac& tag,
                    std::vector<std::uint8_t>& buf, std::size_t plaintext_offset) noexcept {
    if (plaintext_offset > buf.size()) return false;  // malformed offset; nothing to open

    // MAC FIRST — before a single byte is decrypted or interpreted, mirroring
    // SecureChannel::open()'s order exactly. A wrong key, corruption, and deliberate
    // tampering are all indistinguishable from here on: `buf` is left untouched either way.
    const Mac expected = cmac(key, buf.data(), buf.size());
    if (!mac_equal(expected, tag)) return false;

    ctr_xcrypt(key, iv, buf.data() + plaintext_offset, buf.size() - plaintext_offset);
    return true;
}

}  // namespace echo::crypto
