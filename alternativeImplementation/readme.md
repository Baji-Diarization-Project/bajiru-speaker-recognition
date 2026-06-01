# Thanks for reading me!
 
So this is the implementation by Temujin142857 aka @ThePhilosopher and Henry52 aka @WaifuSimulator

For convienience we've been working out of a collab notebook while we do some testing and get something decent cooking. Integrating our code properly into the other folders will be a task I set to the side until/if we cook up something worth integrating. 

I'll update this branch periodically when we have a stable release of sorts, and I'll include a patch note md file with each update.

## Our Approach

Our general approach is to have some processing steps for the audio, then pass it into a CNN for classification.
Areas we plan to test include:
- MFCC vs mel-spectrogram representations for the audio. Hypothosis is the mel-spectrogram will perform much better since it preserves more information and it's structure matches with the strengths of the CNN. Still think it might be worth testing just in case.
- Applying a High/Low pass filter to the audio to clean it before passing to the model. Ngl I forget which one it was that another friend recommended we try, they work more with audio then I do.
- other things that I'll add as we come up with them

Anyways thanks for coming to my Ted Talk, I'll drop a short summary in the discord when we push updates, so keep an eye out for those I suppose :].