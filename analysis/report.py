import json
import math
import statistics
from collections import Counter, defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

LOGS = Path("logs")
FIGS = Path("report_files")
BLOCK = 24


def load(pattern):
    rows = []
    for path in sorted(LOGS.glob(pattern)):
        with path.open() as f:
            rows += [json.loads(line) for line in f]
    return rows


def episodes(rows):
    grouped = defaultdict(list)
    for r in rows:
        grouped[(r["mode"], r["agent"], r["run"], r["popup_p"], r["episode"])].append(r)
    return list(grouped.values())


def outcome(ep):
    return ep[-1]["info"].get("reason", "unknown")


def successes(eps):
    return sum(outcome(e) == "success" for e in eps)


def wilson(k, n, z=1.96):
    p = k / n
    centre = (p + z * z / (2 * n)) / (1 + z * z / n)
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / (1 + z * z / n)
    return p, max(0.0, centre - half), min(1.0, centre + half)


def rate_row(name, eps):
    k, n = successes(eps), len(eps)
    p, lo, hi = wilson(k, n)
    return f"| {name} | {n} | {k} | {p:.1%} | {lo:.1%} – {hi:.1%} |"


def percentile(values, q):
    values = sorted(values)
    return values[min(len(values) - 1, int(q * len(values)))]


def button_text(row):
    action = row["action"]
    if action == "wait":
        return "wait"
    i = int(action[6:-1])
    buttons = row["obs"]["buttons"]
    return buttons[i]["text"] if i < len(buttons) else f"invalid {action}"


def trace(ep, last=8):
    parts = [f'{r["obs"]["screen"]}{" (popup)" if r["popup"] else ""}: {button_text(r)}' for r in ep[-last:]]
    return ("… → " if len(ep) > last else "") + " → ".join(parts)


def failure_kind(ep):
    last = ep[-1]
    reason = outcome(ep)
    if reason == "wrong_order":
        goal_item = last["goal"].rsplit(" x", 1)[0]
        ordered_item = last["obs"]["info"].rsplit(" x", 1)[0]
        return "ordered the wrong item" if ordered_item != goal_item else "ordered the wrong quantity"
    if reason == "step_limit":
        return f'hit the 20-step limit on the {last["obs"]["screen"]} screen'
    return reason.replace("_", " ")


def learning_curve(train):
    curves = []
    for run in sorted({e[0]["run"] for e in train}):
        eps = sorted((e for e in train if e[0]["run"] == run), key=lambda e: e[0]["episode"])
        curves.append([successes(eps[i:i + BLOCK]) / BLOCK for i in range(0, len(eps) - BLOCK + 1, BLOCK)])
    return curves


def plot_learning(curves):
    x = [(i + 1) * BLOCK for i in range(len(curves[0]))]
    columns = list(zip(*curves))
    mean = [statistics.mean(c) for c in columns]
    plt.figure(figsize=(7, 4))
    plt.fill_between(x, [min(c) for c in columns], [max(c) for c in columns], alpha=0.25, label="min – max of runs")
    for i, curve in enumerate(curves):
        plt.plot(x, curve, linewidth=0.8, alpha=0.6, label=f"run {i}")
    plt.plot(x, mean, color="black", linewidth=2, label="mean of runs")
    plt.xlabel("training episode")
    plt.ylabel(f"success rate (per {BLOCK} episodes)")
    plt.ylim(-0.02, 1.02)
    plt.grid(alpha=0.3)
    plt.legend(loc="lower right")
    plt.tight_layout()
    plt.savefig(FIGS / "learning_curve.png", dpi=120)
    plt.close()
    return x, mean, columns


def plot_popups(levels, steps, dismisses):
    plt.figure(figsize=(6, 3.5))
    xs = range(len(levels))
    plt.bar([i - 0.2 for i in xs], steps, width=0.4, label="steps per attempt")
    plt.bar([i + 0.2 for i in xs], dismisses, width=0.4, label="Dismiss clicks per attempt")
    plt.xticks(list(xs), [f"popup_p={p}" for p in levels])
    plt.ylim(0, max(steps) * 1.35)
    plt.grid(axis="y", alpha=0.3)
    plt.legend(loc="upper left")
    plt.tight_layout()
    plt.savefig(FIGS / "popups.png", dpi=120)
    plt.close()


def main():
    FIGS.mkdir(exist_ok=True)
    train = episodes(load("train_*.jsonl"))
    random_test = episodes(load("test_random.jsonl"))
    q_test = episodes(load("test_q_*.jsonl"))
    all_rows = load("*.jsonl")
    run_seconds = json.loads((LOGS / "run_time.json").read_text())["train_and_test_seconds"]
    out = []

    runs = sorted({e[0]["run"] for e in train})
    per_run = len(train) // len(runs)
    out += [
        "# MiniShop RL report",
        "",
        "Every number here is computed by `analysis/report.py` from the JSON logs in `logs/`. "
        "Run `./run.sh` to regenerate the logs and this file.",
        "",
        f"- Training: {len(runs)} runs (seeds {', '.join(map(str, runs))}), {per_run} episodes each, "
        f"cycling through all 12 goals, popup_p=0.15, delay_p=0.10.",
        f"- Testing: page seeds that never appear in training. Greedy policy, no learning.",
        f"- Training plus testing took {run_seconds} s ({run_seconds / 60:.1f} min) on this machine.",
        "",
    ]

    q_default = [e for e in q_test if e[0]["popup_p"] == 0.15]
    out += [
        "## 1. How often does each agent succeed?",
        "",
        "Test attempts at popup_p=0.15, delay_p=0.10. Intervals are 95% Wilson score intervals.",
        "",
        "| agent | attempts | successes | rate | 95% CI |",
        "|---|---|---|---|---|",
        rate_row("random", random_test),
        rate_row("Q-learning (3 runs pooled)", q_default),
    ]
    out += [rate_row(f"Q-learning run {r}", [e for e in q_default if e[0]["run"] == r]) for r in runs]
    random_reasons = Counter(outcome(e) for e in random_test)
    out += [
        "",
        f"The random agent fails mostly by {random_reasons.most_common(1)[0][0].replace('_', ' ')} "
        f"({random_reasons.most_common(1)[0][1]} of {len(random_test)} attempts). "
        f"All its outcomes: {dict(random_reasons)}.",
        "",
    ]

    curves = learning_curve(train)
    x, mean, columns = plot_learning(curves)
    out += [
        "## 2. How the learning agent improves during training",
        "",
        "![learning curve](report_files/learning_curve.png)",
        "",
        f"Success rate in blocks of {BLOCK} training episodes (2 per goal). "
        "Exploration ε falls from 1.0 to 0.05 over the first 80% of training.",
        "",
        "| episodes | mean | min | max | std |",
        "|---|---|---|---|---|",
    ]
    for end, m, col in zip(x, mean, columns):
        out.append(f"| {end - BLOCK + 1}–{end} | {m:.0%} | {min(col):.0%} | {max(col):.0%} | {statistics.pstdev(col):.2f} |")
    out.append("")

    levels = sorted({e[0]["popup_p"] for e in q_test})
    steps, dismisses, rates = [], [], []
    out += [
        "## 3. Learning agent with popup_p = 0, 0.15 and 0.4",
        "",
        "| popup_p | attempts | success rate (95% CI) | steps per attempt | Dismiss clicks per attempt | waits per attempt | time per attempt |",
        "|---|---|---|---|---|---|---|",
    ]
    for p in levels:
        eps = [e for e in q_test if e[0]["popup_p"] == p]
        rate, lo, hi = wilson(successes(eps), len(eps))
        rates.append(rate)
        steps.append(statistics.mean(len(e) for e in eps))
        dismisses.append(statistics.mean(sum(button_text(r) == "Dismiss" for r in e) for e in eps))
        waits = statistics.mean(sum(r["action"] == "wait" for r in e) for e in eps)
        ms = statistics.mean(sum(r["ms"] for r in e) for e in eps)
        out.append(f"| {p} | {len(eps)} | {rate:.1%} ({lo:.1%} – {hi:.1%}) | {steps[-1]:.2f} | "
                   f"{dismisses[-1]:.2f} | {waits:.2f} | {ms:.0f} ms |")
    plot_popups(levels, steps, dismisses)
    out += [
        "",
        "![popups](report_files/popups.png)",
        "",
        (f"Success barely moves ({' → '.join(f'{r:.1%}' for r in rates)}); the cost of popups shows up as length. "
         if max(rates) - min(rates) <= 0.02 else
         f"Success changes with popups ({' → '.join(f'{r:.1%}' for r in rates)}), and attempts get longer. ")
        + f"From popup_p={levels[0]} to popup_p={levels[-1]} an attempt grows from {steps[0]:.2f} to {steps[-1]:.2f} "
        f"steps, and Dismiss clicks grow from {dismisses[0]:.2f} to {dismisses[-1]:.2f} per attempt. "
        "Each screen change can raise a popup, the popup makes every other button unclickable, and the agent "
        "learned in training (popup_p=0.15) that the only useful action in a popup state is Dismiss. The popup "
        "flag is part of its state, so a higher popup rate only means visiting the popup states more often, "
        f"not meeting new ones. An attempt without popups takes {steps[0]:.2f} steps on average and 20 are "
        "allowed, so the extra clicks rarely push an attempt into the step limit.",
        "",
    ]

    q_train_late = [e for e in train if e[0]["episode"] >= int(0.8 * per_run)]
    failed_test = [e for e in q_test if outcome(e) != "success"]
    failed_late = [e for e in q_train_late if outcome(e) != "success"]
    failed_all = [e for e in train if outcome(e) != "success"]
    out += [
        "## 4. Why the learning agent fails",
        "",
        f"- Test attempts that failed: {len(failed_test)} of {len(q_test)}.",
        f"- Failures in the last 20% of training (ε at its 0.05 floor): {len(failed_late)} of {len(q_train_late)}.",
        f"- Failures over all of training, mostly while still exploring: {len(failed_all)} of {len(train)}.",
        "",
    ]
    pool, label = (failed_test, "test") if failed_test else (failed_late, "late training") if failed_late else (failed_all, "training")
    kinds = Counter(failure_kind(e) for e in pool)
    out += [f"Most common reasons ({label} failures):", "", "| reason | count |", "|---|---|"]
    out += [f"| {k} | {n} |" for k, n in kinds.most_common(3)]
    out.append("")
    for kind, _ in kinds.most_common(3):
        ep = next(e for e in pool if failure_kind(e) == kind)
        r = ep[0]
        out += [f"**{kind}**: run {r['run']}, episode {r['episode']}, goal `{r['goal']}`, page seed {r['seed']}:", "",
                f"> {trace(ep)}", ""]
    if not failed_test:
        out += ["The greedy agent did not fail on any test attempt, so the reasons above come from training, "
                "where ε-greedy exploration still picks random buttons.", ""]

    ms_all = [r["ms"] for r in all_rows]
    clicks = [r for r in all_rows if r["action"] != "wait"]
    waits = [r for r in all_rows if r["action"] == "wait"]
    resets = [r["reset_ms"] for r in all_rows if "reset_ms" in r]
    timed = [r for r in clicks if "ms_action" in r["info"]]
    totals = {
        "page loads (reset)": sum(resets),
        "wait steps": sum(r["ms"] for r in waits),
        "click steps": sum(r["ms"] for r in clicks),
    }
    measured = sum(totals.values())
    biggest = max(totals, key=totals.get)
    out += [
        "## 5. How long does one step take?",
        "",
        f"Over all {len(ms_all)} logged steps and {len(resets)} resets (training and testing):",
        "",
        "| what | count | median | 95th percentile |",
        "|---|---|---|---|",
        f"| any step | {len(ms_all)} | {statistics.median(ms_all):.1f} ms | {percentile(ms_all, 0.95):.1f} ms |",
        f"| click step | {len(clicks)} | {statistics.median(r['ms'] for r in clicks):.1f} ms | {percentile([r['ms'] for r in clicks], 0.95):.1f} ms |",
        f"| wait step | {len(waits)} | {statistics.median(r['ms'] for r in waits):.1f} ms | {percentile([r['ms'] for r in waits], 0.95):.1f} ms |",
        f"| reset (load the page) | {len(resets)} | {statistics.median(resets):.1f} ms | {percentile(resets, 0.95):.1f} ms |",
        "",
        "Share of all measured time:",
        "",
        "| part | total | share |",
        "|---|---|---|",
    ]
    out += [f"| {k} | {v / 1000:.1f} s | {v / measured:.0%} |" for k, v in totals.items()]
    out += [
        "",
        f"A typical step is a click and takes {statistics.median(r['ms'] for r in clicks):.1f} ms: sending the mouse "
        f"press and release takes a median of {statistics.median(r['info']['ms_action'] for r in timed):.1f} ms and "
        f"reading the page back {statistics.median(r['info']['ms_observe'] for r in timed):.1f} ms. The slow case is "
        f"the wait action, which sleeps {waits[0]['info']['ms_action']:.0f} ms by design. Overall, the most time goes "
        f"into {biggest} ({totals[biggest] / measured:.0%} of measured time). Of the whole {run_seconds} s run, "
        f"{measured / 1000:.0f} s is measured here; the rest is starting Chrome, the agent's own work and writing logs.",
        "",
    ]

    click_median = statistics.median(r["ms"] for r in clicks)
    out += [
        "## 6. What surprised me",
        "",
        "How much the choice of state mattered compared with anything else. The agent's state is not the raw page: "
        "it describes the page relative to the goal (for example `product:qty-low` = right item, quantity too "
        "low). Because of that, what the agent learns about `+` on one goal is reused on all twelve, and the mean "
        f"success rate across runs goes from {mean[0]:.0%} in the first {BLOCK} episodes to {mean[-1]:.0%} in the "
        f"last {BLOCK}. A first version that used the raw goal and page text as the state learned each goal "
        "separately and was much slower (see DECISIONS.md). A smaller surprise was how cheap a real click "
        f"through CDP is: a median of {click_median:.1f} ms, while loading the page fresh for each attempt takes "
        f"{statistics.median(resets):.0f} ms, about {statistics.median(resets) / click_median:.0f} clicks' worth.",
        "",
    ]

    Path("report.md").write_text("\n".join(out), encoding="utf-8")
    print("wrote report.md")


if __name__ == "__main__":
    main()
