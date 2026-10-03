#pragma once
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

private:
    std::mt19937 rng_;
};
