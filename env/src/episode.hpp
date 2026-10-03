#pragma once
#include <fstream>
#include "minishop_env.hpp"

struct EpisodeResult {
    std::string reason;
    int steps = 0;
    double totalReward = 0;
    bool success() const { return reason == "success"; }
};

template <class Agent>
EpisodeResult runEpisode(MiniShopEnv& env, Agent& agent, const Task& task, int seed, int episode,
                         const json& meta, std::ofstream& log) {
    Observation obs = env.reset(task, seed);
    EpisodeResult result;
    for (int step = 1;; step++) {
        int action = agent.act(obs);
        auto t0 = Clock::now();
        StepResult r = env.step(action);
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        agent.learn(obs, action, r);

        json line = meta;
        line["episode"] = episode;
        line["seed"] = seed;
        line["goal"] = task.goal();
        line["step"] = step;
        line["obs"] = obs.toJson();
        line["action"] = actionName(action);
        line["reward"] = r.reward;
        line["done"] = r.done;
        line["truncated"] = r.truncated;
        line["ms"] = ms;
        line["popup"] = obs.popup;
        line["info"] = r.info;
        log << line.dump() << "\n";

        result.steps = step;
        result.totalReward += r.reward;
        obs = r.obs;
        if (r.done || r.truncated) {
            agent.endEpisode();
            result.reason = r.info.value("reason", "unknown");
            return result;
        }
    }
}
