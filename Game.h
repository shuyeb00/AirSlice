#pragma once

enum class FruitType { Orange, Apple, Watermelon, Lemon };

struct Fruit {
    float x, y;        // position in world units
    float vx, vy;      // velocity in world units per second
    float angle;       // current rotation in degrees
    float spin;        // rotation speed in degrees per second
    float radius;
    FruitType type;
};

void gameInit();
void gameUpdate(float dt);
void gameDraw();
int  gameFruitCount();

// Tell the game where the blade (mouse or camera marker) is now
void gameBladeMove(float x, float y);
