#pragma once
#include <thread>
#include <vector>
#include "browser.hpp"

struct Task {
    std::string item;
    int qty;
    std::string goal() const { return item + " x" + std::to_string(qty); }
};

struct Button {
    int i;
    std::string text;
    bool clickable;
    double x, y;
};

struct Observation {
    std::string screen, goal, info;
    bool popup = false;
    std::vector<Button> buttons;

    json toJson() const {
        json list = json::array();
        for (const auto& b : buttons) list.push_back({{"i", b.i}, {"text", b.text}, {"clickable", b.clickable}});
        return {{"screen", screen}, {"goal", goal}, {"info", info}, {"popup", popup}, {"buttons", list}};
    }
};

struct StepResult {
    Observation obs;
    double reward;
    bool done;
    bool truncated;
    json info;
};

constexpr int WAIT = -1;
inline std::string actionName(int a) { return a == WAIT ? "wait" : "click(" + std::to_string(a) + ")"; }

const char* OBSERVE_JS = R"JS((() => {
  const buttons = [...document.querySelectorAll('button')]
    .filter(b => b.getClientRects().length > 0)
    .map((b, i) => {
      const r = b.getBoundingClientRect();
      const x = r.x + r.width / 2, y = r.y + r.height / 2;
      return { i, text: b.textContent.trim(), x, y,
               clickable: !b.disabled && document.elementFromPoint(x, y) === b };
    });
  return { screen: document.body.dataset.screen,
           info: document.getElementById('info')?.textContent ?? '',
           popup: !document.getElementById('popup').hidden,
           buttons };
})())JS";

class MiniShopEnv {
public:
    static constexpr int MAX_STEPS = 20;
    static constexpr int WAIT_MS = 100;
    static constexpr double STEP_COST = 0.01;

    MiniShopEnv(Browser& browser, std::string pageUrl, double popupP, double delayP, int stepTimeoutMs = 2000)
        : browser_(browser), pageUrl_(std::move(pageUrl)), popupP_(popupP), delayP_(delayP), stepTimeoutMs_(stepTimeoutMs) {}

    Observation reset(const Task& task, int seed) {
        task_ = task;
        steps_ = 0;
        ended_ = false;
        std::string query = "?item=" + task.item + "&qty=" + std::to_string(task.qty) + "&seed=" + std::to_string(seed) +
                            "&popup_p=" + std::to_string(popupP_) + "&delay_p=" + std::to_string(delayP_);
        auto deadline = Clock::now() + std::chrono::seconds(5);
        browser_.call("Page.navigate", {{"url", pageUrl_ + query}}, deadline);
        waitForPage(query, deadline);
        obs_ = observe(deadline);
        return obs_;
    }

    StepResult step(int action) {
        if (ended_) throw std::logic_error("attempt has ended, call reset()");
        steps_++;
        auto deadline = Clock::now() + std::chrono::milliseconds(stepTimeoutMs_);
        json info = json::object();
        try {
            auto t0 = Clock::now();
            if (action == WAIT) std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_MS));
            else if (action >= 0 && action < (int)obs_.buttons.size()) click(obs_.buttons[action], deadline);
            else info["invalid_action"] = true;
            auto t1 = Clock::now();
            obs_ = observe(deadline);
            info["ms_action"] = msBetween(t0, t1);
            info["ms_observe"] = msBetween(t1, Clock::now());
        } catch (const Timeout&) {
            ended_ = true;
            info["reason"] = "step_timeout";
            return {obs_, -STEP_COST, false, true, info};
        }

        double reward = -STEP_COST;
        bool done = obs_.screen == "done";
        bool truncated = !done && steps_ >= MAX_STEPS;
        if (done) {
            bool success = obs_.info == "Order placed: " + task_.goal();
            reward += success ? 1.0 : -1.0;
            info["reason"] = success ? "success" : "wrong_order";
        } else if (truncated) {
            info["reason"] = "step_limit";
        }
        ended_ = done || truncated;
        return {obs_, reward, done, truncated, info};
    }

private:
    Browser& browser_;
    std::string pageUrl_;
    double popupP_, delayP_;
    int stepTimeoutMs_;
    Task task_;
    Observation obs_;
    int steps_ = 0;
    bool ended_ = true;

    static double msBetween(Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    }

    json eval(const std::string& js, Clock::time_point deadline) {
        json r = browser_.call("Runtime.evaluate", {{"expression", js}, {"returnByValue", true}}, deadline);
        if (r.contains("exceptionDetails")) throw std::runtime_error("page error: " + r["exceptionDetails"].dump());
        return r["result"]["value"];
    }

    void waitForPage(const std::string& query, Clock::time_point deadline) {
        std::string check = "location.search === '" + query + "' && document.readyState === 'complete'";
        while (true) {
            try {
                if (eval(check, deadline) == true) return;
            } catch (const Timeout&) {
                throw;
            } catch (const std::runtime_error&) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    Observation observe(Clock::time_point deadline) {
        json v = eval(OBSERVE_JS, deadline);
        Observation o;
        o.screen = v["screen"].get<std::string>();
        o.goal = task_.goal();
        o.info = v["info"].get<std::string>();
        o.popup = v["popup"].get<bool>();
        for (const auto& b : v["buttons"])
            o.buttons.push_back({b["i"].get<int>(), b["text"].get<std::string>(), b["clickable"].get<bool>(),
                                 b["x"].get<double>(), b["y"].get<double>()});
        return o;
    }

    void click(const Button& b, Clock::time_point deadline) {
        for (const char* type : {"mousePressed", "mouseReleased"})
            browser_.call("Input.dispatchMouseEvent",
                          {{"type", type}, {"x", b.x}, {"y", b.y}, {"button", "left"}, {"clickCount", 1}}, deadline);
    }
};
