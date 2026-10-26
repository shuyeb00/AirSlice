#include <GL/glut.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include "Config.h"
#include "Draw.h"
#include "Game.h"

// ---------- World and window ----------
// The game always thinks in a fixed 1280 x 720 world,
// no matter how big the real window is.


int winW = 1280, winH = 720;   // real window size in pixels (changes on resize)

// ---------- Input ----------
float mouseX = WORLD_W / 2, mouseY = WORLD_H / 2;   // mouse in WORLD coordinates

// ---------- Timing ----------
std::chrono::steady_clock::time_point lastTick;
float fps = 0.0f;
int fpsFrames = 0;
float fpsTimer = 0.0f;


// ---------- Game update (game logic goes here later) ----------
void update(float dt) {
    gameUpdate(dt);
    // Measure frames per second, refreshed every 0.5 s
    fpsFrames++;
    fpsTimer += dt;
    if (fpsTimer >= 0.5f) {
        fps = fpsFrames / fpsTimer;
        fpsFrames = 0;
        fpsTimer = 0.0f;
    }
}

// ---------- Draw one frame ----------
void display() {
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();

    drawBackground();
    gameDraw();

    // Temporary "blade": a dot that follows the mouse
    glColor3f(0.4f, 1.0f, 0.5f);
    drawCircle(mouseX, mouseY, 10.0f);

    // HUD
    char buf[96];
    glColor3f(1.0f, 1.0f, 1.0f);
    std::snprintf(buf, sizeof(buf), "FPS: %.0f", fps);
    drawText(20, WORLD_H - 35, buf);
    std::snprintf(buf, sizeof(buf), "Mouse (world): %.0f, %.0f", mouseX, mouseY);
    drawText(20, WORLD_H - 60, buf);
    std::snprintf(buf, sizeof(buf), "Fruits alive: %d", gameFruitCount());
    drawText(20, WORLD_H - 85, buf);

    glutSwapBuffers();
}

// ---------- Game loop ----------
void timer(int) {
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - lastTick).count();   // seconds
    lastTick = now;
    if (dt > 0.05f) dt = 0.05f;   // avoid a huge jump after dragging the window

    update(dt);
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);  // aim for about 60 frames per second
}

// ---------- Window-to-viewport: window pixels -> world coordinates ----------
void mouseMove(int x, int y) {
    // GLUT gives y from the TOP of the window; OpenGL world has y from the BOTTOM
    mouseX = x * WORLD_W / winW;
    mouseY = (winH - y) * WORLD_H / winH;
}

void reshape(int w, int h) {
    if (h == 0) h = 1;
    winW = w;
    winH = h;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0.0, WORLD_W, 0.0, WORLD_H);   // world is always 1280 x 720
    glMatrixMode(GL_MODELVIEW);
}

void keyboard(unsigned char key, int, int) {
    if (key == 27) std::exit(0);   // ESC
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(1280, 720);
    glutCreateWindow("AirSlice");

    glutSetCursor(GLUT_CURSOR_NONE);   // hide the Windows cursor; our dot replaces it

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutPassiveMotionFunc(mouseMove);  // mouse moving, no button pressed
    glutMotionFunc(mouseMove);         // mouse moving with a button held

    gameInit();
    lastTick = std::chrono::steady_clock::now();
    glutTimerFunc(16, timer, 0);
    glutMainLoop();
    return 0;
}
