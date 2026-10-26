#include <GL/glut.h>
#include <cmath>
#include "Config.h"
#include "Draw.h"

void drawText(float x, float y, const char* s) {
    glRasterPos2f(x, y);
    for (const char* c = s; *c; c++)
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *c);
}

void drawCircle(float cx, float cy, float r, int segments) {
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= segments; i++) {
        float a = 2.0f * PI * i / segments;
        glVertex2f(cx + r * std::cos(a), cy + r * std::sin(a));
    }
    glEnd();
}

void drawBackground() {
    glBegin(GL_QUADS);
        glColor3f(0.05f, 0.06f, 0.12f); glVertex2f(0, 0);
                                        glVertex2f(WORLD_W, 0);
        glColor3f(0.16f, 0.10f, 0.24f); glVertex2f(WORLD_W, WORLD_H);
                                        glVertex2f(0, WORLD_H);
    glEnd();
}
