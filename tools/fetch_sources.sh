#!/bin/sh
# Downloads the upstream data Haiku Pinyin is built from into sources/.
#   rime-ice (雾凇拼音)        word list + frequencies,   GPL-3.0
#                              word -> emoji
#   OpenCC                     simplified -> traditional  Apache-2.0
#   rime-double-pinyin         shuangpin speller rules    LGPL-3.0
set -e
cd "$(dirname "$0")/../sources"
ice=https://raw.githubusercontent.com/iDvel/rime-ice/main/cn_dicts
for f in 8105 41448 base ext; do curl -fsSLO "$ice/$f.dict.yaml"; done
curl -fsSLO https://raw.githubusercontent.com/iDvel/rime-ice/main/opencc/emoji.txt
cc=https://raw.githubusercontent.com/BYVoid/OpenCC/master/data/dictionary
for f in STCharacters STPhrases TWVariants TWPhrases HKVariants; do
	curl -fsSLO "$cc/$f.txt"
done
dp=https://raw.githubusercontent.com/rime/rime-double-pinyin/master
for s in double_pinyin double_pinyin_pyjj double_pinyin_flypy \
		double_pinyin_mspy double_pinyin_abc; do
	curl -fsSLO "$dp/$s.schema.yaml"
done
