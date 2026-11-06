#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <algorithm>

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

struct Color { float r, g, b, a; };
struct Bullet { float x, y, vx, vy, radius; bool fromPlayer; Color color; };
struct Enemy { float x, y, vx, vy, radius; int type, hp, maxHp, shootTimer, hitFlash; };
struct PowerUp { float x, y, vy; int type; };
struct Particle { float x, y, vx, vy, size, life, maxLife; Color color; };
struct Star { float x, y, speed, size, brightness; };

struct Game {
    int width = 720, height = 920;
    float px = 360.0f, py = 760.0f;
    float mouseX = 360.0f, mouseY = 760.0f;
    bool useMouse = false;
    int hp = 100;
    int weaponLevel = 1;
    int fireCooldown = 0;
    int score = 0;
    int highScore = 0;
    int frameCount = 0;
    float energy = 100.0f;
    float shakeTimer = 0.0f;
    bool gameOver = false;
    std::vector<Bullet> bullets;
    std::vector<Enemy> enemies;
    std::vector<PowerUp> powerUps;
    std::vector<Particle> particles;
    std::vector<Star> stars;
} g_game;

static float randFloat(float minV, float maxV) {
    return minV + (static_cast<float>(rand()) / RAND_MAX) * (maxV - minV);
}

static void pushTri(float x1, float y1, float x2, float y2, float x3, float y3, Color c) {
    glColor4f(c.r, c.g, c.b, c.a);
    glVertex2f(x1, y1); glVertex2f(x2, y2); glVertex2f(x3, y3);
}

static void drawRect(float x, float y, float w, float h, Color c) {
    pushTri(x, y, x + w, y, x + w, y + h, c);
    pushTri(x, y, x + w, y + h, x, y + h, c);
}

static void drawDiamond(float cx, float cy, float rx, float ry, Color c) {
    pushTri(cx, cy - ry, cx + rx, cy, cx, cy + ry, c);
    pushTri(cx, cy - ry, cx, cy + ry, cx - rx, cy, c);
}

static void drawDigit(int digit, float x, float y, float w, float h, float t, Color c) {
    static const int segs[10] = {0b1110111, 0b0100100, 0b1011101, 0b1101101, 0b0101110, 0b1101011, 0b1111011, 0b0100101, 0b1111111, 0b1101111};
    int mask = segs[digit % 10];
    float hw = h * 0.5f;
    if (mask & 1)  drawRect(x, y, w, t, c);
    if (mask & 2)  drawRect(x, y, t, hw, c);
    if (mask & 4)  drawRect(x + w - t, y, t, hw, c);
    if (mask & 8)  drawRect(x, y + hw - t * 0.5f, w, t, c);
    if (mask & 16) drawRect(x, y + hw, t, hw, c);
    if (mask & 32) drawRect(x + w - t, y + hw, t, hw, c);
    if (mask & 64) drawRect(x, y + h - t, w, t, c);
}

static void drawNumber(int num, float rightX, float y, float scale, Color c) {
    float w = 18.0f * scale, h = 32.0f * scale, t = 4.0f * scale, gap = 6.0f * scale;
    if (num == 0) { drawDigit(0, rightX - w, y, w, h, t, c); return; }
    float curX = rightX - w;
    while (num > 0) {
        drawDigit(num % 10, curX, y, w, h, t, c);
        num /= 10;
        curX -= (w + gap);
    }
}

static void spawnExplosion(Game* g, float x, float y, Color c, int count, float maxSpd) {
    for (int i = 0; i < count; ++i) {
        float angle = randFloat(0.0f, 6.28318f);
        float speed = randFloat(1.5f, maxSpd);
        float life = randFloat(18.0f, 38.0f);
        g->particles.push_back({x, y, std::cos(angle) * speed, std::sin(angle) * speed, randFloat(4.0f, 11.0f), life, life, c});
    }
}

static void resetGame(Game* g) {
    g->px = g->width * 0.5f; g->py = g->height * 0.8f;
    g->mouseX = g->px; g->mouseY = g->py;
    g->hp = 100; g->weaponLevel = 1; g->score = 0; g->energy = 100.0f;
    g->frameCount = 0; g->gameOver = false;
    g->bullets.clear(); g->enemies.clear(); g->powerUps.clear(); g->particles.clear();
}

static void triggerShockwave(Game* g) {
    if (g->gameOver || g->energy < 100.0f) return;
    g->energy = 0.0f; g->shakeTimer = 18.0f; g->bullets.clear();
    spawnExplosion(g, g->px, g->py, {0.6f, 0.3f, 1.0f, 1.0f}, 90, 20.0f);
    for (size_t i = 0; i < g->enemies.size();) {
        auto& e = g->enemies[i];
        e.hp -= 10; e.hitFlash = 8;
        if (e.hp <= 0) {
            g->score += (e.type == 2) ? 350 : 100;
            g->highScore = std::max(g->score, g->highScore);
            spawnExplosion(g, e.x, e.y, {1.0f, 0.5f, 0.2f, 1.0f}, 30, 12.0f);
            g->enemies.erase(g->enemies.begin() + i);
        } else ++i;
    }
}

static void updateGame(Game* g) {
    g->frameCount++;
    if (g->shakeTimer > 0) g->shakeTimer -= 1.0f;
    for (auto& s : g->stars) {
        s.y += s.speed;
        if (s.y > g->height) { s.y = 0; s.x = randFloat(0.0f, (float)g->width); }
    }
    if (g->gameOver) return;
    float moveSpeed = 9.5f;
    bool keyMoved = false;
    if (GetAsyncKeyState(VK_LEFT) & 0x8000 || GetAsyncKeyState('A') & 0x8000) { g->px -= moveSpeed; keyMoved = true; }
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000 || GetAsyncKeyState('D') & 0x8000) { g->px += moveSpeed; keyMoved = true; }
    if (GetAsyncKeyState(VK_UP) & 0x8000 || GetAsyncKeyState('W') & 0x8000) { g->py -= moveSpeed; keyMoved = true; }
    if (GetAsyncKeyState(VK_DOWN) & 0x8000 || GetAsyncKeyState('S') & 0x8000) { g->py += moveSpeed; keyMoved = true; }
    if (keyMoved) g->useMouse = false;
    else if (g->useMouse) {
        g->px += (g->mouseX - g->px) * 0.22f;
        g->py += (g->mouseY - g->py) * 0.22f;
    }
    g->px = std::clamp(g->px, 35.0f, (float)g->width - 35.0f);
    g->py = std::clamp(g->py, 90.0f, (float)g->height - 50.0f);
    if (g->energy < 100.0f) g->energy = std::min(100.0f, g->energy + 0.18f);
    g->particles.push_back({g->px + randFloat(-7.0f, 7.0f), g->py + 30.0f, randFloat(-1.0f, 1.0f), randFloat(4.5f, 9.0f), randFloat(6.0f, 10.0f), 11.0f, 11.0f, {0.0f, 0.85f, 1.0f, 0.9f}});
    if (g->fireCooldown > 0) g->fireCooldown--;
    if (g->fireCooldown <= 0) {
        g->fireCooldown = 7;
        Color bc = {0.2f, 1.0f, 0.9f, 1.0f};
        auto fire = [&](float ox, float vx, float vy) { g->bullets.push_back({g->px + ox, g->py - 28.0f, vx, vy, 8.0f, true, bc}); };
        if (g->weaponLevel == 1) fire(0.0f, 0.0f, -20.0f);
        else if (g->weaponLevel == 2) { fire(-16.0f, 0.0f, -20.0f); fire(16.0f, 0.0f, -20.0f); }
        else if (g->weaponLevel == 3) { fire(0.0f, 0.0f, -21.0f); fire(-18.0f, -2.5f, -19.0f); fire(18.0f, 2.5f, -19.0f); }
        else { fire(0.0f, 0.0f, -22.0f); fire(-15.0f, -2.2f, -20.0f); fire(15.0f, 2.2f, -20.0f); fire(-28.0f, -4.8f, -18.0f); fire(28.0f, 4.8f, -18.0f); }
    }
    int spawnRate = std::max(18, 48 - g->score / 400);
    if (g->frameCount % spawnRate == 0) {
        int roll = rand() % 100;
        int type = (roll < 55) ? 0 : (roll < 85 ? 1 : 2);
        float radius = (type == 0) ? 26.0f : (type == 1 ? 32.0f : 50.0f);
        int hp = (type == 0) ? 2 : (type == 1 ? 5 : 16);
        float vy = (type == 0) ? randFloat(4.5f, 7.0f) : (type == 1 ? 3.6f : 2.0f);
        g->enemies.push_back({randFloat(60.0f, (float)g->width - 60.0f), -radius, randFloat(-2.2f, 2.2f), vy, radius, type, hp, hp, rand() % 50, 0});
    }
    for (size_t i = 0; i < g->bullets.size();) {
        auto& b = g->bullets[i];
        b.x += b.vx; b.y += b.vy;
        if (b.y < -40 || b.y > g->height + 40 || b.x < -40 || b.x > g->width + 40) g->bullets.erase(g->bullets.begin() + i);
        else ++i;
    }
    for (size_t i = 0; i < g->enemies.size();) {
        auto& e = g->enemies[i];
        e.y += e.vy;
        if (e.type == 0) e.x += std::sin(g->frameCount * 0.08f + i) * 3.8f;
        else { e.x += e.vx; if (e.x < e.radius || e.x > g->width - e.radius) e.vx = -e.vx; }
        if (e.hitFlash > 0) e.hitFlash--;
        e.shootTimer++;
        if (e.type == 1 && e.shootTimer > 70) { e.shootTimer = 0; g->bullets.push_back({e.x, e.y + e.radius, 0.0f, 9.0f, 10.0f, false, {1.0f, 0.2f, 0.3f, 1.0f}}); }
        else if (e.type == 2 && e.shootTimer > 90) { e.shootTimer = 0; for (int k = -1; k <= 1; ++k) g->bullets.push_back({e.x, e.y + e.radius, k * 2.8f, 8.0f, 11.0f, false, {1.0f, 0.1f, 0.5f, 1.0f}}); }
        bool destroyed = false;
        for (size_t j = 0; j < g->bullets.size();) {
            auto& b = g->bullets[j];
            if (b.fromPlayer && std::hypot(b.x - e.x, b.y - e.y) < e.radius + b.radius) {
                e.hp--; e.hitFlash = 4;
                spawnExplosion(g, b.x, b.y, {0.3f, 1.0f, 1.0f, 1.0f}, 4, 4.5f);
                g->bullets.erase(g->bullets.begin() + j);
                if (e.hp <= 0) {
                    g->score += (e.type == 0) ? 50 : (e.type == 1 ? 120 : 350);
                    g->highScore = std::max(g->score, g->highScore);
                    Color ec = (e.type == 0) ? Color{1.0f, 0.7f, 0.1f, 1.0f} : (e.type == 1) ? Color{1.0f, 0.2f, 0.8f, 1.0f} : Color{1.0f, 0.2f, 0.2f, 1.0f};
                    spawnExplosion(g, e.x, e.y, ec, e.type == 2 ? 40 : 20, 10.0f);
                    g->shakeTimer = (e.type == 2) ? 12.0f : 4.0f;
                    if (e.type == 2 || (rand() % 100 < 16)) g->powerUps.push_back({e.x, e.y, 3.2f, rand() % 2});
                    destroyed = true; break;
                }
            } else ++j;
        }
        if (destroyed) { g->enemies.erase(g->enemies.begin() + i); continue; }
        if (std::hypot(e.x - g->px, e.y - g->py) < e.radius + 24.0f) {
            g->hp -= 25; g->shakeTimer = 14.0f;
            spawnExplosion(g, e.x, e.y, {1.0f, 0.3f, 0.1f, 1.0f}, 25, 9.0f);
            g->enemies.erase(g->enemies.begin() + i);
            if (g->hp <= 0) { g->hp = 0; g->gameOver = true; spawnExplosion(g, g->px, g->py, {0.0f, 0.9f, 1.0f, 1.0f}, 55, 13.0f); }
            continue;
        }
        if (e.y > g->height + 80) g->enemies.erase(g->enemies.begin() + i);
        else ++i;
    }
    for (size_t i = 0; i < g->bullets.size();) {
        auto& b = g->bullets[i];
        if (!b.fromPlayer && std::hypot(b.x - g->px, b.y - g->py) < b.radius + 20.0f) {
            g->hp -= 15; g->shakeTimer = 9.0f;
            spawnExplosion(g, b.x, b.y, {1.0f, 0.2f, 0.3f, 1.0f}, 12, 6.0f);
            g->bullets.erase(g->bullets.begin() + i);
            if (g->hp <= 0) { g->hp = 0; g->gameOver = true; spawnExplosion(g, g->px, g->py, {0.0f, 0.9f, 1.0f, 1.0f}, 55, 13.0f); }
        } else ++i;
    }
    for (size_t i = 0; i < g->powerUps.size();) {
        auto& p = g->powerUps[i];
        p.y += p.vy;
        if (std::hypot(p.x - g->px, p.y - g->py) < 45.0f) {
            if (p.type == 0) g->weaponLevel = std::min(4, g->weaponLevel + 1);
            else g->hp = std::min(100, g->hp + 35);
            g->score += 100;
            spawnExplosion(g, p.x, p.y, {0.2f, 1.0f, 0.4f, 1.0f}, 15, 7.0f);
            g->powerUps.erase(g->powerUps.begin() + i);
        } else if (p.y > g->height + 40) g->powerUps.erase(g->powerUps.begin() + i);
        else ++i;
    }
    for (size_t i = 0; i < g->particles.size();) {
        auto& pt = g->particles[i];
        pt.x += pt.vx; pt.y += pt.vy; pt.life -= 1.0f;
        if (pt.life <= 0.0f) g->particles.erase(g->particles.begin() + i);
        else ++i;
    }
}

static void renderGame(Game* g, HDC hdc) {
    glViewport(0, 0, g->width, g->height);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, g->width, g->height, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glClearColor(0.03f, 0.04f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    float sx = (g->shakeTimer > 0) ? randFloat(-g->shakeTimer, g->shakeTimer) : 0.0f;
    float sy = (g->shakeTimer > 0) ? randFloat(-g->shakeTimer, g->shakeTimer) : 0.0f;
    glBegin(GL_TRIANGLES);
    for (const auto& s : g->stars) drawRect(s.x + sx * 0.3f, s.y + sy * 0.3f, s.size, s.size, {0.8f, 0.9f, 1.0f, s.brightness});
    for (const auto& p : g->powerUps) {
        Color c = (p.type == 0) ? Color{0.0f, 1.0f, 1.0f, 1.0f} : Color{0.2f, 1.0f, 0.3f, 1.0f};
        drawDiamond(p.x + sx, p.y + sy, 20.0f, 20.0f, c);
        drawDiamond(p.x + sx, p.y + sy, 10.0f, 10.0f, {1.0f, 1.0f, 1.0f, 1.0f});
    }
    for (const auto& b : g->bullets) drawDiamond(b.x + sx, b.y + sy, b.radius, b.radius * 1.8f, b.color);
    for (const auto& e : g->enemies) {
        float ex = e.x + sx, ey = e.y + sy;
        Color baseColor = (e.hitFlash > 0) ? Color{1.0f, 1.0f, 1.0f, 1.0f} : (e.type == 0) ? Color{1.0f, 0.65f, 0.0f, 1.0f} : (e.type == 1) ? Color{0.95f, 0.15f, 0.75f, 1.0f} : Color{0.9f, 0.15f, 0.25f, 1.0f};
        if (e.type == 0) { drawDiamond(ex, ey, e.radius, e.radius, baseColor); drawDiamond(ex, ey, e.radius * 0.45f, e.radius * 0.45f, {1.0f, 0.95f, 0.4f, 1.0f}); }
        else if (e.type == 1) { pushTri(ex, ey + e.radius, ex - e.radius, ey - e.radius * 0.7f, ex + e.radius, ey - e.radius * 0.7f, baseColor); pushTri(ex, ey + e.radius * 0.5f, ex - e.radius * 0.4f, ey - e.radius, ex + e.radius * 0.4f, ey - e.radius, {0.4f, 0.0f, 0.5f, 1.0f}); }
        else { drawDiamond(ex, ey, e.radius * 1.1f, e.radius * 0.85f, baseColor); drawRect(ex - e.radius * 0.6f, ey - e.radius * 0.6f, e.radius * 1.2f, e.radius * 1.2f, baseColor); drawDiamond(ex, ey, e.radius * 0.4f, e.radius * 0.4f, {1.0f, 0.8f, 0.2f, 1.0f}); float bw = e.radius * 1.4f; drawRect(ex - bw * 0.5f, ey - e.radius - 14.0f, bw, 7.0f, {0.3f, 0.0f, 0.0f, 0.8f}); drawRect(ex - bw * 0.5f, ey - e.radius - 14.0f, bw * (float(e.hp) / e.maxHp), 7.0f, {1.0f, 0.2f, 0.2f, 1.0f}); }
    }
    if (!g->gameOver) {
        float x = g->px + sx, y = g->py + sy;
        pushTri(x, y - 24.0f, x - 44.0f, y + 28.0f, x + 44.0f, y + 28.0f, {0.0f, 0.55f, 0.95f, 1.0f});
        pushTri(x, y - 40.0f, x - 20.0f, y + 32.0f, x + 20.0f, y + 32.0f, {0.0f, 0.92f, 1.0f, 1.0f});
        drawDiamond(x, y - 4.0f, 8.0f, 15.0f, {0.9f, 1.0f, 1.0f, 1.0f});
    }
    for (const auto& pt : g->particles) {
        Color c = pt.color; c.a = (pt.life / pt.maxLife); float s = pt.size * c.a;
        drawRect(pt.x - s * 0.5f + sx, pt.y - s * 0.5f + sy, s, s, c);
    }
    drawRect(25.0f, 25.0f, 220.0f, 20.0f, {0.15f, 0.2f, 0.3f, 0.85f});
    Color hpCol = (g->hp > 50) ? Color{0.1f, 0.95f, 0.4f, 1.0f} : (g->hp > 25) ? Color{1.0f, 0.75f, 0.1f, 1.0f} : Color{1.0f, 0.15f, 0.2f, 1.0f};
    drawRect(25.0f, 25.0f, 2.2f * g->hp, 20.0f, hpCol);
    drawRect(25.0f, 52.0f, 220.0f, 10.0f, {0.15f, 0.1f, 0.25f, 0.85f});
    Color empCol = (g->energy >= 100.0f) ? Color{0.75f, 0.35f, 1.0f, 1.0f} : Color{0.45f, 0.2f, 0.7f, 1.0f};
    drawRect(25.0f, 52.0f, 2.2f * g->energy, 10.0f, empCol);
    for (int i = 0; i < g->weaponLevel; ++i) drawRect(25.0f + i * 32.0f, 70.0f, 24.0f, 10.0f, {0.0f, 0.95f, 1.0f, 1.0f});
    drawNumber(g->score, g->width - 30.0f, 25.0f, 1.0f, {1.0f, 1.0f, 1.0f, 1.0f});
    if (g->gameOver) {
        float cy = g->height * 0.45f;
        drawRect(0.0f, cy - 90.0f, (float)g->width, 190.0f, {0.08f, 0.02f, 0.05f, 0.92f});
        drawRect(0.0f, cy - 90.0f, (float)g->width, 6.0f, {1.0f, 0.2f, 0.3f, 1.0f});
        drawRect(0.0f, cy + 94.0f, (float)g->width, 6.0f, {1.0f, 0.2f, 0.3f, 1.0f});
        drawNumber(g->score, g->width * 0.58f, cy - 55.0f, 1.4f, {1.0f, 0.3f, 0.4f, 1.0f});
        drawNumber(g->highScore, g->width * 0.58f, cy + 25.0f, 1.0f, {1.0f, 0.85f, 0.2f, 1.0f});
    }
    glEnd();
    SwapBuffers(hdc);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE: g_game.width = std::max(1, (int)LOWORD(lParam)); g_game.height = std::max(1, (int)HIWORD(lParam)); return 0;
        case WM_MOUSEMOVE: g_game.mouseX = (float)LOWORD(lParam); g_game.mouseY = (float)HIWORD(lParam); g_game.useMouse = true; return 0;
        case WM_LBUTTONDOWN: if (g_game.gameOver) resetGame(&g_game); else triggerShockwave(&g_game); return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) PostQuitMessage(0);
            else if (wParam == VK_SPACE) { if (g_game.gameOver) resetGame(&g_game); else triggerShockwave(&g_game); }
            else if (wParam == 'R' && g_game.gameOver) resetGame(&g_game);
            return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    srand((unsigned)time(nullptr));
    WNDCLASSA wc = {};
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    wc.lpszClassName = "NeonStrikeforceClass";
    RegisterClassA(&wc);
    RECT rect = {0, 0, g_game.width, g_game.height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowA(wc.lpszClassName, "Neon Strikeforce", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, hInst, nullptr);
    HDC hdc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32;
    int pf = ChoosePixelFormat(hdc, &pfd);
    SetPixelFormat(hdc, pf, &pfd);
    HGLRC hglrc = wglCreateContext(hdc);
    wglMakeCurrent(hdc, hglrc);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (int i = 0; i < 85; ++i) {
        g_game.stars.push_back({randFloat(0.0f, (float)g_game.width), randFloat(0.0f, (float)g_game.height), randFloat(1.2f, 6.5f), randFloat(2.0f, 4.5f), randFloat(0.25f, 0.9f)});
    }
    resetGame(&g_game);
    LARGE_INTEGER freq, lastTime, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastTime);
    MSG msg = {};
    bool running = true;
    while (running) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
        QueryPerformanceCounter(&now);
        double elapsedMs = (double)(now.QuadPart - lastTime.QuadPart) * 1000.0 / freq.QuadPart;
        if (elapsedMs >= 16.0) {
            lastTime = now;
            updateGame(&g_game);
            renderGame(&g_game, hdc);
        } else {
            Sleep(1);
        }
    }
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(hglrc);
    ReleaseDC(hwnd, hdc);
    return 0;
}
