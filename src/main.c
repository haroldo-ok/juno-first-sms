/* ---------------------------------------------------------------------------
 * JUNO FIRST for the Sega Master System  (devkitSMS / SDCC)
 *
 * A tribute to Konami's 1983 arcade game: fly forwards/backwards over a
 * perspective dot grid, shoot the enemy formations, rescue the astronaut to
 * turn the field red for bonus scoring, and warp out of trouble.
 *
 * World model
 *   x : u8 world column.  The player flies at depth PLAYER_REL where screen
 *       x == world x; everything else is projected towards the vanishing
 *       point with projx() (perspective, quarter-square multiply).
 *   z : u16 depth in 12.4 fixed point (16 = one depth unit), the world wraps
 *       every 4096 units.  rel = (z - cam_z) >> 4; 0..223 is on screen.
 * The grid is animated by palette cycling (12 pre-drawn phases, one lit).
 * The radar (top centre) is drawn into a RAM tile buffer and streamed to VRAM.
 * ------------------------------------------------------------------------- */
#include <string.h>
#include "SMSlib.h"
#include "PSGlib.h"
#include "gfx.h"
#include "snd.h"

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u16;
typedef int s16;

__sfr __at 0x7F PSGPort;

#define ST_TITLE 0
#define ST_PLAY  1
#define ST_OVER  2

#define PH_INTRO  0
#define PH_ACTION 1
#define PH_OVER   2

#define PL_ALIVE 0
#define PL_DYING 1
#define PL_WARP  2
#define PL_DEAD  3

#define MAX_EN  24
#define MAX_GRP 3
#define GSIZE   8
#define MAX_PB  4
#define MAX_EB  6                 /* 4 in normal waves, the boss fires spreads of up to 5 */
#define MAX_BU  4

#define PLAYER_REL 25
#define PLAYER_Y   150
#define VIS_Z      (224 * 16)        /* z distance beyond which things are off screen */
#define SPR_BUDGET 60

#define FORM_SNAKE 3

#define COL_LINE  RGB(1,2,3)
#define COL_BONUS RGB(3,0,0)
#define COL_RAIL  RGB(0,0,2)      /* static column lines */
#define COL_RAIL_BONUS RGB(1,0,0)

#define HUD_SCORE 1
#define HUD_HI    2
#define HUD_WAVE  4
#define HUD_SHIPS 8
#define HUD_BONUS 16
#define HUD_BOSS  32

#define LAST_WAVE 50
#define MAX_LIVES 5

#define DEATH_TIME 110

/* ---- game state (file-scope so the test harness can see it) ---- */
u8 game_state, phase, phase_timer;
u8 lives, wave, warps;
u8 score[6], hiscore[6];
u8 tenk_count, next_extra;
u16 cam_z;
u8 px;
u16 pxf;                       /* player x, 8.8 */
s16 pvx;                       /* 8.8 px/frame */
s16 pvz;                       /* 1/16 depth units per frame */
u8 pstate, ptimer, invuln, fire_cd;
u8 en_count, groups_left, grp_max, spawn_cd, fire_thr, dive_thr, max_div, n_div, eb_max;
u8 div_idx[3];                 /* enemies diving/returning (255 = free); n_div counts them */
s16 g_speed, dive_vz;
u8 eb_spd;
u16 bonus_timer;
u8 bonus_val, astro_cd;
u8 frame;
u16 loops;
u8 hud_dirty, msg_state;       /* msg_state: 1 = draw pending, 2 = clear pending */
const char *msg_text;
u8 cur_phase, line_col, line_dirty;
u8 sfx_prio, music_on, sound_mode;
u16 keys, oldkeys, pressed;
u16 rng = 0xACE1;
u16 stat_degraded, stat_dropped;   /* sprite limiter activity (for tests) */
u8 stat_rammed, stat_shot;         /* how the player died (for tuning/tests) */
u8 n_spr;
#ifdef PERFDBG
u8 vbl_count, vbl_last, dbg_kills;
u8 miss_nvis[26], miss_radar[17], miss_bu[5], miss_kill, miss_total, ok_nvis[26];
u8 dbg_fire, dbg_spawn, miss_fire, miss_spawn, ok_fire, ok_kill;
#endif
u8 demo;                       /* 1 = attract-mode demo, 2 = autopilot (testing) */
u16 demo_timer;
u8 burst_col;                  /* ship explosion: screen column of its centre */

/* SegaScope 3-D glasses: register at $FFF8 (bit 0 picks the open shutter);
 * writes also land in RAM at $DFF8, which nothing else uses (stack starts at $DFF0) */
#define GLASSES (*(volatile u8 *)0xFFF8)
u8 g3d;                        /* 0 off, 1 on, 2 on with eyes swapped */
u8 eye_l;                      /* frame being built is for the left eye (3D on) */
u8 band_i;                     /* line-interrupt band counter (reset each frame) */
volatile u8 vdp_busy;          /* main loop is streaming to VRAM: line IRQ must not touch the VDP */
const u8 *band_ptr;            /* per-row scroll table for the frame on screen */
const u8 band_zero[9] = {0};
u16 stat_lineirq;

/* boss (every 10th wave; wave 50 is the final one, then the ending) */
u8 boss_wave, boss_st, boss_lv, boss_x, boss_hp, boss_maxhp, boss_flash, boss_t, boss_rel, boss_dead_t;
u8 boss_pal;                   /* palette colour to apply in VBlank (0 = none pending) */
u16 boss_z, boss_fire_cd, boss_spawn_cd;
u8 won;                        /* final boss destroyed: play the ending */
u16 ai_old;                    /* autopilot's previous buttons (for press edges) */
u8 b3_busy;                    /* ROM bank 3 is mapped in slot 2: music must not run */
const u8 boss_col[6] = {0, RGB(0,1,3), RGB(0,2,1), RGB(2,0,3), RGB(3,2,0), RGB(3,0,0)};
#ifdef STEREODBG
u8 snap_eye, snap_n, snap_xn[128], snap_st[MAX_EN], snap_rel[MAX_EN], snap_sx[MAX_EN];
#endif

/* formations: each group of 8 flies as a unit, members hold slots */
u8 g_n[MAX_GRP], g_type[MAX_GRP], g_form[MAX_GRP], g_t[MAX_GRP], g_dir[MAX_GRP], g_base[MAX_GRP];
u16 g_x[MAX_GRP];              /* centre x, 8.8 */
u8 g_xh[MAX_GRP];
u16 g_z[MAX_GRP];              /* lead z */
s16 g_vz[MAX_GRP];

/* en_mode: 0 in formation, 1 diving at the player, 2 flying back to its slot */
typedef struct {
  u8 st, type, grp, slot, mode, xh, rel, tile;   /* rel 255 = off screen */
  s8 dx; u8 sx;                                  /* sx: cached projected x */
  u16 z, dzz;                                    /* dzz: slot depth offset (12.4) */
} Enemy;                                         /* 14 bytes; tests poke by offset */
Enemy en[MAX_EN];

u8 row_n[32];                  /* sprites per 8-line screen band */

u8 pb_st[MAX_PB], pb_x[MAX_PB];
u16 pb_z[MAX_PB];

u8 eb_st[MAX_EB];
u16 eb_x[MAX_EB], eb_z[MAX_EB];
s16 eb_vx[MAX_EB];

u8 as_st, as_x;
u16 as_z;

u8 bu_t[MAX_BU], bu_x[MAX_BU], bu_big[MAX_BU];    /* particle bursts; bu_t 0 = free */
u16 bu_z[MAX_BU];

u8 radar_buf[18 * 32];
u16 rad_off[64];               /* pixels plotted last time (erased next time) */
u8 rad_mask[64], rad_n;

/* visible enemies this frame (built once, used by bullets, sprites, autopilot) */
u8 n_vis, rot_vis;             /* enemies on screen this frame; draw-order rotation */
u8 ep_reuse, ep_g, ep_gvis, ep_gxh, ep_form, ep_gt2, ep_k, ph_g;
u16 ep_gz, ep_gdz;
u8 ep_nv0;
u8 g_any[MAX_GRP];             /* formation has a member on screen this frame */
u8 g_mov[MAX_GRP];             /* members of the formation currently diving/returning */
u8 occ[32];                    /* depth bands (8 units) holding a visible enemy */
/* scratch shared with the assembly routines */
u8 ep_i, ep_hit, ed_j, ed_cnt, ed_f;
u8 d3_x, d3_y, d3_rel, d3_tile;
u8 s1_x, s1_y, s1_tile;
u8 rp_mask;
u8 pd_sx, pd_sy, pd_tile, pd_n;
u8 g_vis[MAX_GRP];             /* formation overlaps the screen this frame */

const u8 spr_pal[16] = {
  RGB(0,0,0), RGB(3,3,0), RGB(2,2,2), RGB(1,1,1), RGB(3,0,0), RGB(3,2,0), RGB(3,3,3), RGB(0,3,0),
  RGB(0,3,3), RGB(0,1,3), RGB(3,0,3), RGB(2,0,0), RGB(0,0,2), RGB(3,2,2), RGB(0,2,0), RGB(0,0,0)
};
const u8 pts_h[4] = {1, 1, 2, 3};
const u8 pts_t[4] = {0, 5, 0, 0};
/* formations of 8: column, diagonal /, diagonal \, snake (dx animated), twin column */
const s8 form_dx[5][GSIZE] = {
  {0, 0, 0, 0, 0, 0, 0, 0},
  {-28, -20, -12, -4, 4, 12, 20, 28},
  {28, 20, 12, 4, -4, -12, -20, -28},
  {0, 0, 0, 0, 0, 0, 0, 0},
  {-14, 14, -14, 14, -14, 14, -14, 14}};
const u8 form_dz[5][GSIZE] = {
  {0, 12, 24, 36, 48, 60, 72, 84},
  {0, 12, 24, 36, 48, 60, 72, 84},
  {0, 12, 24, 36, 48, 60, 72, 84},
  {0, 12, 24, 36, 48, 60, 72, 84},
  {0, 0, 16, 16, 32, 32, 48, 48}};
/* ship-explosion ring colours by age (white-hot -> red -> gone) */
const u8 ring_col[7] = {RGB(3,3,3), RGB(3,3,1), RGB(3,2,0), RGB(3,1,0), RGB(2,0,0), RGB(1,0,0), 0};

/* ------------------------------------------------------------------ utils */
/* bulk tile data lives in ROM bank 3; slot 2 normally holds bank 2 (music) */
void load_tiles_b3(const void *src, u16 tile, u16 size) {
  b3_busy = 1;
  SMS_mapROMBank(3);
  SMS_loadTiles(src, tile, size);
  SMS_mapROMBank(2);
  b3_busy = 0;
}

u8 rand8(void) {
  rng ^= rng << 7; rng ^= rng >> 9; rng ^= rng << 8;
  return (u8)rng;
}

void frame_interrupt(void) {
  band_i = 0;
  if (b3_busy) return;
#ifdef PERFDBG
  vbl_count++;
#endif
  PSGFrame();
  PSGSFXFrame();
}

/* every 24 lines (left-eye frames only): horizontal scroll for the next band of
 * three tile rows, so each depth band of the field gets its own stereo parallax */
void line_interrupt(void) {
  u8 i = ++band_i;
  stat_lineirq++;
  if (i < 9 && !vdp_busy) INLINE_SMS_setBGScrollX(band_ptr[i]);
}

void sfx(const u8 *d, u8 ch, u8 prio) {
  if (demo == 1) return;
  if (prio >= sfx_prio || !PSGSFXGetStatus()) {
    PSGSFXPlay((void *)d, ch);
    sfx_prio = prio;
  }
}

/* perspective: world x at depth rel -> screen x (0 = off screen).
 * sx = 128 +/- (|wx-128| * kptab[rel]) >> 7, multiply by quarter squares:
 * a*k = sq4[a+k] - sq4[|a-k|].  wx in A, rel in L, result in A. */
u8 projx(u8 wx, u8 rel) __naked __sdcccall(1) {
  wx; rel;
  __asm
    ld   c, a
    ld   h, #0
    ld   de, #_kptab
    add  hl, de
    ld   b, (hl)
    ld   a, c
    sub  a, #128
    jr   c, 00110$
    call 00130$
    ld   a, h
    cp   a, #120
    jr   nc, 00120$
    add  a, #128
    ret
00110$:
    neg
    call 00130$
    ld   a, h
    cp   a, #121
    jr   nc, 00120$
    ld   c, a
    ld   a, #128
    sub  a, c
    ret
00120$:
    xor  a, a
    ret
00130$:
    ld   c, a
    sub  a, b
    jr   nc, 00131$
    neg
00131$:
    ld   l, a
    ld   h, #0
    add  hl, hl
    ld   de, #_sq4
    add  hl, de
    ld   e, (hl)
    inc  hl
    ld   d, (hl)
    ld   l, c
    ld   h, #0
    ld   a, b
    add  a, l
    ld   l, a
    jr   nc, 00132$
    inc  h
00132$:
    add  hl, hl
    push de
    ld   de, #_sq4
    add  hl, de
    ld   a, (hl)
    inc  hl
    ld   h, (hl)
    ld   l, a
    pop  de
    or   a, a
    sbc  hl, de
    add  hl, hl
    ret
  __endasm;
}


/* ===================== hand-written Z80 for the hot paths =====================
 * Enemy layout (sizeof 14): st 0, type 1, grp 2, slot 3, mode 4, xh 5, rel 6,
 * tile 7, dx 8, sx 9, z 10-11, dzz 12-13.  Sprite table layout as SMSlib's
 * SMS_addSprite_f: SpriteTableY[n] = y-1, SpriteTableXN[2n] = x, [2n+1] = tile. */
extern u8 SpriteNextFree;
void en_pass(void);       /* positions of formation members, rel, visible list, ram test */
void en_draw(void);       /* draw every visible enemy (scaled, scanline-limited) */
void draw3_fast(void);    /* one scaled object from d3_x, d3_rel, d3_tile */
void spr1_fast(void);     /* one sprite from s1_x, s1_y, s1_tile (scanline-limited) */
u8 par_x(u8 sx, u8 rel) __sdcccall(1);  /* left-eye stereo shift of a projected x */
void part_draw(const u8 *pairs) __sdcccall(1);  /* 4 particles around pd_sx/pd_sy */
void draw_eshots(void);   /* enemy shots (eb_*) */
void draw_pshots(void);   /* player shots (pb_*) */
u8 ds_i, ds_t0, ds_t1;

/* first live on-screen enemy within 5 depth units and 10 px of (x, rel): index+1, or 0 */
u8 pb_hit(u8 x, u8 rel) __naked __sdcccall(1) {
  x; rel;
  __asm
    push ix
    ld   c, a
    ld   a, l
    sub  a, #5
    ld   b, a
    ld   ix, #_en
    xor  a, a
    ld   (_ph_g), a
    ld   de, #14
00700$:
    ld   a, (_ph_g)
    ld   hl, #_g_any
    add  a, l
    ld   l, a
    jr   NC, 00708$
    inc  h
00708$:
    ld   a, (hl)
    or   a, a
    jr   NZ, 00710$
    ld   hl, #112
    ex   de, hl
    add  ix, de
    ex   de, hl
    jr   00790$
00710$:
    ld   h, #8
00701$:
    ld   a, 0 (ix)
    dec  a
    jr   NZ, 00703$
    ld   a, 6 (ix)
    sub  a, b
    cp   a, #11
    jr   NC, 00703$
    ld   a, 5 (ix)
    sub  a, c
    jr   NC, 00705$
    neg
00705$:
    cp   a, #10
    jr   C, 00720$
00703$:
    add  ix, de
    dec  h
    jr   NZ, 00701$
00790$:
    ld   a, (_ph_g)
    inc  a
    ld   (_ph_g), a
    cp   a, #3
    jr   C, 00700$
    xor  a, a
    pop  ix
    ret
00720$:
    push ix
    pop  hl
    ld   bc, #_en
    or   a, a
    sbc  hl, bc
    ld   a, #1
00721$:
    ld   c, l
    ld   b, h
    ld   bc, #14
    or   a, a
    sbc  hl, bc
    jr   C, 00722$
    inc  a
    jr   00721$
00722$:
    pop  ix
    ret
  __endasm;
}

void fast_asm_block(void) __naked {
  __asm
_en_pass::
    push ix
    xor  a, a
    ld   (_n_vis), a
    ld   (_ep_i), a
    ld   (_ep_g), a
    ld   hl, #_occ
    ld   b, #16
00201$:
    ld   (hl), a
    inc  hl
    ld   (hl), a
    inc  hl
    djnz 00201$
    ld   ix, #_en
00203$:
    ld   a, (_ep_g)
    ld   c, a
    ld   b, #0
    ld   hl, #_g_any
    add  hl, bc
    ld   (hl), #0
    ld   hl, #_g_n
    add  hl, bc
    ld   a, (hl)
    or   a, a
    jp   Z, 00280$
    ld   hl, #_g_vis
    add  hl, bc
    ld   a, (hl)
    ld   (_ep_gvis), a
    or   a, a
    jr   NZ, 00204$
    ld   hl, #_g_mov
    add  hl, bc
    ld   a, (hl)
    or   a, a
    jp   Z, 00280$
00204$:
    ld   hl, #_g_xh
    add  hl, bc
    ld   a, (hl)
    ld   (_ep_gxh), a
    ld   hl, #_g_form
    add  hl, bc
    ld   a, (hl)
    ld   (_ep_form), a
    ld   hl, #_g_t
    add  hl, bc
    ld   a, (hl)
    add  a, a
    ld   (_ep_gt2), a
    ld   hl, #_g_z
    add  hl, bc
    add  hl, bc
    ld   e, (hl)
    inc  hl
    ld   d, (hl)
    ld   (_ep_gz), de
    ex   de, hl
    ld   de, (_cam_z)
    or   a, a
    sbc  hl, de
    ld   (_ep_gdz), hl
    ld   a, (_n_vis)
    ld   (_ep_nv0), a
    ld   a, #8
    ld   (_ep_k), a
00202$:
    ld   a, 0 (ix)
    or   a, a
    jp   Z, 00290$
    ld   a, 4 (ix)
    or   a, a
    jr   NZ, 00230$
    ld   a, (_ep_gvis)
    or   a, a
    jr   Z, 00242$
    ld   a, (_ep_form)
    cp   a, #3
    jr   NZ, 00215$
    ld   a, (_ep_gt2)
    add  a, 3 (ix)
    ld   e, a
    ld   d, #0
    ld   hl, #_sin4
    add  hl, de
    ld   a, (hl)
    ld   8 (ix), a
00215$:
    ld   a, (_ep_gxh)
    add  a, 8 (ix)
    ld   5 (ix), a
    ld   hl, (_ep_gdz)
    ld   e, 12 (ix)
    ld   d, 13 (ix)
    add  hl, de
    jr   00241$
00230$:
    ld   l, 10 (ix)
    ld   h, 11 (ix)
    ld   de, (_cam_z)
    or   a, a
    sbc  hl, de
00241$:
    ld   a, h
    cp   a, #0x0E
    jr   C, 00245$
00242$:
    ld   6 (ix), #255
    ld   9 (ix), #0
    jp   00290$
00245$:
    ld   a, l
    rrca
    rrca
    rrca
    rrca
    and  a, #0x0F
    ld   c, a
    ld   a, h
    add  a, a
    add  a, a
    add  a, a
    add  a, a
    or   a, c
    ld   6 (ix), a
    ld   l, a
    ld   a, (_ep_i)
    ld   c, a
    ld   a, (_frame)
    xor  a, c
    and  a, #1
    jr   Z, 00246$
    ld   a, 9 (ix)
    or   a, a
    jr   NZ, 00248$
00246$:
    ld   a, 5 (ix)
    call _projx
    ld   9 (ix), a
00248$:
    ld   a, 6 (ix)
    sub  a, #20
    cp   a, #11
    jr   NC, 00270$
    ld   a, (_pstate)
    or   a, a
    jr   NZ, 00270$
    ld   a, (_invuln)
    or   a, a
    jr   NZ, 00270$
    ld   a, (_px)
    sub  a, 5 (ix)
    jr   NC, 00250$
    neg
00250$:
    cp   a, #10
    jr   NC, 00270$
    ld   a, (_ep_i)
    inc  a
    ld   (_ep_hit), a
    jr   00290$
00270$:
    ld   a, 9 (ix)
    or   a, a
    jr   Z, 00271$
    ld   b, a
    ld   a, (_eye_l)
    or   a, a
    jr   Z, 00272$
    ld   a, b
    ld   l, 6 (ix)
    call _par_x
    ld   b, a
00272$:
    ld   a, (_ed_f)
    add  a, 7 (ix)
    ld   c, a
    ld   a, 6 (ix)
    call _draw3_r
00271$:
    ld   hl, #_n_vis
    inc  (hl)
    ld   a, 6 (ix)
    rrca
    rrca
    rrca
    and  a, #0x1F
    ld   c, a
    ld   b, #0
    ld   hl, #_occ
    add  hl, bc
    ld   (hl), #1
00290$:
    ld   de, #14
    add  ix, de
    ld   hl, #_ep_i
    inc  (hl)
    ld   hl, #_ep_k
    dec  (hl)
    jp   NZ, 00202$
    ld   a, (_n_vis)               ; any member drawn since the group began?
    ld   hl, #_ep_nv0
    cp   a, (hl)
    jr   Z, 00295$
    ld   a, (_ep_g)
    ld   c, a
    ld   b, #0
    ld   hl, #_g_any
    add  hl, bc
    ld   (hl), #1
    jr   00295$
00280$:
    ld   b, #8
    ld   de, #14
00281$:
    ld   6 (ix), #255
    ld   9 (ix), #0
    add  ix, de
    djnz 00281$
    ld   a, (_ep_i)
    add  a, #8
    ld   (_ep_i), a
00295$:
    ld   a, (_ep_g)
    inc  a
    ld   (_ep_g), a
    cp   a, #3
    jp   C, 00203$
    pop  ix
    ret

_en_draw::
    push ix
    ld   a, (_rot_vis)
    inc  a
    cp   a, #3
    jr   C, 00301$
    xor  a, a
00301$:
    ld   (_rot_vis), a
    ld   (_ed_j), a
    ld   ix, #_en
    or   a, a
    jr   Z, 00302$
    ld   de, #112
    ld   b, a
00303$:
    add  ix, de
    djnz 00303$
00302$:
    ld   a, #3
    ld   (_ep_k), a
00305$:
    ld   a, (_ed_j)
    ld   c, a
    ld   b, #0
    ld   hl, #_g_any
    add  hl, bc
    ld   a, (hl)
    or   a, a
    jr   NZ, 00306$
    ld   de, #112
    add  ix, de
    jr   00390$
00306$:
    ld   a, #8
    ld   (_ed_cnt), a
00310$:
    ld   a, 0 (ix)
    dec  a
    jr   NZ, 00380$
    ld   a, 9 (ix)
    or   a, a
    jr   Z, 00380$
    ld   b, a
    ld   a, (_ed_f)
    add  a, 7 (ix)
    ld   c, a
    ld   a, 6 (ix)
    cp   a, #255
    jr   Z, 00380$
    call _draw3_r
00380$:
    ld   de, #14
    add  ix, de
    ld   hl, #_ed_cnt
    dec  (hl)
    jr   NZ, 00310$
00390$:
    ld   a, (_ed_j)
    inc  a
    cp   a, #3
    jr   C, 00391$
    xor  a, a
    ld   ix, #_en
00391$:
    ld   (_ed_j), a
    ld   hl, #_ep_k
    dec  (hl)
    jr   NZ, 00305$
    pop  ix
    ret

_draw3_fast::
    ld   a, (_d3_x)
    ld   b, a
    ld   a, (_d3_tile)
    ld   c, a
    ld   a, (_d3_rel)
_draw3_r::
    ld   e, a
    ld   d, #0
    ld   hl, #_ylut
    add  hl, de
    ld   a, (hl)
    sub  a, #8
    ld   (_d3_y), a
    ld   d, e
    rrca
    rrca
    rrca
    and  a, #0x1F
    add  a, #<_row_n
    ld   l, a
    ld   a, #0
    adc  a, #>_row_n
    ld   h, a
    ld   a, (hl)
    inc  hl
    cp   a, (hl)
    jr   NC, 00401$
    ld   a, (hl)
00401$:
    inc  hl
    cp   a, (hl)
    jr   NC, 00402$
    ld   a, (hl)
00402$:
    dec  hl
    dec  hl
    ld   e, a
    ld   a, d
    cp   a, #136
    jr   NC, 00420$
    ld   a, e
    cp   a, #7
    jr   NC, 00420$
    ld   a, (_n_spr)
    cp   a, #52
    jr   NC, 00420$
    ld   a, d
    cp   a, #64
    jr   C, 00405$
    ld   a, c
    add  a, #4
    ld   c, a
00405$:
    push hl
    ld   a, b
    sub  a, #8
    ld   b, a
    ld   a, (_SpriteNextFree)
    cp   a, #63
    jr   NC, 00409$
    ld   e, a
    ld   d, #0
    ld   hl, #_SpriteTableY
    add  hl, de
    ld   a, (_d3_y)
    dec  a
    ld   (hl), a
    inc  hl
    ld   (hl), a
    ld   hl, #_SpriteTableXN
    add  hl, de
    add  hl, de
    ld   (hl), b
    inc  hl
    ld   (hl), c
    inc  hl
    ld   a, b
    add  a, #8
    ld   (hl), a
    inc  hl
    inc  c
    inc  c
    ld   (hl), c
    ld   a, e
    add  a, #2
    ld   (_SpriteNextFree), a
00409$:
    pop  hl
    ld   b, #2
    jr   00430$
00420$:
    ld   a, e
    cp   a, #8
    jr   NC, 00440$
    ld   a, (_n_spr)
    cp   a, #56
    jr   NC, 00440$
    ld   a, d
    cp   a, #136
    jr   NC, 00425$
    push hl
    ld   hl, (_stat_degraded)
    inc  hl
    ld   (_stat_degraded), hl
    pop  hl
00425$:
    ld   a, c
    add  a, #8
    ld   c, a
    ld   a, b
    sub  a, #4
    ld   b, a
    push hl
    call _emit_spr
    pop  hl
    ld   b, #1
00430$:
    ld   a, (hl)
    add  a, b
    ld   (hl), a
    inc  hl
    ld   a, (hl)
    add  a, b
    ld   (hl), a
    inc  hl
    ld   a, (hl)
    add  a, b
    ld   (hl), a
    ld   a, (_n_spr)
    add  a, b
    ld   (_n_spr), a
    ret
00440$:
    ld   hl, (_stat_dropped)
    inc  hl
    ld   (_stat_dropped), hl
    ret

_emit_spr::
    ld   a, (_SpriteNextFree)
    cp   a, #64
    ret  NC
    ld   e, a
    ld   d, #0
    ld   hl, #_SpriteTableY
    add  hl, de
    ld   a, (_d3_y)
    dec  a
    ld   (hl), a
    ld   hl, #_SpriteTableXN
    add  hl, de
    add  hl, de
    ld   (hl), b
    inc  hl
    ld   (hl), c
    ld   a, e
    inc  a
    ld   (_SpriteNextFree), a
    ret

_spr1_fast::
    ld   a, (_s1_x)
    ld   b, a
    ld   a, (_s1_tile)
    ld   c, a
    ld   a, (_s1_y)
_spr1_r::
    ld   (_d3_y), a
    ld   e, a
    ld   a, (_n_spr)
    cp   a, #60
    jr   NC, 00540$
    ld   a, e
    rrca
    rrca
    rrca
    and  a, #0x1F
    add  a, #<_row_n
    ld   l, a
    ld   a, #0
    adc  a, #>_row_n
    ld   h, a
    ld   a, #7
    cp   a, (hl)
    jr   C, 00540$
    inc  hl
    cp   a, (hl)
    jr   C, 00540$
    inc  hl
    cp   a, (hl)
    jr   C, 00540$
    inc  (hl)
    dec  hl
    inc  (hl)
    dec  hl
    inc  (hl)
    ld   hl, #_n_spr
    inc  (hl)
    jp   _emit_spr
00540$:
    ld   hl, (_stat_dropped)
    inc  hl
    ld   (_stat_dropped), hl
    ret

; enemy shots: x 8.8 in eb_x, z in eb_z; big blinking ball near, small far
_draw_eshots::
    xor  a, a
    ld   (_ds_i), a
00901$:
    ld   a, (_ds_i)
    ld   c, a
    ld   b, #0
    ld   hl, #_eb_st
    add  hl, bc
    ld   a, (hl)
    or   a, a
    jr   Z, 00990$
    ld   hl, #_eb_z
    add  hl, bc
    add  hl, bc
    ld   e, (hl)
    inc  hl
    ld   d, (hl)
    ex   de, hl
    ld   de, (_cam_z)
    or   a, a
    sbc  hl, de
    ld   a, h
    cp   a, #0x0E
    jr   NC, 00990$
    ld   a, l
    rrca
    rrca
    rrca
    rrca
    and  a, #0x0F
    ld   e, a
    ld   a, h
    add  a, a
    add  a, a
    add  a, a
    add  a, a
    or   a, e
    ld   (_d3_rel), a
    ld   hl, #_eb_x + 1
    add  hl, bc
    add  hl, bc
    ld   a, (hl)
    ld   hl, #_d3_rel
    ld   l, (hl)
    call _projx
    or   a, a
    jr   Z, 00990$
    ld   b, a
    ld   a, (_eye_l)
    or   a, a
    ld   a, b
    jr   Z, 00960$
    ld   hl, #_d3_rel
    ld   l, (hl)
    call _par_x
00960$:
    sub  a, #4
    ld   b, a
    ld   a, (_d3_rel)
    ld   e, a
    cp   a, #110
    ld   a, (_ds_t1)
    jr   NC, 00905$
    ld   a, (_ds_t0)
00905$:
    ld   c, a
    ld   d, #0
    ld   hl, #_ylut
    add  hl, de
    ld   a, (hl)
    sub  a, #5
    call _spr1_r
00990$:
    ld   a, (_ds_i)
    inc  a
    ld   (_ds_i), a
    cp   a, #MAX_EB
    jr   C, 00901$
    ret

; player shots: x in pb_x, z in pb_z (always on screen while alive)
_draw_pshots::
    xor  a, a
    ld   (_ds_i), a
00911$:
    ld   a, (_ds_i)
    ld   c, a
    ld   b, #0
    ld   hl, #_pb_st
    add  hl, bc
    ld   a, (hl)
    or   a, a
    jr   Z, 00980$
    ld   hl, #_pb_z
    add  hl, bc
    add  hl, bc
    ld   e, (hl)
    inc  hl
    ld   d, (hl)
    ex   de, hl
    ld   de, (_cam_z)
    or   a, a
    sbc  hl, de
    ld   a, h
    cp   a, #0x0E
    jr   NC, 00980$
    ld   a, l
    rrca
    rrca
    rrca
    rrca
    and  a, #0x0F
    ld   e, a
    ld   a, h
    add  a, a
    add  a, a
    add  a, a
    add  a, a
    or   a, e
    ld   (_d3_rel), a
    ld   hl, #_pb_x
    add  hl, bc
    ld   a, (hl)
    ld   hl, #_d3_rel
    ld   l, (hl)
    call _projx
    or   a, a
    jr   Z, 00980$
    ld   b, a
    ld   a, (_eye_l)
    or   a, a
    ld   a, b
    jr   Z, 00961$
    ld   hl, #_d3_rel
    ld   l, (hl)
    call _par_x
00961$:
    sub  a, #4
    ld   b, a
    ld   a, (_d3_rel)
    ld   e, a
    cp   a, #100
    ld   c, #SPR_PBUL_S
    jr   NC, 00915$
    ld   c, #SPR_PBUL
00915$:
    ld   d, #0
    ld   hl, #_ylut
    add  hl, de
    ld   a, (hl)
    sub  a, #6
    call _spr1_r
00980$:
    ld   a, (_ds_i)
    inc  a
    ld   (_ds_i), a
    cp   a, #4
    jr   C, 00911$
    ret

; A = projected x, L = rel -> A = x for this eye (left eye: x - par_l[rel], kept in 8..247)
_par_x::
    ld   c, a
    ld   a, (_eye_l)
    or   a, a
    ld   a, c
    ret  Z
    ld   h, #0
    ld   de, #_par_l
    add  hl, de
    sub  a, (hl)
    cp   a, #8
    jr   NC, 00951$
    ld   a, #8
    ret
00951$:
    cp   a, #248
    ret  C
    ld   a, #247
    ret

; HL -> (dx,dy) pairs, every other pair is drawn (4 of 8)
_part_draw::
    ld   a, #4
    ld   (_pd_n), a
00601$:
    ld   a, (hl)
    inc  hl
    ld   e, (hl)
    inc  hl
    inc  hl
    inc  hl
    push hl
    ld   l, a
    rla
    sbc  a, a
    ld   h, a
    ld   a, (_pd_sx)
    sub  a, #4
    add  a, l
    ld   l, a
    ld   a, h
    adc  a, #0
    jr   NZ, 00690$
    ld   a, l
    cp   a, #249
    jr   NC, 00690$
    ld   b, a
    ld   a, e
    ld   l, a
    rla
    sbc  a, a
    ld   h, a
    ld   a, (_pd_sy)
    add  a, l
    ld   l, a
    ld   a, h
    adc  a, #0
    jr   NZ, 00690$
    ld   a, l
    cp   a, #22
    jr   C, 00690$
    cp   a, #187
    jr   NC, 00690$
    ld   e, a
    ld   a, (_pd_tile)
    ld   c, a
    ld   a, e
    call _spr1_r
00690$:
    pop  hl
    ld   a, (_pd_n)
    dec  a
    ld   (_pd_n), a
    jr   NZ, 00601$
    ret
  __endasm;
}

void print_at(u8 x, u8 y, const char *s) {
  SMS_setNextTileatXY(x, y);
  while (*s) { SMS_setTile((u16)(*s - 32)); s++; }
}

void print_yel(u8 x, u8 y, const char *s) {
  SMS_setNextTileatXY(x, y);
  while (*s) { SMS_setTile((u16)(*s - 32) | TILE_USE_SPRITE_PALETTE); s++; }
}

void draw_field_row(u8 r) {
  u8 i;
  const u8 *m;
  SMS_setNextTileatXY(0, r);
  if (r < 3) { for (i = 32; i; i--) SMS_setTile(0); return; }
  m = field_map + (u16)(r - 3) * 32;
  for (i = 32; i; i--) SMS_setTile(*m++);
}

void draw_burst_row(u8 r) {
  u8 i, c, v;
  const u8 *m = burst_map + (u16)(r - 3) * 32;
  c = (u8)(16 - burst_col);
  SMS_setNextTileatXY(0, r);
  for (i = 32; i; i--) {
    v = m[c & 31]; c++;
    SMS_setTile(v ? (u16)(BURST_BASE - 1 + v) : 0);
  }
}

void draw_radar_frame(void) {
  u8 r, c;
  for (r = 0; r < 3; r++) {
    SMS_setNextTileatXY(14, r);
    for (c = 0; c < 6; c++) SMS_setTile((u16)(RADAR_BASE + r * 6 + c));
  }
}

void put_digits(u8 x, u8 y, const u8 *d) {
  u8 i;
  SMS_setNextTileatXY(x, y);
  for (i = 0; i < 6; i++) SMS_setTile((u16)(d[i] + 16));
}

void draw_boss_bar(void);

void hud_flush(void) {
  if (hud_dirty & HUD_SCORE) put_digits(7, 0, score);
  if (hud_dirty & HUD_HI) put_digits(26, 0, hiscore);
  if (hud_dirty & HUD_WAVE) {
    SMS_setNextTileatXY(28, 1);
    SMS_setTile((u16)(wave / 10 + 16)); SMS_setTile((u16)(wave % 10 + 16));
  }
  if (hud_dirty & HUD_SHIPS) {
    SMS_setTileatXY(7, 1, (u16)(lives + 16));
    SMS_setTileatXY(7, 2, (u16)(warps + 16));
  }
  if (hud_dirty & HUD_BOSS) draw_boss_bar();
  if ((hud_dirty & HUD_BONUS) && !boss_wave) {
    if (bonus_timer) {
      print_yel(21, 2, "BONUS ");
      SMS_setTile((u16)((bonus_val >= 20 ? 2 : bonus_val >= 10 ? 1 : 0) + 16) | TILE_USE_SPRITE_PALETTE);
      SMS_setTile((u16)(bonus_val % 10 + 16) | TILE_USE_SPRITE_PALETTE);
      SMS_setTile(16 | TILE_USE_SPRITE_PALETTE); SMS_setTile(16 | TILE_USE_SPRITE_PALETTE);
    } else {
      print_at(21, 2, "          ");
    }
  }
  hud_dirty = 0;
}

void show_msg(const char *s) { msg_text = s; msg_state = 1; }
void clear_msg(void) { msg_state = 2; }

void msg_flush(void) {
  u8 n;
  const char *s;
  if (msg_state == 1) {
    draw_field_row(11);
    for (n = 0, s = msg_text; *s; s++) n++;
    print_at((32 - n) >> 1, 11, msg_text);
    if (phase == PH_INTRO) {     /* "WAVE nn": patch the number in */
      SMS_setNextTileatXY(((32 - n) >> 1) + 5, 11);
      SMS_setTile((u16)(wave / 10 + 16)); SMS_setTile((u16)(wave % 10 + 16));
    }
  } else if (msg_state == 2) {
    draw_field_row(11);
  }
  msg_state = 0;
}

void field_update(void) {
  u8 p = (u8)(((cam_z & 511) * 11) >> 9);
  if (p != cur_phase || line_dirty) {
    if (line_dirty) {
      u8 i; for (i = 2; i < 13; i++) SMS_setBGPaletteColor(i, 0);
      SMS_setBGPaletteColor(13, line_col == COL_BONUS ? COL_RAIL_BONUS : COL_RAIL);
    }
    SMS_setBGPaletteColor(2 + cur_phase, 0);
    SMS_setBGPaletteColor(2 + p, line_col);
    cur_phase = p;
    line_dirty = 0;
  }
}

void inc_digit(u8 i) {
  for (;;) {
    if (++score[i] < 10) break;
    score[i] = 0;
    if (i == 2) {                 /* carry into the ten-thousands digit */
      if (++tenk_count == next_extra) {   /* extra ship at 20,000, then every 60,000 */
        next_extra += 6;
        if (lives < MAX_LIVES) { lives++; hud_dirty |= HUD_SHIPS; }
        sfx(sfx_extra, SFX_CHANNEL2, 4);
      }
    }
    if (!i) break;
    i--;
  }
}

void add_score(u8 hundreds, u8 tens) {
  while (tens--) inc_digit(4);
  while (hundreds--) inc_digit(3);
  hud_dirty |= HUD_SCORE;
}

/* ---------------------------------------------------------------- sprites */
/* Sprite limiter: the VDP shows only 8 sprites per scanline.  row_n counts
 * sprites per 8-line band; a 16-line sprite touches 3 bands at most, so the
 * fullest of them is a safe bound. */
void band_add(u8 y, u8 n) {
  u8 *p = row_n + (y >> 3);
  p[0] += n; p[1] += n; p[2] += n;
  n_spr += n;
}

#define spr1(x, y, tile) do { s1_x = (x); s1_y = (y); s1_tile = (tile); spr1_fast(); } while (0)

void draw3(u8 sx, u8 rel, u8 tile) {
  d3_x = sx; d3_rel = rel; d3_tile = tile;
  draw3_fast();
}

void draw16(u8 x, u8 y, u8 tile) {
  SMS_addSprite(x - 8, y, tile); SMS_addSprite(x, y, tile + 2);
}

/* ---------------------------------------------------------------- effects */
void spawn_burst(u8 x, u16 z, u8 big) {
  u8 i, best = 0, bt = 0;
  for (i = 0; i < MAX_BU; i++) {
    if (!bu_t[i]) { best = i; break; }
    if (bu_t[i] > bt) { bt = bu_t[i]; best = i; }   /* recycle the oldest */
  }
  bu_t[best] = 1; bu_x[best] = x; bu_z[best] = z; bu_big[best] = big;
}

void update_bursts(void) {
  u8 i;
  for (i = 0; i < MAX_BU; i++) {
    if (!bu_t[i]) continue;
    if (++bu_t[i] > 23) bu_t[i] = 0;
  }
}

void draw_bursts(void) {
  static u8 i, j, k, t, rel, sx, sy, tile, sc, x, y;
  static s8 dx, dy;
  static const u8 *pt;
  static u16 d;
  for (i = 0; i < MAX_BU; i++) {
    t = bu_t[i];
    if (!t) continue;
    d = bu_z[i] - cam_z;
    if (d >= VIS_Z) continue;
    rel = (u8)(d >> 4);
    sx = projx(bu_x[i], rel);
    if (!sx) continue;
    sy = ylut[rel] - 4;
    sc = bu_big[i] ? 0 : rel < 64 ? 1 : rel < 136 ? 2 : 3;
    tile = SPR_PART + (t < 5 ? 0 : t < 10 ? 2 : t < 16 ? 4 : 6);
    pt = ptab + (u16)sc * 384 + (u16)(t - 1) * 16 + ((frame & 1) << 1);
    pd_sx = eye_l ? par_x(sx, rel) : sx; pd_sy = sy; pd_tile = tile;
    part_draw(pt);                       /* even/odd particle sets on alternate frames */
  }
}

/* ------------------------------------------------------------------ radar */
u8 rp_col;
/* plot radar pixel (x 0..47 in A, y 0..23 in L) in colour rp_col; remember it for erasing */
void rplot(u8 x, u8 y) __naked __sdcccall(1) {
  x; y;
  __asm
    ld   c, a
    ld   h, #0
    add  hl, hl
    ld   de, #_rrow
    add  hl, de
    ld   e, (hl)
    inc  hl
    ld   d, (hl)
    ld   b, #0
    ld   hl, #_rcol
    add  hl, bc
    ld   a, (hl)
    add  a, e
    ld   e, a
    jr   NC, 00601$
    inc  d
00601$:
    ld   hl, #_rmask
    add  hl, bc
    ld   b, (hl)
    ld   hl, #_radar_buf
    add  hl, de
    ld   a, (_rp_col)
    ld   c, a
    bit  0, c
    jr   Z, 00602$
    ld   a, (hl)
    or   a, b
    ld   (hl), a
00602$:
    inc  hl
    bit  1, c
    jr   Z, 00603$
    ld   a, (hl)
    or   a, b
    ld   (hl), a
00603$:
    inc  hl
    bit  2, c
    jr   Z, 00604$
    ld   a, (hl)
    or   a, b
    ld   (hl), a
00604$:
    inc  hl
    bit  3, c
    jr   Z, 00605$
    ld   a, (hl)
    or   a, b
    ld   (hl), a
00605$:
    ld   a, (_rad_n)
    cp   a, #64
    ret  NC
    ld   l, a
    ld   h, #0
    inc  a
    ld   (_rad_n), a
    push hl
    add  hl, hl
    ld   a, b
    ld   bc, #_rad_off
    add  hl, bc
    ld   (hl), e
    inc  hl
    ld   (hl), d
    pop  hl
    ld   bc, #_rad_mask
    add  hl, bc
    cpl
    ld   (hl), a
    ret
  __endasm;
}

void radar_static(void) {
  u8 i;
  memset(radar_buf, 0, sizeof(radar_buf));
  rp_col = 1;
  for (i = 0; i < 48; i++) { rplot(i, 0); rplot(i, 23); }          /* border */
  for (i = 1; i < 23; i++) { rplot(0, i); rplot(47, i); }
  rp_col = 15;
  for (i = 13; i <= 19; i++) { rplot(1, i); rplot(46, i); }       /* the on-screen window */
  rad_n = 0;                           /* none of this is ever erased */
}

/* radar rows 1..22 cover depth -96..+607, 32 units per pixel row; 255 = off radar */
u8 radar_y(u16 z) {
  u16 r = z - cam_z + (96 << 4);
  if (r >= (704 << 4)) return 255;
  return 22 - (u8)(r >> 9);
}

/* 24 parts of 1 enemy (part 0 erases first, part 23 adds astronaut + player) */
void build_radar(u8 part) {
  static u8 i, y, n;
  static u8 *p;
  static Enemy *e;
  if (!part) {
    for (i = 0; i < rad_n; i++) {
      p = radar_buf + rad_off[i];
      y = rad_mask[i];
      p[0] &= y; p[1] &= y; p[2] &= y; p[3] &= y;
    }
    rad_n = 0;
  }
  rp_col = 15;
  e = en + part;
  for (n = 1; n; n--, e++) {
    if (e->st != 1) continue;
    if (e->mode) y = radar_y(e->z), i = e->xh;
    else y = radar_y(g_z[e->grp] + e->dzz), i = g_xh[e->grp] + (u8)e->dx;
    if (y != 255) { i = rxw[i]; rplot(i, y); rplot(i + 1, y); }
  }
  if (part == 23) {
    rp_col = 1;
    if (as_st && (frame & 16)) {
      y = radar_y(as_z);
      if (y != 255) rplot(rxw[as_x], y);
    }
    rp_col = 14;
    i = rxw[px];
    rplot(i, 19); rplot(i + 1, 19); rplot(i, 20); rplot(i + 1, 20);
  }
}

/* ----------------------------------------------------------------- player */
void kill_player(void) {
  if (pstate != PL_ALIVE || invuln) return;
  pstate = PL_DYING; ptimer = DEATH_TIME;
  burst_col = px >> 3;
  spawn_burst(px, cam_z + (PLAYER_REL << 4), 1);
  sfx(sfx_die, SFX_CHANNELS2AND3, 5);
}

void respawn(void) {
  u8 i;
  pstate = PL_ALIVE; invuln = 120; pxf = 0x8000u; px = 128; pvx = 0; warps = 1;
  hud_dirty |= HUD_SHIPS;
  for (i = 0; i < MAX_EB; i++) eb_st[i] = 0;
  /* formations regroup well ahead of the player */
  for (i = 0; i < MAX_GRP; i++) {
    if (!g_n[i]) continue;
    g_z[i] = cam_z + ((u16)(380 + (i << 7)) << 4);
    g_vz[i] = -g_speed;
  }
  for (i = 0; i < MAX_EN; i++) en[i].mode = 0;
  n_div = 0; div_idx[0] = div_idx[1] = div_idx[2] = 255;
}

void update_player(void) {
  u8 i;
  if (pstate == PL_ALIVE) {
    /* lateral: quick but not instant */
    if (keys & PORT_A_KEY_LEFT) { pvx -= 0x60; if (pvx < -0x280) pvx = -0x280; }
    else if (keys & PORT_A_KEY_RIGHT) { pvx += 0x60; if (pvx > 0x280) pvx = 0x280; }
    else if (pvx > 0) { pvx -= 0x40; if (pvx < 0) pvx = 0; }
    else if (pvx < 0) { pvx += 0x40; if (pvx > 0) pvx = 0; }
    pxf += pvx;
    if (pxf < (14 << 8)) { pxf = 14 << 8; pvx = 0; }
    if (pxf > (242u << 8)) { pxf = 242u << 8; pvx = 0; }
    px = pxf >> 8;
    /* thrust: heavy inertia, the ship coasts for seconds */
    if (keys & PORT_A_KEY_UP) { pvz += 2; if (pvz > 40) pvz = 40; }
    else if (keys & PORT_A_KEY_DOWN) { pvz -= 2; if (pvz < -24) pvz = -24; }
    else if (!(frame & 7)) { if (pvz > 0) pvz--; else if (pvz < 0) pvz++; }
    if (fire_cd) fire_cd--;
    if ((pressed & PORT_A_KEY_1) && !fire_cd) {   /* one shot per press, no auto-fire */
      for (i = 0; i < MAX_PB; i++) {
        if (!pb_st[i]) {
          pb_st[i] = 1; pb_x[i] = px; pb_z[i] = cam_z + ((PLAYER_REL + 6) << 4);
          fire_cd = 3;
          sfx(sfx_shot, SFX_CHANNEL2, 1);
          break;
        }
      }
    }
    if ((pressed & PORT_A_KEY_2) && warps) {
      warps--; hud_dirty |= HUD_SHIPS;
      pstate = PL_WARP; ptimer = 30;
      sfx(sfx_warp, SFX_CHANNEL2, 3);
    }
    if (invuln) invuln--;
  } else {
    if (!(frame & 3)) { if (pvz > 0) pvz--; else if (pvz < 0) pvz++; }
    if (pstate == PL_DYING) {
      if (--ptimer == 0) {
        lives--; hud_dirty |= HUD_SHIPS;
        if (!lives) {
          pstate = PL_DEAD; phase = PH_OVER; phase_timer = 250;
          game_state = ST_OVER;
          show_msg("GAME OVER");
          PSGStop();
          if (demo != 1) PSGPlayNoRepeat((void *)mus_over);
          music_on = 0;
        } else respawn();
      }
    } else if (pstate == PL_WARP) {
      if (--ptimer == 0) {
        px = 24 + (rand8() & 127) + (rand8() & 63); pxf = (u16)px << 8; pvx = 0;
        cam_z += (u16)(64 + (rand8() & 63)) << 4;
        pvz = 0;
        pstate = PL_ALIVE; invuln = 45;
      }
    }
  }
  cam_z += pvz;
}

/* ---------------------------------------------------------------- enemies */
void kill_enemy(u8 i, u8 scored) {
  u8 g = en[i].grp;
#ifdef PERFDBG
  dbg_kills++;
#endif
  en[i].st = 0; en_count--;
  if (g_n[g]) g_n[g]--;
  spawn_burst(en[i].xh, cam_z + ((u16)en[i].rel << 4), 0);  /* only on-screen enemies die */
  sfx(sfx_boom, SFX_CHANNEL3, 2);
  if (!scored) return;
  if (bonus_timer) {
    add_score(bonus_val, 0);
    if (bonus_val < 20) bonus_val += 2;
    hud_dirty |= HUD_BONUS;
  } else {
    add_score(pts_h[en[i].type], pts_t[en[i].type]);
    if (astro_cd) astro_cd--;
    if (!astro_cd && !as_st && !boss_wave) {
      as_st = 1; as_x = en[i].xh; as_z = cam_z + ((u16)en[i].rel << 4);
      astro_cd = 10;
    }
  }
}

/* x speed (8.8) that brings a shot fired from (x, rel) onto the player's column */
s16 aim_vx(u8 x, u8 rel) {
  u16 t;
  s16 dx;
  t = ((u16)(u8)(rel - PLAYER_REL) << 4) / eb_spd;      /* frames to reach the player */
  if (!t) t = 1;
  dx = (s16)px - (s16)x;
  dx = (s16)(((s16)(dx << 6) / (s16)t) << 2);
  if (dx > 0x180) dx = 0x180; else if (dx < -0x180) dx = -0x180;
  return dx;
}

u8 free_eb(u8 limit) {
  u8 j;
  for (j = 0; j < limit; j++) if (!eb_st[j]) return j;
  return 255;
}

void fire_at_player(u8 i, u8 rel) {
  u8 j = free_eb(eb_max);
  if (j == 255) return;
#ifdef PERFDBG
  dbg_fire = 1;
#endif
  eb_st[j] = 1; eb_x[j] = (u16)en[i].xh << 8; eb_z[j] = cam_z + ((u16)rel << 4); eb_vx[j] = aim_vx(en[i].xh, rel);
  sfx(sfx_eshot, SFX_CHANNEL2, 0);
}

/* each formation follows its own flight path */
void update_groups(void) {
  static u8 g, t, xh;
  static s16 r;
  for (g = 0; g < MAX_GRP; g++) {      /* each at 30 Hz, double steps: 0 and 2 on */
    if ((g == 1) != (frame & 1)) continue; /* even frames, 1 on odd ones, to spread the load */
    if (!g_n[g]) continue;
    t = (g_t[g] += 2);
    xh = g_xh[g];
    switch (g_type[g]) {
      case 0:                          /* saucers: gentle sway */
        g_x[g] = (u16)(u8)(g_base[g] + sin4[(u8)(t << 1)]) << 8;  /* t advances by 2 */
        break;
      case 1:                          /* darts: track the player */
        if (xh + 2 < px) { if (xh < 208) g_x[g] += 0x140; }
        else if (xh > px + 2) { if (xh > 48) g_x[g] -= 0x140; }
        break;
      case 2:                          /* spinners: sweep edge to edge */
        if (g_dir[g]) { g_x[g] += 0x280; if (xh >= 208) g_dir[g] = 0; }
        else { g_x[g] -= 0x280; if (xh <= 48) g_dir[g] = 1; }
        break;
      default:                         /* bombers: wide slow weave */
        g_x[g] = (u16)(u8)(g_base[g] + (sin4[t] << 1)) << 8;
        break;
    }
    g_xh[g] = (u8)(g_x[g] >> 8);
    /* depth: approach, overshoot behind the player, swing back up */
    g_z[g] += g_vz[g] << 1;
    r = (s16)(g_z[g] - cam_z);           /* 12.4: units * 16 */
    if (r < -200 * 16 || r > 1400 * 16) {  /* lost far behind / far ahead: re-enter ahead */
      g_z[g] = cam_z + ((u16)(560 + (rand8() & 127)) << 4);
      g_vz[g] = -g_speed;
    } else if (g_vz[g] < 0 && r < -130 * 16) {
      g_vz[g] = g_speed * 3;             /* rush back up from behind */
    } else if (g_vz[g] > 0 && r > 110 * 16) {
      g_vz[g] = -g_speed;
    }
    g_vis[g] = (r > -100 * 16 && r < 232 * 16);  /* span is 0..84 units behind the lead */
  }
}

void update_enemies(void) {
  static u8 i, g, xh, d;
  static u16 z, dz;
  static s16 r;
  static Enemy *e;
  g_mov[0] = g_mov[1] = g_mov[2] = 0;
  if (n_div) {                         /* divers / returners (at most 3) */
    for (i = 0; i < 3; i++) {
      if (div_idx[i] == 255) continue;
      e = en + div_idx[i];
      if (e->st != 1 || !e->mode) { div_idx[i] = 255; n_div--; continue; }
      g = e->grp;
      g_mov[g]++;
      xh = e->xh;
      z = e->z;
      if (e->mode == 1) {              /* dive: home on the player, overshoot */
        z += dive_vz;
        if (xh < px) { if ((u8)(px - xh) > 1) xh++; } else if ((u8)(xh - px) > 1) xh--;
        r = (s16)(z - cam_z) >> 4;
        if (r < -40) e->mode = 2;
      } else {                         /* climb back up to the slot */
        z += 44;
        d = g_xh[g] + (u8)e->dx;
        if (xh < d) xh++; else if (xh > d) xh--;
        dz = g_z[g] + e->dzz - z;
        if (dz < 64 || dz > 0xFF00) { e->mode = 0; div_idx[i] = 255; n_div--; }
        else if (dz > 0x8000) z -= 88; /* slot is behind us: drift back instead */
      }
      e->xh = xh; e->z = z;
    }
  }
  ep_hit = 0;
  en_pass();
  if (ep_hit) { stat_rammed++; kill_player(); kill_enemy(ep_hit - 1, 0); }
  { Enemy *e;
  u8 i, rel, d;
  /* one randomly chosen enemy per frame may fire or peel off into a dive */
  if (phase != PH_ACTION || pstate != PL_ALIVE) return;
  i = rand8() & 31;
  if (i >= MAX_EN) return;
  e = en + i;
  if (e->st != 1) return;
  rel = e->rel;
  if (rel == 255) return;
  if (rel < 80 || rel >= 212) return;
  d = rand8();
  if (d < fire_thr || (e->type == 3 && d < (u8)(fire_thr + fire_thr))) fire_at_player(i, rel);
  if (n_div < max_div && !e->mode && rel >= 100) {
    d ^= 0xA5;
    if (d < dive_thr || (e->type == 1 && d < (u8)(dive_thr + dive_thr))) {
      e->z = g_z[e->grp] + e->dzz;     /* formation members do not keep z up to date */
      e->mode = 1; n_div++;
      for (d = 0; d < 3; d++) if (div_idx[d] == 255) { div_idx[d] = i; break; }
    }
  }
  }
}

/* a formation of 8 enters far ahead (visible on the radar first) */
void spawn_group(u16 dist) {
  u8 i, g, type, k, f;
  for (g = 0; g < grp_max; g++) if (!g_n[g]) break;
  if (g == grp_max) return;
  k = wave > 4 ? 4 : wave;
  type = rand8() & 3;
  while (type >= k) type -= k;
  f = rand8() & 7;
  if (f > 4) f -= 3;
  g_type[g] = type; g_form[g] = f; g_t[g] = rand8(); g_dir[g] = rand8() & 1;
  if (type == 3) k = 112 + (rand8() & 31);
  else if (type == 0) k = 80 + (rand8() % 97);
  else k = 48 + (rand8() % 161);
  g_base[g] = k; g_x[g] = (u16)k << 8; g_xh[g] = k;
  g_z[g] = cam_z + (dist << 4);
  g_vz[g] = -g_speed;
  k = 0;
  for (i = g * GSIZE; k < GSIZE; i++) {   /* formation g owns slots g*8 .. g*8+7 */
    en[i].st = 1; en[i].type = type; en[i].grp = g; en[i].slot = k * 24; en[i].mode = 0;
    en[i].dx = form_dx[f][k]; en[i].dzz = (u16)form_dz[f][k] << 4;
    en[i].tile = SPR_ENEMY + type * 20; en[i].sx = 0;
    en[i].xh = k; en[i].z = g_z[g] + en[i].dzz; en[i].rel = 255;
    en_count++; k++;
  }
#ifdef PERFDBG
  dbg_spawn = 1;
#endif
  g_n[g] = k;
  if (groups_left) groups_left--;
}

void pb_killed(u8 j, u8 i) {
  kill_enemy(j, 1);
  en[j].rel = 255;                     /* no longer drawn or hit this frame */
  pb_st[i] = 0;
}

/* move player shots 5 units/frame; retire them at the far end; test hits */
void update_pbullets(void) __naked {
  __asm
    ld   c, #0
00801$:
    ld   b, #0
    ld   hl, #_pb_st
    add  hl, bc
    ld   a, (hl)
    or   a, a
    jp   Z, 00890$
    ld   hl, #_pb_z
    add  hl, bc
    add  hl, bc
    ld   e, (hl)
    inc  hl
    ld   d, (hl)
    ex   de, hl
    ld   a, #80
    add  a, l
    ld   l, a
    jr   NC, 00802$
    inc  h
00802$:
    ex   de, hl
    ld   (hl), d
    dec  hl
    ld   (hl), e
    ex   de, hl
    ld   de, (_cam_z)
    or   a, a
    sbc  hl, de
    ld   a, h
    cp   a, #0x0D
    jr   C, 00805$
    jr   NZ, 00880$
    ld   a, l
    cp   a, #0x80
    jr   NC, 00880$
00805$:
    ld   a, l
    rrca
    rrca
    rrca
    rrca
    and  a, #0x0F
    ld   e, a
    ld   a, h
    add  a, a
    add  a, a
    add  a, a
    add  a, a
    or   a, e
    cp   a, #25
    jr   C, 00880$
    ld   e, a
    sub  a, #5
    rrca
    rrca
    rrca
    and  a, #0x1F
    ld   hl, #_occ
    add  a, l
    ld   l, a
    jr   NC, 00806$
    inc  h
00806$:
    ld   a, (hl)
    inc  hl
    or   a, (hl)
    jr   NZ, 00807$
    inc  hl
    or   a, (hl)
    jr   Z, 00890$
00807$:
    push bc
    ld   hl, #_pb_x
    add  hl, bc
    ld   a, (hl)
    ld   l, e
    call _pb_hit
    pop  bc
    or   a, a
    jr   Z, 00890$
    push bc
    dec  a
    ld   l, c
    call _pb_killed
    pop  bc
    jr   00890$
00880$:
    ld   hl, #_pb_st
    add  hl, bc
    ld   (hl), #0
00890$:
    inc  c
    ld   a, c
    cp   a, #4
    jp   C, 00801$
    ret
  __endasm;
}

void update_ebullets(void) {
  static u8 i, rel, xh, d;
  static u16 dz;
  for (i = 0; i < MAX_EB; i++) {
    if (!eb_st[i]) continue;
    eb_x[i] += eb_vx[i];
    eb_z[i] -= eb_spd;
    xh = (u8)(eb_x[i] >> 8);
    dz = eb_z[i] - cam_z;
    if (dz >= VIS_Z || xh < 6 || xh > 250) { eb_st[i] = 0; continue; }
    rel = (u8)(dz >> 4);
    if (rel < 4) { eb_st[i] = 0; continue; }
    if (rel >= PLAYER_REL - 4 && rel <= PLAYER_REL + 4) {
      d = xh > px ? xh - px : px - xh;
      if (d < 6 && pstate == PL_ALIVE && !invuln) { stat_shot++; kill_player(); eb_st[i] = 0; }
    }
  }
}

void update_astro(void) {
  u8 rel, d;
  s16 r;
  if (bonus_timer) {
    if (--bonus_timer == 0) { line_col = COL_LINE; line_dirty = 1; hud_dirty |= HUD_BONUS; }
  }
  if (!as_st) return;
  as_z -= 4;
  r = (s16)(as_z - cam_z) >> 4;
  if (r < -60 || r > 1200) { as_st = 0; return; }
  if (r < 0 || r > 223) return;
  rel = (u8)r;
  if (rel >= PLAYER_REL - 10 && rel <= PLAYER_REL + 10 && pstate == PL_ALIVE) {
    d = as_x > px ? as_x - px : px - as_x;
    if (d < 14) {
      as_st = 0;
      bonus_timer = 600; bonus_val = 2;
      line_col = COL_BONUS; line_dirty = 1; hud_dirty |= HUD_BONUS;
      add_score(5, 0);
      sfx(sfx_pickup, SFX_CHANNEL2, 3);
    }
  }
}

void start_boss(void);

void start_wave(void) {
  u8 w = wave;
  phase = PH_INTRO; phase_timer = 120;
  groups_left = w < 2 ? 2 : (w < 20 ? 3 : 4);   /* 16, then 24, then 32 enemies a wave */
  grp_max = w < 2 ? 2 : 3;
  fire_thr = (3 + (w >> 2) > 13 ? 13 : 3 + (w >> 2)) << 4;   /* keeps rising until wave 40 */
  dive_thr = (2 + (w >> 2) > 12 ? 12 : 2 + (w >> 2)) << 4;
  max_div = w < 3 ? 1 : (w < 12 ? 2 : 3);
  eb_max = 2 + w / 10; if (eb_max > 4) eb_max = 4;
  eb_spd = 22 + (w >> 1);
  g_speed = 5 + w / 5;
  dive_vz = -(g_speed + 18);
  spawn_cd = 0;
  hud_dirty |= HUD_WAVE;
  boss_wave = (w % 10 == 0);
  if (boss_wave) {
    groups_left = 0; grp_max = 1;         /* one escort formation at a time */
    if (bonus_timer) { bonus_timer = 0; line_col = COL_LINE; line_dirty = 1; }
    show_msg("WAVE 00 - BOSS");
    start_boss();
    return;
  }
  show_msg("WAVE 00");
  /* the first formations are already on their way (watch the radar) */
  spawn_group(360);
  spawn_group(520);
  if (grp_max > 2) spawn_group(680);
}

void start_music(void) {
  if (music_on || demo == 1) return;
  PSGPlay((void *)(sound_mode ? mus_game : mus_growl));
  music_on = 1;
}

void update_phase(void) {
  if (won) return;                     /* the final boss is down: the ending takes over */
  if (phase == PH_INTRO) {
    if (--phase_timer == 0) {
      phase = PH_ACTION;
      clear_msg();
      start_music();
    }
  } else if (phase == PH_ACTION) {
    if (spawn_cd) spawn_cd--;
    else if (groups_left && en_count + GSIZE <= MAX_EN) { spawn_group(600 + (rand8() & 127)); spawn_cd = 90; }
    if (!groups_left && !en_count && pstate == PL_ALIVE && !boss_wave) {
      wave++;
      start_wave();
    }
  } else {
    if (phase_timer) phase_timer--;
    if (!phase_timer || (phase_timer < 190 && (pressed & (PORT_A_KEY_1 | PORT_A_KEY_2)))) game_state = ST_TITLE;
  }
}

/* ------------------------------------------------------------------- boss */
void start_boss(void) {
  boss_lv = wave / 10;
  boss_maxhp = boss_lv == 5 ? 72 : 16 + (boss_lv << 3);
  boss_hp = boss_maxhp;
  boss_st = 1; boss_t = 0; boss_flash = 0; boss_dead_t = 0;
  boss_x = 128;
  boss_z = cam_z + ((u16)420 << 4);             /* far ahead: it shows on the radar first */
  boss_fire_cd = 150;
  boss_spawn_cd = 240;
  boss_rel = 255;
  boss_pal = boss_col[boss_lv];
  hud_dirty |= HUD_BOSS;
  sfx(sfx_warp, SFX_CHANNEL2, 4);
}

void boss_fire(u8 rel) {
  u8 n = boss_lv == 5 ? 5 : 3, k, j;
  s16 base = aim_vx(boss_x, rel), vx;
  for (k = 0; k < n; k++) {
    j = free_eb(MAX_EB);
    if (j == 255) return;
    vx = base + (s16)((s16)k - (s16)(n >> 1)) * 0x90;
    eb_st[j] = 1; eb_x[j] = (u16)boss_x << 8; eb_z[j] = cam_z + ((u16)(rel - 4) << 4); eb_vx[j] = vx;
  }
  sfx(sfx_eshot, SFX_CHANNEL2, 1);
}

void boss_finish(void) {
  u8 i;
  for (i = 0; i < MAX_EN; i++)                  /* its escorts go down with it */
    if (en[i].st == 1) {
      if (en[i].rel != 255) spawn_burst(en[i].xh, cam_z + ((u16)en[i].rel << 4), 0);
      en[i].st = 0;
    }
  for (i = 0; i < MAX_GRP; i++) g_n[i] = 0;
  en_count = 0; n_div = 0; div_idx[0] = div_idx[1] = div_idx[2] = 255;
  for (i = 0; i < MAX_EB; i++) eb_st[i] = 0;
  boss_st = 0; boss_wave = 0;
  hud_dirty |= HUD_BOSS;
  add_score(boss_lv * 50, 0);                   /* 5,000 x boss level */
  if (boss_lv == 5) { won = 1; game_state = ST_TITLE; return; }
  wave++;
  start_wave();
}

void update_boss(void) {
  s16 r, corr;
  u8 target, lim, i;
  if (!boss_st) return;
  boss_t++;
  r = (s16)(boss_z - cam_z) >> 4;
  if (boss_st == 1) {
    /* depth: close in to ~140 units, then hold that distance whatever the player does */
    target = 140 + ((s8)sin4[boss_t] >> 2);
    if (boss_lv >= 3 && (boss_t & 0xC0) == 0xC0) target = 90;   /* lunges closer now and then */
    corr = ((s16)target - r) << 1;
    lim = 10 + (boss_lv << 1);
    if (corr > lim) corr = lim; else if (corr < -(s16)lim) corr = -(s16)lim;
    boss_z += pvz + corr;
    /* sideways sweep, faster for later bosses */
    boss_x = 128 + (u8)((s8)sin4[(u8)(boss_t * (1 + (boss_lv >> 1)))] << 1);
    if (phase == PH_ACTION && pstate == PL_ALIVE && r > 40 && r < 212) {
      if (!--boss_fire_cd) { boss_fire((u8)r); boss_fire_cd = 72 - (boss_lv << 3); }
      if (!--boss_spawn_cd) {
        boss_spawn_cd = 560 - boss_lv * 60;
        if (en_count + GSIZE <= MAX_EN) {
          spawn_group((u16)r + 16);             /* escorts launch from behind the boss */
          for (i = 0; i < MAX_GRP; i++)
            if (g_n[i] && g_t[i] < 2) { g_base[i] = boss_x; g_x[i] = (u16)boss_x << 8; g_xh[i] = boss_x; }
        }
      }
    }
    if (boss_flash) boss_flash--;
  } else {                                      /* exploding */
    boss_z += pvz;
    if (!(boss_t & 7)) {
      spawn_burst(boss_x - 24 + (rand8() % 49), boss_z + ((u16)(rand8() & 15) << 4), boss_dead_t > 50);
      sfx(sfx_boom, SFX_CHANNEL3, 3);
    }
    if (++boss_dead_t >= 100) { boss_finish(); return; }
  }
  boss_rel = (r >= 0 && r < 224) ? (u8)r : 255;
}

/* player shots against the boss (after the enemy hit test) */
void boss_hits(void) {
  u8 i, rel, d, hw;
  if (boss_st != 1 || boss_rel == 255) return;
  hw = boss_rel < 136 ? 22 : 18;
  for (i = 0; i < MAX_PB; i++) {
    if (!pb_st[i]) continue;
    rel = (u8)((pb_z[i] - cam_z) >> 4);
    d = rel > boss_rel ? rel - boss_rel : boss_rel - rel;
    if (d > 8) continue;
    d = pb_x[i] > boss_x ? pb_x[i] - boss_x : boss_x - pb_x[i];
    if (d >= hw) continue;
    pb_st[i] = 0;
    boss_flash = 8;
    add_score(0, 1);
    hud_dirty |= HUD_BOSS;
    if (!--boss_hp) {
      boss_st = 2; boss_dead_t = 0;
      sfx(sfx_die, SFX_CHANNELS2AND3, 5);
      return;
    }
    sfx(sfx_boom, SFX_CHANNEL3, 2);
  }
}

/* 32x32 near, 24x24 mid, 16x16 far; drawn right after the player and enemy shots */
void draw_boss(void) {
  static u8 rel, sx, cols, rows, c, tile, y0, x, half;
  static u16 dz;
  if (!boss_st) return;
  if (boss_st == 1 && (boss_flash & 2)) return;          /* hit flash */
  if (boss_st == 2 && (boss_dead_t & 2) && boss_dead_t > 60) return;
  dz = boss_z - cam_z;
  if (dz >= VIS_Z) return;
  rel = (u8)(dz >> 4);
  sx = projx(boss_x, rel);
  if (!sx) return;
  if (eye_l) sx = par_x(sx, rel);
  y0 = ylut[rel];
  if (rel < 136) { cols = 4; rows = 2; tile = SPR_BOSS_BIG; half = 16; y0 -= 24; }
  else if (rel < 200) { cols = 3; rows = 2; tile = SPR_BOSS_MID; half = 12; y0 -= 22; }
  else { cols = 2; rows = 1; tile = SPR_BOSS_FAR; half = 8; y0 -= 8; }
  if (sx < half || sx > 255 - half) return;              /* never wrap round the screen */
  for (; rows; rows--, y0 += 16) {
    x = sx - half;
    for (c = cols; c; c--, x += 8, tile += 2) SMS_addSprite(x, y0, tile);
    band_add(y0, cols);
  }
}

void draw_boss_bar(void) {
  u8 i, n;
  if (!boss_wave) { print_at(21, 2, "           "); return; }
  n = (u8)(((u16)boss_hp * 6 + boss_maxhp - 1) / boss_maxhp);
  print_yel(21, 2, "BOSS ");
  for (i = 0; i < 6; i++) SMS_setTile((u16)((i < n ? '#' : '-') - 32) | TILE_USE_SPRITE_PALETTE);
}

/* sprite list, part 1 (before the enemy pass, which draws the enemies) */
void begin_sprites(void) {
  SMS_initSprites();
  memset(row_n, 0, sizeof(row_n));
  n_spr = 0;
  if (pstate == PL_ALIVE) {
    if (!(invuln & 2)) {
      draw16(px, PLAYER_Y, SPR_PLAYER + ((pvz > 0 && (keys & PORT_A_KEY_UP) && (frame & 2)) ? 4 : 0));
      band_add(PLAYER_Y, 2);
    }
  }
  ds_t0 = SPR_EBUL + ((frame & 4) ? 2 : 0); ds_t1 = SPR_EBUL_S;
  draw_eshots();                       /* enemy shots first: they must never drop out */
  draw_boss();
  ed_f = (frame & 8) ? 10 : 0;
}

/* sprite list, part 2 (after the enemies) */
void end_sprites(void) {
  static u8 rel, sx;
  static u16 dz;
  if (as_st) {
    dz = as_z - cam_z;
    if (dz < VIS_Z) {
      rel = (u8)(dz >> 4);
      sx = projx(as_x, rel);
      if (sx) draw3(par_x(sx, rel), rel, SPR_ASTRO + ((frame & 16) ? 10 : 0));
    }
  }
  draw_bursts();
  draw_pshots();
  SMS_finalizeSprites();
#ifdef STEREODBG
  { u8 i;                              /* consistent snapshot for the stereo test */
    extern u8 SpriteTableXN[];
    snap_eye = eye_l; snap_n = SpriteNextFree;
    for (i = 0; i < 128; i++) snap_xn[i] = SpriteTableXN[i];
    for (i = 0; i < MAX_EN; i++) { snap_st[i] = en[i].st; snap_rel[i] = en[i].rel; snap_sx[i] = en[i].sx; }
  }
#endif
}

/* Autopilot for the attract mode: dodge shots, line up on the nearest
 * formation member (or the astronaut), keep a comfortable distance, fire. */
u16 ai_keys(void) {
  static u8 i, rel, xh, d, best, bx, hold;
  hold = 0;
  for (i = 0; i < MAX_EB; i++) {
    if (!eb_st[i]) continue;
    rel = (u8)((eb_z[i] - cam_z) >> 4);
    if (rel > 100 || rel < PLAYER_REL - 4) continue;
    xh = (u8)(eb_x[i] >> 8);
    d = xh > px ? xh - px : px - xh;
    if (d < 13) {
      if (px < 40) return PORT_A_KEY_1 | PORT_A_KEY_RIGHT;
      if (px > 216) return PORT_A_KEY_1 | PORT_A_KEY_LEFT;
      return PORT_A_KEY_1 | (xh >= px ? PORT_A_KEY_LEFT : PORT_A_KEY_RIGHT);
    }
    if (d < 26) hold = 1;
  }
  best = 255; bx = px;
  for (i = 0; i < MAX_EN; i++) {
    if (en[i].st != 1) continue;
    rel = en[i].rel;
    if (rel >= 216 || rel < PLAYER_REL - 6) continue;
    xh = en[i].xh;
    if (rel < 55) {                    /* too close to shoot safely: sidestep, back off */
      d = xh > px ? xh - px : px - xh;
      if (d < 16) return PORT_A_KEY_1 | ((xh >= px && px > 40) || px > 216 ? PORT_A_KEY_LEFT : PORT_A_KEY_RIGHT);
      continue;
    }
    if (rel < best) { best = rel; bx = xh; }
  }
  d = 0;
  if (as_st && (u16)(as_z - cam_z) < VIS_Z) {
    rel = (u8)((as_z - cam_z) >> 4);
    if (rel < best && rel >= PLAYER_REL - 8) { best = rel; bx = as_x; d = 1; }
  }
  if (boss_st == 1 && boss_rel < best) { best = boss_rel; bx = boss_x; }
  if (d) {                             /* fly to the astronaut, don't keep a distance */
    i = PORT_A_KEY_1 | (pvz < 16 ? PORT_A_KEY_UP : 0);
    if (bx > px + 3) i |= PORT_A_KEY_RIGHT; else if (bx + 3 < px) i |= PORT_A_KEY_LEFT;
    return i;
  }
  if (best == 255) return PORT_A_KEY_1 | (pvz < 24 ? PORT_A_KEY_UP : 0);
  i = PORT_A_KEY_1;
  if (!hold) { if (bx > px + 3) i |= PORT_A_KEY_RIGHT; else if (bx + 3 < px) i |= PORT_A_KEY_LEFT; }
  if (best > 150 && pvz < 20) i |= PORT_A_KEY_UP; else if (best < 60 && pvz > -4) i |= PORT_A_KEY_DOWN;
  return i;
}

void read_keys(void) {
  keys = SMS_getKeysStatus();
  if (demo) {
    if (demo == 1) {
      if ((keys & (PORT_A_KEY_1 | PORT_A_KEY_2)) || !--demo_timer) game_state = ST_TITLE;
      oldkeys = keys;
    }
    keys = ai_keys();
    if (frame & 4) keys &= ~PORT_A_KEY_1;   /* the autopilot taps fire too */
    pressed = keys & ~ai_old;
    ai_old = keys;
    return;
  }
  pressed = keys & ~oldkeys;
  oldkeys = keys;
}

void draw_hud_frame(void) {
  print_at(0, 0, " SCORE 000000          HI 000000");
  print_at(0, 1, " SHIPS 3               WAVE 01  ");
  print_at(0, 2, " WARP  3                        ");
  draw_radar_frame();
}

/* ship explosion: the field is swapped for a starburst tilemap whose rings
 * are lit in turn by palette writes; then the grid comes back.  Runs in
 * VBlank (all VRAM work), a few rows of tilemap per frame. */
void death_vblank(void) {
  u8 e = DEATH_TIME - ptimer, k, head;
  s8 age;
  if (e == 1) { for (k = 2; k < 14; k++) SMS_setBGPaletteColor(k, 0); }
  if (e >= 1 && e <= 7) {
    k = 3 + (e - 1) * 3;
    draw_burst_row(k); draw_burst_row(k + 1); draw_burst_row(k + 2);
  } else if (e >= 9 && e < 64) {
    head = (e - 9) / 3;
    for (k = 0; k < 11; k++) {
      age = (s8)head - (s8)k;
      SMS_setBGPaletteColor(2 + k, age < 0 ? 0 : age > 6 ? 0 : ring_col[age]);
    }
  } else if (e == 64) {
    for (k = 2; k < 14; k++) SMS_setBGPaletteColor(k, 0);
  } else if (e >= 65 && e <= 71) {
    k = 3 + (e - 65) * 3;
    draw_field_row(k); draw_field_row(k + 1); draw_field_row(k + 2);
    if (e == 71) { line_dirty = 1; if (msg_state == 0 && phase == PH_INTRO) msg_state = 1; }
  }
}

/* ------------------------------------------------------------------ title */
void print_3d_option(void) {
  print_yel(4, 20, g3d == 0 ? "< > 3-D GLASSES: OFF        " :
                   g3d == 1 ? "< > 3-D GLASSES: ON         " :
                              "< > 3-D GLASSES: ON SWAPPED ");
}

void title(void) {
  u8 r, i, blink = 0;
  u16 idle = 0;
  game_state = ST_TITLE; demo = 0;
  SMS_waitForVBlank();
  SMS_displayOff();
  SMS_initSprites(); SMS_finalizeSprites(); SMS_copySpritestoSAT();
  load_tiles_b3(logo_tiles, LOGO_BASE, sizeof(logo_tiles));
  for (r = 0; r < 24; r++) draw_field_row(r);
  print_at(0, 0, " SCORE 000000          HI 000000");
  for (r = 0; r < LOGO_H; r++) {
    SMS_setNextTileatXY(0, 4 + r); SMS_setTile(0);
    for (i = 0; i < LOGO_W; i++) SMS_setTile((u16)(LOGO_BASE + logo_map[r * LOGO_W + i]));
    SMS_setTile(0);
  }
  print_yel(5, 9, "FOR SEGA MASTER SYSTEM");
  print_at(5, 14, "100   150   200   300");
  print_yel(12, 17, "RESCUE = BONUS!");
  print_at(8, 21, "1:FIRE   2:WARP");
  print_at(1, 23, "A TRIBUTE TO KONAMI'S 1983 GAME");
  print_yel(5, 22, sound_mode ? "BUTTON 2 MUSIC: SONG " : "BUTTON 2 MUSIC: GROWL");
  print_3d_option();
  hud_dirty = HUD_SCORE | HUD_HI; hud_flush();
  line_col = COL_LINE; line_dirty = 1;
  SMS_displayOn();
  for (;;) {
    read_keys();
    if (SMS_queryPauseRequested()) SMS_resetPauseRequest();
    frame++; rng += frame;
    cam_z += 24;
    SMS_initSprites();
    for (i = 0; i < 4; i++) draw16(52 + i * 48, 92, SPR_ENEMY + i * 20 + ((frame & 16) ? 10 : 0));
    draw16(80, 132, SPR_ASTRO + ((frame & 32) ? 10 : 0));
    SMS_finalizeSprites();
    SMS_waitForVBlank();
    SMS_copySpritestoSAT();
    field_update();
    if (!(frame & 31)) {
      blink ^= 1;
      if (blink) print_at(9, 19, "PUSH BUTTON 1"); else draw_field_row(19);
    }
    if (pressed & PORT_A_KEY_2) { sound_mode ^= 1; idle = 0; }
    if (pressed & PORT_A_KEY_2) print_yel(5, 22, sound_mode ? "BUTTON 2 MUSIC: SONG " : "BUTTON 2 MUSIC: GROWL");
    if (pressed & (PORT_A_KEY_LEFT | PORT_A_KEY_RIGHT)) {
      g3d = (pressed & PORT_A_KEY_RIGHT) ? (g3d == 2 ? 0 : g3d + 1) : (g3d == 0 ? 2 : g3d - 1);
      print_3d_option();
      idle = 0;
    }
    if (g3d) GLASSES = (frame & 1) ^ (g3d == 2);
    if (pressed & PORT_A_KEY_1) break;
    if (++idle == 600) { demo = 1; demo_timer = 2400; break; }
  }
}

/* ----------------------------------------------------------------- ending */
const char * const end_text[5] = {
  "THE MOTHERSHIP IS DESTROYED",
  "THE INVADERS ARE IN RETREAT",
  "THE SECTOR IS SAFE AGAIN",
  "CONGRATULATIONS, PILOT!",
  "THE END"
};
const u8 end_row[5] = {5, 7, 9, 12, 20};

void print_centered(u8 row, const char *t) {
  u8 n = 0;
  const char *q = t;
  while (*q++) n++;
  print_at((32 - n) >> 1, row, t);
}

void ending(void) {
  u8 line = 0, shiprel = PLAYER_REL, sx, k, blink = 0, ships = lives;
  u16 t = 0, speed = 8;
  for (k = 0; k < ships; k++) add_score(100, 0);         /* 10,000 per ship left */
  for (k = 0; k < MAX_BU; k++) bu_t[k] = 0;
  pstate = PL_ALIVE; invuln = 0; eye_l = 0;
  line_col = COL_LINE; line_dirty = 1;
  PSGStop(); PSGSFXStop();
  PSGPlay((void *)mus_game);
  for (;;) {
    read_keys();
    frame++; t++;
    cam_z += speed;                                      /* the field speeds up */
    if (!(t & 7) && speed < 56) speed++;
    if (t > 90 && shiprel < 223) shiprel++;              /* and the ship flies off */
    if (!(t & 31) && t < 1200)                           /* fireworks */
      spawn_burst(40 + (rand8() % 177), cam_z + ((u16)(120 + (rand8() & 63)) << 4), 1);
    update_bursts();
    SMS_initSprites();
    memset(row_n, 0, sizeof(row_n));
    n_spr = 0;
    if (shiprel < 223) {
      sx = projx(px, shiprel);
      if (sx) {
        if (shiprel < 90) draw16(sx, ylut[shiprel] - 8, SPR_PLAYER + ((frame & 2) ? 4 : 0));
        else SMS_addSprite(sx - 4, ylut[shiprel] - 8, SPR_PLAYER_SMALL);
      }
    }
    draw_bursts();
    SMS_finalizeSprites();
    SMS_waitForVBlank();
    SMS_copySpritestoSAT();
    if (g3d) GLASSES = (frame & 1) ^ (g3d == 2);
    field_update();
    if (t % 150 == 0 && line < 7) {                      /* one line of text at a time */
      if (line < 4) print_centered(end_row[line], end_text[line]);
      else if (line == 4) {
        print_at(7, 15, "SHIP BONUS 0 X 10000");
        SMS_setTileatXY(18, 15, (u16)(ships + 16));
      } else if (line == 5) {
        print_at(7, 17, "FINAL SCORE");
        put_digits(19, 17, score);
      } else print_centered(end_row[4], end_text[4]);
      hud_dirty |= HUD_SCORE;
      line++;
    }
    hud_flush();
    if (line == 7) {
      if (!(t & 31)) {
        blink ^= 1;
        if (blink) print_at(9, 22, "PUSH BUTTON 1"); else draw_field_row(22);
      }
      if ((pressed & (PORT_A_KEY_1 | PORT_A_KEY_2)) || t > 5400) break;
    }
  }
  PSGStop(); PSGSFXStop();
}

/* ------------------------------------------------------------------- play */
void play(void) {
  u8 i;
  SMS_waitForVBlank();
  SMS_displayOff();
  SMS_initSprites(); SMS_finalizeSprites(); SMS_copySpritestoSAT();
  load_tiles_b3(burst_tiles, BURST_BASE, sizeof(burst_tiles));
  radar_static();
  SMS_loadTiles(radar_buf, RADAR_BASE, sizeof(radar_buf));
  for (i = 0; i < 24; i++) draw_field_row(i);
  draw_hud_frame();
  for (i = 0; i < 6; i++) score[i] = 0;
  for (i = 0; i < MAX_EN; i++) en[i].st = 0;
  for (i = 0; i < MAX_PB; i++) pb_st[i] = 0;
  for (i = 0; i < MAX_EB; i++) eb_st[i] = 0;
  for (i = 0; i < MAX_GRP; i++) g_n[i] = 0;
  for (i = 0; i < MAX_BU; i++) bu_t[i] = 0;
  as_st = 0; en_count = 0; n_div = 0; div_idx[0] = div_idx[1] = div_idx[2] = 255;
  boss_st = 0; boss_wave = 0; won = 0; ai_old = 0;
  lives = 3; wave = 1; warps = 1;
  tenk_count = 0; next_extra = 2;
  px = 128; pxf = 0x8000u; pvx = 0; pvz = 0; pstate = PL_ALIVE; invuln = 60; fire_cd = 0;
  bonus_timer = 0; astro_cd = 6;
  line_col = COL_LINE; line_dirty = 1;
  msg_state = 0; sfx_prio = 0; music_on = 0;
  n_vis = 0;
  radar_static();
  start_wave();
  game_state = ST_PLAY;
  hud_dirty = 0xFF; hud_flush(); msg_flush();
  band_ptr = band_zero;
  vdp_busy = 0;
  if (g3d) {
    SMS_setLineInterruptHandler(&line_interrupt);
    SMS_setLineCounter(23);            /* IRQ after lines 23, 47, 71 ... */
  }
  SMS_displayOn();
  if (!demo) PSGPlayNoRepeat((void *)mus_start);
  if (demo) print_yel(9, 11, "PUSH BUTTON 1");

  while (game_state != ST_TITLE) {
    read_keys();
    if (SMS_queryPauseRequested()) {
      SMS_resetPauseRequest();
      PSGStop(); PSGSFXStop();
      for (;;) {
        SMS_waitForVBlank();
        if (g3d) GLASSES = (++frame & 1) ^ (g3d == 2);
        if (SMS_queryPauseRequested()) { SMS_resetPauseRequest(); break; }
      }
      if (music_on) PSGResume();
      read_keys();
    }
    frame++; loops++;
    eye_l = g3d && !(frame & 1);       /* 3D: even frames left eye, odd frames right */
    update_player();
    update_groups();
    begin_sprites();
    update_enemies();                  /* also draws the enemies */
    update_pbullets();
    boss_hits();
    update_boss();
    update_ebullets();
    update_astro();
    update_bursts();
    update_phase();
    if ((frame & 31) < 24 && pstate != PL_DYING) build_radar(frame & 31);
    end_sprites();
    SMS_waitForVBlank();
    vdp_busy = 1;
#ifdef PERFDBG
    {
      u8 nb = 0, k;
      for (k = 0; k < MAX_BU; k++) if (bu_t[k]) nb++;
      if ((u8)(vbl_count - vbl_last) > 1) {
        miss_total++; miss_nvis[n_vis]++; miss_radar[frame & 15]++; miss_bu[nb]++;
        if (dbg_kills) miss_kill++;
        if (dbg_fire) miss_fire++;
        if (dbg_spawn) miss_spawn++;
      } else { ok_nvis[n_vis]++; if (dbg_fire) ok_fire++; if (dbg_kills) ok_kill++; }
      vbl_last = vbl_count; dbg_kills = 0; dbg_fire = 0; dbg_spawn = 0;
    }
#endif
    SMS_copySpritestoSAT();
    if (g3d) {
      GLASSES = (frame & 1) ^ (g3d == 2);  /* open the shutter for the eye just built */
      band_ptr = eye_l ? band_left : band_zero;
      SMS_setBGScrollX(0);
      if (eye_l) SMS_enableLineInterrupt(); else SMS_disableLineInterrupt();
    }
    if (boss_pal) { SMS_setSpritePaletteColor(12, boss_pal); boss_pal = 0; }
    if (pstate == PL_DYING) death_vblank();
    else {
      field_update();
      i = frame & 31;
      if (i >= 24 && i <= 29) SMS_loadTiles(radar_buf + (i - 24) * 96, RADAR_BASE + (i - 24) * 3, 96);
    }
    hud_flush();
    msg_flush();
    vdp_busy = 0;
  }
  if (g3d) {
    SMS_disableLineInterrupt();
    SMS_setBGScrollX(0);
  }
  eye_l = 0;
  PSGStop(); PSGSFXStop();
  if (demo) { demo = 0; return; }
  if (won) ending();
  for (i = 0; i < 6; i++) {
    if (score[i] > hiscore[i]) {
      for (i = 0; i < 6; i++) hiscore[i] = score[i];
      break;
    }
    if (score[i] < hiscore[i]) break;
  }
}

void main(void) {
  u8 i;
  PSGPort = 0x9F; PSGPort = 0xBF; PSGPort = 0xDF; PSGPort = 0xFF;
  SMS_mapROMBank(2);                   /* all music/SFX data; stays mapped */
  SMS_displayOff();
  SMS_useFirstHalfTilesforSprites(0);
  SMS_setSpriteMode(SPRITEMODE_TALL);
  SMS_autoSetUpTextRenderer();
  load_tiles_b3(field_tiles, FIELD_BASE, sizeof(field_tiles));
  load_tiles_b3(sprite_tiles, 256, sizeof(sprite_tiles));
  for (i = 0; i < 16; i++) SMS_setBGPaletteColor(i, 0);
  SMS_setBGPaletteColor(1, RGB(3,3,3));
  SMS_setBGPaletteColor(14, RGB(3,3,0));
  SMS_setBGPaletteColor(15, RGB(3,1,0));
  SMS_loadSpritePalette(spr_pal);
  SMS_setBackdropColor(0);
  hiscore[1] = 1;                      /* 010000 */
  SMS_setFrameInterruptHandler(&frame_interrupt);
  for (;;) {
    title();
    play();
  }
}

SMS_EMBED_SEGA_ROM_HEADER(9999, 0);
SMS_EMBED_SDSC_HEADER_AUTO_DATE(1, 1, "Claude",
    "Juno First SMS",
    "A tribute to Konami's Juno First for the Sega Master System. Built with devkitSMS.");
