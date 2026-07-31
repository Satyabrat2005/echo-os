#include "llm.hpp"

#include "echo/cognitive/route_tag.hpp"
#include "echo/config.hpp"
#include "echo/log.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#if defined(ECHO_WITH_LLAMA)
#include "llama.h"
#endif

namespace echo::cognitive {
namespace {

// ECHO's persona + its intent-routing job, kept short so it fits a small model's
// budget. The [route:<app>] convention lets the runtime hand app-shaped requests
// to the apps layer; anything else is answered conversationally.
constexpr const char* kSystemPrompt =
    "You are ECHO, a calm memory assistant worn as smart glasses by someone who "
    "may have memory loss. Reply in one short, warm, concrete sentence. Never "
    "guess at facts you don't know. If the request is to control a device app, "
    "begin your reply with a tag on its own: [route:media] for music/audio, "
    "[route:mail] for messages, [route:telephony] for calls, [route:search] for "
    "web lookups, [route:memory] for who a person is or the wearer's own reminders "
    "or medication, [route:camera] to take a photo, [route:gallery] for photos, "
    "[route:video] to watch video, [route:browser] to open a page. Otherwise omit "
    "the tag and just answer.";

#if defined(ECHO_WITH_LLAMA)

// KV-cache clearing moved across llama.cpp API revisions, and which symbol exists
// depends entirely on the commit you built against — this is the one call most
// likely to break the ECHO_WITH_LLAMA build. We default to the current memory
// API; if your installed llama.cpp predates the mid-2025 memory refactor, build
// with -DECHO_LLAMA_KV_CLEAR=self (or =cache for pre-2025). See README "Phase 4".
inline void echo_llama_kv_clear(llama_context* ctx) {
#if defined(ECHO_LLAMA_KV_CLEAR_SELF)
    llama_kv_self_clear(ctx);          // mid-2025 API
#elif defined(ECHO_LLAMA_KV_CLEAR_CACHE)
    llama_kv_cache_clear(ctx);         // pre-2025 API
#else
    llama_memory_clear(llama_get_memory(ctx), /*data=*/true);  // current API
#endif
}

// Real reasoning via llama.cpp. The generation loop follows llama.cpp's `simple`
// example against the current API (llama_model_load_from_file / llama_init_from_
// model / sampler chain). If your llama.cpp checkout is older/newer, these few
// calls are the only ones that may need adjusting.
class LlamaLlm final : public ILlm {
public:
    Status initialize() override {
        ggml_backend_load_all();
        llama_backend_init();

        llama_model_params mparams = llama_model_default_params();
        mparams.n_gpu_layers = 99;  // offload to GPU if the build supports it
        model_ = llama_model_load_from_file(config::llama_model().c_str(), mparams);
        if (!model_) {
            log_error("cognitive", "llama model failed to load; LLM disabled");
            return Status::NotReady;
        }
        vocab_ = llama_model_get_vocab(model_);

        llama_context_params cparams = llama_context_default_params();
        cparams.n_ctx   = 2048;
        cparams.n_batch = 512;
        ctx_ = llama_init_from_model(model_, cparams);
        if (!ctx_) {
            log_error("cognitive", "llama context init failed; LLM disabled");
            return Status::NotReady;
        }

        // Greedy sampling: deterministic and lowest-latency, which suits a device
        // that must not surprise a vulnerable user with random phrasing.
        llama_sampler_chain_params sp = llama_sampler_chain_default_params();
        sampler_ = llama_sampler_chain_init(sp);
        llama_sampler_chain_add(sampler_, llama_sampler_init_greedy());

        log_info("cognitive", "LLM ready (llama.cpp)");
        return Status::Ok;
    }

    LlmReply generate(const std::string& user_text) override {
        if (!ctx_ || !model_) return {};

        const std::string prompt = build_prompt(user_text);
        std::vector<llama_token> tokens = tokenize(prompt, /*add_special=*/true);
        if (tokens.empty()) return {};

        std::string out;
        llama_batch batch = llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()));
        const int kMaxNewTokens = 96;  // one short sentence; keeps us near budget
        for (int i = 0; i < kMaxNewTokens; ++i) {
            if (llama_decode(ctx_, batch) != 0) break;
            llama_token id = llama_sampler_sample(sampler_, ctx_, -1);
            if (llama_vocab_is_eog(vocab_, id)) break;
            out += token_to_piece(id);
            cur_token_ = id;
            batch = llama_batch_get_one(&cur_token_, 1);
        }

        // Reset the KV cache so each turn is independent (no cross-turn leakage of
        // a prior person's context — privacy, and predictable behavior).
        echo_llama_kv_clear(ctx_);

        LlmReply reply;
        reply.text   = trim(out);
        reply.intent = parse_route_tag(reply.text);
        return reply;
    }

    void shutdown() override {
        if (sampler_) { llama_sampler_free(sampler_); sampler_ = nullptr; }
        if (ctx_)     { llama_free(ctx_);             ctx_ = nullptr; }
        if (model_)   { llama_model_free(model_);     model_ = nullptr; }
        llama_backend_free();
    }

private:
    std::string build_prompt(const std::string& user_text) {
        // Prefer the model's own chat template so instruct models behave.
        llama_chat_message msgs[2] = {
            {"system", kSystemPrompt},
            {"user",   user_text.c_str()},
        };
        const char* tmpl = llama_model_chat_template(model_, nullptr);
        std::vector<char> buf(4096);
        int32_t n = llama_chat_apply_template(tmpl, msgs, 2, /*add_ass=*/true,
                                              buf.data(), static_cast<int32_t>(buf.size()));
        if (n > 0 && n <= static_cast<int32_t>(buf.size()))
            return std::string(buf.data(), n);
        // Fallback: a generic instruct framing if the model ships no template.
        return std::string(kSystemPrompt) + "\n\nUser: " + user_text + "\nECHO:";
    }

    std::vector<llama_token> tokenize(const std::string& text, bool add_special) {
        int32_t n = -llama_tokenize(vocab_, text.c_str(), static_cast<int32_t>(text.size()),
                                    nullptr, 0, add_special, /*parse_special=*/true);
        std::vector<llama_token> toks(n);
        if (llama_tokenize(vocab_, text.c_str(), static_cast<int32_t>(text.size()),
                           toks.data(), n, add_special, true) < 0)
            return {};
        return toks;
    }

    std::string token_to_piece(llama_token id) {
        char buf[256];
        int32_t n = llama_token_to_piece(vocab_, id, buf, sizeof(buf), 0, /*special=*/false);
        return n > 0 ? std::string(buf, n) : std::string{};
    }

    static std::string trim(std::string s) {
        auto ns = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), ns));
        s.erase(std::find_if(s.rbegin(), s.rend(), ns).base(), s.end());
        return s;
    }

    llama_model*         model_   = nullptr;
    const llama_vocab*   vocab_   = nullptr;
    llama_context*       ctx_     = nullptr;
    llama_sampler*       sampler_ = nullptr;
    llama_token          cur_token_ = 0;
};

#endif  // ECHO_WITH_LLAMA

// Stub: no LLM. Returns empty text so the cognitive core stays in safe mode
// (never fabricates an answer) — exactly the pre-Phase-3 behavior.
class StubLlm final : public ILlm {
public:
    Status   initialize() override {
        log_info("cognitive", "LLM initialized (stub: safe-mode only)");
        return Status::Ok;
    }
    LlmReply generate(const std::string&) override { return {}; }
    void     shutdown() override {}
};

}  // namespace

std::unique_ptr<ILlm> make_llm() {
#if defined(ECHO_WITH_LLAMA)
    return std::make_unique<LlamaLlm>();
#else
    return std::make_unique<StubLlm>();
#endif
}

}  // namespace echo::cognitive
