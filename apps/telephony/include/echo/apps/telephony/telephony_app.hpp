// ECHO OS apps — telephony (phone bridge over Bluetooth).
//
// The glasses have no SIM. "Phone" is a Bluetooth bridge to the wearer's paired
// smartphone — like AirPods or a smartwatch — not a cellular stack (constraint
// #3). Calls are placed and answered by voice; the HUD shows only a phone glyph
// and the call state.
//
// Mock mode simulates the call state machine (idle -> ringing -> connected ->
// ended); real mode drives the phone over the Bluetooth Hands-Free Profile behind
// this identical interface.
#pragma once

#include "echo/apps/framework/app.hpp"
#include <memory>

namespace echo::apps::telephony {
std::unique_ptr<IApp> make_telephony_app(bool mock = true);
}  // namespace echo::apps::telephony
