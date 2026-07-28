// ECHO OS demo — end-to-end latency logging.
//
// Records REAL measured per-turn timings (perception+ASR, cognitive/LLM, voice
// output, and the total) to a CSV and prints a summary on exit, so the README's
// latency table is filled from actual runs on your laptop rather than a made-up
// number. The 120 ms target lives in common/latency.hpp; this just measures how
// close a real local model gets on real hardware and reports it honestly.
#pragma once

#include "echo/latency.hpp"
#include "echo/types.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace echo {

// Milliseconds elapsed since a TimePoint, as a double.
inline double ms_since(TimePoint t0) {
    return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(now() - t0).count();
}

class LatencyLog {
public:
    explicit LatencyLog(std::string path) : path_(std::move(path)) {
        out_.open(path_, std::ios::out | std::ios::trunc);
        if (out_) out_ << "turn,kind,perception_ms,cognitive_ms,voice_ms,total_ms,budget_ms\n";
    }

    void record(const std::string& kind, double perception_ms, double cognitive_ms,
                double voice_ms, double total_ms) {
        ++n_;
        totals_.push_back(total_ms);
        if (out_) {
            out_ << n_ << ',' << kind << ',' << perception_ms << ',' << cognitive_ms << ','
                 << voice_ms << ',' << total_ms << ',' << kEndToEndBudgetMs << '\n';
            out_.flush();
        }
        std::printf("  [latency] turn %d (%s): perception=%.1f  cognitive=%.1f  voice=%.1f  "
                    "TOTAL=%.1f ms  (target %.0f ms)\n",
                    n_, kind.c_str(), perception_ms, cognitive_ms, voice_ms,
                    total_ms, kEndToEndBudgetMs);
    }

    void summarize() const {
        if (totals_.empty()) return;
        double sum = 0, mn = totals_[0], mx = totals_[0];
        for (double v : totals_) { sum += v; mn = v < mn ? v : mn; mx = v > mx ? v : mx; }
        std::printf("\n  [latency] %zu turn(s): min=%.1f  avg=%.1f  max=%.1f ms  "
                    "(target %.0f ms)  -> written to %s\n",
                    totals_.size(), mn, sum / totals_.size(), mx, kEndToEndBudgetMs, path_.c_str());
    }

private:
    std::string        path_;
    std::ofstream      out_;
    int                n_ = 0;
    std::vector<double> totals_;
};

}  // namespace echo
