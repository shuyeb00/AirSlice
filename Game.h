#pragma once

// Starts a new game: score 0, full lives, level 1, empty screen
void gameReset();

// Advances the game by dt seconds.
// bladeX, bladeY: blade position in world units.
// bladeActive: false when the camera has lost the marker (no trail, no cutting).
void gameUpdate(float dt, float bladeX, float bladeY, bool bladeActive);

void gameDraw();

int  gameScore();
int  gameLives();
int  gameLevel();
bool gameIsOver();
int  gameObjectCount();

// Webcam aim is less precise than a mouse: allow slower cuts and a slightly bigger hit area
void gameSetSliceAssist(float minSliceSpeed, float extraHitRadius);
