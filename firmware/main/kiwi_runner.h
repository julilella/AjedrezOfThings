#pragma once

#include <stdbool.h>
#include "buttons.h"

void kiwi_runner_init(void);
void kiwi_runner_on_button(button_id_t btn);
void kiwi_runner_tick(void);
void kiwi_runner_draw(void);
bool kiwi_runner_should_exit(void);

