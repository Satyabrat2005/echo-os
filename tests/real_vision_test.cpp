// ECHO OS — REAL vision engine verification (Phase 11).
//
// perception_test.cpp exercises the perception engine in STUB mode, where vision
// returns no faces. This target is built ONLY with -DECHO_WITH_OPENCV=ON and
// drives the real OpenCV adapter (perception/src/vision.cpp -> OpenCvVision):
// YuNet face detection + SFace recognition against real model weights and real
// face images.
//
// What this proves: the real YuNet/SFace path compiles, links, loads the ONNX
// models, and produces correct detect/no-detect and match/no-match outcomes on
// real frames — no account, small models, CI-sized.
//
// What this does NOT prove (kept honest in STATE.md): field robustness. These are
// clean, well-lit, frontal public-domain portraits, not a moving subject in poor
// light through the glasses' camera. The self-match case (enroll an image, then
// recognize the same identity) is a floor — "the recognizer distinguishes this
// person from a different person" — not a claim about pose/lighting invariance.
//
// Face images are public-domain CV test photos fetched in CI (not committed):
//   - enrolled : Grace Hopper (US Navy, public domain)      -> $ECHO_FACE_ENROLLED_IMG
//   - unknown  : "astronaut" Eileen Collins (NASA, PD)      -> $ECHO_FACE_UNKNOWN_IMG
// The no-face frame is the committed synthetic fixture (no real person needed).

#if !defined(ECHO_WITH_OPENCV)
#error "real_vision_test requires ECHO_WITH_OPENCV=ON — it verifies the real YuNet/SFace engine"
#endif

#include "vision.hpp"  // perception/src (private): the real make_vision()/OpenCvVision

#include "echo/config.hpp"
#include "echo/result.hpp"

#include "check.hpp"
#include "fixture_io.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace echo;
using echo::perception::FaceHit;
using echo::perception::IVision;
using echo::perception::make_vision;

namespace {

std::unique_ptr<IVision> g_vision;

const char* env_or_null(const char* name) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : nullptr;
}

// Detect faces in an image on disk. Returns false (and asserts) if the image
// can't be decoded; fills `out` with the adapter's hits otherwise.
bool detect_in_image(const std::string& path, std::vector<FaceHit>& out) {
    cv::Mat bgr = cv::imread(path, cv::IMREAD_COLOR);
    if (bgr.empty()) {
        std::fprintf(stderr, "  could not read image: %s\n", path.c_str());
        return false;
    }
    if (!bgr.isContinuous()) bgr = bgr.clone();  // adapter reads a flat BGR buffer
    out = g_vision->detect_faces(bgr.data, bgr.cols, bgr.rows);
    return true;
}

// --- Enrolled face: detected AND matched to an enrolled identity -------------
void test_enrolled_face_detected_and_matched() {
    const char* img = env_or_null("ECHO_FACE_ENROLLED_IMG");
    if (!img || !std::filesystem::exists(img)) {
        std::printf("  [skip] enrolled-face check: set $ECHO_FACE_ENROLLED_IMG "
                    "(CI fetches a public-domain portrait)\n");
        return;
    }
    std::vector<FaceHit> hits;
    CHECK(detect_in_image(img, hits));
    CHECK(!hits.empty());  // YuNet must find the face

    // At least one detected face must match an enrolled identity (non-empty
    // identity => cosine cleared the adapter's SFace threshold).
    bool matched = false;
    for (const FaceHit& h : hits) {
        if (!h.identity.empty()) {
            matched = true;
            std::printf("  enrolled -> matched \"%s\" (conf=%.3f)\n",
                        h.identity.c_str(), static_cast<double>(h.confidence));
            CHECK(h.confidence > 0.0f);
        }
    }
    CHECK(matched);
}

// --- Unknown face: detected but NOT matched to any enrollee ------------------
void test_unknown_face_detected_but_unmatched() {
    const char* img = env_or_null("ECHO_FACE_UNKNOWN_IMG");
    if (!img || !std::filesystem::exists(img)) {
        std::printf("  [skip] unknown-face check: set $ECHO_FACE_UNKNOWN_IMG "
                    "(CI fetches a second public-domain portrait)\n");
        return;
    }
    std::vector<FaceHit> hits;
    CHECK(detect_in_image(img, hits));
    CHECK(!hits.empty());  // it IS a face, so YuNet should detect it

    // A different person must NOT match the enrolled identity: every hit's
    // identity is empty (SFace cosine stayed below the match threshold).
    for (const FaceHit& h : hits) {
        if (!h.identity.empty()) {
            std::fprintf(stderr, "  unknown face wrongly matched \"%s\" (conf=%.3f)\n",
                         h.identity.c_str(), static_cast<double>(h.confidence));
        }
        CHECK(h.identity.empty());
    }
    std::printf("  unknown -> detected %zu face(s), none matched (correct)\n", hits.size());
}

// --- No face: a non-face frame yields no detections --------------------------
// Uses the committed synthetic no_face fixture (OpenCV decodes P6 PPM), so no
// real image or download is needed for the negative case.
void test_no_face_frame_detects_nothing() {
    const std::string path = echo::test::fixture_path("no_face.ppm");
    std::vector<FaceHit> hits;
    CHECK(detect_in_image(path, hits));
    CHECK(hits.empty());  // no face-like structure -> no detections
    std::printf("  no-face -> %zu detection(s) (expected 0)\n", hits.size());
}

}  // namespace

int main() {
    g_vision = make_vision();

    // Both models must be on disk for a real run. Built with the flag ON but
    // models not downloaded (a local build without setup) -> skip loudly; CI
    // always provides them.
    const std::string det = config::face_detect_model();
    const std::string rec = config::face_recog_model();
    if (!std::filesystem::exists(det) || !std::filesystem::exists(rec)) {
        std::printf("[real-vision] SKIP: face models not found (detect=\"%s\", recog=\"%s\"). "
                    "Download YuNet+SFace or set $ECHO_FACE_DETECT_MODEL/$ECHO_FACE_RECOG_MODEL "
                    "(see README).\n", det.c_str(), rec.c_str());
        return echo::test::report("real-vision");
    }

    // Models present -> the adapter must come up ready (detector + recognizer
    // loaded). Enrollment happens here from $ECHO_FACES_DIR.
    CHECK(g_vision->initialize() == Status::Ok);

    test_enrolled_face_detected_and_matched();
    test_unknown_face_detected_but_unmatched();
    test_no_face_frame_detects_nothing();

    g_vision->shutdown();
    return echo::test::report("real-vision");
}
