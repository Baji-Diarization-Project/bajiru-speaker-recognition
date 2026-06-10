# Version 1

Switching from MFCC to mel-spectrogram representation of the audio.

## What does it do?

Basically MFCCs are a more compressed way to represent audio, which decorralates the audio in a way which is sometimes useful, but not in this case. Using the mel-spectrogram preserves more information and more of the structure, Pretty predictable that this is better, but I didn't know for sure until I tried it.

## What's next?

the testing setup currently doesn't match the continuous stream of input audio that the program will eventually need to handle, modifying that is on my todo list.

Also currently only using 1 dimensional features (the spectrogram), will look into a 3d setup with chromograms and some other representation as other dimensions

Will be looking into possible pre-processing of the audio for improved accuracy (will consider the time cost if it does in fact impprove accuracy)