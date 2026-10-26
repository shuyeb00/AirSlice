#pragma once
#include <vector>

// Everything the game needs to know about the camera tracker right now
struct VisionStatus {
    bool  running = false;
    bool  found = false;          // marker visible (or lost for only a few frames)
    float x = 0.5f, y = 0.5f;     // smoothed marker position, 0..1, x to the RIGHT, y UP
    float cameraFps = 0.0f;
    float detectRate = 0.0f;      // 0..1, share of frames in the last second with the marker found
    int   frameW = 0, frameH = 0;
    float calibHalfU = 0.0f;      // half-size of the calibration box, as a fraction of frame width
    float calibHalfV = 0.0f;      // ... and of frame height
    int   hLo = 0, hHi = 0, sLo = 0, vLo = 0;   // current HSV range (hue wraps if hLo > hHi)
    char  colorName[32] = "";
    char  message[96] = "";       // last calibration / preset message
};

bool visionStart(int cameraIndex);   // opens the camera and starts the tracking thread
void visionStop();
VisionStatus visionStatus();

void visionSetPreset(int index);     // 0 = Green, 1 = Purple, 2 = Pink
void visionRequestCalibration();     // sample the colour in the centre box on the next frame

// Copies the newest small preview images (RGB, w x h, rows top-down) if newer than lastId.
// mask = the binary mask converted to RGB. Returns false if nothing new.
bool visionGetPreview(unsigned lastId, unsigned& newId,
                      std::vector<unsigned char>& rgb, std::vector<unsigned char>& mask,
                      int& w, int& h);
