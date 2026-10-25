#pragma once

// Branching history (design §5.4): a tree of preset snapshots. Undo moves to
// the parent; redo to the child last visited. A change made after an undo
// becomes a new branch; the old one is kept. In memory, per session.

#include "preset.hpp"

#include <glic/glic.hpp>

#include <string>
#include <vector>

namespace studio {

class History {
public:
    struct Node {
        int parent = -1;
        std::vector<int> children;   // in creation order
        int preferredChild = -1;     // where redo goes
        glic::preset::Preset preset;
        glic::Image thumbnail;       // may be empty until a render arrives
        std::string label;           // what made it ("edit", "randomize", "mutate", ...)
    };

    bool empty() const { return nodes_.empty(); }
    int current() const { return current_; }
    const Node& node(int i) const { return nodes_[static_cast<size_t>(i)]; }
    int size() const { return static_cast<int>(nodes_.size()); }

    // Records `p` as the new current state, a child of the current node,
    // unless it has the same settings as the current node. Returns true if a
    // node was added.
    bool record(const glic::preset::Preset& p, std::string label);
    void setThumbnail(int node, glic::Image thumb);

    // Move along the tree; return the preset to apply, or nullptr if there is
    // nowhere to go.
    const glic::preset::Preset* undo();
    const glic::preset::Preset* redo();
    const glic::preset::Preset* jumpTo(int node);

    bool canUndo() const { return current_ >= 0 && nodes_[static_cast<size_t>(current_)].parent >= 0; }
    bool canRedo() const { return current_ >= 0 && nodes_[static_cast<size_t>(current_)].preferredChild >= 0; }

    // The line the strip shows: root to current, then on along preferred children.
    std::vector<int> mainLine() const;

    void clear();

private:
    std::vector<Node> nodes_;
    int current_ = -1;
};

} // namespace studio
