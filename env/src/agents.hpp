#pragma once
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <random>
#include "minishop_env.hpp"

class RandomAgent {
public:
    explicit RandomAgent(unsigned seed) : rng_(seed) {}

    int act(const Observation& obs) {
        if (obs.buttons.empty()) return WAIT;
        return std::uniform_int_distribution<int>(0, (int)obs.buttons.size() - 1)(rng_);
    }

    void learn(const Observation&, int, const StepResult&) {}
    void endEpisode() {}

private:
    std::mt19937 rng_;
};

class QAgent {
public:
    static constexpr int SLOTS = 8;
    double alpha = 0.5, gamma = 0.95, epsilon = 1.0, epsilonDecay = 1.0, epsilonMin = 0.05;
    bool learning = true;

    explicit QAgent(unsigned seed) : rng_(seed) {}

    int act(const Observation& obs) {
        int n = validSlots(obs);
        bool explore = std::uniform_real_distribution<double>(0, 1)(rng_) < epsilon;
        int slot = explore ? std::uniform_int_distribution<int>(0, n - 1)(rng_) : bestSlot(q_[stateKey(obs)], n);
        return slot - 1;
    }

    void learn(const Observation& obs, int action, const StepResult& r) {
        if (!learning) return;
        double target = r.reward;
        if (!r.done) {
            const auto& next = q_[stateKey(r.obs)];
            target += gamma * next[bestSlot(next, validSlots(r.obs))];
        }
        double& q = q_[stateKey(obs)][action + 1];
        q += alpha * (target - q);
    }

    void endEpisode() { epsilon = std::max(epsilonMin, epsilon * epsilonDecay); }

    void save(const std::string& path) const { std::ofstream(path) << json(q_).dump(); }
    void load(const std::string& path) { q_ = json::parse(std::ifstream(path)).get<decltype(q_)>(); }

private:
    std::map<std::string, std::array<double, SLOTS>> q_;
    std::mt19937 rng_;

    static std::string stateKey(const Observation& o) {
        std::string item = o.goal.substr(0, o.goal.find(' '));
        std::string s = o.screen;
        if (o.screen == "catalog") s += ":" + item;
        if (o.screen == "product" || o.screen == "cart") s += ":" + compareToGoal(o.info, item, o.goal.back());
        return s + (o.popup ? "|popup" : "") + "|" + std::to_string(o.buttons.size());
    }

    static std::string compareToGoal(const std::string& info, const std::string& item, char goalQty) {
        if (info.rfind(item + " x", 0) != 0) return "other-item";
        char qty = info.back();
        return qty < goalQty ? "qty-low" : qty > goalQty ? "qty-high" : "match";
    }

    static int validSlots(const Observation& o) { return std::min<int>(o.buttons.size() + 1, SLOTS); }

    int bestSlot(const std::array<double, SLOTS>& q, int n) {
        double best = *std::max_element(q.begin(), q.begin() + n);
        std::vector<int> ties;
        for (int s = 0; s < n; s++)
            if (q[s] == best) ties.push_back(s);
        return ties[std::uniform_int_distribution<int>(0, (int)ties.size() - 1)(rng_)];
    }
};
