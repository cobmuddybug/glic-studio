#include "preview_scheduler.hpp"

#include <algorithm>

namespace studio {

void PreviewScheduler::changed(double now, bool fitsProxy) {
    version_++;
    wantProxy_ = !fitsProxy;
    wantFull_ = true;
    settleAt_ = fitsProxy ? now : now + settleMs;
}

std::optional<PreviewScheduler::Job> PreviewScheduler::next(double now) {
    if (inFlight_) {
        // A full render of an outdated version is worth abandoning; a proxy is
        // allowed to finish so the screen keeps updating during drags.
        if (!inFlight_->proxy && inFlight_->version != version_ && (wantProxy_ || wantFull_)) {
            // fall through: submitting cancels it
        } else {
            return std::nullopt;
        }
    }
    if (wantProxy_) {
        wantProxy_ = false;
        inFlight_ = Job{nextTicket_++, version_, true};
        return inFlight_;
    }
    if (wantFull_ && now >= settleAt_) {
        wantFull_ = false;
        inFlight_ = Job{nextTicket_++, version_, false};
        return inFlight_;
    }
    return std::nullopt;
}

bool PreviewScheduler::finished(uint64_t ticket, bool cancelled) {
    // Look up what the ticket was. Only the in-flight job can finish, except
    // for a full render we abandoned by submitting over it.
    if (!inFlight_ || inFlight_->ticket != ticket) return false;
    Job job = *inFlight_;
    inFlight_.reset();
    if (cancelled) {
        if (!job.proxy && job.version == version_) wantFull_ = true;  // retry
        return false;
    }
    if (anyShown_ && job.version < shownVersion_) return false;
    if (anyShown_ && job.version == shownVersion_ && job.proxy && shownFull_) return false;
    anyShown_ = true;
    shownVersion_ = job.version;
    shownFull_ = !job.proxy;
    // A proxy finished but the settings moved on meanwhile: render a fresh proxy.
    if (job.proxy && job.version != version_ && !wantProxy_) wantProxy_ = true;
    return true;
}

double PreviewScheduler::wakeIn(double now) const {
    if (wantProxy_ && !inFlight_) return 0;
    if (wantFull_) return std::max(0.0, settleAt_ - now);
    return -1;
}

} // namespace studio
