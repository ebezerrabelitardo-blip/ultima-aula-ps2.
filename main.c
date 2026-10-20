/*
 * A ULTIMA AULA - versao PS2 (homebrew) - pronta para OPL (USB)
 * ps2sdk + gsKit. Raycaster 3D em primeira pessoa, IA do professor, 4 tarefas e susto.
 * Controles: analogico esquerdo = andar, analogico direito (ou D-pad) = virar,
 *            QUADRADO = correr (faz barulho), X = interagir, START = comecar/reiniciar.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <libpad.h>

#define MW 32
#define MH 15
#define NCOL 160
#define PI_F 3.14159265f

static GSGLOBAL *gs;
static int SW = 640, SH = 448;
static float colw;
static char map[MH][MW + 1];
static float zbuf[NCOL];
static char padBuf[256] __attribute__((aligned(64)));

/* ---------- desenho basico ---------- */
static int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
static u64 C(int r, int g, int b) { return GS_SETREG_RGBAQ(clampi(r, 0, 255), clampi(g, 0, 255), clampi(b, 0, 255), 0x80, 0x00); }
static u64 CA(int r, int g, int b, int a) { return GS_SETREG_RGBAQ(clampi(r, 0, 255), clampi(g, 0, 255), clampi(b, 0, 255), clampi(a, 0, 128), 0x00); }
static void rect(float x1, float y1, float x2, float y2, u64 c) {
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > SW) x2 = (float)SW;
    if (y2 > SH) y2 = (float)SH;
    if (x2 <= x1 || y2 <= y1) return;
    gsKit_prim_sprite(gs, x1, y1, x2, y2, 1, c);
}

/* fonte 5x5 (A-Z, 0-9, ! . : -) */
static const char *FCH = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!.:- ";
static const unsigned char FONT[41][5] = {
    {14,17,31,17,17},{30,17,30,17,30},{15,16,16,16,15},{30,17,17,17,30},{31,16,30,16,31},{31,16,30,16,16},{15,16,19,17,15},
    {17,17,31,17,17},{31,4,4,4,31},{7,2,2,18,12},{17,18,28,18,17},{16,16,16,16,31},{17,27,21,17,17},{17,25,21,19,17},
    {14,17,17,17,14},{30,17,30,16,16},{14,17,21,18,13},{30,17,30,18,17},{15,16,14,1,30},{31,4,4,4,4},{17,17,17,17,14},
    {17,17,17,10,4},{17,17,21,27,17},{17,10,4,10,17},{17,10,4,4,4},{31,2,4,8,31},
    {14,19,21,25,14},{4,12,4,4,14},{14,17,2,8,31},{30,1,14,1,30},{18,18,31,2,2},{31,16,30,1,30},{14,16,30,17,14},{31,1,2,4,4},{14,17,14,17,14},{14,17,15,1,14},
    {4,4,4,0,4},{0,0,0,0,4},{0,4,0,4,0},{0,0,14,0,0},{0,0,0,0,0}};
static void text(float x, float y, int sc, const char *s, u64 col) {
    for (; *s; s++) {
        const char *p = strchr(FCH, *s);
        int gi = p ? (int)(p - FCH) : 40;
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 5; c++)
                if (FONT[gi][r] & (16 >> c)) rect(x + c * sc, y + r * sc, x + (c + 1) * sc, y + (r + 1) * sc, col);
        x += 6 * sc;
    }
}
static float text_w(const char *s, int sc) { return (float)strlen(s) * 6 * sc; }

/* ---------- mapa ---------- */
static void fillr(int x1, int x2, int y1, int y2) {
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++) map[y][x] = '.';
}
static void build_map(void) {
    for (int y = 0; y < MH; y++) {
        for (int x = 0; x < MW; x++) map[y][x] = '#';
        map[y][MW] = 0;
    }
    fillr(1, 5, 1, 4);    /* secretaria */
    fillr(7, 14, 1, 4);   /* biblioteca */
    fillr(16, 21, 1, 4);  /* sala 1 */
    fillr(23, 30, 1, 4);  /* diretoria */
    fillr(1, 30, 6, 7);   /* corredor */
    fillr(1, 6, 9, 13);   /* deposito */
    fillr(8, 22, 9, 13);  /* patio */
    map[5][3] = 'D'; map[5][10] = 'D'; map[5][18] = 'D'; map[5][26] = 'E';
    map[8][3] = 'L'; map[8][14] = 'D'; map[8][20] = 'D';
    map[14][15] = 'G';
}
static int closed(char c) { return c == '#' || c == 'D' || c == 'L' || c == 'E' || c == 'G'; }
static int walk_ok(float x, float y) {
    int ix = (int)x, iy = (int)y;
    if (x < 0 || y < 0 || ix >= MW || iy >= MH) return 0;
    char c = map[iy][ix];
    return c == '.' || c == 'o';
}
static int hitc(float x, float y, float r) {
    return !(walk_ok(x - r, y - r) && walk_ok(x + r, y - r) && walk_ok(x - r, y + r) && walk_ok(x + r, y + r));
}
static int los(float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0, d = sqrtf(dx * dx + dy * dy);
    int n = (int)(d / 0.15f) + 1;
    for (int i = 1; i < n; i++) {
        float t = (float)i / n;
        int ix = (int)(x0 + dx * t), iy = (int)(y0 + dy * t);
        if (ix < 0 || iy < 0 || ix >= MW || iy >= MH) return 0;
        if (closed(map[iy][ix])) return 0;
    }
    return 1;
}

/* ---------- estado do jogo ---------- */
typedef struct { float x, y; int type, taken; } Item;
static Item items[3];
static int hasKey, hasFuse, hasGKey, power, gateTimer10, gateOpen;
static float px, py, pa, runNoise;
static int gstate; /* 0 titulo, 1 jogo, 2 susto, 3 fim, 4 vitoria */
static float gtime, scareT, flick, msgT, hbT, hbP, gateT;
static char msg[64];
static int frame;

typedef struct {
    float x, y, ang, spd, lost, rep, wait;
    int state;          /* 0 patrulha, 1 investiga, 2 persegue, 3 procura */
    int path[200], pn, pi;
    float lx, ly;
} Teacher;
static Teacher T;

static void say(const char *s) { strncpy(msg, s, 63); msg[63] = 0; msgT = 4.0f; }

static int bfs(int sx, int sy, int tx, int ty) {
    static int prev[MH * MW], q[MH * MW];
    if (sx < 0 || sy < 0 || sx >= MW || sy >= MH || tx < 0 || ty < 0 || tx >= MW || ty >= MH) { T.pn = 0; T.pi = 0; return 0; }
    for (int i = 0; i < MH * MW; i++) prev[i] = -2;
    int h = 0, t = 0, s = sy * MW + sx, g = ty * MW + tx;
    q[t++] = s; prev[s] = -1;
    while (h < t) {
        int c = q[h++];
        if (c == g) break;
        int cx = c % MW, cy = c / MW;
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++) {
            int nx = cx + dx[k], ny = cy + dy[k];
            if (nx < 0 || ny < 0 || nx >= MW || ny >= MH) continue;
            char ch = map[ny][nx];
            if (ch == '#' || ch == 'G') continue;
            int n = ny * MW + nx;
            if (prev[n] != -2) continue;
            prev[n] = c; q[t++] = n;
        }
    }
    if (prev[g] == -2) { T.pn = 0; T.pi = 0; return 0; }
    int tmp[200], n = 0;
    for (int c = g; c != s && n < 200; c = prev[c]) tmp[n++] = c;
    T.pn = n; T.pi = 0;
    for (int i = 0; i < n; i++) T.path[i] = tmp[n - 1 - i];
    return n;
}

static const int WP[][2] = {{3,6},{10,7},{18,6},{27,7},{10,11},{15,11},{20,11},{3,11},{10,2},{18,2},{3,2},{27,2}};
static void new_patrol(void) {
    int w = rand() % 12;
    if (rand() % 100 < 45) { /* vigia onde voce precisa ir */
        if (!hasKey) w = 10; else if (!hasFuse) w = 8; else if (!power) w = 7; else if (!hasGKey) w = 11; else w = 5;
    }
    bfs((int)T.x, (int)T.y, WP[w][0], WP[w][1]);
}

static void reset_game(void) {
    build_map();
    hasKey = hasFuse = hasGKey = power = gateOpen = 0;
    gateT = 0; gateTimer10 = 0;
    px = 1.5f; py = 6.5f; pa = 0.0f; runNoise = 0;
    items[0].x = 2.5f;  items[0].y = 2.5f; items[0].type = 1; items[0].taken = 0;
    items[1].x = 12.5f; items[1].y = 2.5f; items[1].type = 2; items[1].taken = 0;
    items[2].x = 28.5f; items[2].y = 2.5f; items[2].type = 3; items[2].taken = 0;
    memset(&T, 0, sizeof(T));
    T.x = 15.5f; T.y = 11.5f; T.state = 0; T.spd = 1.6f;
    new_patrol();
    scareT = 0; flick = 0; hbT = 0; hbP = 0;
    say("ACHE A CHAVE NA SECRETARIA");
}

static void follow(float dt, float spd) {
    if (T.pi >= T.pn) return;
    int c = T.path[T.pi], cx = c % MW, cy = c / MW;
    if (map[cy][cx] == 'D' || map[cy][cx] == 'L' || map[cy][cx] == 'E') map[cy][cx] = 'o';
    float tx = cx + 0.5f, ty = cy + 0.5f, dx = tx - T.x, dy = ty - T.y, d = sqrtf(dx * dx + dy * dy);
    if (d < 0.12f) { T.pi++; return; }
    float s = spd * dt; if (s > d) s = d;
    T.x += dx / d * s; T.y += dy / d * s; T.ang = atan2f(dy, dx);
}

static void teacher_update(float dt) {
    float dx = px - T.x, dy = py - T.y, d = sqrtf(dx * dx + dy * dy);
    int see = (d < 9.0f && los(T.x, T.y, px, py)) || d < 1.6f;
    float lvl = 1.0f + 0.12f * (hasKey + hasFuse + power + hasGKey);
    if (see) { T.state = 2; T.lx = px; T.ly = py; T.lost = 0; }
    else if (T.state == 2) {
        T.lost += dt;
        if (T.lost > 1.4f) { T.state = 3; T.wait = 0; bfs((int)T.x, (int)T.y, (int)T.lx, (int)T.ly); }
    }
    if (runNoise > 0 && T.state != 2 && d < 8.0f) { T.state = 1; bfs((int)T.x, (int)T.y, (int)px, (int)py); }
    if (T.state == 2) {
        T.rep -= dt;
        if (T.rep <= 0) { T.rep = 0.3f; bfs((int)T.x, (int)T.y, (int)px, (int)py); }
        if (d < 2.0f && d > 0.001f && los(T.x, T.y, px, py)) { /* encosta direto */
            float s = 2.5f * lvl * dt; T.x += dx / d * s; T.y += dy / d * s; T.ang = atan2f(dy, dx);
        } else follow(dt, 2.5f * lvl);
        if (d < 0.7f && gstate == 1) { gstate = 2; scareT = 0; }
    } else if (T.state == 1) {
        if (T.pi >= T.pn) { T.state = 3; T.wait = 0; } else follow(dt, 2.0f * lvl);
    } else if (T.state == 3) {
        T.wait += dt;
        if (T.pi < T.pn) follow(dt, 1.8f * lvl); else T.ang += dt * 1.5f;
        if (T.wait > 8.0f) { T.state = 0; new_patrol(); }
    } else {
        if (T.pi >= T.pn) new_patrol(); else follow(dt, 1.4f * lvl);
    }
}

/* ---------- entrada ---------- */
static int padOK = 0;
static void pad_setup(void) {
    int tries;
    padInit(0);
    if (padPortOpen(0, 0, padBuf) == 0) return;
    tries = 0;
    while (padGetState(0, 0) != PAD_STATE_STABLE && padGetState(0, 0) != PAD_STATE_FINDCTP1 && tries++ < 500000) { }
    padSetMainMode(0, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);
    padOK = 1;
}
static void pad_read(float *fwd, float *strafe, float *turn, int *run, int *press, int *start) {
    static u32 old = 0;
    struct padButtonStatus b;
    *fwd = *strafe = *turn = 0; *run = 0; *press = 0; *start = 0;
    if (!padOK) return;
    if (padGetState(0, 0) != PAD_STATE_STABLE) return;
    if (padRead(0, 0, &b) == 0) return;
    u32 pd = 0xffff ^ b.btns, nw = pd & ~old;
    old = pd;
    if (padInfoMode(0, 0, PAD_MODECURID, 0) == 7) { /* so le analogico se estiver em modo analogico */
        float lx = ((int)b.ljoy_h - 128) / 128.0f, ly = ((int)b.ljoy_v - 128) / 128.0f, rx = ((int)b.rjoy_h - 128) / 128.0f;
        if (fabsf(lx) < 0.2f) lx = 0;
        if (fabsf(ly) < 0.2f) ly = 0;
        if (fabsf(rx) < 0.2f) rx = 0;
        *strafe = lx; *fwd = -ly; *turn = rx;
    }
    if (pd & PAD_UP) *fwd = 1;
    if (pd & PAD_DOWN) *fwd = -1;
    if (pd & PAD_LEFT) *turn = -1;
    if (pd & PAD_RIGHT) *turn = 1;
    *run = (pd & PAD_SQUARE) ? 1 : 0;
    *press = (nw & PAD_CROSS) ? 1 : 0;
    *start = (nw & PAD_START) ? 1 : 0;
}

/* ---------- interacao ---------- */
static void interact(void) {
    float fx = cosf(pa), fy = sinf(pa);
    int cx = (int)(px + fx * 0.9f), cy = (int)(py + fy * 0.9f);
    if (cx >= 0 && cy >= 0 && cx < MW && cy < MH) {
        char c = map[cy][cx];
        if (c == 'D') { map[cy][cx] = 'o'; say("PORTA ABERTA"); return; }
        if (c == 'L') {
            if (hasKey) { map[cy][cx] = 'o'; say("DEPOSITO DESTRANCADO"); }
            else say("TRANCADA. ACHE A CHAVE");
            return;
        }
        if (c == 'E') {
            if (power) { map[cy][cx] = 'o'; say("PORTA ELETRONICA ABERTA"); }
            else say("SEM ENERGIA");
            return;
        }
        if (c == 'G') {
            if (gateTimer10) return;
            if (hasGKey) {
                gateTimer10 = 1; gateT = 9.0f;
                T.x = 16.5f; T.y = 6.5f; T.state = 2; T.lost = 0; bfs((int)T.x, (int)T.y, (int)px, (int)py);
                say("DESTRANCANDO O PORTAO. SOBREVIVA!");
            } else say("PORTAO TRANCADO. FALTA A CHAVE");
            return;
        }
    }
    for (int i = 0; i < 3; i++) {
        if (items[i].taken) continue;
        float dx = items[i].x - px, dy = items[i].y - py;
        if (dx * dx + dy * dy < 1.3f * 1.3f) {
            items[i].taken = 1;
            if (items[i].type == 1) { hasKey = 1; say("PEGOU A CHAVE DO DEPOSITO"); }
            if (items[i].type == 2) { hasFuse = 1; say("PEGOU O FUSIVEL. LEVE AO QUADRO NO DEPOSITO"); }
            if (items[i].type == 3) { hasGKey = 1; say("PEGOU A CHAVE DO PORTAO! VA AO PATIO"); }
            return;
        }
    }
    if (px < 3.2f && py > 9.0f && py < 13.5f) { /* quadro eletrico */
        if (power) say("ENERGIA JA RESTAURADA");
        else if (hasFuse) { power = 1; hasFuse = 0; say("ENERGIA RESTAURADA. VA A DIRETORIA"); T.state = 1; bfs((int)T.x, (int)T.y, (int)px, (int)py); }
        else say("FALTA UM FUSIVEL");
    }
}

/* ---------- renderizacao ---------- */
typedef struct { float u0, u1, v0, v1; int r, g, b; } Part;
static const Part TEACHER[] = {
    {0.30f, 0.70f, 0.00f, 0.07f, 25, 20, 16},   /* cabelo */
    {0.32f, 0.68f, 0.07f, 0.19f, 190, 130, 100},/* rosto */
    {0.36f, 0.45f, 0.10f, 0.13f, 230, 235, 240},/* olho */
    {0.55f, 0.64f, 0.10f, 0.13f, 230, 235, 240},
    {0.34f, 0.66f, 0.15f, 0.21f, 60, 55, 50},   /* barba */
    {0.40f, 0.60f, 0.16f, 0.17f, 220, 200, 120},/* dentes */
    {0.18f, 0.82f, 0.20f, 0.58f, 190, 20, 20},  /* camisa */
    {0.43f, 0.57f, 0.30f, 0.44f, 235, 215, 80}, /* estrela */
    {0.47f, 0.53f, 0.22f, 0.50f, 20, 20, 22},   /* gravata */
    {0.06f, 0.18f, 0.22f, 0.52f, 175, 20, 20},  /* braco esq */
    {0.82f, 0.94f, 0.22f, 0.52f, 175, 20, 20},  /* braco dir */
    {0.90f, 0.99f, 0.30f, 0.80f, 110, 70, 40},  /* taco */
    {0.24f, 0.47f, 0.58f, 1.00f, 35, 32, 40},   /* perna esq */
    {0.53f, 0.76f, 0.58f, 1.00f, 35, 32, 40}    /* perna dir */
};
#define NTEACH ((int)(sizeof(TEACHER) / sizeof(TEACHER[0])))

static void figure(float xl, float yt, float w, float h, float fog, int zc, float dist) {
    for (float x = xl; x < xl + w; x += colw) {
        int ci = (int)(x / colw);
        if (x < 0 || x >= SW) continue;
        if (zc && (ci < 0 || ci >= NCOL || dist >= zbuf[ci])) continue;
        float u = (x - xl) / w;
        for (int i = 0; i < NTEACH; i++) {
            const Part *p = &TEACHER[i];
            if (u >= p->u0 && u < p->u1)
                rect(x, yt + p->v0 * h, x + colw, yt + p->v1 * h, C((int)(p->r * fog), (int)(p->g * fog), (int)(p->b * fog)));
        }
    }
}

static void render_world(float dim) {
    /* teto e piso */
    rect(0, 0, (float)SW, SH * 0.5f, C(22, 22, 26));
    for (int b = 0; b < 8; b++) {
        int v = (int)(46 - b * 3);
        rect(0, SH * 0.5f + b * SH / 16.0f, (float)SW, SH * 0.5f + (b + 1) * SH / 16.0f, C((int)(v * 0.55f * dim + 10), (int)(v * 0.55f * dim + 10), (int)(v * 0.52f * dim + 10)));
    }
    float dirx = cosf(pa), diry = sinf(pa), plx = -diry * 0.66f, ply = dirx * 0.66f;
    for (int i = 0; i < NCOL; i++) {
        float cam = 2.0f * i / NCOL - 1.0f;
        float rdx = dirx + plx * cam, rdy = diry + ply * cam;
        int mx = (int)px, my = (int)py;
        float ddx = fabsf(1.0f / (rdx == 0 ? 1e-6f : rdx)), ddy = fabsf(1.0f / (rdy == 0 ? 1e-6f : rdy));
        int stx = rdx < 0 ? -1 : 1, sty = rdy < 0 ? -1 : 1;
        float sdx = rdx < 0 ? (px - mx) * ddx : (mx + 1.0f - px) * ddx;
        float sdy = rdy < 0 ? (py - my) * ddy : (my + 1.0f - py) * ddy;
        int side = 0, hit = 0, guard = 0;
        char hc = '#';
        while (!hit && guard++ < 64) {
            if (sdx < sdy) { sdx += ddx; mx += stx; side = 0; } else { sdy += ddy; my += sty; side = 1; }
            if (mx < 0 || my < 0 || mx >= MW || my >= MH) { hit = 1; hc = '#'; break; }
            if (closed(map[my][mx])) { hit = 1; hc = map[my][mx]; }
        }
        float perp = side == 0 ? (mx - px + (1 - stx) * 0.5f) / (rdx == 0 ? 1e-6f : rdx) : (my - py + (1 - sty) * 0.5f) / (rdy == 0 ? 1e-6f : rdy);
        if (perp < 0.05f) perp = 0.05f;
        zbuf[i] = perp;
        float wx = side == 0 ? py + perp * rdy : px + perp * rdx;
        wx -= floorf(wx);
        float lh = SH / perp, ds = SH * 0.5f - lh * 0.5f;
        float fog = dim / (1.0f + 0.22f * perp * perp * 0.35f + 0.12f * perp);
        if (fog < 0.06f) fog = 0.06f;
        if (side) fog *= 0.72f;
        float x1 = i * colw, x2 = x1 + colw;
        int r1, g1, b1, r2, g2, b2;
        if (hc == '#') { r1 = 205; g1 = 198; b1 = 168; r2 = 120; g2 = 150; b2 = 130; }
        else if (hc == 'D') { r1 = r2 = 122; g1 = g2 = 90; b1 = b2 = 56; }
        else if (hc == 'L') { r1 = r2 = 92; g1 = g2 = 62; b1 = b2 = 42; if (wx > 0.4f && wx < 0.6f) { r1 = r2 = 150; g1 = g2 = 30; b1 = b2 = 30; } }
        else if (hc == 'E') { r1 = r2 = 74; g1 = g2 = 106; b1 = b2 = 138; if (wx > 0.4f && wx < 0.6f) { r1 = r2 = 200; g1 = g2 = 40; b1 = b2 = 40; } }
        else { int bar = ((int)(wx * 10) % 2 == 0); r1 = r2 = bar ? 70 : 30; g1 = g2 = bar ? 100 : 40; b1 = b2 = bar ? 140 : 60; }
        float jn = (wx < 0.03f) ? 0.8f : 1.0f; /* junta de parede */
        rect(x1, ds, x2, ds + lh * 0.55f, C((int)(r1 * fog * jn), (int)(g1 * fog * jn), (int)(b1 * fog * jn)));
        rect(x1, ds + lh * 0.55f, x2, ds + lh * 0.93f, C((int)(r2 * fog * jn), (int)(g2 * fog * jn), (int)(b2 * fog * jn)));
        rect(x1, ds + lh * 0.93f, x2, ds + lh, C((int)(55 * fog), (int)(50 * fog), (int)(42 * fog)));
    }
    /* itens e professor (billboards) */
    float invd = 1.0f / (plx * diry - dirx * ply);
    for (int i = 0; i < 3; i++) {
        if (items[i].taken) continue;
        float dx = items[i].x - px, dy = items[i].y - py;
        float tx = invd * (diry * dx - dirx * dy), ty = invd * (-ply * dx + plx * dy);
        if (ty < 0.25f) continue;
        float sx = (SW * 0.5f) * (1.0f + tx / ty), h = SH / ty;
        int ci = (int)(sx / colw);
        if (sx < 0 || ci < 0 || ci >= NCOL || ty >= zbuf[ci]) continue;
        float fog = dim / (1.0f + 0.12f * ty); if (fog < 0.2f) fog = 0.2f;
        float pulse = 0.7f + 0.3f * sinf(gtime * 4.0f + i);
        float y0 = SH * 0.5f - h * 0.5f + h * 0.72f, s = h * 0.09f;
        int r = items[i].type == 1 ? 230 : (items[i].type == 2 ? 240 : 80), g = items[i].type == 1 ? 190 : (items[i].type == 2 ? 130 : 220), b = items[i].type == 3 ? 120 : 40;
        rect(sx - s * 2.2f, y0 - s * 0.6f, sx + s * 2.2f, y0 + s * 1.6f, CA(255, 230, 120, (int)(30 * pulse)));
        rect(sx - s, y0, sx + s, y0 + s * 0.8f, C((int)(r * fog * pulse), (int)(g * fog * pulse), (int)(b * fog * pulse)));
        rect(sx + s, y0 + s * 0.25f, sx + s * 2.0f, y0 + s * 0.5f, C((int)(r * fog), (int)(g * fog), (int)(b * fog)));
    }
    {
        float dx = T.x - px, dy = T.y - py;
        float tx = invd * (diry * dx - dirx * dy), ty = invd * (-ply * dx + plx * dy);
        if (ty > 0.25f) {
            float sx = (SW * 0.5f) * (1.0f + tx / ty), h = SH / ty * 0.95f, w = h * 0.5f;
            float fog = dim / (1.0f + 0.10f * ty); if (fog < 0.15f) fog = 0.15f;
            float yt = SH * 0.5f + (SH / ty) * 0.5f - h;
            figure(sx - w * 0.5f, yt, w, h, fog, 1, ty);
        }
    }
}

static void vignette(float amt) {
    if (amt <= 0.02f) return;
    for (int k = 0; k < 6; k++) {
        int a = (int)(amt * (10 + k * 10)); if (a > 120) a = 120;
        float e = (6 - k) * 18.0f;
        rect(0, 0, (float)SW, e, CA(0, 0, 0, a / 3));
        rect(0, SH - e, (float)SW, (float)SH, CA(0, 0, 0, a / 3));
        rect(0, 0, e, (float)SH, CA(0, 0, 0, a / 3));
        rect(SW - e, 0, (float)SW, (float)SH, CA(0, 0, 0, a / 3));
    }
}

static void hud(void) {
    int done[4] = {hasKey || power || hasGKey || gateTimer10, power || hasGKey || gateTimer10, hasGKey || gateTimer10, gateOpen};
    for (int i = 0; i < 4; i++) {
        rect(10 + i * 26.0f, 10, 32 + i * 26.0f, 32, C(150, 150, 150));
        rect(12 + i * 26.0f, 12, 30 + i * 26.0f, 30, done[i] ? C(60, 200, 90) : C(25, 25, 25));
    }
    if (hasKey) rect(SW - 130.0f, 12, SW - 100.0f, 28, C(230, 190, 40));
    if (hasFuse) rect(SW - 90.0f, 12, SW - 60.0f, 28, C(240, 130, 40));
    if (hasGKey) rect(SW - 50.0f, 12, SW - 20.0f, 28, C(80, 220, 120));
    if (msgT > 0) {
        float w = text_w(msg, 2);
        rect(SW * 0.5f - w * 0.5f - 8, SH - 46.0f, SW * 0.5f + w * 0.5f + 8, SH - 18.0f, CA(0, 0, 0, 90));
        text(SW * 0.5f - w * 0.5f, SH - 40.0f, 2, msg, C(240, 235, 220));
    }
    if (gateTimer10 && !gateOpen) {
        char b[24]; sprintf(b, "PORTAO: %d", (int)gateT + 1);
        text(SW * 0.5f - text_w(b, 4) * 0.5f, 50, 4, b, C(255, 70, 60));
    }
}

static void scare_screen(float t) {
    float sh = t < 2.0f ? 7.0f : 0.0f;
    float ox = ((rand() % 200) / 100.0f - 1.0f) * sh * 3, oy = ((rand() % 200) / 100.0f - 1.0f) * sh * 3;
    rect(0, 0, (float)SW, (float)SH, C(0, 0, 0));
    if (t < 0.12f) { rect(0, 0, (float)SW, (float)SH, C(255, 255, 255)); return; }
    float s = 1.0f + t * 0.25f, cx = SW * 0.5f + ox, cy = SH * 0.52f + oy;
    int open = ((int)(t * 14)) & 1;
    rect(cx - 150 * s, SH - 120.0f, cx + 150 * s, (float)SH, C(170, 20, 20));
    rect(cx - 30 * s, SH - 110.0f, cx + 30 * s, (float)SH, C(20, 20, 22));
    rect(cx - 190 * s, cy - 230 * s, cx + 190 * s, cy + 190 * s, C(150, 95, 78));
    rect(cx - 200 * s, cy - 250 * s, cx + 200 * s, cy - 170 * s, C(18, 14, 12));
    rect(cx - 190 * s, cy - 250 * s, cx - 150 * s, cy - 40 * s, C(18, 14, 12));
    rect(cx + 150 * s, cy - 250 * s, cx + 190 * s, cy - 40 * s, C(18, 14, 12));
    for (int e = -1; e <= 1; e += 2) {
        float ex = cx + e * 88 * s;
        rect(ex - 55 * s, cy - 120 * s, ex + 55 * s, cy - 95 * s + 18 * s, C(10, 6, 6));
        rect(ex - 45 * s, cy - 82 * s, ex + 45 * s, cy - 40 * s - (open ? 0 : 6 * s), C(235, 235, 240));
        rect(ex - 12 * s, cy - 72 * s, ex + 12 * s, cy - 50 * s, C(20, 10, 8));
        rect(ex - 45 * s, cy - 62 * s, ex - 20 * s, cy - 58 * s, C(190, 30, 30));
        rect(ex + 20 * s, cy - 60 * s, ex + 45 * s, cy - 56 * s, C(190, 30, 30));
    }
    rect(cx - 160 * s, cy + 40 * s, cx + 160 * s, cy + 200 * s, C(55, 52, 50));
    float jaw = open ? 70.0f : 35.0f;
    rect(cx - 105 * s, cy + 70 * s, cx + 105 * s, cy + (80 + jaw) * s, C(12, 2, 2));
    for (int k = -4; k <= 4; k++) {
        rect(cx + k * 22 * s - 9 * s, cy + 70 * s, cx + k * 22 * s + 9 * s, cy + 100 * s, C(225, 210, 140));
        rect(cx + k * 22 * s - 9 * s, cy + (80 + jaw) * s - 28 * s, cx + k * 22 * s + 9 * s, cy + (80 + jaw) * s, C(215, 200, 130));
    }
    if (t > 0.3f) { /* tela rachada */
        for (int k = 0; k < 14; k++) {
            float a = k * (2 * PI_F / 14) + 0.3f, x = SW * 0.5f + 40, y = SH * 0.45f;
            for (int j = 0; j < 40; j++) {
                a += (((rand() % 100) / 100.0f) - 0.5f) * 0.35f;
                x += cosf(a) * 14; y += sinf(a) * 14;
                rect(x, y, x + 3, y + 3, C(255, 255, 255));
            }
        }
    }
    for (int k = 0; k < 4; k++) { /* falhas de imagem */
        float y = (float)(rand() % SH);
        rect(0, y, (float)SW, y + 4 + rand() % 10, CA(255, 255, 255, 40));
    }
    vignette(1.0f);
}

static void title_screen(void) {
    rect(0, 0, (float)SW, (float)SH, C(6, 4, 4));
    float f = (((int)(gtime * 10)) % 37 == 0) ? 0.3f : 1.0f;
    const char *t1 = "A ULTIMA", *t2 = "AULA";
    text(SW * 0.06f, 70, 9, t1, C((int)(200 * f), (int)(18 * f), (int)(24 * f)));
    text(SW * 0.06f, 70 + 5 * 9 + 20, 9, t2, C((int)(200 * f), (int)(18 * f), (int)(24 * f)));
    text(SW * 0.06f, 230, 2, "ESCOLA ESTADUAL PROFESSOR HORIZONTE", C(170, 160, 140));
    if (((int)(gtime * 2)) & 1) text(SW * 0.06f, 300, 3, "PRESSIONE START", C(235, 230, 215));
    text(SW * 0.06f, 360, 2, "ANALOGICO: ANDAR E VIRAR  QUADRADO: CORRER  X: INTERAGIR", C(120, 110, 100));
    float h = 330.0f, w = h * 0.5f;
    figure(SW * 0.74f, 70.0f, w, h, 0.55f + 0.2f * sinf(gtime * 3.0f), 0, 0);
    vignette(0.8f);
}

static void end_screen(int win) {
    rect(0, 0, (float)SW, (float)SH, C(4, 3, 3));
    const char *a = win ? "VOCE ESCAPOU" : "FIM DE AULA";
    text(SW * 0.5f - text_w(a, 8) * 0.5f, 120, 8, a, win ? C(90, 220, 120) : C(210, 20, 26));
    const char *b = win ? "O PORTAO SE ABRIU" : "O PROFESSOR TE ACHOU";
    text(SW * 0.5f - text_w(b, 3) * 0.5f, 230, 3, b, C(200, 190, 175));
    if (((int)(gtime * 2)) & 1) text(SW * 0.5f - text_w("PRESSIONE START", 3) * 0.5f, 320, 3, "PRESSIONE START", C(235, 230, 215));
}

/* ---------- loop principal ---------- */
static void game_update(float dt, float fwd, float strafe, float turn, int run, int press) {
    pa += turn * 2.4f * dt;
    float spd = (run ? 3.3f : 2.0f) * dt;
    float mvx = (cosf(pa) * fwd - sinf(pa) * strafe), mvy = (sinf(pa) * fwd + cosf(pa) * strafe);
    float m = sqrtf(mvx * mvx + mvy * mvy);
    runNoise = 0;
    if (m > 0.05f) {
        if (m > 1.0f) { mvx /= m; mvy /= m; }
        float nx = px + mvx * spd, ny = py + mvy * spd;
        if (!hitc(nx, py, 0.22f)) px = nx;
        if (!hitc(px, ny, 0.22f)) py = ny;
        if (run) runNoise = 1.0f;
    }
    if (press) interact();
    if (msgT > 0) msgT -= dt;
    flick -= dt;
    if (flick < -4.0f - (rand() % 400) / 100.0f) flick = 0.25f;
    if (gateTimer10 && !gateOpen) {
        gateT -= dt;
        if (gateT <= 0) { gateOpen = 1; map[14][15] = 'o'; say("PORTAO ABERTO! CORRA!"); }
    }
    teacher_update(dt);
    if (gateOpen && py > 14.2f) gstate = 4;
    float dx = px - T.x, dy = py - T.y, d = sqrtf(dx * dx + dy * dy);
    hbT -= dt; if (hbP > 0) hbP -= dt * 4;
    if (d < 9.0f && hbT <= 0) { hbT = 0.4f + d * 0.09f; hbP = 1.0f; }
}

static void load_pad_modules(void) {
    int r;
    r = SifLoadModule("rom0:SIO2MAN", 0, NULL);
    if (r < 0) SifLoadModule("rom0:XSIO2MAN", 0, NULL);
    r = SifLoadModule("rom0:PADMAN", 0, NULL);
    if (r < 0) SifLoadModule("rom0:XPADMAN", 0, NULL);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* reinicia o IOP (limpa os modulos do OPL) e carrega so o que o jogo usa */
    SifInitRpc(0);
    SifIopReset("", 0);
    while (!SifIopSync()) { }
    SifInitRpc(0);
    SifLoadFileInit();
    load_pad_modules();
    pad_setup();

    /* pool maior de primitivas: o jogo desenha milhares de retangulos por quadro */
    gs = gsKit_init_global_custom(2 * 1024 * 1024, 16384 * 16);
    gs->PSM = GS_PSM_CT24;
    gs->PSMZ = GS_PSMZ_16S;
    gs->ZBuffering = GS_SETTING_OFF;
    gs->DoubleBuffering = GS_SETTING_ON;
    gs->PrimAlphaEnable = GS_SETTING_ON;
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);
    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    SW = gs->Width; SH = gs->Height;
    colw = (float)SW / NCOL;

    build_map();
    reset_game();
    gstate = 0;

    for (;;) {
        float fwd, strafe, turn; int run, press, start;
        pad_read(&fwd, &strafe, &turn, &run, &press, &start);
        const float dt = 1.0f / 60.0f;
        gtime += dt; frame++;
        gsKit_clear(gs, C(0, 0, 0));
        if (gstate == 0) {
            title_screen();
            if (start) { reset_game(); gstate = 1; }
        } else if (gstate == 1) {
            game_update(dt, fwd, strafe, turn, run, press);
            float dim = flick > 0 ? 0.25f : 1.0f;
            render_world(dim);
            float dx = px - T.x, dy = py - T.y, d = sqrtf(dx * dx + dy * dy);
            float nearv = d < 12.0f ? (1.0f - d / 12.0f) : 0.0f;
            vignette(nearv * 0.9f + hbP * 0.25f * nearv);
            hud();
        } else if (gstate == 2) {
            scareT += dt;
            scare_screen(scareT);
            if (scareT > 2.6f) gstate = 3;
        } else if (gstate == 3) {
            end_screen(0);
            if (start) { reset_game(); gstate = 1; }
        } else {
            end_screen(1);
            if (start) { reset_game(); gstate = 0; }
        }
        gsKit_queue_exec(gs);
        gsKit_sync_flip(gs);
    }
    return 0;
}
