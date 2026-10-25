#include "history.hpp"

#include <algorithm>

namespace studio {

bool History::record(const glic::preset::Preset& p, std::string label) {
    if (current_ >= 0 && nodes_[static_cast<size_t>(current_)].preset.sameSettings(p)) return false;
    Node n;
    n.parent = current_;
    n.preset = p;
    n.label = std::move(label);
    nodes_.push_back(std::move(n));
    const int id = static_cast<int>(nodes_.size()) - 1;
    if (current_ >= 0) {
        Node& parent = nodes_[static_cast<size_t>(current_)];
        parent.children.push_back(id);
        parent.preferredChild = id;
    }
    current_ = id;
    return true;
}

void History::setThumbnail(int node, glic::Image thumb) {
    if (node >= 0 && node < size()) nodes_[static_cast<size_t>(node)].thumbnail = std::move(thumb);
}

const glic::preset::Preset* History::undo() {
    if (!canUndo()) return nullptr;
    const int child = current_;
    current_ = nodes_[static_cast<size_t>(current_)].parent;
    nodes_[static_cast<size_t>(current_)].preferredChild = child;  // redo comes back here
    return &nodes_[static_cast<size_t>(current_)].preset;
}

const glic::preset::Preset* History::redo() {
    if (!canRedo()) return nullptr;
    current_ = nodes_[static_cast<size_t>(current_)].preferredChild;
    return &nodes_[static_cast<size_t>(current_)].preset;
}

const glic::preset::Preset* History::jumpTo(int node) {
    if (node < 0 || node >= size()) return nullptr;
    // Make the path to `node` the preferred one, so redo retraces it.
    for (int n = node; nodes_[static_cast<size_t>(n)].parent >= 0; n = nodes_[static_cast<size_t>(n)].parent)
        nodes_[static_cast<size_t>(nodes_[static_cast<size_t>(n)].parent)].preferredChild = n;
    current_ = node;
    return &nodes_[static_cast<size_t>(current_)].preset;
}

std::vector<int> History::mainLine() const {
    std::vector<int> line;
    for (int n = current_; n >= 0; n = nodes_[static_cast<size_t>(n)].parent) line.push_back(n);
    std::reverse(line.begin(), line.end());
    for (int n = current_ >= 0 ? nodes_[static_cast<size_t>(current_)].preferredChild : -1; n >= 0;
         n = nodes_[static_cast<size_t>(n)].preferredChild)
        line.push_back(n);
    return line;
}

void History::clear() {
    nodes_.clear();
    current_ = -1;
}

} // namespace studio
