#pragma once
#include "state.h"
#include <Avatar.h>

void renderCurrent();
void handleInput();
bool screenAnimates();
void startIdleAvatar();
void stopIdleAvatar();
extern bool avatarActive;
