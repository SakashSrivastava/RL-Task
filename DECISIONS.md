# Decisions

## Environment

- **Observation** (read from the live page on every step with `Runtime.evaluate`): screen name, goal, the `#info` line (`blue-mug x2`, `empty`, `Order placed: ...`), whether the popup is open, and every visible `<button>` with its index, text and `clickable`. A button is clickable when it is not disabled and `document.elementFromPoint` at its centre returns the button itself, so a button covered by the popup is reported as not clickable.
- **Actions**: `click(i)` sends a real `mousePressed` + `mouseReleased` through `Input.dispatchMouseEvent` at the button's centre. `wait` sleeps 100 ms (the page's delays are 50–300 ms). An index that does not exist is a no-op and is flagged in `info`.
- **Reward**: −0.01 every step, +1 extra when the placed order matches the goal exactly, −1 extra for any other order. The step cost is the only reward along the way: it makes shorter paths better than longer ones and makes the Newsletter screen and repeated waits cost something. 20 steps × 0.01 = 0.2, so it can never outweigh a correct order. A wrong order is worse than running out of time, because placing a wrong order is the outcome a real shop would least want.
- **When an attempt ends**: `done` when the Done screen appears (order placed, right or wrong). `truncated` after 20 steps, or when one step takes longer than 2 s (any CDP reply missing its deadline). Q-learning treats `done` as terminal and still bootstraps on `truncated`, because a cut-off attempt says nothing about the value of the state it stopped in.
- **Chrome**: started with `fork`/`execlp` in its own process group, with `PR_SET_PDEATHSIG`. The `Browser` destructor kills the whole group. Tested with normal exit, Ctrl+C and `kill -9`: no Chrome process is left. If a reset or step throws, the run restarts Chrome and carries on.
- **Page**: the cart holds one item (Add to cart replaces it), so success is a single text comparison. Every screen change draws exactly three random numbers (popup, delay, delay length), so the same seed and the same clicks always produce the same popups and delays.

## Agent

- **Q-table state** (`agents.hpp`): the observation reduced to what matters, written relative to the goal: screen, then the goal item on the catalog or `other-item` / `qty-low` / `qty-high` / `match` on product and cart, then the popup flag, then the number of visible buttons (0 means still loading). For example `product:qty-low|popup|5`. An early version used the raw goal and `#info` text as the state; it had to learn the 12 goals separately and reached only about two thirds success after 600 training episodes (about 4 minutes for one run). The goal-relative state shares what it learns across goals and converges in about 100 episodes.
- **Action slots**: slot 0 is `wait`, slot k is `click(k-1)`, 8 slots in total. Only `buttons + 1` slots are valid in a state. Ties between equal Q values are broken at random; otherwise a new state (all zeros) would always choose `wait`.
- **Learning**: α = 0.5, γ = 0.95, ε-greedy with ε decaying from 1.0 to 0.05 over the first 80% of 240 episodes. Tests load the saved table, set ε = 0 and never update it. Test page seeds never overlap training seeds.

## Skipped

- Part 5 (parallel environments or Harbor packaging).
- The WebSocket client only handles what Chrome sends here: unfragmented text frames and close. No ping/pong or fragmented messages.
- Runs are reproducible in what the page does for a given seed and set of clicks, but not bit-for-bit across machines: whether a delayed button has appeared after one `wait` depends on real time.
- The goal-relative state relies on the catalog keeping the same product order. Shuffling products would need the button texts in the state.

## Next, with more time

- Run several tabs on one Chrome, one environment per thread, and measure the speed-up (Part 5).
- Make the page harder (shuffled products, popups that move buttons, multi-item carts) and replace the hand-written state with one built from the button texts, then compare against the current one.
- Keep the raw-state agent as a second option in the code so the state comparison can be rerun from the logs.
