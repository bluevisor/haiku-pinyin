#!/bin/sh
# Puts the AI's models where the input method looks for them, in
# ~/Library/Application Support/HaikuPinyin/models: Qwen's base models
# (better than chat ones at guessing the next words) in 4-bit MLX.
#   macos/ai/fetch-models.sh                 downloads and converts them
#                                            (mlx-lm, through uvx)
#   macos/ai/fetch-models.sh <directory>     links copies already there
set -e
models="$HOME/Library/Application Support/HaikuPinyin/models"
mkdir -p "$models"
for pair in Qwen3-1.7B-Base-4bit:Qwen/Qwen3-1.7B-Base \
	Qwen3.5-0.8B-Base-4bit:Qwen/Qwen3.5-0.8B-Base; do
	name=${pair%%:*}
	source=${pair#*:}
	dest="$models/$name"
	if [ -f "$dest/config.json" ]; then
		echo "$name: there"
	elif [ -n "$1" ] && [ -f "$1/$name/config.json" ]; then
		ln -sfn "$(cd "$1/$name" && pwd)" "$dest"
		echo "$name: linked"
	else
		uvx --from mlx-lm mlx_lm.convert --hf-path "$source" \
			--mlx-path "$dest" -q --q-bits 4
		echo "$name: converted"
	fi
done
pkill -x HaikuPinyinAI || true	# the input method starts it again
