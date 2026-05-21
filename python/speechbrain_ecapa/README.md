# Speaker classification using pre-trained model by Speechbrain

This part of the project contains scripts that use pre-trained models provided by Speechbrain for classifying speakers in audio. The classification compares the similarity between the features of segments of audio with unknown speakers, with pre-created profiles of speakers. 

## Purpose

The purpose of these scripts is to have something to compare out purposefully trained custom models against. The `speechbrain/spkrec-ecapa-voxceleb` model is a general model trained on the Voxceleb dataset, and our assumption is that a model trained specifically with the purpose of classifying audio between Baji and Lowji will perform better. It is still good to have something to compare against.

## Scripts

### Create speaker profile

This script creates a speaker profile based on known audio clips of the speaker. It's possible to either input a list of audio files, from which the script automatically extracts audio segments, or provide a CSV-file which describes curated audio segments.

The produced speaker profile is basically just a unit vector that points toward the geometric mean of the features extracted from the audio segments, in addition to some metadata.

```bash
uv run speechbrain-create-profile  --inputs <path to audio file> --speaker <name of speaker> --output <path to profile.npz>
```

### Analyse audio

The `analyse-audio.py` script analyses an audio file, and attempts to classify any spoken segments by compairing the characteristics of these segments with the characteristics of two provided speaker profiles.

The script outputs a csv file with the result for temporal step, as well as .wav files with a few of the outputs. These include the similiarity between audio and profile a, audio and profile b, the difference in similarity, and encoding of assigned speaker label. These .wav files can be imported into software like Audacity for easier inspection (though the tracks should be muted and not played).

Configurations can be passed into the script directly or by changing values in `config.py`. By default a window of 1500 ms used, with a step size of 200 ms.

```bash
uv run speechbrain-analyse-audio --input-path <path to audio file> --output-dir <path to folder where output will be written>
```

## Technical description

The scripts use SpeechBrain's `speechbrain/spkrec-ecapa-voxceleb` model, a pre-trained ECAPA-TDNN speaker-recognition model. ECAPA-TDNN extracts a fixed-length embedding from an audio clip, where clips from the same speaker should produce embeddings that point in similar directions. The model was trained on VoxCeleb, so it is used here as a general-purpose speaker embedding baseline rather than a model specifically trained for Baji and Lowji.

Speaker profiles are built by extracting embeddings from known examples of a speaker, averaging them, and normalizing the result to a unit vector. During analysis, each audio window is embedded in the same way and compared with the two speaker profiles using cosine similarity. Cosine similarity measures the angle between two normalized vectors: values closer to `1.0` mean the window is more similar to the profile, values near `0.0` mean little similarity, and the higher-scoring profile is selected when the score and margin are above the configured thresholds.
