"""Create a speaker profile from audio files or manually selected segments.

The script extracts ECAPA speaker embeddings from selected clips, averages them
into one speaker profile, and saves the profile as an ``.npz`` file with the
settings used to create it.
"""

import argparse
from pathlib import Path

from speechbrain_ecapa.config import (
    MERGE_SILENCE_MS,
    MIN_ENERGY_THRESHOLD,
    MIN_SPEECH_RATIO,
    SAMPLE_RATE,
    STREAM_BLOCK_MS,
    VAD_THRESHOLD,
    WINDOW_DURATION_MS,
)
from speechbrain_ecapa.model import load_model
from speechbrain_ecapa.speaker_profile import create_profile_from_segments, save_profile
from speechbrain_ecapa.speech_segments import create_segments_from_paths, load_segments_from_csv


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments for speaker profile creation.

    Returns:
        Parsed command-line arguments containing the speaker name, profile
        source, output path, and clip selection settings.

    """
    parser = argparse.ArgumentParser(
        description=("Create a speaker profile based on an audio file, or on a csv with manually created segments")
    )
    parser.add_argument("--speaker", required=True, help="Name of speaker the profile is created for")
    input_group = parser.add_mutually_exclusive_group(required=True)
    input_group.add_argument(
        "--inputs",
        type=Path,
        nargs="+",
        help="One or more audio files that will be used to create the speaker profile",
    )
    input_group.add_argument(
        "--segments-csv",
        type=Path,
        help=("CSV of manually selected audio segments. Required columns: speaker,file,start_s,end_s"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="Output .npz profile path.",
    )
    parser.add_argument("--clip-ms", type=float, default=WINDOW_DURATION_MS)
    parser.add_argument("--max-clips-per-file", type=int, default=20)

    return parser.parse_args()


def main() -> None:
    """Run the speaker profile creation command-line workflow."""
    args = parse_args()

    classifier = load_model()

    if args.segments_csv is not None:
        segments = load_segments_from_csv(args.segments_csv, args.speaker)
    else:
        segments = create_segments_from_paths(
            args.inputs, args.speaker, clip_ms=args.clip_ms, max_clips=args.max_clips_per_file
        )

    print("Audio segments loaded")
    profile = create_profile_from_segments(classifier, segments, args.speaker)
    save_profile(
        profile,
        args.output,
        sample_rate=SAMPLE_RATE,
        clip_ms=args.clip_ms,
        max_clips_per_file=args.max_clips_per_file,
        vad_frame_ms=STREAM_BLOCK_MS,
        vad_threshold=VAD_THRESHOLD,
        min_speech_ratio=MIN_SPEECH_RATIO,
        min_energy_threshold=MIN_ENERGY_THRESHOLD,
        merge_gap_ms=MERGE_SILENCE_MS,
    )

    print("Saved profile")
    print(f"  name={profile.name}")
    print(f"  clips={profile.clip_count}")
    print(f"  output={args.output}")


if __name__ == "__main__":
    main()
