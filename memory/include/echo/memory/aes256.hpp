// ECHO OS memory — AES-256 (alias).
//
// The implementation this header used to declare MOVED to echo/crypto/aes256.hpp
// (echo::common) in Phase 22. companion-sync needs the same block cipher to frame
// the caregiver transport, and linking echo::memory from companion-sync would have
// pulled SQLite and the on-device store into the module whose entire job is to be
// unable to see them. The primitive is the same Phase 16 code, KATs and all; only
// its home changed.
//
// Everything here keeps working: echo::memory::crypto::{Key256, Block,
// encrypt_block, ctr_xcrypt} still name exactly what they did before.
//
// This block cipher primitive is CONFIDENTIALITY ONLY — AES-CTR alone is malleable
// and carries no MAC, exactly the ADR-14 caveat. Phase 22 composed it with
// AES-256-CMAC (echo/crypto/cmac.hpp) for the caregiver transport's encrypt-then-MAC
// construction; Phase 24 reuses that same composition (echo/crypto/sealed_box.hpp)
// for the at-rest store too, closing the gap ADR-14 originally left open — see
// ADR-21. This header still only declares the bare block cipher; the authenticated
// composition lives in sealed_box.hpp, not here.
#pragma once

#include "echo/crypto/aes256.hpp"

namespace echo::memory::crypto {

using Key256 = ::echo::crypto::Key256;
using Block  = ::echo::crypto::Block;

using ::echo::crypto::ctr_xcrypt;
using ::echo::crypto::encrypt_block;

}  // namespace echo::memory::crypto
