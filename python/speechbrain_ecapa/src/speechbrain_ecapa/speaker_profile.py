"""A module with methods for creating, saving and loading speaker profiles used for speaker classification."""

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np

from speechbrain_ecapa.config import MIN_ENERGY_THRESHOLD, MODEL_ID
from speechbrain_ecapa.model import extract_embedding
from speechbrain_ecapa.speech_segments import Segment


@dataclass(frozen=True)
class SpeakerProfile:
    """Stored speaker identity data used for enrollment and comparison.

    Attributes:
        name: Human-readable speaker name.
        embedding: Normalized speaker embedding vector.
        clip_count: Number of audio clips used to build the profile.
        source_paths: Source audio paths or segment identifiers used for enrollment.

    """

    name: str
    embedding: np.ndarray
    clip_count: int
    source_paths: tuple[str, ...]


def create_profile_from_segments(
    classifier: Any,
    segments: list[Segment],
    name: str,
    *,
    energy_threshold: float = MIN_ENERGY_THRESHOLD,
) -> SpeakerProfile:
    """Create a speaker profile by averaging embeddings from usable audio segments.

        Empty segments raise an error, while segments below the energy threshold are
    skipped. The returned profile embedding is a unit vector of float32.

    Args:
        classifier: SpeechBrain classifier used to extract segment embeddings.
        segments: Candidate enrollment segments for the speaker.
        name: Speaker name to store in the profile.
        energy_threshold: Minimum RMS energy required for a segment to be used.

    Returns:
        A speaker profile built from the accepted segments.

    Raises:
        RuntimeError: If a segment has no audio data, or if no segment passes
            the usability checks.

    """
    embeddings: list[np.ndarray] = []
    used_segments: list[str] = []

    for segment in segments:
        waveform = segment.waveform
        if waveform.size == 0:
            msg = (f"Enrollment segment produced no audio for {segment.source_path}: "
                f"{segment.start_s:.3f}-{segment.end_s:.3f}s")
            raise RuntimeError(msg)

        rms = float(np.sqrt(np.mean(np.square(waveform))))
        if rms < energy_threshold:
            continue

        embeddings.append(extract_embedding(classifier, waveform))
        used_segments.append(f"{segment.source_path}[{segment.start_s:.3f},{segment.end_s:.3f}]")

    if not embeddings:
        msg = f"No usable enrollment segments found for {name}"
        raise RuntimeError(msg)

    profile = np.mean(np.stack(embeddings, axis=0), axis=0)
    profile /= np.linalg.norm(profile) + 1e-12
    return SpeakerProfile(
        name=name,
        embedding=profile.astype(np.float32),
        clip_count=len(embeddings),
        source_paths=tuple(used_segments),
    )


def save_profile(
    prototype: SpeakerProfile,
    output_path: Path,
    *,
    sample_rate: int,
    clip_ms: float,
    max_clips_per_file: int,
    vad_frame_ms: int,
    vad_threshold: int,
    min_speech_ratio: float,
    min_energy_threshold: float,
    merge_gap_ms: float,
) -> None:
    """Save a speaker profile and enrollment metadata to an ``.npz`` file.

    Args:
        prototype: Speaker profile to save.
        output_path: Destination path for the profile file.
        sample_rate: Audio sample rate used during enrollment.
        clip_ms: Target clip length used during enrollment, in milliseconds.
        max_clips_per_file: Maximum number of clips taken from each source file.
        vad_frame_ms: Voice activity detection frame size, in milliseconds.
        vad_threshold: Voice activity detection aggressiveness threshold.
        min_speech_ratio: Minimum speech ratio required for a clip.
        min_energy_threshold: Minimum RMS energy required for a clip.
        merge_gap_ms: Maximum gap between speech regions to merge, in milliseconds.

    """
    metadata = {
        "name": prototype.name,
        "clip_count": prototype.clip_count,
        "source_paths": list(prototype.source_paths),
        "sample_rate": sample_rate,
        "clip_ms": clip_ms,
        "max_clips_per_file": max_clips_per_file,
        "vad_frame_ms": vad_frame_ms,
        "vad_threshold": vad_threshold,
        "min_speech_ratio": min_speech_ratio,
        "min_energy_threshold": min_energy_threshold,
        "merge_gap_ms": merge_gap_ms,
        "model_id": MODEL_ID,
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("wb") as handle:
        np.savez(
            handle,
            embedding=prototype.embedding.astype(np.float32),
            metadata=np.array(json.dumps(metadata)),
        )


def load_profile(profile_path: Path) -> SpeakerProfile:
    """Load a speaker profile from an ``.npz`` file created by ``save_profile``.

    Args:
        profile_path: Path to the saved profile file.

    Returns:
        The stored speaker profile.

    """
    with np.load(profile_path, allow_pickle=False) as data:
        embedding = data["embedding"].astype(np.float32)
        metadata = json.loads(str(data["metadata"]))

    return SpeakerProfile(
        name=str(metadata["name"]),
        embedding=embedding,
        clip_count=int(metadata.get("clip_count", 0)),
        source_paths=tuple(metadata.get("source_paths", [])),
    )
