// ECHO OS — runtime configuration for the local-AI model files.
//
// Phase 3 wires real, fully local models (wake-word, ASR, LLM, TTS, face/object)
// into the pipeline. Where those models live on disk is deployment policy, not
// something baked into any module — so every real adapter reads its path from
// here instead of hard-coding one.
//
// Resolution order for each path:
//   1. the engine-specific environment variable (e.g. ECHO_WHISPER_MODEL), if set;
//   2. otherwise <models-dir>/<default-filename>, where <models-dir> is
//      $ECHO_MODELS_DIR or "models" (relative to the working directory).
//
// Nothing here reaches the network — these are on-device file paths only
// (principle #4). Missing files are surfaced by the engines as NotReady, which
// makes the pipeline fall back to its stub/safe behavior rather than crash.
#pragma once

#include <string>

namespace echo::config {

// The directory that holds all downloaded model files. $ECHO_MODELS_DIR or
// "models". Callers rarely need this directly — prefer the typed helpers below.
std::string models_dir();

// Resolve a model path: returns $<env_var> if set and non-empty, else
// models_dir()/<default_filename>.
std::string resolve(const char* env_var, const char* default_filename);

// --- Typed helpers, one per Phase-3 engine ---------------------------------
// Each corresponds to a file the README's "Phase 3" section tells the user to
// download and place under models/.

// Porcupine wake-word: the custom "Hey ECHO" keyword file and the engine params.
std::string porcupine_keyword();  // ECHO_PORCUPINE_KEYWORD  | hey-echo.ppn
std::string porcupine_params();   // ECHO_PORCUPINE_PARAMS   | porcupine_params.pv
// Porcupine needs a one-time free AccessKey (offline at runtime). Read from
// $PV_ACCESS_KEY or models/porcupine_access_key.txt.
std::string porcupine_access_key();

// openWakeWord (Phase 13): the account-free wake-word backend. Its three-stage
// ONNX pipeline is three separate model files — a shared audio-feature front end
// (melspectrogram + Google speech-embedding) plus the per-keyword classifier.
// All are fetched free (Apache-2.0), unlike Porcupine's account-gated .ppn.
std::string openwakeword_melspec();    // ECHO_OWW_MELSPEC    | melspectrogram.onnx
std::string openwakeword_embedding();  // ECHO_OWW_EMBEDDING  | embedding_model.onnx
std::string openwakeword_model();      // ECHO_OWW_MODEL      | hey_jarvis_v0.1.onnx

// whisper.cpp quantized model (e.g. ggml-base.en-q5_1.bin).
std::string whisper_model();      // ECHO_WHISPER_MODEL      | ggml-base.en-q5_1.bin

// llama.cpp instruct GGUF (e.g. Phi-3.5-mini or Llama-3.2-3B, Q4_K_M).
std::string llama_model();        // ECHO_LLAMA_MODEL        | llm.gguf

// Piper voice + its config json (e.g. en_US-amy-medium.onnx / .onnx.json).
std::string piper_binary();       // ECHO_PIPER_BIN          | piper (on PATH)
std::string piper_voice();        // ECHO_PIPER_VOICE        | en_US-amy-medium.onnx

// OpenCV face detector + recognizer, lightweight object classifier, and the
// directory of enrolled reference faces used for the demo test set.
std::string face_detect_model();  // ECHO_FACE_DETECT_MODEL  | face_detection_yunet.onnx
std::string face_recog_model();   // ECHO_FACE_RECOG_MODEL   | face_recognition_sface.onnx
std::string object_model();       // ECHO_OBJECT_MODEL       | mobilenet_v2.onnx
std::string object_labels();      // ECHO_OBJECT_LABELS      | imagenet_classes.txt
std::string faces_dir();          // ECHO_FACES_DIR          | <models-dir>/faces

// --- Phase 15: the on-device memory store -----------------------------------
// The SQLite file that persists people/reminders/events. Written data (not a
// downloaded model), so it lives outside models/. Never leaves the device.
std::string memory_db();          // ECHO_MEMORY_DB          | echo_memory.db

}  // namespace echo::config
