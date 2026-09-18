// ECHO OS — CMAC known-answer-vector test (Phase 24).
//
// `common/src/crypto/cmac.cpp`'s header comment has, since Phase 22, claimed verification
// "against the SP 800-38B AES-256 vectors in tests/crypto_test.cpp" — a file that did not
// exist. CMAC was exercised only functionally (the bit-flip tamper test in
// tests/caregiver_link_test.cpp), never against a published vector, unlike the AES-CTR
// cipher it's built on (see test_aes_known_answer_vectors in tests/memory_engine_test.cpp).
// This file closes that gap for real.
//
// PROVENANCE, stated plainly because a fabricated "known-answer vector" would be worse than
// having none: the four AES-256 message/tag pairs below are NIST SP 800-38B Appendix D.3's
// standard CMAC example set. They were not typed from memory — they were fetched from two
// independent, widely-used open-source implementations that each cite the same NIST
// publication (github.com/aead/cmac and the Botan crypto library's test-vector corpus), and
// cross-checked against each other: all three overlapping vectors (Mlen 0/128/320) matched
// byte-for-byte between the two sources, and the fourth (Mlen 512) came from Botan alone.
// The AES-256 key used here, 603deb10...14dff4, is not a coincidence: NIST reuses this same
// key across its SP 800-38A CTR examples AND its SP 800-38B CMAC examples, and it already
// appears — independently trusted — in this codebase's own test_aes_known_answer_vectors
// (tests/memory_engine_test.cpp), which is one more point of cross-agreement.
//
// The plaintext blocks (6bc1bee2...) are NIST's standard example plaintext, reused verbatim
// across the whole SP 800-38A/38B family — recognizable as the same blocks the existing
// AES-CTR KAT test already uses.
#include "echo/crypto/cmac.hpp"

#include "check.hpp"

#include <cstdio>
#include <cstring>
#include <cstdint>

using namespace echo::crypto;

namespace {

void hex_to(const char* h, std::uint8_t* out, int n) {
    for (int i = 0; i < n; ++i) {
        unsigned v = 0;
        (void)std::sscanf(h + 2 * i, "%2x", &v);
        out[i] = static_cast<std::uint8_t>(v);
    }
}

Mac hex_to_mac(const char* h) {
    Mac m{};
    hex_to(h, m.data(), 16);
    return m;
}

// NIST SP 800-38B Appendix D.3 — AES-256 CMAC, all four standard message lengths.
void test_cmac_tag_kat() {
    Key256 key{};
    hex_to("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", key.data(), 32);

    // Mlen = 0.
    {
        const Mac tag = cmac(key, nullptr, 0);
        CHECK(tag == hex_to_mac("028962f61b7bf89efc6b551f4667d983"));
    }
    // Mlen = 128 bits (16 bytes).
    {
        std::uint8_t msg[16];
        hex_to("6bc1bee22e409f96e93d7e117393172a", msg, 16);
        const Mac tag = cmac(key, msg, sizeof(msg));
        CHECK(tag == hex_to_mac("28a7023f452e8f82bd4bf28d8c37c35c"));
    }
    // Mlen = 320 bits (40 bytes).
    {
        std::uint8_t msg[40];
        hex_to("6bc1bee22e409f96e93d7e117393172a"
               "ae2d8a571e03ac9c9eb76fac45af8e51"
               "30c81c46a35ce411",
               msg, 40);
        const Mac tag = cmac(key, msg, sizeof(msg));
        CHECK(tag == hex_to_mac("aaf3d8f1de5640c232f5b169b9c911e6"));
    }
    // Mlen = 512 bits (64 bytes).
    {
        std::uint8_t msg[64];
        hex_to("6bc1bee22e409f96e93d7e117393172a"
               "ae2d8a571e03ac9c9eb76fac45af8e51"
               "30c81c46a35ce411e5fbc1191a0a52ef"
               "f69f2445df4f9b17ad2b417be66c3710",
               msg, 64);
        const Mac tag = cmac(key, msg, sizeof(msg));
        CHECK(tag == hex_to_mac("e1992190549f6ed5696a2c056c315410"));
    }
}

// mac_equal() behavior: every-byte-position mismatch must be rejected, and an identical
// tag must be accepted — mirrors the tamper-test style already used for the secure channel
// in tests/caregiver_link_test.cpp. This proves CORRECTNESS, not constant-timeness: the
// no-early-exit property (mac_equal ORs every byte rather than returning on the first
// mismatch, per cmac.cpp) is a code-reading claim, not something a functional test can
// measure — stated here rather than overclaimed.
void test_mac_equal_behavior() {
    Mac a{};
    hex_to("aaf3d8f1de5640c232f5b169b9c911e6", a.data(), 16);
    Mac b = a;
    CHECK(mac_equal(a, b));

    for (std::size_t i = 0; i < a.size(); ++i) {
        Mac tampered = a;
        tampered[i] ^= 0x01;  // flip one bit at this byte position
        CHECK(!mac_equal(a, tampered));
    }

    // Two independently-empty-message tags for different keys must differ (sanity: this
    // isn't just returning true unconditionally for equal-length inputs).
    Key256 key1{};
    hex_to("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", key1.data(), 32);
    Key256 key2{};
    hex_to("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key2.data(), 32);
    CHECK(!mac_equal(cmac(key1, nullptr, 0), cmac(key2, nullptr, 0)));
}

}  // namespace

int main() {
    test_cmac_tag_kat();
    test_mac_equal_behavior();
    return echo::test::report("crypto");
}
