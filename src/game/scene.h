#ifndef SCENE_H
#define SCENE_H

/*
 * scene.h - the hand-authored starting world.
 *
 * The world store is the module-level 48x48x8 grid declared by world.h, so
 * the builder needs no handle: it clears that store and writes the island
 * through the range-checked World_Set accessor.
 */
void Scene_BuildIsland(void);

#endif /* SCENE_H */
