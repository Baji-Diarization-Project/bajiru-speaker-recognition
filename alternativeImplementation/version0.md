# Version 0

This is basically us porting over the approach from a previous speech recognition project Henry52 worked on with only simple modifications. 

## What does it do?

It loads the Baji file, the Ru file, remove silence, processes the results into their mfcc's, trains off them, then uses the mixed file for testing. 

## What's next?

Switching to a mel-spectrogram representation, should be way better than the MFCC respresention.

Additionally our testing setup currently doesn't match the continuous stream of input audio that the program will eventually need to handle, modifying that is on my todo list.
