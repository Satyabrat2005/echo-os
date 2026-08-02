// ECHO OS cognitive-core — the dependency-injection seam (private component).
//
// WHY THIS EXISTS (Phase 21). The public make_cognitive_core() builds its own LLM
// via make_llm(), which meant the safe-mode gate could only ever be exercised with
// whatever engine the build happened to compile in: a real llama.cpp model, or the
// stub that returns empty text. There was no way to hand the core an engine that
// answers *fluently and wrongly* — which is precisely the case the answer-side gate
// exists to catch. That is why the ungated-answer hole survived twenty phases: not
// because nobody could see it, but because nobody could write a test for it.
//
// This header is deliberately NOT in the public include directory. ILlm and LlmReply
// are private to cognitive-core (llm.hpp says so), and Phase 21 does not change that:
// the public interface is still make_cognitive_core(config, memory), unchanged and
// source-compatible for every existing caller. Tests reach in the same way the real-
// engine tests already do — by adding cognitive-core/src to their include path (see
// tests/CMakeLists.txt, which has done this for real_asr/real_vision/real_llm since
// Phase 11).
#pragma once

#include "echo/cognitive/cognitive_core.hpp"
#include "echo/memory/memory_engine.hpp"

#include "llm.hpp"

#include <memory>

namespace echo::cognitive {

// Build a cognitive core around a caller-supplied LLM. Identical in every respect
// to make_cognitive_core() except that the engine is injected rather than built, so
// a test can script exactly what the model "says" and at what confidence.
//
// `llm` must not be null. `memory` is non-owning and may be null (same contract as
// the public factory).
std::unique_ptr<ICognitiveCore> make_cognitive_core_with_llm(SafeModeConfig config,
                                                             memory::IMemoryEngine* memory,
                                                             std::unique_ptr<ILlm> llm);

}  // namespace echo::cognitive
