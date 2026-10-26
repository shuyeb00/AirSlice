#include <GL/glut.h>
#include <vector>
#include <deque>
#include <random>
#include <cmath>
#include <algorithm>
#include "Config.h"
#include "Draw.h"
#include "Game.h"

// ---------- Settings ----------
constexpr float GRAVITY    = 900.0f;  // world units per second^2, pulls down
constexpr float TRAIL_LIFE = 0.15f;   // seconds a blade point stays visible
constexpr int   TRAIL_MAX  = 24;      // max points kept in the trail

// ---------- Game state ----------
struct BladePoint {
    float x, y;
    float age;   // seconds since this point was added
};

static std::vector<Fruit> fruits;
static std::deque<BladePoint> blade;  // oldest point at the front, newest at the back
static float spawnTimer = 0.0f;       // seconds until the next wave
static std::mt19937 rng;              // random number generator

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
    blade.clear();
    spawnTimer = 0.5f;
}

void gameBladeMove(float x, float y) {
    // Ignore tiny movements so a still hand does not pile up points
    if (!blade.empty()) {
        float dx = x - blade.back().x;
        float dy = y - blade.back().y;
        if (dx * dx + dy * dy < 4.0f) return;   // moved less than 2 units
    }
    blade.push_back({x, y, 0.0f});
    if ((int)blade.size() > TRAIL_MAX) blade.pop_front();
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

    // Age the blade points and drop the ones that are too old
    for (BladePoint& p : blade) p.age += dt;
    while (!blade.empty() && blade.front().age > TRAIL_LIFE) blade.pop_front();
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

// Blade trail: a ribbon through the recent points, wide and bright at the head,
// thin and transparent at the tail (uses alpha blending)
static void drawBlade() {
    int n = (int)blade.size();
    if (n < 2) return;

    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i < n; i++) {
        const BladePoint& p = blade[i];

        // Direction of travel at this point, from its neighbours
        int a = (i == 0) ? 0 : i - 1;
        int b = (i == n - 1) ? n - 1 : i + 1;
        float dx = blade[b].x - blade[a].x;
        float dy = blade[b].y - blade[a].y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.001f) len = 0.001f;

        // Perpendicular (normal) to the direction: rotate (dx, dy) by 90 degrees
        float nx = -dy / len;
        float ny =  dx / len;

        float k = (float)i / (n - 1);              // 0 at the tail, 1 at the head
        float fade = 1.0f - p.age / TRAIL_LIFE;    // 1 when new, 0 when about to vanish
        if (fade < 0.0f) fade = 0.0f;

        float halfWidth = 1.0f + 9.0f * k * fade;
        float alpha = k * fade;

        glColor4f(0.75f, 0.95f, 1.0f, alpha);
        glVertex2f(p.x + nx * halfWidth, p.y + ny * halfWidth);
        glVertex2f(p.x - nx * halfWidth, p.y - ny * halfWidth);
    }
    glEnd();
}

void gameDraw() {
    for (const Fruit& f : fruits) {
        glPushMatrix();
        glTranslatef(f.x, f.y, 0.0f);         // 1. move to the fruit's position
        glRotatef(f.angle, 0.0f, 0.0f, 1.0f); // 2. rotate around its own centre
        drawFruitShape(f);                    // 3. draw it at (0,0)
        glPopMatrix();
    }
    drawBlade();   // blade on top of the fruits
}

int gameFruitCount() {
    return (int)fruits.size();
}
