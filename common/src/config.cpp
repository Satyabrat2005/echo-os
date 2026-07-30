#include "echo/config.hpp"

#include <cstdlib>

namespace echo::config {
namespace {

// Read an environment variable, returning "" when unset or empty.
std::string env(const char* name) {
    if (!name) return {};
#if defined(_MSC_VER)
    // getenv is deprecated under MSVC's /W4; use the safe variant.
    char*  buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, name) != 0 || buf == nullptr) return {};
    std::string value(buf);
    std::free(buf);
    return value;
#else
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string{};
#endif
}

std::string join(const std::string& dir, const char* file) {
    if (dir.empty()) return file;
    char last = dir.back();
    if (last == '/' || last == '\\') return dir + file;
    return dir + "/" + file;
}

}  // namespace

std::string models_dir() {
    std::string d = env("ECHO_MODELS_DIR");
    return d.empty() ? std::string("models") : d;
}

std::string resolve(const char* env_var, const char* default_filename) {
    std::string e = env(env_var);
    if (!e.empty()) return e;
    return join(models_dir(), default_filename);
}

std::string porcupine_keyword()   { return resolve("ECHO_PORCUPINE_KEYWORD", "hey-echo.ppn"); }
std::string porcupine_params()    { return resolve("ECHO_PORCUPINE_PARAMS",  "porcupine_params.pv"); }
std::string porcupine_access_key() {
    std::string k = env("PV_ACCESS_KEY");
    if (!k.empty()) return k;
    // Fall back to a file so the key never has to live in shell history.
    return join(models_dir(), "porcupine_access_key.txt");
}

std::string openwakeword_melspec()   { return resolve("ECHO_OWW_MELSPEC",   "melspectrogram.onnx"); }
std::string openwakeword_embedding() { return resolve("ECHO_OWW_EMBEDDING", "embedding_model.onnx"); }
std::string openwakeword_model()     { return resolve("ECHO_OWW_MODEL",     "hey_jarvis_v0.1.onnx"); }

std::string whisper_model()       { return resolve("ECHO_WHISPER_MODEL", "ggml-base.en-q5_1.bin"); }

std::string llama_model()         { return resolve("ECHO_LLAMA_MODEL", "llm.gguf"); }

std::string piper_binary() {
    std::string b = env("ECHO_PIPER_BIN");
    return b.empty() ? std::string("piper") : b;  // assume on PATH by default
}
std::string piper_voice()         { return resolve("ECHO_PIPER_VOICE", "en_US-amy-medium.onnx"); }

std::string face_detect_model()   { return resolve("ECHO_FACE_DETECT_MODEL", "face_detection_yunet.onnx"); }
std::string face_recog_model()    { return resolve("ECHO_FACE_RECOG_MODEL",  "face_recognition_sface.onnx"); }
std::string object_model()        { return resolve("ECHO_OBJECT_MODEL",  "mobilenet_v2.onnx"); }
std::string object_labels()       { return resolve("ECHO_OBJECT_LABELS", "imagenet_classes.txt"); }
std::string faces_dir()           { return resolve("ECHO_FACES_DIR", "faces"); }

}  // namespace echo::config
