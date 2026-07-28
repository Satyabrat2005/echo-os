// ECHO OS apps — IPC message format between an app process and the host.
//
// Apps run as separate OS processes (constraint #2). The host talks to each app
// over a line-oriented channel: one message per line, tab-separated key=value
// fields, values escaped so they never contain a raw tab or newline. It is
// intentionally boring and dependency-free (no JSON library) — the wire format
// is the contract, and it is trivially inspectable when debugging on a laptop.
//
// Two message kinds cross the boundary:
//   Cmd  host -> app : a VoiceCommand to handle.
//   Rsp  app -> host : the resulting AppResponse (speech + HUD frame + status).
//
// The same codec is used whether the transport is a real pipe (supervised child
// process) or an in-process queue (unit tests / single-binary laptop host), so
// wiring the real transport later needs zero changes to callers.
#pragma once

#include "echo/apps/framework/app.hpp"

#include <map>
#include <optional>
#include <string>

namespace echo::apps::ipc {

enum class MessageKind { Cmd, Rsp };

// A decoded message: a kind plus a flat string map. Helpers below convert to and
// from the typed VoiceCommand / AppResponse so callers rarely touch fields raw.
struct Message {
    MessageKind                         kind = MessageKind::Cmd;
    std::map<std::string, std::string>  fields;
};

// Line codec.
std::string            encode(const Message& msg);
std::optional<Message> decode(const std::string& line);

// Typed conversions.
Message      to_message(const VoiceCommand& command);
VoiceCommand to_command(const Message& msg);

Message     to_message(const AppResponse& response);
AppResponse to_response(const Message& msg);

}  // namespace echo::apps::ipc
