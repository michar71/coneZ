We want to develop a software in processing/java that manages mostly lighting effects.

it shall have a time-line that can be zooomed in/out via buttons.
The timeline is based on a song.

The timeline/song can be opened and then played/paused. Keys can be used to set cue marks during playyback.

There is aply-head that can be moved around. 

There is a timecode diaply that shows the current time in HH:MM:SS:MS.

The timeline can have mulrtiple channels stacked on top of each other.

On the left is a list of possible effects. These are either C or badsic scripts in a directory.
They casn be dragged and dropped into the timeline and aligned with the cue marks.

On the right the parameters for each script csan be changed when highlighted/selected.

Double clicking on a scirpt either in the list or the timeline opens the script for editing.
There is also a button on top of the script list to generate a new script.

The whole project chan be saved with all the info in a JSON file. (And loaded again).
It contains links to the scripts used, time positions, links to the sound file and info about the cue marks.

