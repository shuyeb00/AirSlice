#include <GL/glut.h>
#include <cmath>
#include "Config.h"
#include "Draw.h"

static float worldPerPixel = 1.0f;

void drawSetWorldPerPixel(float wpp) {
    worldPerPixel = wpp;
}

static void* bitmapFont(int size) {
    if (size <= 12) return GLUT_BITMAP_HELVETICA_12;
    if (size <= 18) return GLUT_BITMAP_HELVETICA_18;
    return GLUT_BITMAP_TIMES_ROMAN_24;
}

// ---------- Text ----------
void drawText(float x, float y, const char* s, int size) {
    void* font = bitmapFont(size);
    glRasterPos2f(x, y);                      // colour must be set BEFORE this call
    for (const char* c = s; *c; c++)
        glutBitmapCharacter(font, *c);
}

float textWidth(const char* s, int size) {
    void* font = bitmapFont(size);
    int pixels = 0;
    for (const char* c = s; *c; c++)
        pixels += glutBitmapWidth(font, *c);
    return pixels * worldPerPixel;            // bitmap text is measured in screen pixels
}

void drawTextCentered(float cx, float y, const char* s, int size) {
    drawText(cx - textWidth(s, size) / 2.0f, y, s, size);
}

void drawStrokeText(float cx, float y, const char* s, float height, float lineWidth) {
    float w = 0.0f;
    for (const char* c = s; *c; c++)
        w += (float)glutStrokeWidth(GLUT_STROKE_ROMAN, *c);

    float scale = height / 119.05f;           // GLUT_STROKE_ROMAN letters are 119.05 units tall

    glPushMatrix();
    glTranslatef(cx - w * scale / 2.0f, y, 0.0f);
    glScalef(scale, scale, 1.0f);             // scaling transformation
    glLineWidth(lineWidth);
    for (const char* c = s; *c; c++)
        glutStrokeCharacter(GLUT_STROKE_ROMAN, *c);
    glLineWidth(1.0f);
    glPopMatrix();
}

// ---------- Shapes ----------
void drawCircle(float cx, float cy, float r, int segments) {
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);                       // centre
    for (int i = 0; i <= segments; i++) {     // points around the edge
        float a = 2.0f * PI * i / segments;
        glVertex2f(cx + r * std::cos(a), cy + r * std::sin(a));
    }
    glEnd();
}

void drawCircleOutline(float cx, float cy, float r, int segments) {
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segments; i++) {
        float a = 2.0f * PI * i / segments;
        glVertex2f(cx + r * std::cos(a), cy + r * std::sin(a));
    }
    glEnd();
}

// Ellipse = a unit circle scaled differently in x and y (scaling transformation)
void drawEllipse(float cx, float cy, float rx, float ry) {
    glPushMatrix();
    glTranslatef(cx, cy, 0.0f);
    glScalef(rx, ry, 1.0f);
    drawCircle(0.0f, 0.0f, 1.0f);
    glPopMatrix();
}

void drawRect(float x, float y, float w, float h) {
    glBegin(GL_QUADS);
        glVertex2f(x, y);
        glVertex2f(x + w, y);
        glVertex2f(x + w, y + h);
        glVertex2f(x, y + h);
    glEnd();
}

void drawRectOutline(float x, float y, float w, float h) {
    glBegin(GL_LINE_LOOP);
        glVertex2f(x, y);
        glVertex2f(x + w, y);
        glVertex2f(x + w, y + h);
        glVertex2f(x, y + h);
    glEnd();
}

void drawBackground() {
    // Vertical gradient: dark navy at the bottom, dark purple at the top
    glBegin(GL_QUADS);
        glColor3f(0.05f, 0.06f, 0.12f); glVertex2f(0, 0);
                                        glVertex2f(WORLD_W, 0);
        glColor3f(0.16f, 0.10f, 0.24f); glVertex2f(WORLD_W, WORLD_H);
                                        glVertex2f(0, WORLD_H);
    glEnd();
}
