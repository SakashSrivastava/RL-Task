#pragma once
#include <fstream>
#include "minishop_env.hpp"

template <class Agent>
std::string runEpisode(MiniShopEnv& env, Agent& agent, const Task& task, int seed, int episode,
                       const json& meta, std::ofstream& log) {
    auto start = Clock::now();
    Observation obs = env.reset(task, seed);
    double resetMs = msBetween(start, Clock::now());
    for (int step = 1;; step++) {
        int action = agent.act(obs);
        auto t0 = Clock::now();
        StepResult r = env.step(action);
        double ms = msBetween(t0, Clock::now());
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
        if (step == 1) line["reset_ms"] = resetMs;
        line["popup"] = obs.popup;
        line["info"] = r.info;
        log << line.dump() << "\n";

        obs = r.obs;
        if (r.done || r.truncated) {
            agent.endEpisode();
            return r.info.value("reason", "unknown");
        }
    }
}
