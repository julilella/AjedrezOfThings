#pragma once

#include <stdbool.h>
#include "buttons.h"

void arkanoid_init(void);
void arkanoid_on_button(button_id_t btn);
void arkanoid_tick(void);
void arkanoid_draw(void);
bool arkanoid_should_exit(void);

