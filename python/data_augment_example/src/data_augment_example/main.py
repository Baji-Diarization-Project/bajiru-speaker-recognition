"""Example script for a downstream package to confirm working ``from data_augment import Augmenter``

Run with:

    uv run --project python/data_augment_example augment-example

Generates a signal, applies the default augmentation chain, and prints output
shapes / counts. This is the cheapest way to confirm that ``from data_augment import Augmenter``
works from a downstream package with its own dependency declaration.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

import numpy as np
from data_augment import DEFAULT_AUGMENTATIONS, Augmenter

if TYPE_CHECKING:
    from numpy.typing import NDArray


def _voice_like(f0: float, sr: int, duration_s: float, n_harmonics: int = 10) -> NDArray[np.float64]:
    """Harmonic stack with 1/k falloff — a synthetic stand-in for voiced speech."""
    t = np.arange(int(duration_s * sr)) / sr
    x = sum(np.sin(2 * np.pi * k * f0 * t) / k for k in range(1, n_harmonics + 1))
    return np.asarray(0.3 * x / np.max(np.abs(x)), dtype=np.float64)


def main() -> None:
    sr = 16000
    x = _voice_like(f0=180.0, sr=sr, duration_s=1.0)
    aug = Augmenter(sample_rate=sr, seed=0)

    print(f"Input: shape={x.shape}, dtype={x.dtype}, sr={sr} Hz")
    print(f"\nDefault chain ({len(DEFAULT_AUGMENTATIONS)} entries):")
    for name, cfg in DEFAULT_AUGMENTATIONS:
        print(f"  {name}: {cfg}")

    variants = aug.augment(x)
    print(f"\nDefault augment(): {len(variants)} variants")
    print(f"  all same shape? {len({v.shape for v in variants}) == 1}")
    print(f"  shape: {variants[0].shape}")

    custom = aug.augment(x, [("pitch", {"semitones": [-3.0, 0.0, 3.0]})])
    print(f"\nCustom single-augmentation chain (pitch x 3 modes): {len(custom)} variants")

    batch = np.stack([x, x, x])
    batch_out = aug.augment(batch, [("gain", {"db": [-3.0, 3.0]})])
    print(f"Batched input (3 signals) x 2 gain modes: {len(batch_out)} variants (expected 6)")

    print("\nOK — data_augment imported and augmented cleanly from a separate package.")


if __name__ == "__main__":
    main()
