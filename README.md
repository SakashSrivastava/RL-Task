# MiniShop RL

A small shop page, a C++ environment that drives it in headless Chrome over the Chrome DevTools Protocol, and a tabular Q-learning agent that learns to buy the right item.

## Setup (Ubuntu or WSL2)

```bash
sudo apt install -y build-essential cmake python3-venv nlohmann-json3-dev
```

Chrome or Chromium must be installed. `run.sh` looks for `google-chrome`, `chromium` and `chromium-browser` in that order. To use another binary:

```bash
CHROME=/path/to/chrome ./run.sh
```

## Run everything

```bash
./run.sh
```

This builds the C++ code, trains the Q-learning agent 3 times (seeds 0, 1, 2), tests the random agent and the learned agents, and writes `report.md` with charts in `report_files/`. It takes about 6–8 minutes. Matplotlib is used from the system Python if present, otherwise it is installed into a virtual environment in `~/.cache/minishop-venv`.

## Run parts by hand

```bash
cmake -S env -B env/build -DCMAKE_BUILD_TYPE=Release && cmake --build env/build
./env/build/minishop --mode train --seed 0 --episodes 240 --log logs/train_0.jsonl --qtable logs/qtable_0.json
./env/build/minishop --mode test --seed 0 --popup_p 0.4 --episodes 72 --log logs/test.jsonl --qtable logs/qtable_0.json
./env/build/minishop --mode test --agent random --episodes 216 --log logs/random.jsonl
python3 analysis/report.py
```

Other options: `--chrome`, `--site`, `--delay_p`.

The page can also be opened directly in a browser:
`site/index.html?item=blue-mug&qty=2&seed=42&popup_p=0.5&delay_p=0.5`

## Files

| File | What it does |
|---|---|
| `site/index.html` | MiniShop: catalog, product, cart, done and newsletter screens, with seeded popups and delayed buttons |
| `env/src/websocket.hpp` | Minimal WebSocket client on POSIX sockets; every read has a deadline |
| `env/src/browser.hpp` | Starts headless Chrome, opens a tab, sends CDP commands and matches replies by id, kills Chrome on exit |
| `env/src/minishop_env.hpp` | The environment: `reset(task, seed)` and `step(action)`, observation, real mouse clicks, reward, limits |
| `env/src/episode.hpp` | Runs one attempt and writes one JSON log line per step |
| `env/src/agents.hpp` | Random agent and Q-learning agent |
| `env/src/main.cpp` | Command line, train and test loops, Chrome restart on failure, Ctrl+C handling |
| `analysis/report.py` | Reads `logs/` and writes `report.md` |
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
