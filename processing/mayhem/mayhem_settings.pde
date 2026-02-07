class MayhemSettings {
  int W = 1280;
  int H = 720;
  int leftW = 240;
  int rightW = 260;
  int topH = 56;
  int bottomH = 28;

  float minZoom = 0.4;
  float maxZoom = 8.0;

  int channelCount = 4;
  float channelH = 60;
  float channelGap = 6;

  int topBtnW = 96;
  int topBtnH = 28;
  int topBtnY = 10;
  int topBtnXStart = 12;
  int topBtnGap = 12;
}

MayhemSettings settings = new MayhemSettings();

void settings() {
  size(settings.W, settings.H);
}
