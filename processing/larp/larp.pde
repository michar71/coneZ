import controlP5.*;

ControlP5 cp5;

final int CANVAS_W = 1200;
final int CANVAS_H = 760;

final int PLOT_X = 300;
final int PLOT_Y = 40;
final int PLOT_W = 860;
final int PLOT_H = 640;

final int X_DOMAIN_MIN = 0;
final int X_DOMAIN_MAX = 1000;
final int Y_DOMAIN_MIN = 0;
final int Y_DOMAIN_MAX = 65535;

void setup() {
  size(1200, 700);
  surface.setTitle("LARP Function Test Harness");

  cp5 = new ControlP5(this);

  int sx = 20;
  int sy = 30;
  int gap = 54;

  addLabeledIntSlider("x_min", "x_min", sx, sy + gap * 0, 0, 1000, 0);
  addLabeledIntSlider("x_max", "x_max", sx, sy + gap * 1, 0, 1000, 1000);
  addLabeledIntSlider("min_val", "min_val", sx, sy + gap * 2, 0, 65535, 0);
  addLabeledIntSlider("max_val", "max_val", sx, sy + gap * 3, 0, 65535, 65535);
  addLabeledIntSlider("offset", "offset", sx, sy + gap * 4, 0, 100, 0);
  addLabeledIntSlider("window", "window", sx, sy + gap * 5, 1, 100, 0);
  addLabeledIntSlider("stride", "stride", sx, sy + gap * 6, 1, 100, 20);

  textFont(createFont("Arial", 12));
}

void draw() {
  background(248);

  int xMin = sliderInt("x_min");
  int xMax = sliderInt("x_max");
  int minVal = sliderInt("min_val");
  int maxVal = sliderInt("max_val");
  int offset = sliderInt("offset");
  int window = sliderInt("window");
  int stride = sliderInt("stride");
  int stepSize = max(1, stride);

  drawPlotFrame();

  stroke(20, 90, 210);
  strokeWeight(2);
  noFill();
  beginShape();
  for (int xPos = X_DOMAIN_MIN; xPos <= X_DOMAIN_MAX; xPos += stepSize) {
    int yVal = larp(xPos, xMin, xMax, minVal, maxVal, offset, window, stride);
    float px = map(xPos, X_DOMAIN_MIN, X_DOMAIN_MAX, PLOT_X, PLOT_X + PLOT_W);
    float py = map(yVal, Y_DOMAIN_MIN, Y_DOMAIN_MAX, PLOT_Y + PLOT_H, PLOT_Y);
    vertex(px, py);
  }
  if (X_DOMAIN_MAX % stepSize != 0) {
    int xPos = X_DOMAIN_MAX;
    int yVal = larp(xPos, xMin, xMax, minVal, maxVal, offset, window, stride);
    float px = map(xPos, X_DOMAIN_MIN, X_DOMAIN_MAX, PLOT_X, PLOT_X + PLOT_W);
    float py = map(yVal, Y_DOMAIN_MIN, Y_DOMAIN_MAX, PLOT_Y + PLOT_H, PLOT_Y);
    vertex(px, py);
  }
  endShape();

  drawLegend(xMin, xMax, minVal, maxVal, offset, window, stride, stepSize);
}

void addIntSlider(String name, int x, int y, int minV, int maxV, int defaultV) {
  cp5.addSlider(name)
    .setPosition(x, y)
    .setSize(240, 18)
    .setRange(minV, maxV)
    .setValue(defaultV)
    .setDecimalPrecision(0)
    .setColorForeground(color(120, 120, 150))
    .setColorActive(color(30, 120, 220))
    .setColorBackground(color(220));
}

void addLabeledIntSlider(String name, String label, int x, int y, int minV, int maxV, int defaultV) {
  addIntSlider(name, x, y, minV, maxV, defaultV);
  cp5.addTextlabel(name + "_label")
    .setText(label)
    .setPosition(x, y - 16)
    .setColorValue(color(20));
}

int sliderInt(String name) {
  return round(cp5.getController(name).getValue());
}

int larp(int x_pos, int x_min, int x_max, int min_val, int max_val, int offset, int window, int stride) {
  // Placeholder implementation: linear interpolation with clamped t.
  // offset/window/stride are accepted for API compatibility and future logic.
  if (x_min == x_max) 
  {
    return min_val;
  }

  //Calulate offset from breginning and end.
    int range = x_max - x_min;
    int offset_int = (range/2)* offset / 100; // Calculate offset in terms of the range and percentage
    int window_int = offset_int * window / 100; // Calculate window in terms of the range
    float avg_x = 0;
    float sum = 0;
    int count = 0;
    for (int i = x_pos - (window_int/2); i <= x_pos + (window_int/2); i += stride) 
    {
      //Sampling outside of the x-range or the flat range...
      if (i<x_min)
      {
        sum = sum + min_val;
        count++;
      } 
      else if (i>x_max)
      {
        sum = sum + max_val;
        count++;        
      }

      //Sampling inside the x-range interpolation area...
      else
      {
        float t = (float)(i - (x_min+offset_int)) / ((x_max-offset_int) - (x_min+offset_int));
        t = constrain(t, 0, 1);
        int val = round(lerp(min_val, max_val, t));
        sum = sum + val;
        count++;
      }
    }

    if (count > 0) 
    {
      avg_x = sum / count;    
    } 
    else 
    {
      avg_x = min_val; // Fallback to the original x_pos if no samples are within the range
    }

  return round(avg_x);
}

void drawPlotFrame() {
  stroke(60);
  strokeWeight(1);
  fill(255);
  rect(PLOT_X, PLOT_Y, PLOT_W, PLOT_H);

  stroke(220);
  for (int i = 1; i < 10; i++) {
    float gx = map(i, 0, 10, PLOT_X, PLOT_X + PLOT_W);
    line(gx, PLOT_Y, gx, PLOT_Y + PLOT_H);
  }
  for (int i = 1; i < 8; i++) {
    float gy = map(i, 0, 8, PLOT_Y, PLOT_Y + PLOT_H);
    line(PLOT_X, gy, PLOT_X + PLOT_W, gy);
  }

  fill(20);
  textAlign(CENTER, TOP);
  text("x_pos (0..1000)", PLOT_X + PLOT_W * 0.5, PLOT_Y + PLOT_H + 10);

  pushMatrix();
  translate(PLOT_X - 42, PLOT_Y + PLOT_H * 0.5);
  rotate(-HALF_PI);
  textAlign(CENTER, TOP);
  text("larp output (0..65535)", 0, 0);
  popMatrix();
}

void drawLegend(int xMin, int xMax, int minVal, int maxVal, int offset, int window, int stride, int stepSize) {
  int legendX = 20;
  int legendY = 430;
  int lineH = 18;

  fill(20);
  textAlign(LEFT, TOP);
  text("LARP parameters", legendX, legendY - 24);
  text("x_min: " + xMin, legendX, legendY + lineH * 0);
  text("x_max: " + xMax, legendX, legendY + lineH * 1);
  text("min_val: " + minVal, legendX, legendY + lineH * 2);
  text("max_val: " + maxVal, legendX, legendY + lineH * 3);
  text("offset: " + offset, legendX, legendY + lineH * 4);
  text("window: " + window, legendX, legendY + lineH * 5);
  text("stride: " + stride, legendX, legendY + lineH * 6);
  text("sampling step used for plotting: " + stepSize + " (0 becomes 1)", legendX, legendY + lineH * 8);
}
