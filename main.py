import time

import librosa
import numpy as np
import pyaudio
import torch
from dotenv import dotenv_values

# ml testing
from pyannote.audio import Pipeline
from pyannote.audio.pipelines.utils.hook import ProgressHook

config = dotenv_values(".env")
n_fft = 512
hop = (int)(512 / 4)


class Thing:
    def process(self, data):
        S = librosa.stft(data, n_fft=n_fft)
        freqs = librosa.fft_frequencies(sr=self.RATE, n_fft=n_fft)

        print(self.test(S, freqs, 380))

    def test(self, S, freqs, split):
        split_bin = np.argmax(freqs >= split)

        lowband = S[:split_bin, :]
        highband = S[split_bin:, :]

        y_low = librosa.istft(lowband, hop_length=hop)
        y_high = librosa.istft(highband, hop_length=hop)

        low = np.average(y_low)
        high = np.average(y_high)

        if low > high:
            return "low"
        else:
            return "high"

    def __init__(self):
        self.FORMAT = pyaudio.paFloat32
        self.CHANNELS = 1
        self.RATE = 44100
        self.CHUNK = 44100
        self.p = None
        self.stream = None
        # ml testing
        self.pipeline = Pipeline.from_pretrained(
            "pyannote/speaker-diarization-community-1",
            token=config["HUGGING"],
        )
        # self.pipeline.to(torch.device("cuda"))

    def start(self):
        self.p = pyaudio.PyAudio()
        self.stream = self.p.open(
            format=self.FORMAT,
            channels=self.CHANNELS,
            rate=self.RATE,
            input=True,
            output=False,
            stream_callback=self.callback,
            frames_per_buffer=self.CHUNK,
        )

    def stop(self):
        self.stream.close()
        self.p.terminate()

    def callback(self, in_data, frame_count, time_info, flag):
        # print(frame_count, time_info, flag)
        data = np.frombuffer(in_data, dtype=np.float32)
        # self.process(data)

        # ml testing
        test = torch.tensor(data).reshape((2, -1))
        output = self.pipeline(
            {"waveform": test, "sample_rate": self.RATE}
        )  # runs locally

        print(output)

        return None, pyaudio.paContinue

    def waiter(self):
        while self.stream.is_active():
            time.sleep(2.0)


audio = Thing()
audio.start()  # open the mic stream
audio.waiter()
audio.stop()
