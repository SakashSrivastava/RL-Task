# MiniShop RL

A small shop web page, a C++ environment that drives it in headless Chrome over the Chrome DevTools Protocol (CDP), and two agents (random baseline and tabular Q-learning) that try to buy the right item.

Video walkthrough: https://www.loom.com/share/9e5d537087f5452f989eb420880e3b27

## What I implemented

1. **Web page** (`site/index.html`, plain HTML and JavaScript). MiniShop with catalog, product, cart and done screens, plus a useless newsletter screen. The goal, seed, popup chance and delay chance come from the URL, for example `index.html?item=blue-mug&qty=2&seed=42&popup_p=0.15&delay_p=0.10`. On every screen change the page may show a popup that covers everything until Dismiss is clicked, and may show its buttons 50–300 ms late. A seeded random generator makes the same seed and the same clicks always behave the same.
2. **Environment** (`env/src`, C++17). Starts headless Chrome, talks to it over a WebSocket client written from scratch, and exposes `reset(task, seed)` and `step(action)`. The observation is read from the live page on every step. `click(i)` is a real mouse press and release sent through CDP at the button's position. Every step has a time limit, attempts stop after 20 steps, every step is logged as one JSON line, and Chrome is always shut down.
3. **Agents** (`env/src/agents.hpp`). A random agent and a tabular Q-learning agent, trained on all 12 goals (4 items × 3 quantities) with 3 different seeds.
4. **Analysis** (`analysis/report.py`). Reads only the logs and writes `report.md` with charts.

The only library used for the environment is nlohmann/json. No Puppeteer, Playwright, Selenium or other browser-control library.

## Approach and reasoning

Full details are in [DECISIONS.md](DECISIONS.md). The main choices:

- **Observation:** screen name, goal, the page's info line (`blue-mug x2`, `empty`, `Order placed: ...`), whether the popup is open, and every visible button with its index, text and `clickable`. A button counts as clickable only if it is enabled and `document.elementFromPoint` at its centre returns the button itself, so buttons under the popup are reported as not clickable. Nothing is hard-coded per screen.
- **Actions:** `click(i)` and `wait` (100 ms, against delays of 50–300 ms).
- **Reward:** −0.01 per step, +1 extra for the exact right order, −1 extra for any other order. The step cost is the only shaping. It makes shorter paths better, and 20 steps cost only 0.2, so it never outweighs a correct order.
- **Episode end:** `done` when an order is placed. `truncated` after 20 steps or when a step takes longer than 2 s. Q-learning treats `done` as terminal but still bootstraps on `truncated`, because a cut-off attempt says nothing about the value of the state it stopped in.
- **Q-learning state:** the observation written relative to the goal, for example `product:qty-low|5` (product page, right item, quantity too low, 5 buttons visible). What the agent learns about `+` on one goal then transfers to the other eleven. A first version that keyed the table on the raw goal and page text had to learn each goal separately and was much slower.
- **Hyperparameters:** α = 0.5, γ = 0.95, ε-greedy with ε decaying from 1.0 to 0.05 over the first 80% of 240 episodes. Ties between equal Q values are broken at random.

## How to run

Requirements (Ubuntu or WSL2):

```bash
sudo apt install -y build-essential cmake python3-venv nlohmann-json3-dev
```

Chrome or Chromium must be installed. `run.sh` looks for `google-chrome`, `chromium` and `chromium-browser`. To use another binary: `CHROME=/path/to/chrome ./run.sh`.

Everything in one command:

```bash
./run.sh
```

This builds the C++ code, trains the Q-learning agent 3 times (seeds 0, 1, 2), tests the random agent and the learned agents, and writes `report.md` with charts in `report_files/`. It takes about 6 minutes. Matplotlib is taken from the system Python if present, otherwise it is installed into `~/.cache/minishop-venv`.

Parts by hand:

```bash
cmake -S env -B env/build -DCMAKE_BUILD_TYPE=Release && cmake --build env/build
./env/build/minishop --mode train --seed 0 --episodes 240 --log logs/train_0.jsonl --qtable logs/qtable_0.json
./env/build/minishop --mode test --seed 0 --popup_p 0.4 --episodes 72 --log logs/test_q_0_p0.4.jsonl --qtable logs/qtable_0.json
./env/build/minishop --mode test --agent random --episodes 216 --log logs/test_random.jsonl
python3 analysis/report.py
```

Other options: `--chrome`, `--site`, `--delay_p`.

## Evaluation method

- **Training:** 3 runs (seeds 0, 1, 2), 240 episodes each, cycling through all 12 goals, popup_p = 0.15, delay_p = 0.10.
- **Testing:** the saved Q-table is loaded with ε = 0 and learning switched off. Test page seeds never overlap training seeds.
  - Random agent: 216 attempts at popup_p = 0.15.
  - Each trained agent: 72 attempts (6 per goal) at popup_p = 0, 0.15 and 0.4, so 216 attempts per popup level across the 3 runs.
- **Metrics:** success rate with 95% Wilson confidence intervals, a learning curve averaged over the 3 runs with min–max spread, steps and Dismiss clicks per attempt, failure reasons with example traces, and median and 95th-percentile step and reset times.
- Every number in `report.md` is computed by `analysis/report.py` from the logs in `logs/`.

## Results and observations

The committed `logs/` and `report.md` come from the run shown in the video. Headline numbers:

| | result |
|---|---|
| Random agent | 7 / 216 = 3.2% (95% CI 1.6–6.5%) |
| Q-learning, popup_p = 0.15 | 216 / 216 = 100% (95% CI 98.3–100%) |
| Q-learning, popup_p = 0 / 0.15 / 0.4 | 100% at every level; 5.11 → 5.59 → 6.38 steps per attempt; the extra steps are Dismiss clicks (0 → 0.47 → 1.23) |
| Learning curve | mean success 3% in the first 24 episodes, 100% from episode 169 on in all 3 runs |
| Step time | click median 3.1 ms (p95 7.9 ms), wait about 102 ms, page load median 37 ms; waits are 68% of all measured time |
| Training plus testing | 346 s |

Observations:
- The design of the state mattered more than anything else. Describing the page relative to the goal let one goal's experience help all twelve.
- Popups do not lower success for the trained agent. They make attempts longer, because the popup is part of its state and it learned that Dismiss is the only useful action there.
- The trained agent never failed in testing. Its training failures come from exploration, mostly hitting the 20-step limit.
- Talking to Chrome over CDP is fast; most of the time goes into waiting for the page's delayed buttons.

## Limitations and pending parts

- **Part 5 (optional) is not done.** To complete it I would open several tabs on one Chrome, run one environment per thread, and compare episodes per second against a single environment.
- The goal-relative state relies on the catalog always listing products in the same order. With shuffled products, the state would need the button texts. Next step: make the page harder and compare a state built from button texts against the current one.
- The 100% test success says this task is easy for this state design. The confidence intervals are reported, but a harder page is needed to separate better agents.
- The WebSocket client handles only what Chrome sends here: unfragmented text frames and close, no ping/pong.
- Runs are reproducible in what the page does for a given seed and clicks, but not bit-for-bit, because whether a delayed button has appeared after one `wait` depends on real time.
- The comparison with the raw-goal state is from an early experiment, not from the committed logs. Next step: keep both state versions as options so the comparison can be rerun from logs.

## Files

| File | What it does |
|---|---|
| `site/index.html` | The MiniShop page |
| `env/src/websocket.hpp` | Minimal WebSocket client on POSIX sockets; every read has a deadline |
| `env/src/browser.hpp` | Starts headless Chrome, opens a tab, sends CDP commands and matches replies by id, kills Chrome on exit |
| `env/src/minishop_env.hpp` | The environment: `reset`, `step`, observation, real clicks, reward, limits |
| `env/src/episode.hpp` | Runs one attempt and writes one JSON log line per step |
| `env/src/agents.hpp` | Random agent and Q-learning agent |
| `env/src/main.cpp` | Command line, train and test loops, Chrome restart on failure, Ctrl+C handling |
| `analysis/report.py` | Reads `logs/` and writes `report.md` |
| `logs/` | Evaluation logs from the run shown in the video, plus the trained Q-tables |
| `report.md`, `report_files/` | Generated report and charts |
| `DECISIONS.md` | State, actions, reward, episode end, what was skipped |

## Log format

One JSON line per step:

```json
{"mode": "test", "agent": "q", "run": 0, "popup_p": 0.15, "delay_p": 0.1, "episode": 17, "seed": 1000017,
 "goal": "blue-mug x2", "step": 3, "obs": {"screen": "product", "goal": "blue-mug x2", "info": "blue-mug x1",
 "popup": false, "buttons": [{"i": 0, "text": "-", "clickable": true}, ...]}, "action": "click(1)",
 "reward": -0.01, "done": false, "truncated": false, "ms": 2.4, "popup": false,
 "info": {"ms_action": 1.6, "ms_observe": 0.8}}
```

`obs` is what the agent saw before choosing `action`, and `ms` is how long the step took. The first step of an attempt also has `reset_ms` (loading the page). The last step has `info.reason`: `success`, `wrong_order`, `step_limit` or `step_timeout`.
