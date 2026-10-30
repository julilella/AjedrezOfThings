#pragma once

#include <stdbool.h>
#include "buttons.h"

void flappy_kiwi_init(void);
void flappy_kiwi_on_button(button_id_t btn);
void flappy_kiwi_tick(void);
void flappy_kiwi_draw(void);
bool flappy_kiwi_should_exit(void);

