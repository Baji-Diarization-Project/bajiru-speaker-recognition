from nn_model.augmentation import BinShiftAugment, NoiseAugment, VolumeAugment
from nn_model.model import AudioClassifier
from nn_model.preprocessing import MelCompressor

sample_rate = 48000
segment_samples = 24000
win_length = 2400
hop_length = 800
n_classes = 5
model = AudioClassifier(
    sample_rate=sample_rate,
    n_fft=win_length,
    hop_length=hop_length,
    n_classes=n_classes,
    n_heads=4,
    n_attns=3,
    d_model=128,
    n_mels=320,
    n_channels=128,
    n_convs=3,
    n_history=1 + (segment_samples - win_length) // hop_length,
    mel_prep=[
        MelCompressor(
            threshold=-54,
            ratio=8,
            knee=40,
            attack_frames=(1 / 1000) * (sample_rate / hop_length),  # 1ms attack
            release_frames=(40 / 1000) * (sample_rate / hop_length),  # 40ms release
            makeup=30,
        )
    ],
    mel_augm=[
        # Optional augmentations
        BinShiftAugment(2),
        VolumeAugment(0.5),
        NoiseAugment(0.005),
    ],
)
