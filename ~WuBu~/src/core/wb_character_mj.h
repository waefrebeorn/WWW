/*
 * wb_character_mj.h -- MuJoCo adapter for the character controller.
 *
 * Declared separately from wb_character.h so that a host can use the controller
 * without MuJoCo's types anywhere in its include graph: the backend-neutral
 * controller lives in wb_character.h, and this header is the only place the
 * MuJoCo-backed entry point appears. Link wb_character_mj.c (and the physics
 * modules) only where MuJoCo is actually wanted.
 */
#ifndef WB_CHARACTER_MJ_H
#define WB_CHARACTER_MJ_H

#include "wb_character.h"
#include "wb_physics.h"

/* Apply one frame of actions to a MuJoCo body. `cam` supplies the movement
 * basis (camera relative controls: "forward" is where the camera looks). */
void wb_character_apply(wb_character *c, wb_physics *p, int body,
                        const wb_actions *a, const wb_camera *cam, double dt);

#endif /* WB_CHARACTER_MJ_H */
