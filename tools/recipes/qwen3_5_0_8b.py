"""Qwen3.5-0.8B dense groupwise conversion.

The representation plan is the official `qwen3_8_27b` one unchanged — it selects by logical
NAME PATTERN, not by dimension, so it applies to any Qwen3.5 dense model (official_recipes.py:57-90).
What this adds is one model-specific correction the official recipes never needed.

THE TIED VOCABULARY. Qwen3.5-0.8B sets `tie_word_embeddings: true` and ships no `lm_head.weight`;
both shipped 27B/35B artifacts are `false`, so this path has never run in our conversions. The
adapter handles it — qwen3_5.py:959-963 selects `embed_tokens.weight` as the head source when
tied — but `text/token_embedding` and `text/output_head` then read the SAME source tensor and
sharing is NOT implied: `recipe.share` is explicit (recipe.py:245). Measured on this model with a
converter dry-run (2026-09-17): without the share, `prepare()` emits the 248320x1024 Q8 vocabulary
matrix as TWO physical jobs. At 1.0625 B/param that is ~270 MiB duplicated, on a model whose whole
text arena is roughly 600 MiB.

Usage — note `generation_config.json` is supplied explicitly because the upstream repo does not
ship one, and resources.py:87-88 reads it with no existence check:

    python -m tools.convert \
      --model <hf>/Qwen3.5-0.8B \
      --recipe tools/recipes/qwen3_5_0_8b.py \
      --components text \
      --resource generation_config.json=<hf>/Qwen3.5-0.8B/generation_config.json \
      --name qwen3.5-0.8b \
      --out models/qwen3_5_0_8b.ninfer

UNVERIFIED: no artifact has been produced with this file and nothing has been served. Vision is
deliberately out of scope — this model's tower differs from the 27B/35B one (depth 12, hidden 768
against 27 and 1152), so `--components text` only.
"""

from __future__ import annotations

from tools.convert.official_recipes import qwen3_8_27b


def configure(model, recipe, sources):
    qwen3_8_27b(model, recipe, sources)
    if model.config["tie_word_embeddings"]:
        recipe.share("text/output_head", "text/token_embedding")
