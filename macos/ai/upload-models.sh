#!/bin/sh
# Publishes the AI's models where the settings window downloads them from
# (macos/Models.swift): Hugging Face repos under bluevisor, one per model,
# with Qwen's Apache-2.0 license and where the weights came from. Needs
# `hf auth login` as bluevisor. Run after fetch-models.sh; a model whose
# files change needs its sizes and SHA-256s updated in Models.swift too.
#   macos/ai/upload-models.sh
set -e
models="$HOME/Library/Application Support/HaikuPinyin/models"
for pair in Qwen3-1.7B-Base-4bit:Qwen/Qwen3-1.7B-Base \
	Qwen3.5-0.8B-Base-4bit:Qwen/Qwen3.5-0.8B-Base; do
	name=${pair%%:*}
	source=${pair#*:}
	repo=bluevisor/$name-mlx
	dir="$models/$name"
	[ -f "$dir/config.json" ] || { echo "no $dir: run fetch-models.sh" >&2; exit 1; }
	stage=$(mktemp -d)
	# Links resolved: what's uploaded is the files.
	cp -RL "$dir/" "$stage/"
	rm -f "$stage/README.md"
	curl -fsSL "https://huggingface.co/$source/resolve/main/LICENSE" -o "$stage/LICENSE"
	cat > "$stage/README.md" <<README
---
library_name: mlx
license: apache-2.0
license_link: https://huggingface.co/$source/blob/main/LICENSE
base_model: $source
pipeline_tag: text-generation
tags:
- mlx
---

# $name (MLX)

[$source](https://huggingface.co/$source) converted to MLX with 4-bit
quantization (\`mlx_lm.convert -q --q-bits 4\`), unchanged otherwise. It is
the model behind the AI picks and completions of
[海酷拼音 Haiku Pinyin](https://github.com/bluevisor/haiku-pinyin) on the Mac,
which downloads it from here.

The weights are Qwen's, under the Apache License 2.0 (LICENSE).
README
	hf repos create "$repo" --type model --exist-ok
	hf upload "$repo" "$stage" . --commit-message "$name, 4-bit MLX"
	rm -rf "$stage"
	echo "$name: https://huggingface.co/$repo"
done
