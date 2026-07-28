# ECHO OS — local model files

Every model ECHO OS runs is **local and offline**. This directory is where you
place the downloaded weights; nothing here is committed to git (the files are
large and some carry their own licenses). The engines resolve these paths at
runtime — override any of them with the matching environment variable (see
[`common/include/echo/config.hpp`](../common/include/echo/config.hpp)).

Put files here with these exact names (or set the env var):

| File (default name)                | Engine / flag                    | Env override            |
|------------------------------------|----------------------------------|-------------------------|
| `hey-echo.ppn`                     | Porcupine wake word              | `ECHO_PORCUPINE_KEYWORD`|
| `porcupine_params.pv`             | Porcupine engine params          | `ECHO_PORCUPINE_PARAMS` |
| `porcupine_access_key.txt`         | Porcupine AccessKey (1 line)     | `PV_ACCESS_KEY`         |
| `ggml-base.en-q5_1.bin`            | whisper.cpp ASR                  | `ECHO_WHISPER_MODEL`    |
| `llm.gguf`                         | llama.cpp reasoning              | `ECHO_LLAMA_MODEL`      |
| `en_US-amy-medium.onnx` (+ `.json`)| Piper TTS voice                  | `ECHO_PIPER_VOICE`      |
| `face_detection_yunet.onnx`        | OpenCV YuNet face detector       | `ECHO_FACE_DETECT_MODEL`|
| `face_recognition_sface.onnx`      | OpenCV SFace face embeddings     | `ECHO_FACE_RECOG_MODEL` |
| `mobilenet_v2.onnx` (optional)     | OpenCV object classifier         | `ECHO_OBJECT_MODEL`     |
| `imagenet_classes.txt` (optional)  | object class labels              | `ECHO_OBJECT_LABELS`    |
| `faces/<name>.jpg`                 | enrolled face test set           | `ECHO_FACES_DIR`        |
| `earcons/<name>.wav` (optional)    | short audio cues (e.g. `wake`)   | —                       |

See the **"Phase 3: Real Local AI"** section of the top-level
[`README.md`](../README.md) for exact download commands and the swap-to-hardware
notes.

## Enrolled faces (the demo test set)

Drop a few clear, front-facing photos in `faces/`, one person per file, named
after the person: `faces/grace.jpg`, `faces/sam.png`, … The filename stem
becomes the spoken identity ("That's Grace."). One good photo per person is
enough; more is fine.
