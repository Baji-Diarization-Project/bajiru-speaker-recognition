import time

import librosa
import numpy as np
import pyaudio

# import torch
from dotenv import dotenv_values

# ml testing
# from pyannote.audio import Pipeline
# from pyannote.audio.pipelines.utils.hook import ProgressHook

config = dotenv_values(".env")
n_fft = 512
hop = (int)(512 / 4)


class Thing:
    def process(self, data):
        ftData = librosa.stft(data, n_fft=n_fft)
        freqs = librosa.fft_frequencies(sr=self.RATE, n_fft=n_fft)

        peakAmplitude = np.max(np.abs(data))  # Peak volume in amplitude
        peakDb = librosa.amplitude_to_db(peakAmplitude, ref=1.0)

        print(peakAmplitude, peakDb)
        if peakDb < -45:
            return print("below min db skipping")
        print("result", self.test(ftData, freqs, 400))

    def test(self, ftData, freqs, split):
        lowBand = np.argmax(freqs >= split)
        midBand = np.argmax(freqs >= 8000)

        zeroToSplit = ftData[:lowBand, :]
        splitToEightK = ftData[lowBand:midBand, :]
        # print("low", zeroToSplit)
        # print("high", splitToEightK)

        # figure out why amplitude doesn't convert to proper db (above 0)
        lowPeak = librosa.amplitude_to_db(np.max(np.abs(zeroToSplit)), ref=1.0)
        highPeak = librosa.amplitude_to_db(np.max(np.abs(splitToEightK)), ref=1.0)

        # reconstruct real/imaginary parts from magnitude and phase

        spec = np.abs(ftData)
        freqs = librosa.core.fft_frequencies(n_fft=n_fft)
        # times = librosa.core.frames_to_time(
        #     spec[0], sr=self.RATE, n_fft=n_fft, hop_length=hop
        # )
        # for i, _ in freqs:
        #     for t, _ in times:
        #         print("freq (Hz)", freqs[i])
        #         print("time (s)", times[t])
        # print("amplitude", spec[12, 1000])
        # y_low = librosa.istft(zeroToSplit, hop_length=hop)
        # y_high = librosa.istft(splitToEightK, hop_length=hop)

        # low = np.average(lowPeak)
        # high = np.average(highPeak)
        print("low: ", lowPeak, "high: ", highPeak)

        if lowPeak > highPeak:
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
        # self.pipeline = Pipeline.from_pretrained(
        #     "pyannote/speaker-diarization-community-1",
        #     token=config["HUGGING"],
        # )
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
        self.process(data)

        # ml testing
        # test = torch.tensor(data).reshape((2, -1))
        # output = self.pipeline(
        # {"waveform": test, "sample_rate": self.RATE}
        # )  # runs locally

        # print(output)

        return None, pyaudio.paContinue

    def waiter(self):
        while self.stream.is_active():
            time.sleep(2.0)


audio = Thing()
audio.start()  # open the mic stream
audio.waiter()
audio.stop()
