#include "vision.hpp"

#include "echo/config.hpp"
#include "echo/log.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#if defined(ECHO_WITH_OPENCV)
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/objdetect/face.hpp>
#include <opencv2/dnn.hpp>
#include <filesystem>
#include <fstream>
#endif

namespace echo::perception {
namespace {

#if defined(ECHO_WITH_OPENCV)

namespace fs = std::filesystem;

// SFace cosine-similarity threshold above which two faces are "the same person".
// 0.363 is the value OpenCV documents for SFace; we keep a touch of margin.
constexpr double kFaceMatchCosine = 0.38;

class OpenCvVision final : public IVision {
public:
    Status initialize() override {
        // --- Face detector (YuNet) + recognizer (SFace) ------------------------
        try {
            detector_ = cv::FaceDetectorYN::create(
                config::face_detect_model(), "", cv::Size(320, 320), 0.7f, 0.3f, 5000);
            recognizer_ = cv::FaceRecognizerSF::create(config::face_recog_model(), "");
        } catch (const cv::Exception& e) {
            log_error("perception", "OpenCV face models failed to load; faces disabled");
            detector_.reset();
            recognizer_.reset();
        }
        if (detector_ && recognizer_) enroll_reference_faces();

        // --- Object classifier (optional) --------------------------------------
        try {
            object_net_ = cv::dnn::readNetFromONNX(config::object_model());
            load_object_labels();
        } catch (const cv::Exception&) {
            log_warn("perception", "object classifier not loaded; objects disabled");
        }

        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "vision ready (OpenCV; %zu enrolled face(s), objects=%s)",
                      enrolled_.size(), object_net_.empty() ? "off" : "on");
        log_info("perception", buf);
        return (detector_ && recognizer_) ? Status::Ok : Status::NotReady;
    }

    std::vector<FaceHit> detect_faces(const std::uint8_t* bgr, int w, int h) override {
        std::vector<FaceHit> out;
        if (!detector_ || !recognizer_ || !bgr || w <= 0 || h <= 0) return out;

        cv::Mat frame(h, w, CV_8UC3, const_cast<std::uint8_t*>(bgr));
        detector_->setInputSize(cv::Size(w, h));
        cv::Mat faces;
        detector_->detect(frame, faces);

        for (int i = 0; i < faces.rows; ++i) {
            cv::Mat aligned, feature;
            recognizer_->alignCrop(frame, faces.row(i), aligned);
            recognizer_->feature(aligned, feature);
            feature = feature.clone();

            FaceHit hit;
            const float* f = faces.ptr<float>(i);
            hit.x = f[0] / w; hit.y = f[1] / h;
            hit.w = f[2] / w; hit.h = f[3] / h;
            hit.confidence = f[14];  // YuNet detection score

            // Match against enrolled identities by cosine similarity.
            double best = -1.0;
            for (const auto& ref : enrolled_) {
                double cos = recognizer_->match(feature, ref.feature,
                                                cv::FaceRecognizerSF::FR_COSINE);
                if (cos > best) { best = cos; if (cos > kFaceMatchCosine) hit.identity = ref.name; }
            }
            if (!hit.identity.empty()) hit.confidence = static_cast<float>(best);
            out.push_back(std::move(hit));
        }
        return out;
    }

    std::vector<ObjectHit> classify_objects(const std::uint8_t* bgr, int w, int h) override {
        std::vector<ObjectHit> out;
        if (object_net_.empty() || !bgr || w <= 0 || h <= 0) return out;

        cv::Mat frame(h, w, CV_8UC3, const_cast<std::uint8_t*>(bgr));
        cv::Mat blob = cv::dnn::blobFromImage(frame, 1.0 / 255.0, cv::Size(224, 224),
                                              cv::Scalar(0.485, 0.456, 0.406) * 255.0,
                                              /*swapRB=*/true, /*crop=*/false);
        object_net_.setInput(blob);
        cv::Mat scores = object_net_.forward();
        scores = scores.reshape(1, 1);

        // softmax-max as confidence, argmax as label
        cv::Point maxLoc; double maxVal = 0;
        cv::minMaxLoc(scores, nullptr, &maxVal, nullptr, &maxLoc);
        cv::Mat exp; cv::exp(scores - maxVal, exp);
        float denom = static_cast<float>(cv::sum(exp)[0]);
        float conf  = denom > 0 ? 1.0f / denom : 0.0f;  // exp(0)/sum

        const int idx = maxLoc.x;
        std::string label = (idx >= 0 && idx < static_cast<int>(labels_.size()))
                                ? labels_[idx] : ("class_" + std::to_string(idx));
        out.push_back(ObjectHit{label, conf});
        return out;
    }

    void shutdown() override {
        detector_.reset();
        recognizer_.reset();
        enrolled_.clear();
    }

private:
    struct Enrolled { std::string name; cv::Mat feature; };

    // Build the small test set: every image in faces_dir/ becomes an identity
    // named after its filename stem ("grace.jpg" -> "Grace").
    void enroll_reference_faces() {
        const std::string dir = config::faces_dir();
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) {
            log_warn("perception", "no faces/ directory; recognition will report 'unknown'");
            return;
        }
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            const std::string path = entry.path().string();
            cv::Mat img = cv::imread(path);
            if (img.empty()) continue;
            detector_->setInputSize(img.size());
            cv::Mat faces;
            detector_->detect(img, faces);
            if (faces.rows < 1) continue;
            cv::Mat aligned, feature;
            recognizer_->alignCrop(img, faces.row(0), aligned);
            recognizer_->feature(aligned, feature);
            std::string name = entry.path().stem().string();
            if (!name.empty()) name[0] = static_cast<char>(std::toupper(name[0]));
            enrolled_.push_back({name, feature.clone()});
        }
    }

    void load_object_labels() {
        std::ifstream f(config::object_labels());
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            labels_.push_back(line);
        }
    }

    cv::Ptr<cv::FaceDetectorYN>   detector_;
    cv::Ptr<cv::FaceRecognizerSF> recognizer_;
    cv::dnn::Net                  object_net_;
    std::vector<Enrolled>         enrolled_;
    std::vector<std::string>      labels_;
};

#endif  // ECHO_WITH_OPENCV

class StubVision final : public IVision {
public:
    Status initialize() override {
        log_info("perception", "vision initialized (stub: no faces/objects)");
        return Status::Ok;
    }
    std::vector<FaceHit>   detect_faces(const std::uint8_t*, int, int) override { return {}; }
    std::vector<ObjectHit> classify_objects(const std::uint8_t*, int, int) override { return {}; }
    void shutdown() override {}
};

}  // namespace

std::unique_ptr<IVision> make_vision() {
#if defined(ECHO_WITH_OPENCV)
    return std::make_unique<OpenCvVision>();
#else
    return std::make_unique<StubVision>();
#endif
}

}  // namespace echo::perception
