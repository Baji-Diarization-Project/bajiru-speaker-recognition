import pathlib

import librosa
import numpy as np
import pytest

from baji_recognition import main


@pytest.fixture
def data_dir():
    return pathlib.Path(__file__).parent / "data"


def load_and_process(file):
    in_data = librosa.load(file)[0]
    data = np.frombuffer(in_data, dtype=np.float32)
    return main.process(data, main.RATE)


def test_low_voice(data_dir):
    low_voice = data_dir / "low.mp3"

    res = load_and_process(low_voice)

    assert res == "low"


def test_normal_voice(data_dir):
    normal_voice = data_dir / "normal.mp3"

    res = load_and_process(normal_voice)

    assert res == "high"
