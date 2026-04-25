"""Analyse an audio file and export inference results.

The script loads an input audio file, computes a voice activity mask, runs
sliding-window speaker inference against two speaker profiles, and writes the
results as CSV and WAV tracks.
"""

import argparse
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from speechbrain_ecapa.audio import load_file
from speechbrain_ecapa.config import MIN_ENERGY_THRESHOLD, MIN_SPEECH_RATIO, SAMPLE_RATE, WINDOW_DURATION_MS
from speechbrain_ecapa.inference import run_inference, write_csv, write_tracks
from speechbrain_ecapa.model import load_model
from speechbrain_ecapa.speaker_profile import load_profile
from speechbrain_ecapa.vad import compute_vad_mask


def _parse_args() -> argparse.Namespace:
    """Parse command-line arguments for audio analysis.

    Returns:
        Parsed command-line arguments containing input paths, speaker profile
        paths, inference thresholds, window settings, and the output directory.

    """
    parser = argparse.ArgumentParser(
        description=("Script for analysing, classifying and labling two speakers in an audio file")
    )
    parser.add_argument(
        "--input-path",
        type=Path,
        required=True,
        help="Input audio file to analyse.",
    )
    parser.add_argument(
        "--speaker-a-profile",
        type=Path,
        default=Path(__file__).resolve().parent.parent.joinpath("resources", "baji_profile_example.npz"),
        help="Profile for speaker A.",
    )
    parser.add_argument(
        "--speaker-b-profile",
        type=Path,
        default=Path(__file__).resolve().parent.parent.joinpath("resources", "lowji_profile_example.npz"),
        help="Profile for speaker B.",
    )
    parser.add_argument("--sample-rate", type=int, default=SAMPLE_RATE)
    parser.add_argument(
        "--window-ms",
        type=float,
        default=WINDOW_DURATION_MS,
        help="Size of the sliding window inference is done on",
    )
    parser.add_argument(
        "--step-ms",
        type=float,
        default=200.0,
        help="Inference hop size in milliseconds.",
    )
    parser.add_argument(
        "--energy-threshold",
        type=float,
        default=MIN_ENERGY_THRESHOLD,
        help="Threshold for average energy in the sliding window. If energy below the threshold, inference is not done",
    )
    parser.add_argument(
        "--min_speech_ratio",
        type=float,
        default=MIN_SPEECH_RATIO,
        help="Minimum amount of speech in the sliding window for inference to be done",
    )
    parser.add_argument(
        "--assignment-threshold",
        type=float,
        default=0.2,
        help="Minimum best-speaker similarity required for assignment.",
    )
    parser.add_argument(
        "--assignment-margin",
        type=float,
        default=0.05,
        help="Minimum similarity gap between the top two speakers.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        required=True,
        help="Directory for CSV and WAV outputs.",
    )
    return parser.parse_args()


@dataclass(frozen=True)
class AudioWindow:
    """Audio samples and VAD mask for one analysis window.

    Attributes:
        audio: Audio samples in the analysis window.
        vad_mask: Voice activity mask values aligned with ``audio``.

    """

    audio: np.ndarray
    vad_mask: np.ndarray


def main() -> None:
    """Run the audio analysis command-line workflow."""
    args = _parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    classifier = load_model()

    profile_a = load_profile(args.speaker_a_profile)
    profile_b = load_profile(args.speaker_b_profile)

    audio = load_file(args.input_path)

    print("Classifier, profiles and audio loaded.")

    _, audio_vad_mask = compute_vad_mask(audio, args.sample_rate)

    window_samples = round(args.window_ms * args.sample_rate / 1000)
    step_samples = round(args.step_ms * args.sample_rate / 1000)
    audio_windows = [
        AudioWindow(audio[i : i + window_samples], audio_vad_mask[i : i + window_samples])
        for i in range(0, len(audio), step_samples)
    ]

    print(f"Starting inference on {len(audio_windows)} audio windows.")

    analysed_segments = [
        run_inference(
            classifier,
            audio_window.audio,
            audio_window.vad_mask,
            profile_a,
            profile_b,
            min_speech_ratio=args.min_speech_ratio,
            energy_threshold=args.energy_threshold,
            assignment_threshold=args.assignment_threshold,
            assignment_margin=args.assignment_margin,
        )
        for audio_window in audio_windows
    ]

    stem = args.input_path.stem.replace(" ", "_").lower()
    write_tracks(
        analysed_segments, audio_vad_mask, window_samples, step_samples, args.sample_rate, args.output_dir, stem
    )
    write_csv(analysed_segments, args.window_ms, args.step_ms, args.output_dir, stem)

    print(f"Analysis complete, and output written to {args.output_dir}")


if __name__ == "__main__":
    main()
