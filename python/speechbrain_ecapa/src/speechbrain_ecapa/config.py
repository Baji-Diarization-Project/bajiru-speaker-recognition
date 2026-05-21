"""Default configuration values for ECAPA speaker recognition."""

MODEL_ID: str = "speechbrain/spkrec-ecapa-voxceleb"
"""ID of the speechbrain model used for inference"""

SAMPLE_RATE: int = 16000
"""Sample rate in Hz. 'speechbrain/spkrec-ecapa-voxceleb' is trained on 16 MHz, same for VAD implementation"""

STREAM_BLOCK_MS: int = 20
"""Audio stream block size in milliseconds, used for VAD analysys etc."""

WINDOW_DURATION_MS: int = 1500
"""Length of the rolling classification window in milliseconds."""

VAD_THRESHOLD: float = 0.5
"""Silero VAD speech-probability threshold (0.0-1.0). Higher values are stricter."""

MIN_SPEECH_RATIO: float = 0.1
"""The minimum amount of VAD-recognized speech in the rolling window for classification to be attempted"""

MIN_ENERGY_THRESHOLD: float = 0.001
"""Minimum RMS value for inference to be done, or enrollment clip to be used"""

MERGE_SILENCE_MS: int = 200
"""How long silences can be between speaking segments, and still be considered speaking"""

LABELS: dict[int, str] = {0: "LOW VOICE", 1: "HIGH VOICE"}
"""Human-readable class names keyed by integer label index."""
