// ECHO OS perception — face + object recognition (private component).
//
// Real path is OpenCV: YuNet for face detection, SFace for a lightweight face
// embedding matched against a small enrolled test set (the caregiver-anchored
// identities), and a MobileNet ONNX classifier for objects/scenes. Fallback is a
// stub returning nothing. Operates on raw BGR frames handed up from the camera
// source (no copy of the pixels is persisted — principle #4).
#pragma once

#include "echo/types.hpp"
#include "echo/result.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace echo::perception {

struct FaceHit {
    std::string identity;      // "" when detected but not matched to an enrollee
    float confidence = 0.0f;   // match confidence (or detection score if unknown)
    float x = 0, y = 0, w = 0, h = 0;  // normalized [0,1] bbox
};

struct ObjectHit {
    std::string label;
    float       confidence = 0.0f;
};

class IVision {
public:
    virtual ~IVision() = default;
    virtual Status initialize() = 0;
    // Detect + recognize faces in a BGR frame (width*height*3 bytes at `bgr`).
    virtual std::vector<FaceHit>   detect_faces(const std::uint8_t* bgr, int w, int h) = 0;
    // Top object/scene labels for the frame.
    virtual std::vector<ObjectHit> classify_objects(const std::uint8_t* bgr, int w, int h) = 0;
    virtual void shutdown() = 0;
};

std::unique_ptr<IVision> make_vision();

}  // namespace echo::perception
