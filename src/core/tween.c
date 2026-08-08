/*
 * fdk/src/core/tween.c — Lightweight tween / easing system
 *
 * No heap allocations during tick; tweens live in a fixed pool of
 * FDK_TWEEN_MAX slots.  fdk_tween() returns a pointer into the pool;
 * the pointer is valid until the tween finishes or is cancelled.
 *
 * fdk__tweens_tick() is called once per frame by fdk_ui_step().
 */
#include "fdk/fdk.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FDK_TWEEN_MAX 64

struct FDK_Tween {
    float             from, to;
    uint32_t          duration_ms;
    uint32_t          elapsed_ms;
    FDK_Easing        easing;
    FDK_TweenUpdateCb on_update;
    FDK_TweenDoneCb   on_done;
    void             *ud;
    bool              active;
    bool              done;
};

static FDK_Tween g_pool[FDK_TWEEN_MAX];

/* ── Easing functions ────────────────────────────────────────────────────── */
static float ease(FDK_Easing e, float t)
{
    /* t is in [0,1] */
    switch (e) {
    case FDK_EASE_LINEAR:
        return t;
    case FDK_EASE_IN_QUAD:
        return t * t;
    case FDK_EASE_OUT_QUAD:
        return t * (2.f - t);
    case FDK_EASE_IN_OUT_QUAD:
        return t < 0.5f ? 2.f*t*t : -1.f + (4.f - 2.f*t)*t;
    case FDK_EASE_IN_CUBIC:
        return t * t * t;
    case FDK_EASE_OUT_CUBIC: {
        float u = t - 1.f;
        return u*u*u + 1.f;
    }
    case FDK_EASE_IN_OUT_CUBIC:
        return t < 0.5f ? 4.f*t*t*t
                        : (t-1.f)*(2.f*t-2.f)*(2.f*t-2.f) + 1.f;
    case FDK_EASE_OUT_ELASTIC: {
        if (t <= 0.f) return 0.f;
        if (t >= 1.f) return 1.f;
        float p = 0.3f;
        return (float)(powf(2.f, -10.f*t) *
               sinf((t - p/4.f) * (2.f * 3.14159265f) / p) + 1.f);
    }
    case FDK_EASE_OUT_BOUNCE: {
        if (t < 1.f/2.75f)       return 7.5625f * t * t;
        if (t < 2.f/2.75f) { t -= 1.5f  /2.75f; return 7.5625f*t*t + 0.75f;  }
        if (t < 2.5f/2.75f){ t -= 2.25f /2.75f; return 7.5625f*t*t + 0.9375f;}
                              t -= 2.625f/2.75f; return 7.5625f*t*t + 0.984375f;
    }
    default:
        return t;
    }
}

/* ── Public API ─────────────────────────────────────────────────────────── */

FDK_Tween *fdk_tween(float from, float to, uint32_t duration_ms,
                      FDK_Easing easing,
                      FDK_TweenUpdateCb on_update,
                      FDK_TweenDoneCb   on_done,
                      void *ud)
{
    /* Find a free slot */
    for (int i = 0; i < FDK_TWEEN_MAX; i++) {
        if (!g_pool[i].active) {
            FDK_Tween *t = &g_pool[i];
            memset(t, 0, sizeof *t);
            t->from        = from;
            t->to          = to;
            t->duration_ms = duration_ms > 0 ? duration_ms : 1;
            t->easing      = easing;
            t->on_update   = on_update;
            t->on_done     = on_done;
            t->ud          = ud;
            t->active      = true;
            /* Fire initial value immediately */
            if (on_update) on_update(from, ud);
            return t;
        }
    }
    /* Pool full — fire callback with target value and return NULL */
    if (on_update) on_update(to, ud);
    if (on_done)   on_done(ud);
    return NULL;
}

void fdk_tween_cancel(FDK_Tween *t)
{
    if (t) t->active = false;
}

bool fdk_tween_is_done(const FDK_Tween *t)
{
    return !t || t->done;
}

/* Called once per frame by fdk_ui_step() */
void fdk__tweens_tick(void)
{
    static uint64_t last_ms = 0;
    uint64_t now = fdk_time_ms();
    if (last_ms == 0) { last_ms = now; return; }
    uint32_t delta = (uint32_t)(now - last_ms);
    last_ms = now;

    for (int i = 0; i < FDK_TWEEN_MAX; i++) {
        FDK_Tween *t = &g_pool[i];
        if (!t->active) continue;

        t->elapsed_ms += delta;

        float progress = (float)t->elapsed_ms / (float)t->duration_ms;
        if (progress >= 1.f) progress = 1.f;

        float value = t->from + (t->to - t->from) * ease(t->easing, progress);
        if (t->on_update) t->on_update(value, t->ud);

        if (progress >= 1.f) {
            t->done   = true;
            t->active = false;
            if (t->on_done) t->on_done(t->ud);
        }
    }
}
