#ifndef SCENE_H
#include <Types.h>
#define SCENE_H

/*
 * scene.h - the hand-authored starting world.
 *
 * The world store is the module-level 48x48x8 grid declared by world.h, so
 * the builder needs no handle: it clears that store and writes the island
 * through the range-checked World_Set accessor.
 */
void Scene_BuildIsland(void);

/* True when a column belongs to the island (coast or lagoon). Exposed so the
 * scene test can assert containment without keeping its own copy of the coast
 * geometry, which went stale the moment the island was resized. */
Boolean Scene_IsIslandCell(short x, short y);

#endif /* SCENE_H */
