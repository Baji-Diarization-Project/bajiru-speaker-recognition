"""A set of functions and utilities related to VAD."""

import functools
import logging

import numpy as np
import torch
from silero_vad import get_speech_timestamps, load_silero_vad

from speechbrain_ecapa.config import VAD_THRESHOLD

logger = logging.getLogger(__name__)


@functools.cache
def _vad_model() -> object:
    """Return the Silero VAD model, loading it on the first call.

    The result is cached for the lifetime of the process so the model is only
    loaded once regardless of how many times ``filter_speech_frames`` or
    ``extract_features`` are called.

    Returns:
        The loaded Silero VAD torch model.

    """
    logger.debug("Loading Silero VAD model…")
    return load_silero_vad()


def compute_vad_mask(
    audio: np.ndarray,
    sample_rate: int,
    *,
    threshold: float = VAD_THRESHOLD,
) -> tuple[list[dict[str, int]], np.ndarray]:
    """Compute speech regions and a frame-level VAD mask for an audio signal.

    Args:
        audio: One-dimensional audio waveform as a NumPy array.
        sample_rate: Audio sample rate in Hz.
        threshold: Silero VAD speech detection threshold.

    Returns:
        A tuple containing the speech timestamp regions and a per frame mask where
        speech frames are marked as ``1.0`` and non-speech frames as ``0.0``.

    """
    frame_count = len(audio)
    if frame_count == 0:
        return np.zeros(0, dtype=bool)

    model = _vad_model()
    tensor = torch.from_numpy(audio)
    timestamps = get_speech_timestamps(
        tensor,
        model,
        sampling_rate=sample_rate,
        threshold=threshold,
        return_seconds=False,
    )

    mask = np.zeros(frame_count, dtype=np.float32)

    for segment in timestamps:
        start_frame = segment["start"]
        end_frame = segment["end"]
        mask[start_frame:end_frame] = 1.0

    return timestamps, mask


def merge_vad_regions(regions: list[dict[str, int]], max_gap_frames: int) -> list[tuple[int, int]]:
    """Merge adjacent VAD assigned speech regions separated by short segments of silence.

    Args:
        regions: VAD timestamp dictionaries with ``start`` and ``end`` frame
            positions.
        max_gap_frames: Maximum number of non-speech frames allowed between two
            regions before they remain separate.

    Returns:
        A list of merged ``(start, stop)`` frame ranges.

    """
    if not regions:
        return []

    merged: list[list[int]] = [[regions[0]["start"], regions[0]["end"]]]
    for region in regions:
        start = region["start"]
        stop = region["end"]
        previous = merged[-1]
        if start - previous[1] <= max_gap_frames:
            previous[1] = stop
        else:
            merged.append([start, stop])

    return list(merged)
