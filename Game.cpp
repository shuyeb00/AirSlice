#include <GL/glut.h>
#include <vector>
#include <deque>
#include <string>
#include <random>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include "Config.h"
#include "Draw.h"
#include "Game.h"
#include "Sound.h"

// =====================================================================
// Settings (tune the game here)
// =====================================================================
constexpr float GRAVITY          = 900.0f;  // world units / s^2, pulls down
constexpr float TRAIL_LIFE       = 0.15f;   // seconds a blade point stays visible
constexpr int   TRAIL_MAX        = 24;      // max points kept in the trail
constexpr float COMBO_WINDOW     = 0.30f;   // seconds between cuts that still count as one swipe
constexpr int   START_LIVES      = 3;
constexpr int   POINTS_PER_FRUIT = 10;
constexpr int   POINTS_PER_LEVEL = 100;     // level goes up every 100 points
constexpr int   MAX_LEVEL        = 10;
constexpr float FLASH_TIME       = 0.5f;    // red screen flash after cutting a bomb

// =====================================================================
// Data types
// =====================================================================
enum class ObjType { Orange, Apple, Watermelon, Lemon, Bomb };

struct Obj {                 // a whole fruit or a bomb flying through the air
    float x, y, vx, vy;
    float angle, spin;       // degrees, degrees per second
    float radius;
    ObjType type;
};

struct Half {                // one piece of a cut fruit
    float x, y, vx, vy;
    float cutAngle;          // direction of the cut, in degrees
    float fruitAngle;        // the fruit's rotation at the moment it was cut
    float rot, spin;         // extra rotation of the piece after the cut
    float radius;
    ObjType type;
    int side;                // +1 = piece on the left of the cut, -1 = on the right
};

struct Particle {            // juice drop or explosion spark
    float x, y, vx, vy;
    float life, maxLife;
    float size;
    float r, g, b;
};

struct Popup {               // floating text: "+10", "COMBO x3", "MISS"...
    float x, y;
    std::string text;
    float life, maxLife;
    float r, g, b;
    bool big;                // big = scalable stroke text with a "pop" animation
};

struct BladePoint {
    float x, y;
    float age;
};

struct Rgb { float r, g, b; };

// =====================================================================
// Game state
// =====================================================================
static std::vector<Obj>       objects;
static std::vector<Half>      halves;
static std::vector<Particle>  particles;
static std::vector<Popup>     popups;
static std::deque<BladePoint> blade;      // oldest point at the front

static int   score = 0, lives = START_LIVES, level = 1;
static bool  over = false;
static float spawnTimer = 0.0f;
static float comboTimer = 0.0f;
static int   comboCount = 0;
static float lastCutX = 0.0f, lastCutY = 0.0f;
static float flashTimer = 0.0f;
static float timeNow = 0.0f;              // seconds since the game started

static float minSliceSpeed = 800.0f;     // blade must move this fast (units/s) to cut
static float extraHitRadius = 0.0f;      // added to every fruit's radius in the hit test

static bool  havePrev = false;            // blade position in the previous frame
static float prevX = 0.0f, prevY = 0.0f;

static std::mt19937 rng(std::random_device{}());

static float randf(float a, float b) {
    std::uniform_real_distribution<float> d(a, b);
    return d(rng);
}
static int randi(int a, int b) {          // inclusive
    std::uniform_int_distribution<int> d(a, b);
    return d(rng);
}

// =====================================================================
// Colours of each fruit
// =====================================================================
static Rgb juiceColor(ObjType t) {
    switch (t) {
        case ObjType::Orange:     return {1.0f, 0.6f, 0.1f};
        case ObjType::Apple:      return {1.0f, 0.95f, 0.7f};
        case ObjType::Watermelon: return {0.95f, 0.15f, 0.2f};
        case ObjType::Lemon:      return {1.0f, 0.95f, 0.3f};
        default:                  return {1.0f, 0.6f, 0.1f};
    }
}

static Rgb fleshColor(ObjType t) {         // colour of the inside, seen on the cut face
    switch (t) {
        case ObjType::Orange:     return {1.0f, 0.72f, 0.3f};
        case ObjType::Apple:      return {1.0f, 0.95f, 0.75f};
        case ObjType::Watermelon: return {0.95f, 0.25f, 0.3f};
        case ObjType::Lemon:      return {1.0f, 1.0f, 0.65f};
        default:                  return {1.0f, 1.0f, 1.0f};
    }
}

// =====================================================================
// Effects
// =====================================================================
static void addPopup(float x, float y, const std::string& text,
                     Rgb c, bool big, float life = 1.0f) {
    popups.push_back({x, y, text, life, life, c.r, c.g, c.b, big});
}

static void burst(float x, float y, float baseVx, float baseVy, int count,
                  float minSpeed, float maxSpeed, float minSize, float maxSize,
                  float minLife, float maxLife, Rgb c, bool varyColor) {
    for (int i = 0; i < count; i++) {
        float a = randf(0.0f, 2.0f * PI);
        float s = randf(minSpeed, maxSpeed);
        Particle p;
        p.x = x;  p.y = y;
        p.vx = std::cos(a) * s + baseVx * 0.3f;
        p.vy = std::sin(a) * s + baseVy * 0.3f;
        p.maxLife = p.life = randf(minLife, maxLife);
        p.size = randf(minSize, maxSize);
        float k = varyColor ? randf(0.6f, 1.0f) : 1.0f;
        p.r = c.r * k;  p.g = c.g * k;  p.b = c.b * k;
        particles.push_back(p);
    }
}

// =====================================================================
// Spawning
// =====================================================================
static void spawnObject(bool bomb) {
    Obj o;
    o.x = randf(200.0f, WORLD_W - 200.0f);
    o.y = -60.0f;                                   // just below the screen

    // Choose how high it flies, then the launch speed from v = sqrt(2 g h)
    float peakY = randf(0.55f, 0.85f) * WORLD_H;
    o.vy = std::sqrt(2.0f * GRAVITY * (peakY - o.y));
    o.vx = randf(-150.0f, 150.0f) + (WORLD_W / 2 - o.x) * 0.4f;   // pull toward centre

    o.angle = randf(0.0f, 360.0f);

    if (bomb) {
        o.type = ObjType::Bomb;
        o.radius = 34.0f;
        o.spin = randf(-60.0f, 60.0f);
    } else {
        o.type = (ObjType)randi(0, 3);
        o.spin = randf(-180.0f, 180.0f);
        switch (o.type) {
            case ObjType::Watermelon: o.radius = 55.0f; break;
            case ObjType::Orange:     o.radius = 38.0f; break;
            case ObjType::Apple:      o.radius = 36.0f; break;
            default:                  o.radius = 32.0f; break;   // Lemon
        }
    }
    objects.push_back(o);
}

static void spawnWave() {
    int maxCount = std::min(2 + level / 2, 5);
    int count = randi(1, maxCount);
    float bombChance = std::min(0.06f + 0.03f * level, 0.30f);

    for (int i = 0; i < count; i++)
        spawnObject(randf(0.0f, 1.0f) < bombChance);

    float gap = std::max(0.5f, 1.5f - 0.1f * (level - 1));   // waves come faster each level
    spawnTimer = gap * randf(0.8f, 1.2f);
}

// =====================================================================
// Cutting
// =====================================================================
// Does the segment A-B touch the circle (C, r)?
// Finds the point on the segment closest to C and compares its distance with r.
static bool segmentHitsCircle(float ax, float ay, float bx, float by,
                              float cx, float cy, float r) {
    float dx = bx - ax, dy = by - ay;
    float len2 = dx * dx + dy * dy;
    float t = 0.0f;
    if (len2 > 0.0f) t = ((cx - ax) * dx + (cy - ay) * dy) / len2;
    t = std::max(0.0f, std::min(1.0f, t));
    float px = ax + t * dx, py = ay + t * dy;
    float ex = cx - px, ey = cy - py;
    return ex * ex + ey * ey <= r * r;
}

// dirX, dirY: unit vector of the blade direction
static void cutFruit(const Obj& o, float dirX, float dirY) {
    float cutDeg = std::atan2(dirY, dirX) * 180.0f / PI;
    float nx = -dirY, ny = dirX;          // normal: blade direction rotated by 90 degrees

    for (int side = 1; side >= -1; side -= 2) {
        Half h;
        h.x = o.x + nx * side * 4.0f;
        h.y = o.y + ny * side * 4.0f;
        h.vx = o.vx * 0.6f + nx * side * 140.0f + dirX * 80.0f;   // pieces fly apart
        h.vy = o.vy * 0.6f + ny * side * 140.0f + dirY * 80.0f;
        h.cutAngle = cutDeg;
        h.fruitAngle = o.angle;
        h.rot = 0.0f;
        h.spin = side * randf(120.0f, 260.0f);
        h.radius = o.radius;
        h.type = o.type;
        h.side = side;
        halves.push_back(h);
    }

    burst(o.x, o.y, o.vx, o.vy, 18, 150.0f, 450.0f, 3.0f, 7.0f, 0.4f, 0.8f,
          juiceColor(o.type), true);

    score += POINTS_PER_FRUIT;
    addPopup(o.x, o.y + o.radius + 10.0f, "+10", {1.0f, 1.0f, 1.0f}, false, 0.7f);

    soundPlay(Sfx::Slice);
    comboCount++;
    comboTimer = COMBO_WINDOW;
    lastCutX = o.x;
    lastCutY = o.y;
}

static void cutBomb(const Obj& o) {
    burst(o.x, o.y, 0.0f, 0.0f, 30, 200.0f, 600.0f, 4.0f, 9.0f, 0.5f, 1.0f,
          {1.0f, 0.55f, 0.1f}, true);
    burst(o.x, o.y, 0.0f, 0.0f, 15, 100.0f, 300.0f, 5.0f, 10.0f, 0.6f, 1.2f,
          {0.4f, 0.4f, 0.4f}, true);
    soundPlay(Sfx::Bomb);
    lives--;
    flashTimer = FLASH_TIME;
    comboCount = 0;
    addPopup(o.x, o.y + 50.0f, "BOMB!  -1 LIFE", {1.0f, 0.3f, 0.2f}, true, 1.2f);
}

// =====================================================================
// Public: reset and update
// =====================================================================
void gameReset() {
    objects.clear();
    halves.clear();
    particles.clear();
    popups.clear();
    blade.clear();
    score = 0;
    lives = START_LIVES;
    level = 1;
    over = false;
    spawnTimer = 2.0f;                    // 2 seconds to get your hand ready
    comboTimer = 0.0f;
    comboCount = 0;
    flashTimer = 0.0f;
    timeNow = 0.0f;
    havePrev = false;
    addPopup(WORLD_W / 2, WORLD_H * 0.55f, "GET READY", {1.0f, 1.0f, 1.0f}, true, 1.8f);
    soundPlay(Sfx::Blip);
}

void gameSetSliceAssist(float minSpeed, float extraRadius) {
    minSliceSpeed = minSpeed;
    extraHitRadius = extraRadius;
}

void gameUpdate(float dt, float bx, float by, bool active) {
    timeNow += dt;

    // ---------- Blade trail ----------
    if (active) {
        bool moved = blade.empty();
        if (!moved) {
            float dx = bx - blade.back().x, dy = by - blade.back().y;
            moved = dx * dx + dy * dy >= 4.0f;          // ignore movements under 2 units
        }
        if (moved) {
            blade.push_back({bx, by, 0.0f});
            if ((int)blade.size() > TRAIL_MAX) blade.pop_front();
        }
    } else {
        blade.clear();                                  // marker lost: no trail
    }
    for (BladePoint& p : blade) p.age += dt;
    while (!blade.empty() && blade.front().age > TRAIL_LIFE) blade.pop_front();

    // ---------- Spawning ----------
    if (!over) {
        spawnTimer -= dt;
        if (spawnTimer <= 0.0f) spawnWave();
    }

    // ---------- Physics: semi-implicit Euler ----------
    for (Obj& o : objects) {
        o.vy -= GRAVITY * dt;
        o.x  += o.vx * dt;
        o.y  += o.vy * dt;
        o.angle += o.spin * dt;
    }

    // ---------- Cutting: test this frame's blade segment against every object ----------
    if (active && havePrev && !over && dt > 0.0f) {
        float dx = bx - prevX, dy = by - prevY;
        float len = std::sqrt(dx * dx + dy * dy);
        float speed = len / dt;
        if (speed >= minSliceSpeed && len > 1.0f) {
            float dirX = dx / len, dirY = dy / len;
            for (size_t i = 0; i < objects.size();) {
                const Obj& o = objects[i];
                if (segmentHitsCircle(prevX, prevY, bx, by, o.x, o.y, o.radius + extraHitRadius)) {
                    if (o.type == ObjType::Bomb) cutBomb(o);
                    else                         cutFruit(o, dirX, dirY);
                    objects.erase(objects.begin() + i);   // do not advance i
                } else {
                    i++;
                }
            }
        }
    }
    havePrev = active;
    prevX = bx;
    prevY = by;

    // ---------- Combo: several cuts in one swipe ----------
    if (comboCount > 0) {
        comboTimer -= dt;
        if (comboTimer <= 0.0f) {
            if (comboCount >= 3) {
                int bonus = POINTS_PER_FRUIT * comboCount;
                score += bonus;
                char buf[48];
                std::snprintf(buf, sizeof(buf), "COMBO x%d  +%d", comboCount, bonus);
                addPopup(lastCutX, lastCutY + 60.0f, buf, {1.0f, 0.85f, 0.2f}, true, 1.3f);
                soundPlay(Sfx::Combo);
            }
            comboCount = 0;
        }
    }

    // ---------- Level ----------
    int newLevel = std::min(1 + score / POINTS_PER_LEVEL, MAX_LEVEL);
    if (newLevel > level) {
        level = newLevel;
        char buf[24];
        std::snprintf(buf, sizeof(buf), "LEVEL %d", level);
        addPopup(WORLD_W / 2, WORLD_H * 0.62f, buf, {0.5f, 0.9f, 1.0f}, true, 1.5f);
        soundPlay(Sfx::LevelUp);
    }

    // ---------- Objects that fell off the bottom ----------
    for (size_t i = 0; i < objects.size();) {
        const Obj& o = objects[i];
        if (o.vy < 0.0f && o.y < -o.radius - 30.0f) {
            if (o.type != ObjType::Bomb && !over) {      // a dropped fruit costs a life
                lives--;
                float mx = std::max(60.0f, std::min(WORLD_W - 60.0f, o.x));
                addPopup(mx, 40.0f, "MISS", {1.0f, 0.3f, 0.3f}, false, 1.0f);
                soundPlay(Sfx::Miss);
            }
            objects.erase(objects.begin() + i);
        } else {
            i++;
        }
    }

    // ---------- Halves ----------
    for (Half& h : halves) {
        h.vy -= GRAVITY * dt;
        h.x  += h.vx * dt;
        h.y  += h.vy * dt;
        h.rot += h.spin * dt;
    }
    halves.erase(std::remove_if(halves.begin(), halves.end(),
                     [](const Half& h) { return h.y < -150.0f; }),
                 halves.end());

    // ---------- Particles ----------
    for (Particle& p : particles) {
        p.vy -= 700.0f * dt;
        p.x  += p.vx * dt;
        p.y  += p.vy * dt;
        p.life -= dt;
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(),
                        [](const Particle& p) { return p.life <= 0.0f; }),
                    particles.end());

    // ---------- Pop-up texts ----------
    for (Popup& p : popups) {
        p.y += 40.0f * dt;
        p.life -= dt;
    }
    popups.erase(std::remove_if(popups.begin(), popups.end(),
                     [](const Popup& p) { return p.life <= 0.0f; }),
                 popups.end());

    if (flashTimer > 0.0f) flashTimer -= dt;

    if (lives <= 0 && !over) {
        lives = 0;
        over = true;
        soundPlay(Sfx::GameOver);
    }
}

// =====================================================================
// Drawing
// =====================================================================
// Draws one object centred at (0,0). The caller has already moved and rotated.
static void drawShape(ObjType type, float r) {
    switch (type) {
    case ObjType::Orange:
        glColor3f(1.0f, 0.55f, 0.05f); drawCircle(0, 0, r);
        glColor3f(1.0f, 0.75f, 0.35f); drawCircle(-r * 0.35f, r * 0.35f, r * 0.22f);
        glColor3f(0.2f, 0.6f, 0.15f);  drawEllipse(0, r * 0.95f, r * 0.25f, r * 0.12f);
        break;

    case ObjType::Apple:
        glColor3f(0.85f, 0.1f, 0.12f); drawCircle(0, 0, r);
        glColor3f(1.0f, 0.45f, 0.45f); drawCircle(-r * 0.35f, r * 0.3f, r * 0.2f);
        glColor3f(0.4f, 0.25f, 0.1f);  drawRect(-r * 0.06f, r * 0.85f, r * 0.12f, r * 0.4f);
        glColor3f(0.2f, 0.65f, 0.2f);  drawEllipse(r * 0.28f, r * 1.1f, r * 0.22f, r * 0.1f);
        break;

    case ObjType::Watermelon:
        glColor3f(0.1f, 0.45f, 0.15f); drawCircle(0, 0, r);
        glColor3f(0.3f, 0.75f, 0.3f);                       // stripes
        glLineWidth(4.0f);
        glBegin(GL_LINES);
        for (int k = -1; k <= 1; k++) {
            float x = k * 0.5f * r;
            float h = std::sqrt(r * r - x * x) * 0.92f;
            glVertex2f(x, -h); glVertex2f(x, h);
        }
        glEnd();
        glLineWidth(1.0f);
        break;

    case ObjType::Lemon:
        glColor3f(1.0f, 0.9f, 0.15f);
        drawEllipse(0, 0, r * 1.2f, r * 0.9f);
        drawCircle( r * 1.2f, 0, r * 0.18f);                 // pointed tips
        drawCircle(-r * 1.2f, 0, r * 0.18f);
        glColor3f(1.0f, 0.97f, 0.6f);  drawEllipse(-r * 0.4f, r * 0.3f, r * 0.3f, r * 0.15f);
        break;

    case ObjType::Bomb: {
        glColor3f(0.12f, 0.12f, 0.14f); drawCircle(0, 0, r);
        glColor3f(0.9f, 0.15f, 0.1f);                        // red ring: "danger"
        glLineWidth(3.0f); drawCircleOutline(0, 0, r - 2.0f); glLineWidth(1.0f);
        glColor3f(0.4f, 0.4f, 0.45f);   drawCircle(-r * 0.35f, r * 0.35f, r * 0.18f);
        glColor3f(0.3f, 0.3f, 0.32f);   drawRect(-r * 0.18f, r * 0.85f, r * 0.36f, r * 0.25f);
        glColor3f(0.6f, 0.45f, 0.25f);                       // fuse
        glLineWidth(3.0f);
        glBegin(GL_LINE_STRIP);
            glVertex2f(0.0f, r * 1.1f);
            glVertex2f(r * 0.15f, r * 1.3f);
            glVertex2f(r * 0.35f, r * 1.4f);
        glEnd();
        glLineWidth(1.0f);
        float s = 0.6f + 0.4f * std::sin(timeNow * 30.0f);   // flickering spark
        glColor3f(1.0f, 0.6f, 0.1f);  drawCircle(r * 0.35f, r * 1.4f, r * 0.18f * s + 2.0f);
        glColor3f(1.0f, 1.0f, 0.6f);  drawCircle(r * 0.35f, r * 1.4f, r * 0.08f * s + 1.0f);
        break;
    }
    }
}

// A cut piece: the whole fruit drawn with a CLIP PLANE that hides one side of the cut line,
// plus a flesh-coloured band along the cut so the inside shows.
static void drawHalf(const Half& h) {
    glPushMatrix();
    glTranslatef(h.x, h.y, 0.0f);
    glRotatef(h.cutAngle + h.rot, 0.0f, 0.0f, 1.0f);   // x axis now lies along the cut

    GLdouble plane[4] = {0.0, (double)h.side, 0.0, 0.0};  // keep side*y >= 0
    glClipPlane(GL_CLIP_PLANE0, plane);
    glEnable(GL_CLIP_PLANE0);

    glPushMatrix();
    glRotatef(h.fruitAngle - h.cutAngle, 0.0f, 0.0f, 1.0f);   // back to the fruit's own rotation
    drawShape(h.type, h.radius);
    glPopMatrix();

    Rgb f = fleshColor(h.type);
    float rx = (h.type == ObjType::Lemon) ? h.radius * 1.05f : h.radius * 0.9f;
    glColor3f(f.r, f.g, f.b);
    drawEllipse(0.0f, 0.0f, rx, h.radius * 0.28f);

    glDisable(GL_CLIP_PLANE0);
    glPopMatrix();
}

// Blade trail: a ribbon through the recent points, wide and bright at the head,
// thin and transparent at the tail (alpha blending)
static void drawBlade() {
    int n = (int)blade.size();
    if (n < 2) return;

    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i < n; i++) {
        const BladePoint& p = blade[i];

        int a = (i == 0) ? 0 : i - 1;
        int b = (i == n - 1) ? n - 1 : i + 1;
        float dx = blade[b].x - blade[a].x;
        float dy = blade[b].y - blade[a].y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.001f) len = 0.001f;
        float nx = -dy / len, ny = dx / len;       // perpendicular to the direction

        float k = (float)i / (n - 1);              // 0 at the tail, 1 at the head
        float fade = std::max(0.0f, 1.0f - p.age / TRAIL_LIFE);
        float halfWidth = 1.0f + 9.0f * k * fade;

        glColor4f(0.75f, 0.95f, 1.0f, k * fade);
        glVertex2f(p.x + nx * halfWidth, p.y + ny * halfWidth);
        glVertex2f(p.x - nx * halfWidth, p.y - ny * halfWidth);
    }
    glEnd();
}

static void drawParticles() {
    glBegin(GL_QUADS);
    for (const Particle& p : particles) {
        float a = p.life / p.maxLife;              // fade out as it dies
        float s = p.size * 0.5f;
        glColor4f(p.r, p.g, p.b, a);
        glVertex2f(p.x - s, p.y - s);
        glVertex2f(p.x + s, p.y - s);
        glVertex2f(p.x + s, p.y + s);
        glVertex2f(p.x - s, p.y + s);
    }
    glEnd();
}

static void drawPopups() {
    for (const Popup& p : popups) {
        float t = p.life / p.maxLife;              // 1 when new, 0 when gone
        float alpha = std::min(1.0f, t * 2.0f);
        glColor4f(p.r, p.g, p.b, alpha);
        if (p.big) {
            // Pop animation: starts 1.5x size and shrinks to 1x in the first 20% of its life
            float scale = (t > 0.8f) ? 1.0f + (t - 0.8f) * 2.5f : 1.0f;
            glPushMatrix();
            glTranslatef(p.x, p.y, 0.0f);
            glScalef(scale, scale, 1.0f);
            drawStrokeText(0.0f, 0.0f, p.text.c_str(), 38.0f, 3.0f);
            glPopMatrix();
        } else {
            drawTextCentered(p.x, p.y, p.text.c_str(), 18);
        }
    }
}

void gameDraw() {
    for (const Obj& o : objects) {
        glPushMatrix();
        glTranslatef(o.x, o.y, 0.0f);          // 1. move to the object's position
        glRotatef(o.angle, 0.0f, 0.0f, 1.0f);  // 2. rotate around its own centre
        drawShape(o.type, o.radius);           // 3. draw it at (0,0)
        glPopMatrix();
    }
    for (const Half& h : halves) drawHalf(h);
    drawParticles();
    drawBlade();
    drawPopups();

    if (flashTimer > 0.0f) {                   // red flash after a bomb
        glColor4f(1.0f, 0.1f, 0.05f, 0.45f * flashTimer / FLASH_TIME);
        drawRect(0.0f, 0.0f, WORLD_W, WORLD_H);
    }
}

// =====================================================================
// Getters
// =====================================================================
int  gameScore()       { return score; }
int  gameLives()       { return lives; }
int  gameLevel()       { return level; }
bool gameIsOver()      { return over; }
int  gameObjectCount() { return (int)objects.size(); }
