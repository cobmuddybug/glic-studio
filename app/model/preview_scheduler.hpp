#pragma once

// Decides when to ask the render worker for what (design doc §4.3):
//  - every change gets a fast proxy render (unless the image is small enough
//    to render at full size directly);
//  - while proxies are slow, changes are coalesced: a new proxy is submitted
//    only when the previous one has finished, so dragging a slider keeps
//    updating at the proxy render rate instead of cancelling forever;
//  - once input has been quiet for `settleMs`, a full-resolution render runs;
//  - a full render in flight is abandoned (cancelled by the next submit) as
//    soon as anything changes.
//
// Pure logic, no threads or clocks: the caller passes `now` in milliseconds.

#include <cstdint>
#include <optional>

namespace studio {

class PreviewScheduler {
public:
    struct Job {
        uint64_t ticket = 0;    // pass as RenderRequest::generation
        uint64_t version = 0;   // settings version this job renders
        bool proxy = false;
    };

    double settleMs = 150;

    // Something affecting the output changed. `fitsProxy`: the source is no
    // larger than the proxy, so render it at full size straight away.
    void changed(double now, bool fitsProxy);

    // What to submit now, if anything. Call every frame / wake-up.
    std::optional<Job> next(double now);

    // A job finished (or was cancelled). Returns true if its result should be
    // shown: it is not older than what is on screen, and a proxy never
    // replaces a full render of the same version.
    bool finished(uint64_t ticket, bool cancelled);

    // Milliseconds until next() may have work (for idle waits); -1 = none pending.
    double wakeIn(double now) const;

    uint64_t version() const { return version_; }
    bool fullIsCurrent() const { return shownFull_ && shownVersion_ == version_; }
    bool idle() const { return !inFlight_ && !wantProxy_ && !wantFull_; }

private:
    uint64_t version_ = 0;
    uint64_t nextTicket_ = 1;
    bool wantProxy_ = false;
    bool wantFull_ = false;
    double settleAt_ = 0;

    std::optional<Job> inFlight_;
    uint64_t shownVersion_ = 0;
    bool shownFull_ = false;
    bool anyShown_ = false;
};

} // namespace studio
