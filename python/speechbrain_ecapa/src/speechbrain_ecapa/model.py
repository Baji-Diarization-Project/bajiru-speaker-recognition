
"""Utilities for loading ECAPA speaker models and extracting embeddings."""

from pathlib import Path
from typing import Any

import numpy as np
import torch
from speechbrain.inference.speaker import EncoderClassifier
from torch.nn.functional import normalize

from speechbrain_ecapa.config import MODEL_ID


def load_model(model_name: str = MODEL_ID) -> Any:
    """Load a SpeechBrain ECAPA speaker embedding model.

    Args:
        model_name: Model identifier or local model source.

    Returns:
        An ``EncoderClassifier`` initialized from the requested model.

    """
    model_cache_dir = Path(__file__).resolve().parents[2] / "pretrained_models" / model_name
    return EncoderClassifier.from_hparams(source=model_name, savedir=str(model_cache_dir))


def extract_embedding(model: Any, waveform: np.ndarray) -> np.ndarray:
    """Extract a normalized speaker embedding from a waveform.

    Args:
        model: SpeechBrain model used to extact embeddings.
        waveform: One-dimensional audio waveform as a NumPy array.

    Returns:
        A float32 NumPy array containing the L2-normalized speaker embedding.

    """
    signal = torch.from_numpy(waveform).unsqueeze(0)
    lengths = torch.tensor([1.0], dtype=torch.float32)

    with torch.no_grad():
        embedding = model.encode_batch(signal, lengths)

    embedding = embedding.squeeze()
    embedding = normalize(embedding, p=2, dim=0)  # Create unit vector

    return embedding.cpu().numpy().astype(np.float32, copy=False)
