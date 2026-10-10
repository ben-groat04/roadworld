#pragma once
// Every player edit is a Command applied to the world through a CommandStack.
// That one rule gives undo/redo now and saves/replays/tests later.
#include <memory>
#include <string>
#include <vector>

namespace cp {

template <typename World>
class Command {
public:
    virtual ~Command() = default;
    // Returns false if the command could not be applied; the world must be
    // left unchanged in that case and the command is discarded.
    virtual bool apply(World& world) = 0;
    virtual void revert(World& world) = 0;
    virtual std::string name() const = 0;
};

template <typename World>
class CommandStack {
public:
    using Ptr = std::unique_ptr<Command<World>>;

    explicit CommandStack(World& world) : world_(world) {}

    bool execute(Ptr cmd) {
        if (!cmd->apply(world_)) return false;
        done_.push_back(std::move(cmd));
        undone_.clear();
        return true;
    }

    bool undo() {
        if (done_.empty()) return false;
        done_.back()->revert(world_);
        undone_.push_back(std::move(done_.back()));
        done_.pop_back();
        return true;
    }

    bool redo() {
        if (undone_.empty()) return false;
        if (!undone_.back()->apply(world_)) return false;
        done_.push_back(std::move(undone_.back()));
        undone_.pop_back();
        return true;
    }

    bool can_undo() const { return !done_.empty(); }
    bool can_redo() const { return !undone_.empty(); }
    std::size_t undo_depth() const { return done_.size(); }

private:
    World& world_;
    std::vector<Ptr> done_;
    std::vector<Ptr> undone_;
};

// Groups several commands into one undo step (e.g. "draw road" = add nodes,
// add segments, regenerate junctions). Reverts in reverse order.
template <typename World>
class CompositeCommand : public Command<World> {
public:
    explicit CompositeCommand(std::string name) : name_(std::move(name)) {}

    void add(std::unique_ptr<Command<World>> c) { parts_.push_back(std::move(c)); }

    bool apply(World& world) override {
        for (std::size_t i = 0; i < parts_.size(); ++i) {
            if (!parts_[i]->apply(world)) {
                while (i-- > 0) parts_[i]->revert(world);
                return false;
            }
        }
        return true;
    }

    void revert(World& world) override {
        for (auto it = parts_.rbegin(); it != parts_.rend(); ++it) (*it)->revert(world);
    }

    std::string name() const override { return name_; }

private:
    std::string name_;
    std::vector<std::unique_ptr<Command<World>>> parts_;
};

const char* sim_core_version();

}  // namespace cp
