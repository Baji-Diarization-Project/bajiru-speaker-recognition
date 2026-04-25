"""Audio I/O: microphone recording and file loading."""

import functools
import logging
from pathlib import Path

import librosa
import numpy as np
import soundfile as sf

from speechbrain_ecapa.config import SAMPLE_RATE

logger = logging.getLogger(__name__)


def load_file(path: str | Path, *, sample_rate: int = SAMPLE_RATE) -> np.ndarray:
    """Load an audio file and resample it to *sample_rate*.

    Args:
        path: Path to any audio format supported by librosa (MP3, WAV, FLAC…).
        sample_rate: Target sample rate in Hz.

    Returns:
        Float32 mono audio array resampled to *sample_rate*.

    """
    audio, _ = librosa.load(path, sr=sample_rate, mono=True)
    logger.debug("Loaded %s — %d samples at %d Hz", path, len(audio), sample_rate)
    return audio


@functools.cache
def _load_file_cached_impl(path: str, sample_rate: int) -> np.ndarray:
    return load_file(path, sample_rate=sample_rate)


def load_file_cached(path: str | Path, *, sample_rate: int = SAMPLE_RATE) -> np.ndarray:
    """Load an audio file and resample it to *sample_rate*. The audio is cached for the lifecycle of the script.

    Args:
        path: Path to any audio format supported by librosa (MP3, WAV, FLAC…).
        sample_rate: Target sample rate in Hz.

    Returns:
        Float32 mono audio array resampled to *sample_rate*.

    """
    return _load_file_cached_impl(str(Path(path).resolve()), sample_rate)


def save_file(path: str | Path, audio: np.ndarray, sample_rate: int) -> None:
    """Save an audio track to file.

    Args:
        path: Path of any audio format supported by soundfile
        sample_rate: Sample rate of the audio track in Hz

    Returns:
        None

    """
    sf.write(path, audio, sample_rate)
