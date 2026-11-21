/*
 * particles.c - block-break specks.
 *
 * A fixed table, fixed-point positions and no allocation: this is the same
 * budget as the rest of the simulation. Each particle is a two-pixel square in
 * the broken block's colour, thrown up and out, pulled down by the same gravity
 * as the player, and gone after half a second.
 */

#include <Quickdraw.h>
#include <Types.h>

#include "particles.h"
#include "sprite_sheet.h"
#include "../game/iso.h"
#include "../game/world.h"
#include "hud.h"

#define kMaxParticles 24
#define kPerBreak 8

/* Third of a block per tick outward, two thirds up, gravity as the player's. */
#define kPartSpeed 21845L
#define kPartRise 43690L
#define kPartGravity 5243L
#define kPartLife 34

typedef struct {
    long x, y, z;    /* 16.16, world coordinates (tile + fraction) */
    long vz;
    short vx, vy;    /* velocity in 16.16 per tick */
    short life;
    unsigned long colour;
} Particle;

static Particle sParts[kMaxParticles];
static short sAlive = 0;

/* Cheap deterministic spread: a counter stepping by an odd number, so a break
 * throws specks in different directions without a random number generator. */
static short sSpin = 0;

void Particles_Clear(void)
{
    sAlive = 0;
}

void Particles_Spawn(short x, short y, short z, BlockID id)
{
    short i;
    /* The side colour, not the top: specks of the same green as the grass they
     * came out of are invisible, and the darker face reads against any ground. */
    const BlockLook *look = Block_Look(id);
    BlockColor c = look->left;

    for (i = 0; i < kPerBreak && sAlive < kMaxParticles; i++) {
        Particle *p = &sParts[sAlive++];
        short angle = (short)((sSpin += 7) & 15);

        p->x = ((long)x << 16) + ((long)(sSpin & 7) << 13);
        p->y = ((long)y << 16) + ((long)((sSpin >> 3) & 7) << 13);
        p->z = ((long)z << 16) + (1L << 16);
        p->vx = (short)(((angle & 3) - 2) * (kPartSpeed / 2));
        p->vy = (short)((((angle >> 2) & 3) - 2) * (kPartSpeed / 2));
        p->vz = kPartRise - ((long)(sSpin & 7) << 12);
        p->life = (short)(kPartLife - (sSpin & 7));
        /* The face colour lifted a third towards white: specks of the block's
         * own shade vanish against the same block's ground, and this reads on
         * grass, sand and stone alike. */
        {
            long r = (long)(c.red >> 8);
            long g = (long)(c.green >> 8);
            long b = (long)(c.blue >> 8);

            r += (255 - r) / 3;
            g += (255 - g) / 3;
            b += (255 - b) / 3;
            p->colour = ((unsigned long)r << 16) | ((unsigned long)g << 8) |
                        (unsigned long)b;
        }
    }
}

Boolean Particles_Update(void)
{
    short i = 0;

    while (i < sAlive) {
        Particle *p = &sParts[i];

        p->x += p->vx;
        p->y += p->vy;
        p->vz -= kPartGravity;
        p->z += p->vz;
        p->life--;

        if (p->life <= 0 || p->z < 0) {
            /* Swap-remove: order does not matter for specks. */
            sParts[i] = sParts[--sAlive];
            continue;
        }
        i++;
    }
    return (Boolean)(sAlive > 0);
}

void Particles_Bounds(const Rect *display, short camX, short camY, Rect *out)
{
    short i;
    short minX = 0, maxX = 0, minY = 0, maxY = 0;
    Boolean any = false;

    if (out == NULL) {
        return;
    }
    SetRect(out, 0, 0, 0, 0);
    if (display == NULL) {
        return;
    }

    for (i = 0; i < sAlive; i++) {
        short px = 0;
        short py = 0;

        Iso_Project((short)(sParts[i].x >> 16), (short)(sParts[i].y >> 16),
                    (short)(sParts[i].z >> 16), camX, camY, &px, &py);
        px = (short)(px + display->left);
        py = (short)(py + display->top);
        if (!any) {
            minX = maxX = px;
            minY = maxY = py;
            any = true;
        }
        else {
            if (px < minX) minX = px;
            if (px > maxX) maxX = px;
            if (py < minY) minY = py;
            if (py > maxY) maxY = py;
        }
    }

    if (any) {
        SetRect(out, (short)(minX - 3), (short)(minY - 3),
                (short)(maxX + 4), (short)(maxY + 4));
    }
}

void Particles_Draw(const Rect *display, short camX, short camY)
{
    short i;

    if (display == NULL) {
        return;
    }

    for (i = 0; i < sAlive; i++) {
        short px = 0;
        short py = 0;
        Rect dot;

        Iso_Project((short)(sParts[i].x >> 16), (short)(sParts[i].y >> 16),
                    (short)(sParts[i].z >> 16), camX, camY, &px, &py);
        px = (short)(px + display->left);
        py = (short)(py + display->top);

        SetRect(&dot, px, py, (short)(px + 3), (short)(py + 3));
        Sprite_FillRect(&dot, sParts[i].colour);
    }
}
