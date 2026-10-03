#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

CHROME="${CHROME:-$(command -v google-chrome || command -v chromium || command -v chromium-browser || true)}"
if [ -z "$CHROME" ]; then
    echo "Could not find Chrome or Chromium. Install one, or run: CHROME=/path/to/chrome ./run.sh"
    exit 1
fi
echo "Using $CHROME"

cmake -S env -B env/build -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build env/build -j > /dev/null
BIN=env/build/minishop

rm -rf logs
mkdir -p logs
START=$SECONDS

for seed in 0 1 2; do
    $BIN --chrome "$CHROME" --mode train --seed $seed --episodes 240 \
         --log logs/train_$seed.jsonl --qtable logs/qtable_$seed.json
done

$BIN --chrome "$CHROME" --mode test --agent random --seed 0 --episodes 216 --log logs/test_random.jsonl

for p in 0 0.15 0.4; do
    for seed in 0 1 2; do
        $BIN --chrome "$CHROME" --mode test --seed $seed --popup_p $p --episodes 72 \
             --log logs/test_q_${seed}_p$p.jsonl --qtable logs/qtable_$seed.json
    done
done

echo "{\"train_and_test_seconds\": $((SECONDS - START))}" > logs/run_time.json

PYTHON=python3
if ! python3 -c "import matplotlib" 2> /dev/null; then
    VENV="$HOME/.cache/minishop-venv"
    [ -x "$VENV/bin/python" ] || python3 -m venv "$VENV"
    "$VENV/bin/pip" install -q matplotlib
    PYTHON="$VENV/bin/python"
fi
$PYTHON analysis/report.py
echo "Done in $((SECONDS - START))s. Open report.md"
