#pragma once

#include <stdbool.h>
#include "buttons.h"

void buscaminas_init(void);
void buscaminas_on_button(button_id_t btn);
void buscaminas_tick(void);
void buscaminas_draw(void);
bool buscaminas_should_exit(void);

