// Mayhem prototype sketch (Processing)
// Basic timeline, effects list, params panel, cue marks, playhead, save/load
import processing.sound.*;
import java.io.File;
import controlP5.*;

// --- Timeline ---
float songLengthSec = 180.0; // 3 min placeholder
float playheadSec = 0;
boolean playing = false;
float zoom = 1.0; // 1.0 = full song fits
SoundFile song = null;
String songPath = "";

// cues
ArrayList<Float> cues = new ArrayList<Float>();

// effects list
ArrayList<EffectItem> effects = new ArrayList<EffectItem>();

// clips on timeline
ArrayList<Clip> clips = new ArrayList<Clip>();
Clip selectedClip = null;

// drag state
EffectItem draggingEffect = null;
float dragX, dragY;
Clip draggingClip = null;
float dragClipOffsetSec = 0;

// UI
ControlP5 cp5;
controlP5.Button uiOpen, uiPlay, uiCue, uiZoomIn, uiZoomOut, uiSong, uiSave, uiLoad, uiNewScript;
controlP5.Slider uiIntensity, uiSpeed, uiHue;

float zoomOutX = 0;
float zoomOutW = 0;
float saveX = 0;

// status
String statusMsg = "Ready";
int statusTimer = 0;

// double click tracking
int lastClickTime = 0;
Clip lastClickClip = null;
EffectItem lastClickEffect = null;

void setup() {
  surface.setTitle("Mayhem Prototype");
  textFont(createFont("SansSerif", 12));
  cp5 = new ControlP5(this);
  cp5.setAutoDraw(false);
  cp5.setFont(createFont("SansSerif", 12));
  initEffects();
  initControls();
  updatePlayButtonColor();
}

void draw() {
  background(20);

  updatePlayback();
  drawTopBar();
  drawLeftPanel();
  drawRightPanel();
  drawTimeline();
  drawBottomBar();
  cp5.draw();
  drawDragGhost();
}

void updatePlayback() {
  if (playing) {
    if (song != null && song.isPlaying()) {
      playheadSec = song.position();
    } else {
      playheadSec += (1.0 / frameRate);
    }
    if (playheadSec > songLengthSec) {
      playheadSec = songLengthSec;
      playing = false;
      if (song != null) song.pause();
      updatePlayButtonColor();
    }
  }
}

// --- UI Drawing ---
void drawTopBar() {
  fill(30);
  noStroke();
  rect(0, 0, width, settings.topH);

  // timecode
  String tc = formatTimecode(playheadSec);
  fill(230);
  textAlign(CENTER, CENTER);
  textSize(24);
  float leftEdge = zoomOutX + zoomOutW + 100;
  float rightEdge = saveX - 16;
  float tcX = (leftEdge + rightEdge) * 0.5;
  text(tc, tcX, (settings.topH / 2.0)- 6);
}

void drawLeftPanel() {
  fill(26);
  noStroke();
  rect(0, settings.topH, settings.leftW, height - settings.topH - settings.bottomH);

  // header
  fill(200);
  textAlign(LEFT, TOP);
  text("Effects", 12, settings.topH + 26);

  float y = settings.topH + 46;
  for (EffectItem e : effects) {
    e.draw(0, y, settings.leftW, 24);
    y += 26;
  }
}

void drawRightPanel() {
  int x = width - settings.rightW;
  fill(26);
  noStroke();
  rect(x, settings.topH, settings.rightW, height - settings.topH - settings.bottomH);

  fill(200);
  textAlign(LEFT, TOP);
  text("Parameters", x + 12, settings.topH + 26);

  float y = settings.topH + 46;
  if (selectedClip == null) {
    fill(140);
    text("Select a clip to edit parameters", x + 12, y);
    setParamVisibility(false);
    return;
  }

  fill(220);
  text("Script: " + selectedClip.effect.name, x + 12, y);
  setParamVisibility(true);
}

void setParamVisibility(boolean visible) {
  if (uiIntensity == null) return;
  uiIntensity.setVisible(visible);
  uiSpeed.setVisible(visible);
  uiHue.setVisible(visible);
}

void drawTimeline() {
  int x = settings.leftW;
  int y = settings.topH;
  int w = width - settings.leftW - settings.rightW;
  int h = height - settings.topH - settings.bottomH;

  fill(22);
  noStroke();
  rect(x, y, w, h);

  // grid time scale
  drawTimeScale(x, y, w);

  // channels
  float cy = y + 28;
  for (int i = 0; i < settings.channelCount; i++) {
    float chY = cy + i * (settings.channelH + settings.channelGap);
    fill(28);
    rect(x + 8, chY, w - 16, settings.channelH);
    fill(120);
    textAlign(LEFT, CENTER);
    text("CH " + (i + 1), x + 12, chY + settings.channelH / 2);
  }

  // cues
  stroke(80, 200, 255);
  for (Float c : cues) {
    float px = timeToX(c, x, w);
    line(px, y, px, y + h);
  }

  // clips
  for (Clip c : clips) {
    c.draw(x, y, w);
  }

  // playhead
  float phx = timeToX(playheadSec, x, w);
  stroke(255, 80, 80);
  line(phx, y, phx, y + h);
}

void drawTimeScale(int x, int y, int w) {
  // scale background
  fill(18);
  rect(x, y, w, 24);

  stroke(60);
  int ticks = 10;
  for (int i = 0; i <= ticks; i++) {
    float t = map(i, 0, ticks, 0, songLengthSec / zoom);
    float px = timeToX(t, x, w);
    line(px, y + 24, px, y + 28);
    fill(130);
    textAlign(CENTER, TOP);
    text(formatTimeLabel(t), px, y + 4);
  }
}

void drawBottomBar() {
  fill(30);
  noStroke();
  rect(0, height - settings.bottomH, width, settings.bottomH);

  drawSongStrip();

  fill(180);
  textAlign(LEFT, CENTER);
  text("Status: " + statusMsg, 12, height - settings.bottomH / 2.0);
}

void drawSongStrip() {
  int x = settings.leftW;
  int y = height - settings.bottomH + 4;
  int w = width - settings.leftW - settings.rightW;
  int h = settings.bottomH - 8;

  fill(24);
  rect(x, y, w, h, 4);

  stroke(80);
  int ticks = 12;
  for (int i = 0; i <= ticks; i++) {
    float t = map(i, 0, ticks, 0, songLengthSec);
    float px = timeToXFull(t, x, w);
    line(px, y + h - 6, px, y + h);
  }

  // playhead marker
  float phx = timeToXFull(playheadSec, x, w);
  stroke(255, 80, 80);
  line(phx, y, phx, y + h);
}

void drawDragGhost() {
  if (draggingEffect == null) return;
  fill(60, 160, 255, 160);
  noStroke();
  rect(dragX - 60, dragY - 12, 120, 24, 4);
  fill(10);
  textAlign(CENTER, CENTER);
  text(draggingEffect.name, dragX, dragY);
}

// --- Input ---
void mousePressed() {
  // effects list
  EffectItem e = hitEffect(mouseX, mouseY);
  if (e != null) {
    draggingEffect = e;
    dragX = mouseX;
    dragY = mouseY;
    handleDoubleClickEffect(e);
    return;
  }

  // timeline clips
  Clip c = hitClip(mouseX, mouseY);
  if (c != null) {
    selectedClip = c;
    syncParamSliders();
    if (inTimeline(mouseX, mouseY)) {
      draggingClip = c;
      dragClipOffsetSec = xToTime(mouseX, settings.leftW, width - settings.leftW - settings.rightW) - c.startSec;
    }
    handleDoubleClickClip(c);
    return;
  }

  // move playhead if clicked in timeline
  if (inTimeline(mouseX, mouseY)) {
    float t = xToTime(mouseX, settings.leftW, width - settings.leftW - settings.rightW);
    seekPlayhead(t, true);
  }
}

void mouseDragged() {
  if (draggingEffect != null) {
    dragX = mouseX;
    dragY = mouseY;
  }
  if (draggingClip != null) {
    float t = xToTime(mouseX, settings.leftW, width - settings.leftW - settings.rightW) - dragClipOffsetSec;
    t = constrain(t, 0, songLengthSec);
    t = snapToCue(t);
    draggingClip.startSec = t;
    int ch = timelineChannelAt(mouseY);
    if (ch >= 0) draggingClip.channel = ch;
  }
}

void mouseReleased() {
  if (draggingEffect != null && inTimeline(mouseX, mouseY)) {
    int ch = timelineChannelAt(mouseY);
    if (ch >= 0) {
    float t = xToTime(mouseX, settings.leftW, width - settings.leftW - settings.rightW);
      float snapped = snapToCue(t);
      Clip c = new Clip(draggingEffect, ch, snapped, 6.0);
      clips.add(c);
      selectedClip = c;
      syncParamSliders();
      status("Dropped " + draggingEffect.name + " on CH " + (ch + 1));
    }
  }
  draggingEffect = null;
  draggingClip = null;
}

void keyPressed() {
  if (key == ' ') {
    togglePlay();
  } else if (key == 'c' || key == 'C') {
    cues.add(playheadSec);
    status("Cue added at " + formatTimeLabel(playheadSec));
  } else if (key == 's' || key == 'S') {
    saveProject("mayhem_project.json");
  } else if (key == 'l' || key == 'L') {
    loadProject("mayhem_project.json");
  }
}

// --- Logic ---
void togglePlay() {
  playing = !playing;
  if (song != null) {
    if (playing) {
      song.play();
    } else {
      song.pause();
    }
  }
  status(playing ? "Play" : "Pause");
  if (uiPlay != null) uiPlay.setLabel(playing ? "Pause" : "Play");
  updatePlayButtonColor();
}

void seekPlayhead(float t, boolean restartIfPlaying) {
  playheadSec = constrain(t, 0, songLengthSec);
  if (song != null) {
    boolean wasPlaying = playing;
    if (restartIfPlaying && wasPlaying) song.pause();
    song.cue(playheadSec);
    if (restartIfPlaying && wasPlaying) song.play();
  }
}

void updatePlayButtonColor() {
  if (uiPlay == null) return;
  if (playing) {
    uiPlay.setColorBackground(color(200, 60, 60));
    uiPlay.setColorActive(color(240, 90, 90));
  } else {
    uiPlay.setColorBackground(color(60, 160, 80));
    uiPlay.setColorActive(color(90, 200, 110));
  }
}

float snapToCue(float t) {
  float snapPx = 8;
  int w = width - settings.leftW - settings.rightW;
  for (Float c : cues) {
    float px = abs(timeToX(c, settings.leftW, w) - timeToX(t, settings.leftW, w));
    if (px <= snapPx) return c;
  }
  return t;
}

boolean inTimeline(float mx, float my) {
  return mx >= settings.leftW && mx <= width - settings.rightW && my >= settings.topH && my <= height - settings.bottomH;
}

int timelineChannelAt(float my) {
  float cy = settings.topH + 28;
  for (int i = 0; i < settings.channelCount; i++) {
    float chY = cy + i * (settings.channelH + settings.channelGap);
    if (my >= chY && my <= chY + settings.channelH) return i;
  }
  return -1;
}

EffectItem hitEffect(float mx, float my) {
  float y = settings.topH + 32;
  for (EffectItem e : effects) {
    if (mx >= 0 && mx <= settings.leftW && my >= y && my <= y + 24) return e;
    y += 26;
  }
  return null;
}

Clip hitClip(float mx, float my) {
  for (Clip c : clips) {
    if (c.hit(mx, my, settings.leftW, width - settings.leftW - settings.rightW)) return c;
  }
  return null;
}

// --- Double click handlers ---
void handleDoubleClickClip(Clip c) {
  int now = millis();
  if (lastClickClip == c && now - lastClickTime < 350) {
    status("Open script: " + c.effect.name + " (from timeline)");
  }
  lastClickClip = c;
  lastClickTime = now;
}

void handleDoubleClickEffect(EffectItem e) {
  int now = millis();
  if (lastClickEffect == e && now - lastClickTime < 350) {
    status("Open script: " + e.name + " (from list)");
  }
  lastClickEffect = e;
  lastClickTime = now;
}

void status(String msg) {
  statusMsg = msg;
  statusTimer = millis();
}

// --- Project I/O ---
void saveProject(String path) {
  JSONObject root = new JSONObject();
  root.setString("audio", songPath);
  root.setFloat("length_sec", songLengthSec);

  JSONArray cuesArr = new JSONArray();
  for (Float c : cues) cuesArr.append(c);
  root.setJSONArray("cues", cuesArr);

  JSONArray clipsArr = new JSONArray();
  for (Clip c : clips) {
    JSONObject o = new JSONObject();
    o.setString("script", c.effect.name);
    o.setInt("channel", c.channel);
    o.setFloat("start", c.startSec);
    o.setFloat("duration", c.durationSec);
    o.setFloat("intensity", c.intensity);
    o.setFloat("speed", c.speed);
    o.setFloat("hue", c.hue);
    clipsArr.append(o);
  }
  root.setJSONArray("clips", clipsArr);

  saveJSONObject(root, path);
  status("Saved " + path);
}

void loadProject(String path) {
  JSONObject root = loadJSONObject(path);
  if (root == null) {
    status("Load failed: " + path);
    return;
  }
  String audio = root.getString("audio", "");
  if (audio != null && audio.length() > 0) {
    loadSong(audio);
  }
  songLengthSec = root.getFloat("length_sec", songLengthSec);
  cues.clear();
  JSONArray cuesArr = root.getJSONArray("cues");
  if (cuesArr != null) {
    for (int i = 0; i < cuesArr.size(); i++) {
      cues.add(cuesArr.getFloat(i));
    }
  }

  clips.clear();
  JSONArray clipsArr = root.getJSONArray("clips");
  if (clipsArr != null) {
    for (int i = 0; i < clipsArr.size(); i++) {
      JSONObject o = clipsArr.getJSONObject(i);
      String name = o.getString("script", "Effect");
      EffectItem e = findEffect(name);
      if (e == null) e = new EffectItem(name);
      Clip c = new Clip(e, o.getInt("channel", 0), o.getFloat("start", 0), o.getFloat("duration", 6.0));
      c.intensity = o.getFloat("intensity", 0.8);
      c.speed = o.getFloat("speed", 0.5);
      c.hue = o.getFloat("hue", 0.2);
      clips.add(c);
    }
  }

  status("Loaded " + path);
}

EffectItem findEffect(String name) {
  for (EffectItem e : effects) {
    if (e.name.equals(name)) return e;
  }
  return null;
}

// --- Helpers ---
String formatTimecode(float sec) {
  int totalMs = int(sec * 1000);
  int ms = totalMs % 1000;
  int totalSec = totalMs / 1000;
  int s = totalSec % 60;
  int totalMin = totalSec / 60;
  int m = totalMin % 60;
  int h = totalMin / 60;
  return nf(h, 2) + ":" + nf(m, 2) + ":" + nf(s, 2) + ":" + nf(ms, 3);
}

String formatTimeLabel(float sec) {
  int s = int(sec);
  int m = s / 60;
  s = s % 60;
  return nf(m, 2) + ":" + nf(s, 2);
}

float timeToX(float t, int x, int w) {
  float viewLen = songLengthSec / zoom;
  float tt = constrain(t, 0, viewLen);
  return x + 8 + (tt / viewLen) * (w - 16);
}

float timeToXFull(float t, int x, int w) {
  float len = max(0.001, songLengthSec);
  float tt = constrain(t, 0, len);
  return x + 8 + (tt / len) * (w - 16);
}

float xToTime(float px, int x, int w) {
  float viewLen = songLengthSec / zoom;
  float t = map(px, x + 8, x + w - 8, 0, viewLen);
  return constrain(t, 0, songLengthSec);
}

void initEffects() {
  effects.add(new EffectItem("RainbowSweep.bas"));
  effects.add(new EffectItem("PulseBeat.bas"));
  effects.add(new EffectItem("Sparkle.c"));
  effects.add(new EffectItem("FireWaves.c"));
  effects.add(new EffectItem("Worms.bas"));
}

void initControls() {
  int bx = settings.topBtnXStart;
  int by = settings.topBtnY;
  int bw = settings.topBtnW;
  int bh = settings.topBtnH;

  uiOpen = cp5.addButton("openProject")
    .setPosition(bx, by)
    .setSize(bw, bh)
    .setLabel("Open");
  bx += bw + settings.topBtnGap;

  uiPlay = cp5.addButton("playPause")
    .setPosition(bx, by)
    .setSize(bw, bh)
    .setLabel("Play");
  bx += bw + settings.topBtnGap;

  uiCue = cp5.addButton("addCue")
    .setPosition(bx, by)
    .setSize(bw, bh)
    .setLabel("Cue");
  bx += bw + settings.topBtnGap;

  uiZoomIn = cp5.addButton("zoomIn")
    .setPosition(bx, by)
    .setSize(bw, bh)
    .setLabel("Zoom +");
  bx += bw + settings.topBtnGap;

  uiZoomOut = cp5.addButton("zoomOut")
    .setPosition(bx, by)
    .setSize(bw, bh)
    .setLabel("Zoom -");
  zoomOutX = bx;
  zoomOutW = bw;
  bx += bw + settings.topBtnGap;

  uiSong = cp5.addButton("selectSong")
    .setPosition(bx, by)
    .setSize(bw, bh)
    .setLabel("Song");

  // right side buttons
  uiNewScript = cp5.addButton("newScript")
    .setPosition(8, settings.topH + 4)
    .setSize(settings.leftW - 16, 20)
    .setLabel("New Script");

  uiSave = cp5.addButton("saveProjectBtn")
    .setPosition(width - settings.rightW - 160, 10)
    .setSize(64, 28)
    .setLabel("Save");
  saveX = width - settings.rightW - 160;

  uiLoad = cp5.addButton("loadProjectBtn")
    .setPosition(width - settings.rightW - 88, 10)
    .setSize(64, 28)
    .setLabel("Load");

  int rx = width - settings.rightW + 12;
  int ry = settings.topH + 60;
  int rw = settings.rightW - 24;

  uiIntensity = cp5.addSlider("intensity")
    .setPosition(rx, ry)
    .setSize(rw, 16)
    .setRange(0, 1)
    .setValue(0.8)
    .setLabel("Intensity");
  ry += 36;
  uiSpeed = cp5.addSlider("speed")
    .setPosition(rx, ry)
    .setSize(rw, 16)
    .setRange(0, 1)
    .setValue(0.5)
    .setLabel("Speed");
  ry += 36;
  uiHue = cp5.addSlider("hue")
    .setPosition(rx, ry)
    .setSize(rw, 16)
    .setRange(0, 1)
    .setValue(0.2)
    .setLabel("Hue");

  setParamVisibility(false);
}

// --- Classes ---

void songSelected(File selection) {
  if (selection == null) {
    status("Song load canceled");
    return;
  }
  loadSong(selection.getAbsolutePath());
}

void loadSong(String path) {
  try {
    if (song != null) {
      song.stop();
    }
    song = new SoundFile(this, path);
    songPath = path;
    songLengthSec = song.duration();
    playheadSec = 0;
    if (playing) {
      song.play();
      if (uiPlay != null) uiPlay.setLabel("Pause");
    } else {
      if (uiPlay != null) uiPlay.setLabel("Play");
    }
    updatePlayButtonColor();
    status("Loaded song: " + new File(path).getName());
  } catch (Exception e) {
    status("Song load failed");
  }
}

void syncParamSliders() {
  if (selectedClip == null) return;
  uiIntensity.setValue(selectedClip.intensity);
  uiSpeed.setValue(selectedClip.speed);
  uiHue.setValue(selectedClip.hue);
}

// --- ControlP5 callbacks ---
void openProject(int v) { status("Open project (stub)"); }
void playPause(int v) { togglePlay(); }
void addCue(int v) { cues.add(playheadSec); status("Cue added at " + formatTimeLabel(playheadSec)); }
void zoomIn(int v) { zoom = constrain(zoom * 1.25, settings.minZoom, settings.maxZoom); status("Zoom: " + nf(zoom, 1, 2)); }
void zoomOut(int v) { zoom = constrain(zoom / 1.25, settings.minZoom, settings.maxZoom); status("Zoom: " + nf(zoom, 1, 2)); }
void selectSong(int v) { selectInput("Select audio file", "songSelected"); }
void saveProjectBtn(int v) { saveProject("mayhem_project.json"); }
void loadProjectBtn(int v) { loadProject("mayhem_project.json"); }
void newScript(int v) { status("New script (stub)"); }

void intensity(float v) { if (selectedClip != null) selectedClip.intensity = v; }
void speed(float v) { if (selectedClip != null) selectedClip.speed = v; }
void hue(float v) { if (selectedClip != null) selectedClip.hue = v; }

class EffectItem {
  String name;
  EffectItem(String name) { this.name = name; }

  void draw(float x, float y, float w, float h) {
    boolean hover = mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h;
    fill(hover ? 60 : 40);
    rect(x + 8, y, w - 16, h, 4);
    fill(220);
    textAlign(LEFT, CENTER);
    text(name, x + 16, y + h / 2);
  }
}

class Clip {
  EffectItem effect;
  int channel;
  float startSec;
  float durationSec;
  float intensity = 0.8;
  float speed = 0.5;
  float hue = 0.2;

  Clip(EffectItem effect, int channel, float startSec, float durationSec) {
    this.effect = effect;
    this.channel = channel;
    this.startSec = startSec;
    this.durationSec = durationSec;
  }

  void draw(int x, int y, int w) {
    float cy = y + 28 + channel * (settings.channelH + settings.channelGap);
    float xs = timeToX(startSec, x, w);
    float xe = timeToX(startSec + durationSec, x, w);
    float clipW = max(24, xe - xs);

    boolean isSel = (this == selectedClip);
    fill(isSel ? color(255, 160, 60) : color(80, 140, 220));
    rect(xs, cy + 8, clipW, settings.channelH - 16, 4);
    fill(20);
    textAlign(LEFT, CENTER);
    text(effect.name, xs + 6, cy + settings.channelH / 2);
  }

  boolean hit(float mx, float my, int x, int w) {
    float cy = settings.topH + 28 + channel * (settings.channelH + settings.channelGap);
    float xs = timeToX(startSec, x, w);
    float xe = timeToX(startSec + durationSec, x, w);
    float clipW = max(24, xe - xs);
    return mx >= xs && mx <= xs + clipW && my >= cy + 8 && my <= cy + settings.channelH - 8;
  }
}
