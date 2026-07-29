import torch
from torch import nn


class VolumeAugment(nn.Module):
    """Applies a random volume change to the mel-pow spectrogram"""

    def __init__(
        self,
        max_level: float = 0.1,
        eps: float = 1e-6,
    ):
        """Create VolumeAugment

        Args:
            max_level (float, optional): The maximum volume change (db)
            eps (float, optional): The epsilon used for calucations

        """
        super().__init__()
        self.eps = eps
        self.max_level = max_level

    def _linear_to_db(self, x: torch.Tensor) -> torch.Tensor:
        return 10.0 * torch.log10(x.clamp(min=self.eps))

    def _db_to_linear(self, x: torch.Tensor) -> torch.Tensor:
        return torch.pow(10.0, x / 10.0)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        y = torch.rand(x.size(0), 1, 1, device=x.device, dtype=x.dtype) * self.max_level
        x = self._linear_to_db(x)
        return self._db_to_linear(x + y)


class NoiseAugment(nn.Module):
    """Applies a random noise to the mel-pow spectrogram"""

    def __init__(
        self,
        noise_level: float = 0.1,
        eps: float = 1e-6,
    ):
        """Create NoiseAugment

        Args:
            noise_level (float, optional): The maximum noise level (db)
            eps (float, optional): The epsilon used for calucations

        """
        super().__init__()
        self.eps = eps
        self.noise_level = noise_level

    def _linear_to_db(self, x: torch.Tensor) -> torch.Tensor:
        return 10.0 * torch.log10(x.clamp(min=self.eps))

    def _db_to_linear(self, x: torch.Tensor) -> torch.Tensor:
        return torch.pow(10.0, x / 10.0)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        y = torch.randn_like(x, device=x.device, dtype=x.dtype) * (
            torch.rand(x.size(0), 1, 1, device=x.device, dtype=x.dtype)
            * self.noise_level
        )
        x = self._linear_to_db(x)
        return self._db_to_linear(x + y)


class BinShiftAugment(nn.Module):
    """Randomly shifts the bins of the mel-pow spectrogram"""

    def __init__(
        self,
        shift_bins: int = 2,
    ):
        """Create BinShiftAugment

        Args:
            shift_bins (float, optional): The maximum number of bins to be shifted

        """
        super().__init__()
        self.shift_bins = shift_bins

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        shift = torch.randint(
            -self.shift_bins, self.shift_bins + 1, (1,), device=x.device
        ).item()
        if shift != 0:
            x = torch.roll(x, shifts=shift, dims=1)  # ty:ignore[invalid-argument-type]
            x = x.clone()
            if shift > 0:
                x[:, :shift, :] = 0.0
            else:
                x[:, shift:, :] = 0.0
        return x
