#include <GL/glut.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include "Config.h"
#include "Draw.h"
#include "Game.h"

// ---------- Window and letterbox viewport ----------
int winW = 1280, winH = 720;          // real window size in pixels
int vpX = 0, vpY = 0;                 // where the 16:9 game area starts inside the window
int vpW = 1280, vpH = 720;            // size of the 16:9 game area in pixels

// ---------- Input ----------
float mouseX = WORLD_W / 2, mouseY = WORLD_H / 2;   // mouse in WORLD coordinates

// ---------- Timing ----------
std::chrono::steady_clock::time_point lastTick;
float fps = 0.0f;
int fpsFrames = 0;
float fpsTimer = 0.0f;

// ---------- Game update ----------
void update(float dt) {
    gameBladeMove(mouseX, mouseY);   // the mouse is the blade for now
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
    glClear(GL_COLOR_BUFFER_BIT);    // clears the whole window, so the bars become black
    glLoadIdentity();

    drawBackground();
    gameDraw();

    // Small dot showing the exact blade position
    glColor3f(1.0f, 1.0f, 1.0f);
    drawCircle(mouseX, mouseY, 4.0f);

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
    // GLUT gives y from the TOP of the window; OpenGL counts from the BOTTOM
    float px = (float)(x - vpX);
    float py = (float)((winH - y) - vpY);

    mouseX = px * WORLD_W / vpW;
    mouseY = py * WORLD_H / vpH;

    // Keep the blade inside the world when the mouse is over the black bars
    if (mouseX < 0) mouseX = 0;
    if (mouseX > WORLD_W) mouseX = WORLD_W;
    if (mouseY < 0) mouseY = 0;
    if (mouseY > WORLD_H) mouseY = WORLD_H;
}

// Letterboxing: find the largest 16:9 area that fits in the window and centre it
void reshape(int w, int h) {
    if (h == 0) h = 1;
    winW = w;
    winH = h;

    float target = WORLD_W / WORLD_H;           // 16:9
    if ((float)w / h > target) {                // window too wide: bars left and right
        vpH = h;
        vpW = (int)(h * target);
        vpX = (w - vpW) / 2;
        vpY = 0;
    } else {                                    // window too tall: bars top and bottom
        vpW = w;
        vpH = (int)(w / target);
        vpX = 0;
        vpY = (h - vpH) / 2;
    }

    glViewport(vpX, vpY, vpW, vpH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0.0, WORLD_W, 0.0, WORLD_H);     // world is always 1280 x 720
    glMatrixMode(GL_MODELVIEW);
}

void keyboard(unsigned char key, int, int) {
    if (key == 27) std::exit(0);   // ESC
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(1280, 720);
    glutCreateWindow("AirSlice");

    glutSetCursor(GLUT_CURSOR_NONE);   // hide the Windows cursor

    // Alpha blending: lets the blade trail be partly transparent
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);   // black letterbox bars

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
