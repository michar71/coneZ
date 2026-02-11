We want to develop a software called "MAYHEM" that acts as a development tool for lighting and special effects to sync them with audio or video.
"MAYHEM" will be developed in C# using the Avalonia UI framework.

Workflow:

The workflow is as follows:
  - User opens sound or video file in app
  - User can set cue (timing) marks in the file (Or they can be auto-detected by AI) that can be used to align/snap effects to. (But effects don't nessessarily have to align with cue marks)
  - User can drag/drop effects onto a timeline with multiple channels (These could be scripts or media or just soldid colors or predefined effects)
  - User can set parameters for effects
  -The project can be saved and loaded (As a JSON file)
  - At the end of the workflow the user exports the project into a device-specific cue list and compiled scripts.


Layout:

In general the layout of the app follows other apps that are timeline centric (For example Abelton for Music or Adobe Premiere for Video).

The Layout of the app shall look as follows:
- Top menu with System Specific entries where load/save Project and import media and project properties can be accessed as well as standard functions like copy/paste for effects.
- A top bar with the main controls. These are 
  - A 'Play/Pause' button to start and stop media playback.
  - A 'Cue' button to set cue marks
  - A Timecode Display in HH:MM:SS:MS that shows the playhead position
  - A +/- slider to zoom the timeline in/out.
- Status bar and debug/command log at the bottom.
- On the left side a panel that has mutliple tabs:
    - A file broswer to show a project directory (showing for example basic and C scripts) Double-clicking a file opens it in a text editor where it ca nbe changed and saved.
    - A Topic-based browser for internal effects. (Fixed FX or Color)
    - A project setup page
- On the right is a panel that shows proerties for effects. These proerties are exposed by the effect itself when clicking on an effect in the timeline and can be edited in that panel.
- In the middle is the timeline. This is where the user can arrange effects in relationship to the cue-marks and the media in different channels.
   - It has a playhead at the top that can be moved to change the playback position.
   - At the bottom it either shows the waveform of the media file or images from a video file.
   - Above it it shows the channels. The number of channels can be changed in the Project setup. 
   - Channels are identiefied by name at the left side and the name can be changed by double-clicking it.
   - At the top of the timeline are timing tick-marks.

- Panels on the left and right and the status at the bottom can be resized.

Functionality:

- When the user imports media (Audio or video) the timeline is limnited in length to the duration of the media
- After opening the media it is rendered out either as a waveform display or inidivudal video frames in the media row at the bottom.
- The user can play back the imported media via the play/pause button (Or pressing the space-bar)
- The user can srub through the media by moving the playhead at the top. 
- The user can soom the timeline with the slider which rerenders it.
- The user can set cue marks by pressing the "Cue" button or 'C'. This will insert a Vertical line on the timeline. 
- Cue lines can be selected with the mouse.
- Cue marks can be moved by mouse.
- Cue marks can be deleted by selecting them and pressing "Delete"

- Effects can be dragged over from either the file browswer or the effect browswer and can be dropped into one of the cahnnels 
- When an effect is selected its properties are shown in the proerty-panel.
- Effects automatically snap to cue lines if blaced with the left side next to them
- Effects can be selected with the mouse
- effects can be deleted by context menu or pressing "delete"
- Effects can be moved around horizontally or between channels by drag/drop
- Effect duration can be changed with the mouse by grabbing the right side and making it wider/narrower.
- At a minimum each effect has the following properties
   - Type (Shoewn but not editable)
   - Start Time (in ms)
   - Duration (in ms)


Inital Scope:
- This is currently  a prototype. Start by implementing the general layout.
- Implement the loading and rendering of media
- Implement the playback/pause and playhead scrubbing for the media.
- Implement the cue system (Allow users to insert/delete cue-lines and move them around)
- Implement dragging/dropping effects from the file browser andf rearranging them on the channels
- Implement the parameters panel

Ask questions if the spec is too unclear to remove uncertainty.
We'll evaluate the next steps after this functionality has been implemented.