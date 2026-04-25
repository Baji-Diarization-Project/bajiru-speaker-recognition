"""Load and create labeled speech segments for speaker profile enrollment."""

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pandas as pd

from speechbrain_ecapa.audio import load_file_cached
from speechbrain_ecapa.config import (
    MERGE_SILENCE_MS,
    MIN_ENERGY_THRESHOLD,
    MIN_SPEECH_RATIO,
    SAMPLE_RATE,
    WINDOW_DURATION_MS,
)
from speechbrain_ecapa.vad import compute_vad_mask, merge_vad_regions


@dataclass(frozen=True)
class Segment:
    """A labeled slice of audio used for enrollment or recognition.

    Attributes:
        speaker: Speaker label associated with the segment.
        source_path: Audio file the segment was loaded from.
        start_s: Segment start time in seconds.
        end_s: Segment end time in seconds.
        speech_ratio: Fraction of the segment marked as speech.
        waveform: Segment audio samples.

    """

    speaker: str
    source_path: Path
    start_s: float
    end_s: float
    speech_ratio: float
    waveform: np.ndarray


def load_segments_from_csv(csv_path: Path, speaker_name: str, *, sample_rate: int = SAMPLE_RATE) -> list[Segment]:
    """Load labeled speaker segments from a CSV file.

    The CSV must contain ``speaker``, ``file``, ``start_s``, and ``end_s``
    columns. Relative file paths are resolved from the CSV file's parent
    directory, and only rows matching ``speaker_name`` are loaded.

    Args:
        csv_path: Path to the segment annotation CSV file.
        speaker_name: Speaker label to select from the CSV.
        sample_rate: Sample rate used to convert timestamps to sample indices.

    Returns:
        Segments for the requested speaker.

    Raises:
        RuntimeError: If the CSV is missing any required columns.

    """
    required_columns = {"speaker", "file", "start_s", "end_s"}
    segments: list[Segment] = []

    df = pd.read_csv(csv_path)
    missing_columns = required_columns - set(df.columns)
    if missing_columns:
        missing = ", ".join(sorted(missing_columns))
        error = f"Segments CSV {csv_path} is missing required column(s): {missing}"
        raise RuntimeError(error)

    for _, row in df.iterrows():
        speaker = str(row["speaker"]).strip()
        if speaker != speaker_name:
            continue

        file_value = str(row["file"]).strip()
        start_s = float(row["start_s"])
        end_s = float(row["end_s"])
        start_sample = int(start_s * sample_rate)
        end_sample = int(end_s * sample_rate)

        audio_path = Path(file_value)
        if not audio_path.is_absolute():
            audio_path = (csv_path.parent / audio_path).resolve()

        audio = load_file_cached(audio_path)[start_sample:end_sample]

        _, vad_mask = compute_vad_mask(audio)

        segments.append(
            Segment(
                speaker=speaker,
                source_path=audio_path,
                start_s=start_s,
                end_s=end_s,
                speech_ratio=np.mean(vad_mask),
                waveform=audio,
            )
        )

    return segments


def create_segments_from_paths(
    audio_paths: list[Path],
    speaker: str,
    *,
    sample_rate: int = SAMPLE_RATE,
    clip_ms: float = WINDOW_DURATION_MS,
    max_clips: int = 20,
    min_speech_ratio: float = MIN_SPEECH_RATIO,
    merge_gap_ms: float = MERGE_SILENCE_MS,
    energy_threshold: float = MIN_ENERGY_THRESHOLD,
) -> list[Segment]:
    """Create fixed-length speech segments from one or more audio files.

    Audio is filtered with voice activity detection, nearby speech regions are
    merged, and candidate clips are kept only when they satisfy the speech-ratio
    and energy thresholds. Segment creation stops when ``max_clips`` have been
    collected.

    Args:
        audio_paths: Audio files to scan for usable speech.
        speaker: Speaker label to attach to each segment.
        sample_rate: Sample rate used when loading audio.
        clip_ms: Desired segment duration in milliseconds.
        max_clips: Maximum number of segments to return across all files.
        min_speech_ratio: Minimum fraction of a segment that must be speech.
        merge_gap_ms: Maximum silence gap to merge between VAD regions.
        energy_threshold: Minimum RMS energy required for a segment.

    Returns:
        Speech segments extracted from the input files.

    """
    segments: list[Segment] = []
    for audio_path in audio_paths:
        audio = load_file_cached(audio_path, sample_rate=sample_rate)
        vad_regions, vad_mask = compute_vad_mask(
            audio,
            sample_rate,
        )
        clip_frames = max(1, round(clip_ms / 1000.0 * sample_rate))
        max_gap_frames = max(0, round(merge_gap_ms / 1000.0 * sample_rate))

        regions = merge_vad_regions(vad_regions, max_gap_frames)

        for region_start, region_stop in regions:
            region_length = region_stop - region_start
            if region_length < clip_frames:
                continue

            # Create multiple segments if region with speech is large enough
            max_start = region_stop - clip_frames
            start_candidates = range(region_start, max_start + 1, clip_frames)

            for start_frame in start_candidates:
                stop_frame = start_frame + clip_frames
                speech_ratio = float(np.mean(vad_mask[start_frame:stop_frame]))
                if speech_ratio < min_speech_ratio:
                    continue

                waveform = audio[start_frame:stop_frame]

                rms = float(np.sqrt(np.mean(np.square(waveform))))
                if rms < energy_threshold:
                    continue

                segments.append(
                    Segment(
                        speaker=speaker,
                        source_path=audio_path,
                        start_s=start_frame / sample_rate,
                        end_s=stop_frame / sample_rate,
                        speech_ratio=speech_ratio,
                        waveform=waveform.astype(np.float32, copy=False),
                    )
                )
                if len(segments) >= max_clips:
                    return segments

    return segments
