#pragma once

// ---------- Text ----------
// Bitmap text (fixed pixel size). size: 12, 18 or 24.
void  drawText(float x, float y, const char* s, int size = 18);
void  drawTextCentered(float cx, float y, const char* s, int size = 18);
float textWidth(const char* s, int size = 18);      // width in world units

// Stroke (vector) text: scalable, used for titles and pop-ups.
// cx = horizontal centre, y = baseline, height = letter height in world units.
void  drawStrokeText(float cx, float y, const char* s, float height, float lineWidth = 2.0f);

// Called on every window resize so text widths can be converted to world units
void  drawSetWorldPerPixel(float worldPerPixel);

// ---------- Shapes ----------
void drawCircle(float cx, float cy, float r, int segments = 32);
void drawCircleOutline(float cx, float cy, float r, int segments = 32);
void drawEllipse(float cx, float cy, float rx, float ry);
void drawRect(float x, float y, float w, float h);
void drawRectOutline(float x, float y, float w, float h);
void drawBackground();
