#include <GL/glut.h>
#include <vector>
#include <random>
#include <cmath>
#include <algorithm>
#include "Config.h"
#include "Draw.h"
#include "Game.h"

// ---------- Settings ----------
constexpr float GRAVITY = 900.0f;   // world units per second^2, pulls down

// ---------- Game state ----------
static std::vector<Fruit> fruits;
static float spawnTimer = 0.0f;     // seconds until the next wave
static std::mt19937 rng;            // random number generator

static float randf(float a, float b) {
    std::uniform_real_distribution<float> d(a, b);
    return d(rng);
}

// ---------- Spawning ----------
static void spawnFruit() {
    Fruit f;
    f.x = randf(200.0f, WORLD_W - 200.0f);
    f.y = -60.0f;                                  // start just below the screen

    // Choose how high it should fly, then compute the launch speed: v = sqrt(2 g h)
    float peakY = randf(0.55f, 0.85f) * WORLD_H;
    f.vy = std::sqrt(2.0f * GRAVITY * (peakY - f.y));

    // Sideways speed: a little random, with a pull toward the centre
    f.vx = randf(-150.0f, 150.0f) + (WORLD_W / 2 - f.x) * 0.4f;

    f.angle = randf(0.0f, 360.0f);
    f.spin  = randf(-180.0f, 180.0f);

    int t = (int)randf(0.0f, 4.0f);
    if (t > 3) t = 3;
    f.type = (FruitType)t;
    switch (f.type) {
        case FruitType::Watermelon: f.radius = 55.0f; break;
        case FruitType::Orange:     f.radius = 38.0f; break;
        case FruitType::Apple:      f.radius = 36.0f; break;
        case FruitType::Lemon:      f.radius = 32.0f; break;
    }
    fruits.push_back(f);
}

// ---------- Public functions ----------
void gameInit() {
    rng.seed(std::random_device{}());
    fruits.clear();
    spawnTimer = 0.5f;
}

void gameUpdate(float dt) {
    // Launch a new wave of 1-3 fruits every 0.9-1.6 seconds
    spawnTimer -= dt;
    if (spawnTimer <= 0.0f) {
        int count = (int)randf(1.0f, 4.0f);
        for (int i = 0; i < count; i++) spawnFruit();
        spawnTimer = randf(0.9f, 1.6f);
    }

    // Physics: semi-implicit Euler
    for (Fruit& f : fruits) {
        f.vy -= GRAVITY * dt;
        f.x  += f.vx * dt;
        f.y  += f.vy * dt;
        f.angle += f.spin * dt;
    }

    // Remove fruits that have fallen back below the screen
    fruits.erase(std::remove_if(fruits.begin(), fruits.end(),
                     [](const Fruit& f) { return f.y < -120.0f && f.vy < 0.0f; }),
                 fruits.end());
}

// ---------- Drawing ----------
// Ellipse = a unit circle scaled differently in x and y (scaling transformation)
static void drawEllipse(float cx, float cy, float rx, float ry) {
    glPushMatrix();
    glTranslatef(cx, cy, 0.0f);
    glScalef(rx, ry, 1.0f);
    drawCircle(0.0f, 0.0f, 1.0f);
    glPopMatrix();
}

// Draws one fruit centred at (0,0). The caller has already moved and rotated.
static void drawFruitShape(const Fruit& f) {
    float r = f.radius;
    switch (f.type) {
    case FruitType::Orange:
        glColor3f(1.0f, 0.55f, 0.05f); drawCircle(0, 0, r);
        glColor3f(1.0f, 0.75f, 0.35f); drawCircle(-r * 0.35f, r * 0.35f, r * 0.22f);
        glColor3f(0.2f, 0.6f, 0.15f);  drawEllipse(0, r * 0.95f, r * 0.25f, r * 0.12f);
        break;

    case FruitType::Apple:
        glColor3f(0.85f, 0.1f, 0.12f); drawCircle(0, 0, r);
        glColor3f(1.0f, 0.45f, 0.45f); drawCircle(-r * 0.35f, r * 0.3f, r * 0.2f);
        glColor3f(0.4f, 0.25f, 0.1f);                       // stem
        glBegin(GL_QUADS);
            glVertex2f(-r * 0.06f, r * 0.85f); glVertex2f(r * 0.06f, r * 0.85f);
            glVertex2f( r * 0.06f, r * 1.25f); glVertex2f(-r * 0.06f, r * 1.25f);
        glEnd();
        glColor3f(0.2f, 0.65f, 0.2f);  drawEllipse(r * 0.28f, r * 1.1f, r * 0.22f, r * 0.1f);
        break;

    case FruitType::Watermelon:
        glColor3f(0.1f, 0.45f, 0.15f); drawCircle(0, 0, r);
        glColor3f(0.3f, 0.75f, 0.3f);                       // stripes
        glLineWidth(4.0f);
        glBegin(GL_LINES);
        for (float sx = -0.5f; sx <= 0.51f; sx += 0.5f) {
            float x = sx * r;
            float h = std::sqrt(r * r - x * x) * 0.92f;
            glVertex2f(x, -h); glVertex2f(x, h);
        }
        glEnd();
        glLineWidth(1.0f);
        break;

    case FruitType::Lemon:
        glColor3f(1.0f, 0.9f, 0.15f);
        drawEllipse(0, 0, r * 1.2f, r * 0.9f);
        drawCircle( r * 1.2f, 0, r * 0.18f);                 // pointed tips
        drawCircle(-r * 1.2f, 0, r * 0.18f);
        glColor3f(1.0f, 0.97f, 0.6f);  drawEllipse(-r * 0.4f, r * 0.3f, r * 0.3f, r * 0.15f);
        break;
    }
}

void gameDraw() {
    for (const Fruit& f : fruits) {
        glPushMatrix();
        glTranslatef(f.x, f.y, 0.0f);         // 1. move to the fruit's position
        glRotatef(f.angle, 0.0f, 0.0f, 1.0f); // 2. rotate around its own centre
        drawFruitShape(f);                    // 3. draw it at (0,0)
        glPopMatrix();
    }
}

int gameFruitCount() {
    return (int)fruits.size();
}
