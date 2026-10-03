#include <atomic>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <map>
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
        {"chrome", "google-chrome"}, {"site", "site/index.html"}, {"episodes", "24"}, {"seed", "0"},
        {"popup_p", "0.15"}, {"delay_p", "0.10"}, {"log", "logs/demo.jsonl"}};
    for (int i = 1; i + 1 < argc; i += 2) args[std::string(argv[i]).substr(2)] = argv[i + 1];
    return args;
}

int run(int argc, char** argv) {
    auto args = parseArgs(argc, argv);
    std::signal(SIGINT, [](int) { stopRequested = true; });
    std::signal(SIGTERM, [](int) { stopRequested = true; });

    std::string pageUrl = "file://" + std::filesystem::absolute(args["site"]).string();
    double popupP = std::stod(args["popup_p"]), delayP = std::stod(args["delay_p"]);
    int runSeed = std::stoi(args["seed"]), episodes = std::stoi(args["episodes"]);

    std::filesystem::create_directories(std::filesystem::path(args["log"]).parent_path());
    std::ofstream log(args["log"]);
    json meta = {{"agent", "random"}, {"run", runSeed}, {"popup_p", popupP}, {"delay_p", delayP}};

    std::unique_ptr<Browser> browser;
    std::unique_ptr<MiniShopEnv> env;
    auto startChrome = [&] {
        env = nullptr;
        browser =std::make_unique<Browser>(args["chrome"]);
        env = std::make_unique<MiniShopEnv>(*browser, pageUrl, popupP, delayP);
    };
    startChrome();

    RandomAgent agent(runSeed);
    auto tasks = allTasks();
    int successes = 0;
    for (int ep = 0; ep < episodes && !stopRequested; ep++) {
        const Task& task = tasks[ep % tasks.size()];
        int pageSeed = runSeed * 100000 + ep;
        try {
            EpisodeResult r = runEpisode(*env, agent, task, pageSeed, ep, meta, log);
            successes += r.success();
            std::cout << "episode " << ep << "  " << task.goal() << "  -> " << r.reason << " in " << r.steps << " steps\n";
        } catch (const std::exception& e) {
            std::cerr << "episode " << ep << " failed (" << e.what() << "), restarting Chrome\n";
            startChrome();
        }
    }
    std::cout << successes << " successes, log written to " << args["log"] << "\n";
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
