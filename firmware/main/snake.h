#pragma once

#include <stdbool.h>
#include "buttons.h"

void snake_init(void);
void snake_on_button(button_id_t btn);
void snake_tick(void);
void snake_draw(void);
bool snake_should_exit(void);

