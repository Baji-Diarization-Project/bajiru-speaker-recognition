"""Speaker assignment helpers for ECAPA embedding inference."""

import csv
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np

from speechbrain_ecapa.audio import save_file
from speechbrain_ecapa.model import extract_embedding
from speechbrain_ecapa.speaker_profile import SpeakerProfile


@dataclass(frozen=True)
class InferenceStep:
    """Contains the results from one inference step.

    Attributes:
        speech_ratio: Ratio of audio labeled as speech by VAD.
        similarity_a: Cosine similarity between input audio and speaker A's profile.
        similarity_b: Cosine similarity between input audio and speaker B's profile.
        similarity_diff: similarity_a - similarity_b, clipped to [-1, 1]
        margin: Absolute value of similarity_a - similarity_b
        assigned_label: Label assigned to the audio step
        assigned_code: -1 if speaker A, 1 if speaker 2, otherwise 0.

    """

    speech_ratio: float
    similarity_a: float
    similarity_b: float
    similarity_diff: float
    margin: float
    assigned_label: str
    assigned_code: float


def assign_speaker(
    similarity_a: float,
    similarity_b: float,
    speaker_a_name: str,
    speaker_b_name: str,
    *,
    threshold: float,
    margin: float,
) -> tuple[str, float]:
    """Assign a speaker label based on the similarity score for speaker A and speaker B.

    Args:
        similarity_a: Cosine similarity to speaker A's profile embedding.
        similarity_b: Cosine similarity to speaker B's profile embedding.
        speaker_a_name: Label to return when speaker A is selected.
        speaker_b_name: Label to return when speaker B is selected.
        threshold: Minimum best similarity required to assign a known speaker.
        margin: Minimum score gap required to avoid an ambiguous assignment.

    Returns:
        A tuple containing the assigned label and numeric code. Speaker A uses
        ``-1.0``, speaker B uses ``1.0``, and unresolved labels use ``0.0``.

    """
    best_similarity = max(similarity_a, similarity_b)
    similarity_gap = abs(similarity_a - similarity_b)

    if best_similarity < threshold:
        return "unknown", 0.0
    if similarity_gap < margin:
        return "ambiguous", 0.0
    if similarity_a > similarity_b:
        return speaker_a_name, -1.0
    return speaker_b_name, 1.0


def run_inference(
    classifier: Any,
    audio_window: np.ndarray,
    vad_mask: np.ndarray,
    profile_a: SpeakerProfile,
    profile_b: SpeakerProfile,
    *,
    min_speech_ratio: float,
    energy_threshold: float,
    assignment_threshold: float,
    assignment_margin: float,
) -> InferenceStep:
    """Run speaker inference for one audio window.

    Args:
        classifier: SpeechBrain classifier used to extract speaker embeddings.
        audio_window: Audio samples for the window being classified.
        vad_mask: Voice activity mask aligned with ``audio_window``.
        profile_a: Speaker profile for the first reference speaker.
        profile_b: Speaker profile for the second reference speaker.
        min_speech_ratio: Minimum average VAD mask value required for inference.
        energy_threshold: Minimum RMS energy required to treat the window as
            non-silence.
        assignment_threshold: Minimum similarity required for known-speaker
            assignment.
        assignment_margin: Minimum similarity gap required to avoid ambiguity.

    Returns:
        An ``InferenceStep`` describing the similarities and assigned label.

    """
    rms = float(np.sqrt(np.mean(np.square(audio_window))))
    if rms < energy_threshold:
        return InferenceStep(0, 0, 0, 0, 0, "silence", 0)

    speech_ratio = np.average(vad_mask)
    if speech_ratio < min_speech_ratio:
        return InferenceStep(0, 0, 0, 0, 0, "ambiguous", 0)

    embedding = extract_embedding(classifier, audio_window)

    similarity_a = float(np.dot(embedding, profile_a.embedding))
    similarity_b = float(np.dot(embedding, profile_b.embedding))
    similarity_diff = float(np.clip(similarity_b - similarity_a, -1.0, 1.0))
    margin = abs(similarity_a - similarity_b)
    assigned_label, assigned_code = assign_speaker(
        similarity_a,
        similarity_b,
        profile_a.name,
        profile_b.name,
        threshold=assignment_threshold,
        margin=assignment_margin,
    )

    return InferenceStep(
        speech_ratio, similarity_a, similarity_b, similarity_diff, margin, assigned_label, assigned_code
    )


def write_csv(
    analysed_segments: list[InferenceStep], window_ms: int, step_ms: int, output_dir: Path, stem: str
) -> None:
    """Write per-window inference results to a CSV file.

    Args:
        analysed_segments: Inference results to write, in analysis order.
        window_ms: Analysis window length in milliseconds.
        step_ms: Step length between consecutive analysis windows in
            milliseconds.
        output_dir: Directory where the CSV file should be written.
        stem: Base filename used for the output CSV.

    """
    csv_path = output_dir / f"{stem}_results.csv"
    with csv_path.open("w") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(
            [
                "step_index",
                "start_s",
                "end_s",
                "speech_ratio",
                "similarity_speaker_a",
                "similarity_speaker_b",
                "similarity_diff",
                "margin",
                "assigned_label",
                "assigned_code",
            ]
        )
        for i, step in enumerate(analysed_segments):
            if i == 0:
                start_s = 0
                end_s = window_ms / 1000.0
            else:
                start_s = (window_ms + (i - 1) * step_ms) / 1000.0
                end_s = (window_ms + i * step_ms) / 1000.0
            writer.writerow(
                [
                    i,
                    start_s,
                    end_s,
                    step.speech_ratio,
                    step.similarity_a,
                    step.similarity_b,
                    step.similarity_diff,
                    step.margin,
                    step.assigned_label,
                    step.assigned_code,
                ]
            )


def write_tracks(
    analysed_segments: list[InferenceStep],
    audio_vad_mask: np.ndarray,
    window_samples: int,
    step_samples: int,
    sample_rate: int,
    output_dir: Path,
    stem: str,
) -> None:
    """Write inference scores and labels as sample-aligned audio tracks.

    Args:
        analysed_segments: Inference results to convert into continuous tracks.
        audio_vad_mask: Voice activity mask used to zero score tracks during
            non-speech regions.
        window_samples: Analysis window length in samples.
        step_samples: Step length between consecutive analysis windows in
            samples.
        sample_rate: Sample rate used when writing output files.
        output_dir: Directory where the audio tracks should be written.
        stem: Base filename used for the output tracks.

    """
    audio_length = len(audio_vad_mask)
    similarity_a_track = np.zeros(audio_length, dtype=np.float32)
    similarity_b_track = np.zeros(audio_length, dtype=np.float32)
    similarity_diff_track = np.zeros(audio_length, dtype=np.float32)
    label_track = np.zeros(audio_length, dtype=np.float32)

    for i, step in enumerate(analysed_segments):
        if i == 0:
            start_sample = 0
            stop_sample = window_samples
        else:
            start_sample = int(((i - 1) * step_samples) + window_samples)
            stop_sample = int(((i) * step_samples) + window_samples)
        similarity_a_track[start_sample:stop_sample] = step.similarity_a
        similarity_b_track[start_sample:stop_sample] = step.similarity_b
        similarity_diff_track[start_sample:stop_sample] = step.similarity_diff
        label_track[start_sample:stop_sample] = (
            -1 if step.assigned_label == "baji" else 1 if step.assigned_label == "lowji" else 0
        )

    # Set output to 0 where VAD says there's no speech
    similarity_a_track = similarity_a_track * audio_vad_mask
    similarity_b_track = similarity_b_track * audio_vad_mask
    similarity_diff_track = similarity_diff_track * audio_vad_mask

    save_file(output_dir / f"{stem}_vad.wav", audio_vad_mask, sample_rate)
    save_file(output_dir / f"{stem}_speaker_a_score.wav", similarity_a_track, sample_rate)
    save_file(output_dir / f"{stem}_speaker_b_score.wav", similarity_b_track, sample_rate)
    save_file(output_dir / f"{stem}_diff.wav", similarity_diff_track, sample_rate)
    save_file(output_dir / f"{stem}_label.wav", label_track, sample_rate)
