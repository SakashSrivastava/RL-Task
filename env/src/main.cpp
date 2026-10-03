#include <atomic>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <iostream>
#include "agents.hpp"
#include "episode.hpp"

std::atomic<bool> stopRequested{false};

std::vector<Task> allTasks() {
    std::vector<Task> tasks;
    for (std::string item : {"blue-mug", "red-mug", "green-shirt", "black-cap"})
        for (int qty = 1; qty <= 3; qty++) tasks.push_back({item, qty});
    return tasks;
}

std::map<std::string, std::string> parseArgs(int argc, char** argv) {
    std::map<std::string, std::string> args = {
        {"mode", "train"}, {"agent", "q"}, {"chrome", "google-chrome"}, {"site", "site/index.html"},
        {"episodes", "240"}, {"seed", "0"}, {"popup_p", "0.15"}, {"delay_p", "0.10"},
        {"log", "logs/train_0.jsonl"}, {"qtable", "logs/qtable_0.json"}};
    for (int i = 1; i + 1 < argc; i += 2) args[std::string(argv[i]).substr(2)] = argv[i + 1];
    return args;
}

int run(int argc, char** argv) {
    auto args = parseArgs(argc, argv);
    std::signal(SIGINT, [](int) { stopRequested = true; });
    std::signal(SIGTERM, [](int) { stopRequested = true; });

    bool training = args["mode"] == "train";
    std::string pageUrl = "file://" + std::filesystem::absolute(args["site"]).string();
    double popupP = std::stod(args["popup_p"]), delayP = std::stod(args["delay_p"]);
    int runSeed = std::stoi(args["seed"]), episodes = std::stoi(args["episodes"]);

    std::filesystem::create_directories(std::filesystem::path(args["log"]).parent_path());
    std::ofstream log(args["log"]);
    json meta = {{"mode", args["mode"]}, {"agent", args["agent"]}, {"run", runSeed}, {"popup_p", popupP}, {"delay_p", delayP}};

    std::unique_ptr<Browser> browser;
    std::unique_ptr<MiniShopEnv> env;
    auto startChrome = [&] {
        env = nullptr;
        browser = std::make_unique<Browser>(args["chrome"]);
        env = std::make_unique<MiniShopEnv>(*browser, pageUrl, popupP, delayP);
    };
    startChrome();

    auto tasks = allTasks();
    auto runAll = [&](auto& agent) {
        int successes = 0;
        for (int ep = 0; ep < episodes && !stopRequested; ep++) {
            const Task& task = tasks[ep % tasks.size()];
            int pageSeed = (training ? 0 : 1000000) + runSeed * 100000 + ep;
            try {
                successes += runEpisode(*env, agent, task, pageSeed, ep, meta, log) == "success";
            } catch (const std::exception& e) {
                std::cerr << "episode " << ep << " failed (" << e.what() << "), restarting Chrome\n";
                startChrome();
            }
            if ((ep + 1) % 100 == 0) std::cout << "  " << ep + 1 << "/" << episodes << " episodes\n";
        }
        std::cout << args["mode"] << " " << args["agent"] << " run " << runSeed << " popup_p " << popupP << ": "
                  << successes << "/" << episodes << " successes\n";
    };

    if (args["agent"] == "random") {
        RandomAgent agent(runSeed);
        runAll(agent);
        return 0;
    }
    QAgent agent(runSeed);
    if (training) {
        agent.epsilonDecay = std::pow(agent.epsilonMin, 1.0 / (0.8 * episodes));
        runAll(agent);
        agent.save(args["qtable"]);
    } else {
        agent.load(args["qtable"]);
        agent.epsilon = 0;
        agent.learning = false;
        runAll(agent);
    }
    return 0;
}

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
