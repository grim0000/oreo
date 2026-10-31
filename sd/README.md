# SD card files

Copy the `sesame` folder to the root of a FAT32 microSD card.

## Wake word: "hey Oreo"

`sesame/wake.tflite` is the model the robot loads at start (microWakeWord, streaming, int8, 62 KB).
To use a different model, replace that file and restart the robot. You don't need to rebuild the firmware.

| Model | Trained on | Cutoff to use |
|---|---|---|
| `wake_models/hey_oreo_real.tflite` (the default `wake.tflite`) | generated voices, then fine-tuned on about 40 real takes from one speaker through the robot's own mic | `0.98` |
| `wake_models/hey_oreo_synthetic.tflite` | generated (Piper TTS) voices only: 86 % recall at 0 false wakes/hour on the test set, but weaker on real voices | `0.5` |

The firmware starts at 0.98, which suits the default model. To change it (it is saved on the robot):

    http://<robot-ip>/gemini?ww=1&wwcut=0.98&wwwin=5

`/gemini` shows `wwState`, `wwNow` (the current score), `wwMax` and `wwHits`. If the default model misses your
voice, lower `wwcut` a little or try the synthetic model. If it wakes by itself, raise it.

For your own phrase or voice, train a model with [microWakeWord](https://github.com/kahrendt/microWakeWord)
(the robot expects its standard 40-feature, 10 ms-step streaming model) and save it as `sesame/wake.tflite`.
