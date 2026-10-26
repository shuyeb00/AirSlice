#include <GL/glut.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <vector>
#include <fstream>
#include <algorithm>
#include "Config.h"
#include "Draw.h"
#include "Game.h"
#include "Vision.h"
#include "Sound.h"

// Windows gl.h only knows OpenGL 1.1; this constant is from OpenGL 1.2
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// =====================================================================
// Application state
// =====================================================================
enum class State { Menu, Calibrate, Playing, Paused, GameOver };
enum class InputMode { Mouse, Webcam };

static State     state = State::Menu;
static InputMode inputMode = InputMode::Mouse;

// ---------- Window and letterbox viewport ----------
static int winW = 1280, winH = 720;   // real window size in pixels
static int vpX = 0, vpY = 0;          // where the 16:9 game area starts inside the window
static int vpW = 1280, vpH = 720;     // size of the 16:9 game area in pixels

// ---------- Input ----------
static float mouseX = WORLD_W / 2, mouseY = WORLD_H / 2;   // mouse in WORLD coordinates
static float bladeX = WORLD_W / 2, bladeY = WORLD_H / 2;   // blade position used by the game
static bool  bladeActive = false;

// Only the middle part of the camera image is mapped to the screen,
// so the screen edges are easy to reach without leaving the camera's view
constexpr float CAM_MARGIN   = 0.10f;
constexpr float BLADE_FOLLOW = 45.0f;   // how fast the blade catches up with the camera target

// ---------- Options ----------
static bool showDebug = false;          // D while playing
static bool showPreview = true;         // V while playing (webcam mode)
static bool calibShowMask = false;      // D on the webcam setup screen

// ---------- Camera preview textures ----------
static bool   cameraOk = false, cameraFailed = false;
static GLuint camTex = 0, maskTex = 0;
static bool   texReady = false;
static unsigned lastFrameId = 0;
static std::vector<unsigned char> camRgb, maskRgb;

// ---------- High score ----------
static const char* HIGHSCORE_FILE = "highscore.txt";
static int  highScore = 0;
static bool newHighScore = false;

// ---------- Timing ----------
static std::chrono::steady_clock::time_point lastTick;
static float fps = 0.0f;
static int   fpsFrames = 0;
static float fpsTimer = 0.0f;

// =====================================================================
// Helpers
// =====================================================================
static float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

static void loadHighScore() {
    std::ifstream f(HIGHSCORE_FILE);
    if (!(f >> highScore)) highScore = 0;
}

static void saveHighScore() {
    std::ofstream f(HIGHSCORE_FILE);
    f << highScore << "\n";
}

static void setState(State s) {
    state = s;
    // Hide the Windows cursor only while playing (our blade replaces it)
    glutSetCursor(s == State::Playing ? GLUT_CURSOR_NONE : GLUT_CURSOR_LEFT_ARROW);
}

static void startGame(InputMode mode) {
    inputMode = mode;
    if (mode == InputMode::Webcam) gameSetSliceAssist(450.0f, 15.0f);   // easier for the camera
    else                           gameSetSliceAssist(800.0f, 0.0f);
    gameReset();
    newHighScore = false;
    bladeActive = false;
    setState(State::Playing);
}

static bool openCamera() {
    if (!cameraOk) {
        cameraOk = visionStart(0);
        cameraFailed = !cameraOk;
    }
    return cameraOk;
}

// Copies the newest camera preview into two OpenGL textures (camera image and mask)
static void updatePreviewTextures() {
    unsigned newId;
    int w, h;
    if (!visionGetPreview(lastFrameId, newId, camRgb, maskRgb, w, h)) return;
    lastFrameId = newId;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (!texReady) {
        glBindTexture(GL_TEXTURE_2D, camTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, camRgb.data());
        glBindTexture(GL_TEXTURE_2D, maskTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, maskRgb.data());
        texReady = true;
    } else {
        glBindTexture(GL_TEXTURE_2D, camTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, camRgb.data());
        glBindTexture(GL_TEXTURE_2D, maskTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, maskRgb.data());
    }
}

// Blade position: the mouse, or the camera marker mapped to the world (window-to-viewport)
static void updateBlade(float dt) {
    if (inputMode == InputMode::Mouse) {
        bladeX = mouseX;
        bladeY = mouseY;
        bladeActive = true;
        return;
    }

    VisionStatus st = visionStatus();
    if (!st.found) {
        bladeActive = false;
        return;
    }
    float tx = clamp01((st.x - CAM_MARGIN) / (1.0f - 2.0f * CAM_MARGIN)) * WORLD_W;
    float ty = clamp01((st.y - CAM_MARGIN) / (1.0f - 2.0f * CAM_MARGIN)) * WORLD_H;

    if (!bladeActive) {                 // just found again: jump straight there
        bladeX = tx;
        bladeY = ty;
    } else {                            // camera runs at ~20 FPS, game at 60: glide between updates
        float k = std::min(1.0f, dt * BLADE_FOLLOW);
        bladeX += (tx - bladeX) * k;
        bladeY += (ty - bladeY) * k;
    }
    bladeActive = true;
}

// =====================================================================
// Update
// =====================================================================
static void update(float dt) {
    fpsFrames++;
    fpsTimer += dt;
    if (fpsTimer >= 0.5f) {
        fps = fpsFrames / fpsTimer;
        fpsFrames = 0;
        fpsTimer = 0.0f;
    }

    if (cameraOk) updatePreviewTextures();

    if (state == State::Playing) {
        updateBlade(dt);
        gameUpdate(dt, bladeX, bladeY, bladeActive);

        if (gameIsOver()) {
            if (gameScore() > highScore) {
                highScore = gameScore();
                newHighScore = true;
                saveHighScore();
            }
            setState(State::GameOver);
        }
    }
}

// =====================================================================
// Drawing pieces
// =====================================================================
// Camera image (or mask) as a textured rectangle, plus calibration box and marker cross
static void drawPreview(float x, float y, float w, float h, bool mask, bool showBox) {
    if (!texReady) {
        glColor3f(0.0f, 0.0f, 0.0f);
        drawRect(x, y, w, h);
        glColor3f(0.7f, 0.7f, 0.7f);
        drawTextCentered(x + w / 2, y + h / 2, "Waiting for camera...", 18);
        return;
    }

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, mask ? maskTex : camTex);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);   // texture rows go top-down, so v is flipped
        glTexCoord2f(0.0f, 1.0f); glVertex2f(x,     y);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(x + w, y);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(x + w, y + h);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(x,     y + h);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    VisionStatus st = visionStatus();

    if (showBox) {   // yellow calibration box in the centre
        float bw = st.calibHalfU * w, bh = st.calibHalfV * h;
        glColor3f(1.0f, 0.9f, 0.1f);
        glLineWidth(2.0f);
        drawRectOutline(x + w / 2 - bw, y + h / 2 - bh, 2 * bw, 2 * bh);
        glLineWidth(1.0f);
    }

    // Region of the camera that maps to the screen
    glColor4f(1.0f, 1.0f, 1.0f, 0.35f);
    drawRectOutline(x + CAM_MARGIN * w, y + CAM_MARGIN * h,
                    (1 - 2 * CAM_MARGIN) * w, (1 - 2 * CAM_MARGIN) * h);

    if (st.found) {   // green cross where the marker was detected
        float mx = x + st.x * w, my = y + st.y * h;
        glColor3f(0.2f, 1.0f, 0.3f);
        glLineWidth(2.0f);
        glBegin(GL_LINES);
            glVertex2f(mx - 10, my); glVertex2f(mx + 10, my);
            glVertex2f(mx, my - 10); glVertex2f(mx, my + 10);
        glEnd();
        drawCircleOutline(mx, my, 8.0f, 20);
        glLineWidth(1.0f);
    }

    glColor3f(0.8f, 0.8f, 0.85f);
    drawRectOutline(x, y, w, h);
}

static void drawMenu() {
    glColor3f(0.45f, 1.0f, 0.55f);
    drawStrokeText(WORLD_W / 2, 470.0f, "AirSlice", 120.0f, 5.0f);

    glColor3f(0.85f, 0.85f, 0.9f);
    drawTextCentered(WORLD_W / 2, 420.0f, "Slice the fruit by waving a coloured object at your webcam", 18);

    glColor3f(1.0f, 1.0f, 1.0f);
    drawTextCentered(WORLD_W / 2, 330.0f, "[W]   Play with webcam", 24);
    drawTextCentered(WORLD_W / 2, 290.0f, "[M]   Play with mouse", 24);
    drawTextCentered(WORLD_W / 2, 250.0f, "[ESC]   Quit", 24);
    glColor3f(0.75f, 0.75f, 0.85f);
    drawTextCentered(WORLD_W / 2, 215.0f, soundEnabled() ? "[S]  Sound: ON" : "[S]  Sound: OFF", 18);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "High score: %d", highScore);
    glColor3f(1.0f, 0.85f, 0.3f);
    drawTextCentered(WORLD_W / 2, 175.0f, buf, 18);

    if (cameraFailed) {
        glColor3f(1.0f, 0.4f, 0.4f);
        drawTextCentered(WORLD_W / 2, 135.0f,
            "Webcam could not be opened. Check camera privacy settings or close other apps using it.", 18);
    }

    glColor3f(0.55f, 0.55f, 0.65f);
    drawTextCentered(WORLD_W / 2, 40.0f,
        "CSE 402 Computer Graphics & Image Processing Lab   |   Md Suheb Ahmed (231-115-322)", 12);
}

static void drawCalibrate() {
    VisionStatus st = visionStatus();

    glColor3f(1.0f, 1.0f, 1.0f);
    drawText(60.0f, 670.0f, "Webcam setup", 24);

    drawPreview(60.0f, 140.0f, 640.0f, 480.0f, calibShowMask, true);
    glColor3f(0.7f, 0.7f, 0.75f);
    drawText(60.0f, 110.0f, calibShowMask ? "Showing: binary mask (D for camera)"
                                          : "Showing: camera (D for mask)", 18);

    float x = 760.0f, y = 600.0f, line = 32.0f;
    glColor3f(1.0f, 1.0f, 1.0f);
    drawText(x, y,            "1.  Hold your coloured object in the yellow box", 18);
    drawText(x, y - line,     "2.  Press C to calibrate to its colour", 18);
    drawText(x, y - 2 * line, "      or 1 / 2 / 3 for Green / Purple / Pink", 18);
    drawText(x, y - 3 * line, "3.  Press D: the object should be white,", 18);
    drawText(x, y - 4 * line, "      everything else black", 18);
    drawText(x, y - 5 * line, "4.  SPACE to start,  ESC to go back", 18);

    char buf[128];
    y = 330.0f;
    glColor3f(0.8f, 0.85f, 1.0f);
    std::snprintf(buf, sizeof(buf), "Colour: %s", st.colorName);
    drawText(x, y, buf, 18);
    if (st.hLo <= st.hHi)
        std::snprintf(buf, sizeof(buf), "HSV: H %d-%d,  S >= %d,  V >= %d", st.hLo, st.hHi, st.sLo, st.vLo);
    else
        std::snprintf(buf, sizeof(buf), "HSV: H %d-179 & 0-%d,  S >= %d,  V >= %d", st.hLo, st.hHi, st.sLo, st.vLo);
    drawText(x, y - line, buf, 18);

    if (st.found) { glColor3f(0.3f, 1.0f, 0.4f); drawText(x, y - 2 * line, "Marker: FOUND", 24); }
    else          { glColor3f(1.0f, 0.35f, 0.35f); drawText(x, y - 2 * line, "Marker: NOT FOUND", 24); }

    glColor3f(0.8f, 0.85f, 1.0f);
    std::snprintf(buf, sizeof(buf), "Detection: %.0f%%    Camera FPS: %.0f", st.detectRate * 100.0f, st.cameraFps);
    drawText(x, y - 3 * line, buf, 18);

    glColor3f(1.0f, 0.85f, 0.3f);
    drawText(x, y - 4 * line, st.message, 18);
}

static void drawHud() {
    char buf[128];

    glColor3f(1.0f, 1.0f, 1.0f);
    std::snprintf(buf, sizeof(buf), "Score: %d", gameScore());
    drawText(20.0f, WORLD_H - 40.0f, buf, 24);
    glColor3f(0.6f, 0.9f, 1.0f);
    std::snprintf(buf, sizeof(buf), "Level %d", gameLevel());
    drawText(20.0f, WORLD_H - 70.0f, buf, 18);

    // Lives as three circles: filled = remaining, outline = lost
    glColor3f(1.0f, 1.0f, 1.0f);
    drawText(WORLD_W - 190.0f, WORLD_H - 40.0f, "Lives", 18);
    for (int i = 0; i < 3; i++) {
        float cx = WORLD_W - 120.0f + i * 35.0f, cy = WORLD_H - 34.0f;
        if (i < gameLives()) { glColor3f(1.0f, 0.25f, 0.3f); drawCircle(cx, cy, 11.0f); }
        else                 { glColor3f(0.5f, 0.5f, 0.55f); drawCircleOutline(cx, cy, 11.0f); }
    }

    glColor3f(0.7f, 0.7f, 0.8f);
    if (inputMode == InputMode::Mouse) {
        drawText(20.0f, 20.0f, "Input: mouse    P pause   D debug   S sound   ESC menu", 12);
    } else {
        VisionStatus st = visionStatus();
        std::snprintf(buf, sizeof(buf), "Input: webcam (%s)    P pause   D debug   V preview   S sound   ESC menu", st.colorName);
        drawText(20.0f, 20.0f, buf, 12);
        if (!st.found) {
            glColor3f(1.0f, 0.4f, 0.4f);
            drawTextCentered(WORLD_W / 2, 20.0f, "Marker not found", 18);
        }
    }

    // Small dot showing exactly where the blade is
    if (bladeActive) {
        glColor3f(1.0f, 1.0f, 1.0f);
        drawCircle(bladeX, bladeY, 4.0f);
    }
}

static void drawDebug() {
    char buf[128];
    float x = 20.0f, y = WORLD_H - 110.0f;
    glColor3f(0.4f, 1.0f, 0.5f);
    std::snprintf(buf, sizeof(buf), "Game FPS: %.0f    Objects: %d", fps, gameObjectCount());
    drawText(x, y, buf, 12);
    if (inputMode == InputMode::Webcam) {
        VisionStatus st = visionStatus();
        std::snprintf(buf, sizeof(buf), "Camera FPS: %.0f    Detection: %.0f%%", st.cameraFps, st.detectRate * 100.0f);
        drawText(x, y - 18.0f, buf, 12);
        std::snprintf(buf, sizeof(buf), "HSV: H %d..%d  S>=%d  V>=%d", st.hLo, st.hHi, st.sLo, st.vLo);
        drawText(x, y - 36.0f, buf, 12);
    }
}

static void drawOverlay(float alpha) {
    glColor4f(0.0f, 0.0f, 0.0f, alpha);
    drawRect(0.0f, 0.0f, WORLD_W, WORLD_H);
}

static void drawPaused() {
    drawOverlay(0.55f);
    glColor3f(1.0f, 1.0f, 1.0f);
    drawStrokeText(WORLD_W / 2, 380.0f, "PAUSED", 80.0f, 4.0f);
    drawTextCentered(WORLD_W / 2, 320.0f, "P or SPACE to resume,  ESC for menu", 18);
}

static void drawGameOver() {
    drawOverlay(0.6f);
    glColor3f(1.0f, 0.3f, 0.3f);
    drawStrokeText(WORLD_W / 2, 430.0f, "GAME OVER", 90.0f, 4.0f);

    char buf[64];
    glColor3f(1.0f, 1.0f, 1.0f);
    std::snprintf(buf, sizeof(buf), "Score: %d", gameScore());
    drawTextCentered(WORLD_W / 2, 370.0f, buf, 24);
    std::snprintf(buf, sizeof(buf), "High score: %d", highScore);
    drawTextCentered(WORLD_W / 2, 335.0f, buf, 18);

    if (newHighScore) {
        glColor3f(1.0f, 0.85f, 0.2f);
        drawTextCentered(WORLD_W / 2, 295.0f, "NEW HIGH SCORE!", 24);
    }
    glColor3f(0.8f, 0.8f, 0.85f);
    drawTextCentered(WORLD_W / 2, 240.0f, "SPACE to play again,  M for menu", 18);
}

// =====================================================================
// GLUT callbacks
// =====================================================================
static void display() {
    glClear(GL_COLOR_BUFFER_BIT);     // clears the whole window, so the letterbox bars are black
    glLoadIdentity();
    drawBackground();

    switch (state) {
    case State::Menu:
        drawMenu();
        break;
    case State::Calibrate:
        drawCalibrate();
        break;
    case State::Playing:
    case State::Paused:
    case State::GameOver:
        gameDraw();
        drawHud();
        if (inputMode == InputMode::Webcam && showPreview)
            drawPreview(WORLD_W - 260.0f, 20.0f, 240.0f, 180.0f, showDebug, false);
        if (showDebug) drawDebug();
        if (state == State::Paused)   drawPaused();
        if (state == State::GameOver) drawGameOver();
        break;
    }

    glutSwapBuffers();
}

static void timer(int) {
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - lastTick).count();   // seconds
    lastTick = now;
    if (dt > 0.05f) dt = 0.05f;   // avoid a huge jump after dragging the window

    update(dt);
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);  // about 60 frames per second
}

// Window-to-viewport: window pixels -> world coordinates
static void mouseMove(int x, int y) {
    float px = (float)(x - vpX);
    float py = (float)((winH - y) - vpY);      // GLUT y is from the top, OpenGL from the bottom
    mouseX = std::max(0.0f, std::min(WORLD_W, px * WORLD_W / vpW));
    mouseY = std::max(0.0f, std::min(WORLD_H, py * WORLD_H / vpH));
}

// Letterboxing: the largest 16:9 area that fits in the window, centred
static void reshape(int w, int h) {
    if (h == 0) h = 1;
    winW = w;
    winH = h;

    float target = WORLD_W / WORLD_H;
    if ((float)w / h > target) {              // window too wide: bars left and right
        vpH = h;
        vpW = (int)(h * target);
        vpX = (w - vpW) / 2;
        vpY = 0;
    } else {                                  // window too tall: bars top and bottom
        vpW = w;
        vpH = (int)(w / target);
        vpX = 0;
        vpY = (h - vpH) / 2;
    }
    drawSetWorldPerPixel(WORLD_W / vpW);

    glViewport(vpX, vpY, vpW, vpH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0.0, WORLD_W, 0.0, WORLD_H);   // world is always 1280 x 720
    glMatrixMode(GL_MODELVIEW);
}

static void keyboard(unsigned char rawKey, int, int) {
    unsigned char key = (unsigned char)std::tolower(rawKey);
    const unsigned char ESC = 27, ENTER = 13;

    if (key == 's') {                      // sound on/off works on every screen
        soundSetEnabled(!soundEnabled());
        return;
    }

    switch (state) {
    case State::Menu:
        if (key == ESC) std::exit(0);
        if (key == 'm') startGame(InputMode::Mouse);
        if (key == 'w' && openCamera()) setState(State::Calibrate);
        break;

    case State::Calibrate:
        if (key == ESC) setState(State::Menu);
        if (key == 'c') visionRequestCalibration();
        if (key >= '1' && key <= '3') visionSetPreset(key - '1');
        if (key == 'd') calibShowMask = !calibShowMask;
        if (key == ' ' || key == ENTER) startGame(InputMode::Webcam);
        break;

    case State::Playing:
        if (key == ESC) setState(State::Menu);
        if (key == 'p' || key == ' ') setState(State::Paused);
        if (key == 'd') showDebug = !showDebug;
        if (key == 'v') showPreview = !showPreview;
        if (key >= '1' && key <= '3') visionSetPreset(key - '1');
        break;

    case State::Paused:
        if (key == ESC) setState(State::Menu);
        if (key == 'p' || key == ' ') setState(State::Playing);
        break;

    case State::GameOver:
        if (key == ' ' || key == ENTER) startGame(inputMode);
        if (key == 'm' || key == ESC) setState(State::Menu);
        break;
    }
}

// =====================================================================
// main
// =====================================================================
int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(1280, 720);
    glutCreateWindow("AirSlice");

    // Alpha blending: lets trails, particles and overlays be partly transparent
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);   // black letterbox bars

    // Two textures for the camera preview: the image and the binary mask
    glGenTextures(1, &camTex);
    glGenTextures(1, &maskTex);
    for (GLuint t : {camTex, maskTex}) {
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    soundInit();
    std::atexit(soundShutdown);  // close the sound channels when the program exits
    std::atexit(visionStop);     // stop the camera thread cleanly when the program exits
    loadHighScore();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutPassiveMotionFunc(mouseMove);   // mouse moving, no button pressed
    glutMotionFunc(mouseMove);          // mouse moving with a button held

    setState(State::Menu);
    lastTick = std::chrono::steady_clock::now();
    glutTimerFunc(16, timer, 0);
    glutMainLoop();
    return 0;
}
