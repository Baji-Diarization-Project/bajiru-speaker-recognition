import math

import torch
from torch import nn


class MelCompressor(nn.Module):
    """Compressor operating on mel frames"""

    def __init__(
        self,
        threshold: float = -30.0,
        ratio: float = 4.0,
        knee: float = 6.0,
        attack_frames: float = 1.0,
        release_frames: float = 10.0,
        makeup: float = 0.0,
        eps: float = 1e-6,
    ):
        """Create MelCompressor

        Args:
            threshold (float, optional): Input level threshold in db. Defaults to -30.0.
            ratio (float, optional): Compression ratio. Defaults to 4.0.
            knee (float, optional): Soft knee width in db. Defaults to 6.0.
            attack_frames (float, optional): Attack duration (number of frames). Defaults to 1.0.
            release_frames (float, optional): Release duration (number of frames). Defaults to 10.0.
            makeup (float, optional): Additional gain. Defaults to 0.0.
            eps (float, optional): Epsilon used for linear/db conversion. Defaults to 1e-6.

        """
        super().__init__()
        self.threshold = threshold
        self.ratio = ratio
        self.half_knee = knee / 2.0
        self.eps = eps

        def _calc_coef(tau) -> float:
            return math.exp(-1.0 / max(1e-6, tau))

        self.register_buffer(
            "_attack_coef",
            torch.tensor(_calc_coef(attack_frames)),
            persistent=False,
        )
        self.register_buffer(
            "_release_coef",
            torch.tensor(_calc_coef(release_frames)),
            persistent=False,
        )
        self.register_buffer(
            "_makeup",
            self._db_to_linear(torch.tensor(makeup)),
            persistent=False,
        )

    def _linear_to_db(self, x: torch.Tensor) -> torch.Tensor:
        return 10.0 * torch.log10(x.clamp(min=self.eps))

    def _db_to_linear(self, x: torch.Tensor) -> torch.Tensor:
        return torch.pow(10.0, x / 10.0)

    def _target_level(self, level: torch.Tensor) -> torch.Tensor:
        level = 1.0 + (level - 1.0).sum(
            dim=-1
        )  # (n_frames,n_mels) -> (n_frames,); energies sum
        level = self._linear_to_db(level)
        over_thr = level - self.threshold

        coef = 1.0 / self.ratio - 1.0

        out = torch.zeros_like(over_thr)
        out = torch.where(over_thr >= self.half_knee, over_thr * coef, out)

        o_mid = over_thr + self.half_knee
        out = torch.where(
            ~((over_thr <= -self.half_knee) | (over_thr >= self.half_knee)),
            coef * (o_mid * o_mid) / (4.0 * self.half_knee),
            out,
        )
        return self._db_to_linear(out)

    def _apply_level(self, inp: torch.Tensor, lvl: torch.Tensor) -> torch.Tensor:
        return inp * lvl.unsqueeze(-1) * self._makeup  # ty:ignore[unsupported-operator]

    def forward(self, x) -> torch.Tensor:
        x = x.transpose(2, 1)  # (B,mels,frames)
        n_batches, n_frames, _ = x.shape
        tgt_lvl = self._target_level(x)  # (B,n_frames)

        out_lvls = []
        lvl = torch.ones((n_batches,), device=tgt_lvl.device, dtype=tgt_lvl.dtype)
        for frame in range(n_frames):
            tgt = tgt_lvl[:, frame]  # (B,)

            coef: torch.Tensor = torch.where(
                tgt < lvl, self._attack_coef, self._release_coef
            )  # ty:ignore[no-matching-overload]

            lvl = torch.lerp(tgt, lvl, coef)
            out_lvls.append(lvl)
        out_lvl = torch.stack(out_lvls, dim=1)

        x = self._apply_level(x, out_lvl)
        return x.transpose(2, 1)  # (B,mels,frames)
