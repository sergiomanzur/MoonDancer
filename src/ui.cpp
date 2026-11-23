#define IMGUI_DEFINE_MATH_OPERATORS
#include "ui.h"

#include "input.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

/* The browser scans plain directories with the same per-platform branch the
 * launcher uses for dir_exists().  NOMINMAX keeps windows.h out of the way of
 * the std::min/std::max calls above. */
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

struct mw_ui {
    SDL_Window *window;
    SDL_Renderer *renderer;
    ImFont *font_title;
    ImFont *font_h1;
    ImFont *font_button;
    ImFont *font_body;
    ImFont *font_small;
    SDL_Texture *background;
    int bg_w;
    int bg_h;
    float dpi_scale;
    float layout_scale;
    /* Overlay navigation (MW_UI_TAB_*) and the armed rebinding capture, so the
     * CONTROLLERS panel can highlight the row that is waiting for input.
     * tab_count is 3 in the game overlay (SETTINGS/CONTROLLERS/CHEATS) and 2
     * in the launcher, whose SETTINGS panel passes in_game == 0. */
    int tab;
    int tab_count;
    int cap_player;
    int cap_action;
    int cap_pad;
};

namespace {

const ImU32 C_CYAN    = IM_COL32(52, 231, 255, 255);
const ImU32 C_MAGENTA = IM_COL32(255, 79, 216, 255);
const ImU32 C_VIOLET  = IM_COL32(154, 107, 255, 255);
const ImU32 C_TEXT    = IM_COL32(234, 247, 255, 255);
const ImU32 C_MUTED   = IM_COL32(143, 166, 184, 255);
const ImU32 C_DIM     = IM_COL32(88, 108, 128, 255);
const ImU32 C_DARK    = IM_COL32(7, 9, 18, 255);

ImU32 A(ImU32 c, int alpha)
{
    return (c & 0x00FFFFFFu) | ((ImU32)(alpha < 0 ? 0 : alpha > 255 ? 255 : alpha) << 24);
}

ImU32 mix(ImU32 a, ImU32 b, float t)
{
    t = std::max(0.0f, std::min(1.0f, t));
    int ar = (a >> 0) & 0xFF, ag = (a >> 8) & 0xFF, ab = (a >> 16) & 0xFF, aa = (a >> 24) & 0xFF;
    int br = (b >> 0) & 0xFF, bg = (b >> 8) & 0xFF, bb = (b >> 16) & 0xFF, ba = (b >> 24) & 0xFF;
    return IM_COL32((int)(ar + (br - ar) * t), (int)(ag + (bg - ag) * t),
                    (int)(ab + (bb - ab) * t), (int)(aa + (ba - aa) * t));
}

float ui_scale(mw_ui *ui)
{
    ImGuiIO &io = ImGui::GetIO();
    float s = std::min(io.DisplaySize.x / 1280.0f, io.DisplaySize.y / 720.0f);
    if (s < 0.58f) s = 0.58f;
    if (s > 1.55f) s = 1.55f;
    ui->layout_scale = s;
    return s;
}

ImVec2 text_size(ImFont *font, float size, const char *text)
{
    if (!font || !text) return ImVec2(0, 0);
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
}

void text(ImDrawList *dl, ImFont *font, float size, ImVec2 pos, ImU32 col, const char *s)
{
    if (font && s && *s)
        dl->AddText(font, size, pos, col, s);
}

void text_spaced(ImDrawList *dl, ImFont *font, float size, ImVec2 pos, ImU32 col,
                 const char *s, float spacing)
{
    if (!font || !s) return;
    float x = pos.x;
    for (const char *p = s; *p; ++p) {
        char ch[2] = { *p, 0 };
        dl->AddText(font, size, ImVec2(x, pos.y), col, ch);
        x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, ch).x + spacing;
    }
}

void text_glow(ImDrawList *dl, ImFont *font, float size, ImVec2 pos,
               const char *s, ImU32 main_col, ImU32 glow_col, float scl)
{
    if (!font || !s) return;
    for (int i = 1; i <= 4; ++i) {
        float o = i * 1.6f * scl;
        int alpha = 26 - i * 4;
        dl->AddText(font, size, ImVec2(pos.x - o, pos.y), A(glow_col, alpha), s);
        dl->AddText(font, size, ImVec2(pos.x + o, pos.y), A(glow_col, alpha), s);
        dl->AddText(font, size, ImVec2(pos.x, pos.y - o), A(glow_col, alpha), s);
        dl->AddText(font, size, ImVec2(pos.x, pos.y + o), A(glow_col, alpha), s);
    }
    dl->AddText(font, size, ImVec2(pos.x + 3.0f * scl, pos.y + 3.0f * scl), A(C_MAGENTA, 120), s);
    dl->AddText(font, size, pos, main_col, s);
}

std::string file_name(const char *path)
{
    if (!path || !*path) return std::string();
    const char *a = strrchr(path, '/');
    const char *b = strrchr(path, '\\');
    const char *p = a > b ? a : b;
    return std::string(p ? p + 1 : path);
}

std::string ellipsize(ImFont *font, float size, const std::string &s, float maxw)
{
    if (s.empty()) return s;
    if (text_size(font, size, s.c_str()).x <= maxw) return s;
    std::string out = s;
    while (out.size() > 3) {
        out.pop_back();
        std::string candidate = out + "...";
        if (text_size(font, size, candidate.c_str()).x <= maxw)
            return candidate;
    }
    return "...";
}

void draw_panel(ImDrawList *dl, ImVec2 p, ImVec2 sz, float rounding, bool strong)
{
    dl->AddRectFilled(ImVec2(p.x + 10, p.y + 10), ImVec2(p.x + sz.x + 10, p.y + sz.y + 10),
                      IM_COL32(0, 0, 0, 110), rounding + 8.0f);
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y),
                      IM_COL32(7, 10, 20, strong ? 244 : 228), rounding);
    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y),
                A(C_CYAN, strong ? 150 : 82), rounding, 0, 1.2f);
    dl->AddRectFilledMultiColor(ImVec2(p.x + rounding, p.y),
                                ImVec2(p.x + sz.x - rounding, p.y + 2.6f),
                                C_MAGENTA, C_CYAN, C_CYAN, C_MAGENTA);
}

enum ButtonKind { BUTTON_PRIMARY, BUTTON_SECONDARY, BUTTON_GHOST, BUTTON_DANGER };

/* Interaction rule shared by every hand-drawn widget in this file.  The glow
 * follows the mouse and the gamepad nav cursor; the action fires on a mouse
 * click or a nav activation.  IsItemClicked() and IsItemActivated() are both
 * true on the mouse-press frame (ButtonBehavior sets the active id there), so
 * the OR stays a single trigger.  The focus half of the glow is gated on
 * io.NavVisible because a mouse click focuses the item too: without the gate a
 * click would leave the button lit after the pointer moved away. */
bool item_hot()
{
    if (ImGui::IsItemHovered()) return true;
    return ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;
}

bool item_pressed()
{
    return ImGui::IsItemClicked() || ImGui::IsItemActivated();
}

bool neon_button(mw_ui *ui, const char *id, const char *label,
                 ImVec2 pos, ImVec2 sz, ButtonKind kind, bool enabled, float s)
{
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, sz, ImGuiButtonFlags_EnableNav);
    bool hovered = item_hot() && enabled;
    bool held = ImGui::IsItemActive() && enabled;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float rounding = 10.0f * s;
    float glow = hovered ? 1.0f : 0.0f;

    if (hovered) {
        dl->AddRectFilled(ImVec2(pos.x - 5 * s, pos.y - 5 * s),
                          ImVec2(pos.x + sz.x + 5 * s, pos.y + sz.y + 5 * s),
                          A(C_CYAN, 22), rounding + 5 * s);
        dl->AddRectFilled(ImVec2(pos.x - 2 * s, pos.y - 2 * s),
                          ImVec2(pos.x + sz.x + 2 * s, pos.y + sz.y + 2 * s),
                          A(C_MAGENTA, 18), rounding + 2 * s);
    }

    ImU32 text_col = C_TEXT;
    if (!enabled) {
        dl->AddRectFilled(pos, ImVec2(pos.x + sz.x, pos.y + sz.y),
                          IM_COL32(10, 13, 22, 190), rounding);
        dl->AddRect(pos, ImVec2(pos.x + sz.x, pos.y + sz.y),
                    IM_COL32(70, 82, 96, 110), rounding, 0, 1.0f);
        text_col = IM_COL32(88, 102, 116, 255);
    } else if (kind == BUTTON_PRIMARY) {
        ImU32 base = hovered ? IM_COL32(255, 84, 205, 255) : IM_COL32(246, 57, 184, 255);
        if (held) base = IM_COL32(226, 38, 164, 255);
        dl->AddRectFilled(ImVec2(pos.x - 6 * s, pos.y - 6 * s),
                          ImVec2(pos.x + sz.x + 6 * s, pos.y + sz.y + 6 * s),
                          A(IM_COL32(255, 60, 190, 255), hovered ? 34 : 20), rounding + 6 * s);
        dl->AddRectFilled(pos, ImVec2(pos.x + sz.x, pos.y + sz.y), base, rounding);
        dl->AddRectFilledMultiColor(ImVec2(pos.x + rounding, pos.y),
                                    ImVec2(pos.x + sz.x - rounding, pos.y + 2.0f * s),
                                    A(C_MAGENTA, 235), A(C_CYAN, 235), A(C_CYAN, 235), A(C_MAGENTA, 235));
        dl->AddRect(pos, ImVec2(pos.x + sz.x, pos.y + sz.y),
                    A(IM_COL32(220, 255, 255, 255), hovered ? 230 : 150), rounding, 0, 1.3f);
        text_col = IM_COL32(5, 8, 16, 255);
    } else if (kind == BUTTON_SECONDARY) {
        dl->AddRectFilled(pos, ImVec2(pos.x + sz.x, pos.y + sz.y),
                          IM_COL32(10, 15, 27, 235), rounding);
        dl->AddRect(pos, ImVec2(pos.x + sz.x, pos.y + sz.y),
                    A(C_CYAN, hovered ? 210 : 90), rounding, 0, 1.2f);
        text_col = hovered ? IM_COL32(235, 255, 255, 255) : C_TEXT;
    } else if (kind == BUTTON_DANGER) {
        dl->AddRectFilled(pos, ImVec2(pos.x + sz.x, pos.y + sz.y),
                          IM_COL32(18, 8, 19, 220), rounding);
        dl->AddRect(pos, ImVec2(pos.x + sz.x, pos.y + sz.y),
                    A(C_MAGENTA, hovered ? 210 : 70), rounding, 0, 1.1f);
        text_col = hovered ? IM_COL32(255, 220, 250, 255) : A(C_MAGENTA, 220);
    } else {
        dl->AddRectFilled(pos, ImVec2(pos.x + sz.x, pos.y + sz.y),
                          IM_COL32(7, 10, 20, 120), rounding);
        text_col = hovered ? C_TEXT : C_MUTED;
    }

    float fsize = 25.0f * s;
    if (kind == BUTTON_SECONDARY || kind == BUTTON_GHOST || kind == BUTTON_DANGER)
        fsize = 20.0f * s;
    ImVec2 ts = text_size(ui->font_button, fsize, label);
    text(dl, ui->font_button, fsize,
         ImVec2(pos.x + (sz.x - ts.x) * 0.5f, pos.y + (sz.y - ts.y) * 0.5f - 1.0f * s),
         text_col, label);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return enabled && item_pressed();
}

bool toggle_row(mw_ui *ui, const char *id, const char *label, const char *desc,
                bool *value, ImVec2 pos, float width, float s, int *changed)
{
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, ImVec2(width, 46.0f * s), ImGuiButtonFlags_EnableNav);
    bool hovered = item_hot();
    bool clicked = item_pressed();
    if (clicked) {
        *value = !*value;
        if (changed) *changed = 1;
    }
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float fsize = 21.0f * s;
    float dsize = 14.0f * s;
    text(dl, ui->font_body, fsize, pos, hovered ? C_TEXT : IM_COL32(224, 238, 248, 255), label);
    text(dl, ui->font_small, dsize, ImVec2(pos.x, pos.y + 26.0f * s), C_DIM, desc);

    float pw = 54.0f * s, ph = 26.0f * s;
    ImVec2 p( pos.x + width - pw, pos.y + 8.0f * s );
    ImU32 track = *value ? A(C_CYAN, hovered ? 245 : 215) : IM_COL32(22, 28, 44, 235);
    dl->AddRectFilled(p, ImVec2(p.x + pw, p.y + ph), track, ph * 0.5f);
    if (!*value)
        dl->AddRect(p, ImVec2(p.x + pw, p.y + ph), A(C_CYAN, hovered ? 105 : 65), ph * 0.5f, 0, 1.0f);
    float kr = ph * 0.5f - 3.0f * s;
    float kx = *value ? p.x + pw - ph * 0.5f : p.x + ph * 0.5f;
    dl->AddCircleFilled(ImVec2(kx, p.y + ph * 0.5f), kr,
                        *value ? IM_COL32(4, 9, 16, 255) : IM_COL32(194, 210, 224, 255));
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return clicked;
}

bool display_mode_row(mw_ui *ui, mw_settings *settings, ImVec2 pos, float width,
                      float s, int *changed)
{
    static const char *labels[MW_DISPLAY_COUNT] = {
        "4:3 ORIGINAL", "16:9", "16:10", "21:9"
    };
    float gap = 8.0f * s;
    float bw = (width - gap * (MW_DISPLAY_COUNT - 1)) / (float)MW_DISPLAY_COUNT;
    float bh = 44.0f * s;
    bool changed_now = false;
    ImDrawList *dl = ImGui::GetWindowDrawList();

    for (int i = 0; i < MW_DISPLAY_COUNT; i++) {
        ImVec2 p(pos.x + i * (bw + gap), pos.y);
        ImGui::SetCursorScreenPos(p);
        char id[32];
        snprintf(id, sizeof(id), "##display%d", i);
        ImGui::InvisibleButton(id, ImVec2(bw, bh), ImGuiButtonFlags_EnableNav);
        bool hovered = item_hot();
        bool selected = settings->display_mode == i;
        ImU32 fill = selected ? A(C_CYAN, 235) : IM_COL32(8, 12, 22, 235);
        if (hovered && !selected) fill = mix(fill, C_CYAN, 0.10f);
        ImU32 border = selected ? A(IM_COL32(210, 255, 255, 255), 245)
                                : A(C_CYAN, hovered ? 150 : 65);
        dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + bh), fill, 8.0f * s);
        dl->AddRect(p, ImVec2(p.x + bw, p.y + bh), border, 8.0f * s, 0,
                    selected ? 1.5f : 1.0f);
        if (selected)
            dl->AddRectFilledMultiColor(ImVec2(p.x + 8 * s, p.y + bh - 3 * s),
                                        ImVec2(p.x + bw - 8 * s, p.y + bh - 1 * s),
                                        C_MAGENTA, C_CYAN, C_CYAN, C_MAGENTA);
        float fsize = 16.0f * s;
        ImVec2 ts = text_size(ui->font_button, fsize, labels[i]);
        text(dl, ui->font_button, fsize,
             ImVec2(p.x + (bw - ts.x) * 0.5f, p.y + (bh - ts.y) * 0.5f - 1.0f * s),
             selected ? IM_COL32(4, 9, 16, 255) : C_TEXT, labels[i]);
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (item_pressed() && settings->display_mode != i) {
            settings->display_mode = i;
            changed_now = true;
            if (changed) *changed = 1;
        }
    }
    return changed_now;
}

/* Shared 0..100% slider row (volume, scanline intensity). */
bool percent_row(mw_ui *ui, const char *id, const char *label, int *value,
                 ImVec2 pos, float width, float s, int *changed)
{
    if (!value) return false;
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, ImVec2(width, 52.0f * s), ImGuiButtonFlags_EnableNav);
    bool hovered = item_hot();
    bool active = ImGui::IsItemActive();
    /* A pad cannot drag the handle, and nav activation also sets the active id,
     * so only a held left button may move the value.  Otherwise pressing A on a
     * focused slider would snap it to wherever the mouse happens to rest. */
    bool dragging = active && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    ImDrawList *dl = ImGui::GetWindowDrawList();

    int shown = *value;
    if (shown < 0) shown = 0;
    if (shown > 100) shown = 100;

    text(dl, ui->font_body, 21.0f * s, pos, C_TEXT, label);
    char pct[32];
    snprintf(pct, sizeof(pct), "%d%%", shown);
    ImVec2 pts = text_size(ui->font_button, 19.0f * s, pct);
    text(dl, ui->font_button, 19.0f * s, ImVec2(pos.x + width - pts.x, pos.y), A(C_CYAN, 230), pct);

    ImVec2 track(pos.x, pos.y + 31.0f * s);
    float tw = width;
    float th = 6.0f * s;
    dl->AddRectFilled(track, ImVec2(track.x + tw, track.y + th), IM_COL32(22, 28, 44, 240), th * 0.5f);
    float fill = tw * (shown / 100.0f);
    if (fill > 1.0f)
        dl->AddRectFilledMultiColor(track, ImVec2(track.x + fill, track.y + th),
                                    C_MAGENTA, C_CYAN, C_CYAN, C_MAGENTA);
    float kr = 9.0f * s;
    float kx = track.x + fill;
    if (kx < track.x + kr) kx = track.x + kr;
    if (kx > track.x + tw - kr) kx = track.x + tw - kr;
    if (hovered || active)
        dl->AddCircleFilled(ImVec2(kx, track.y + th * 0.5f), kr + 6.0f * s, A(C_CYAN, 30));
    dl->AddCircleFilled(ImVec2(kx, track.y + th * 0.5f), kr,
                        active ? IM_COL32(235, 255, 255, 255) : IM_COL32(190, 243, 255, 255));

    bool changed_now = false;
    if (dragging) {
        float mx = ImGui::GetIO().MousePos.x;
        float rel = (mx - track.x) / tw;
        rel = std::max(0.0f, std::min(1.0f, rel));
        int nv = (int)(rel * 100.0f + 0.5f);
        if (nv != *value) {
            *value = nv;
            changed_now = true;
            if (changed) *changed = 1;
        }
    }
    if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return changed_now;
}

/* One-line cycle control: label on the left, "< value >" chip on the right.
 * Returns the index to switch to when clicked, or -1.  An index outside
 * 0..count-1 shows `fallback` (e.g. a hand-edited custom window size) and
 * the first click snaps to entry 0. */
int cycle_row(mw_ui *ui, const char *id, const char *label,
              const char *const *names, int count, int index,
              const char *fallback, ImVec2 pos, float width, float s)
{
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, ImVec2(width, 40.0f * s), ImGuiButtonFlags_EnableNav);
    bool hovered = item_hot();
    bool clicked = item_pressed();
    ImDrawList *dl = ImGui::GetWindowDrawList();

    const char *shown = (index >= 0 && index < count) ? names[index] : fallback;
    if (!shown) shown = "-";

    text(dl, ui->font_body, 21.0f * s, pos,
         hovered ? C_TEXT : IM_COL32(224, 238, 248, 255), label);

    float cw = 186.0f * s, chh = 32.0f * s;
    ImVec2 c(pos.x + width - cw, pos.y + 1.0f * s);
    dl->AddRectFilled(c, ImVec2(c.x + cw, c.y + chh), IM_COL32(8, 12, 22, 235), 8.0f * s);
    dl->AddRect(c, ImVec2(c.x + cw, c.y + chh), A(C_CYAN, hovered ? 200 : 80), 8.0f * s, 0, 1.2f);
    float cy = c.y + chh * 0.5f;
    ImU32 arrow = A(C_CYAN, hovered ? 240 : 150);
    dl->AddTriangleFilled(ImVec2(c.x + 13 * s, cy), ImVec2(c.x + 21 * s, cy - 5 * s),
                          ImVec2(c.x + 21 * s, cy + 5 * s), arrow);
    dl->AddTriangleFilled(ImVec2(c.x + cw - 13 * s, cy), ImVec2(c.x + cw - 21 * s, cy - 5 * s),
                          ImVec2(c.x + cw - 21 * s, cy + 5 * s), arrow);
    float fsize = 17.0f * s;
    ImVec2 ts = text_size(ui->font_button, fsize, shown);
    if (ts.x > cw - 56.0f * s) {
        fsize = 14.0f * s;
        ts = text_size(ui->font_button, fsize, shown);
    }
    text(dl, ui->font_button, fsize,
         ImVec2(c.x + (cw - ts.x) * 0.5f, c.y + (chh - ts.y) * 0.5f - 1.0f * s),
         A(C_CYAN, 245), shown);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (!clicked) return -1;
    return (index >= 0 && index < count) ? (index + 1) % count : 0;
}

/* ---------------- controllers panel helpers ---------------- */

const char *const k_action_names[MW_IN_ACTIONS] = {
    "UP", "DOWN", "LEFT", "RIGHT", "B1", "B2", "B3"
};

/* Overlay nav chips drawn by every panel.  Clicking one switches the tab the
 * host dispatches on next frame.  The launcher is two chips (SETTINGS,
 * CONTROLLERS); the game overlay adds CHEATS as the third. */
void overlay_tabs(mw_ui *ui, float right_x, float y, float s)
{
    static const char *const labels[3] = { "SETTINGS", "CONTROLLERS", "CHEATS" };
    int count = ui->tab_count;
    if (count < 2) count = 2;
    if (count > 3) count = 3;
    const float cw = 124.0f * s, ch = 28.0f * s, gap = 6.0f * s;
    /* Leave a little air before the X button that sits at right_x. */
    float start = right_x - ((float)count * cw + (float)(count - 1) * gap + 10.0f * s);
    ImDrawList *dl = ImGui::GetWindowDrawList();

    for (int i = 0; i < count; i++) {
        ImVec2 p(start + i * (cw + gap), y);
        char id[32];
        ImVec2 ts;
        bool hovered, selected;
        snprintf(id, sizeof(id), "##overlaytab%d", i);
        ImGui::SetCursorScreenPos(p);
        ImGui::InvisibleButton(id, ImVec2(cw, ch), ImGuiButtonFlags_EnableNav);
        hovered = item_hot();
        selected = ui->tab == i;
        dl->AddRectFilled(p, ImVec2(p.x + cw, p.y + ch),
                          selected ? A(C_CYAN, 235) : IM_COL32(8, 12, 22, 235), 6.0f * s);
        dl->AddRect(p, ImVec2(p.x + cw, p.y + ch),
                    selected ? A(IM_COL32(210, 255, 255, 255), 245) : A(C_CYAN, hovered ? 150 : 60),
                    6.0f * s, 0, selected ? 1.4f : 1.0f);
        ts = text_size(ui->font_small, 11.0f * s, labels[i]);
        text(dl, ui->font_small, 11.0f * s,
             ImVec2(p.x + (cw - ts.x) * 0.5f, p.y + (ch - ts.y) * 0.5f - 1.0f * s),
             selected ? IM_COL32(4, 9, 16, 255) : C_TEXT, labels[i]);
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (item_pressed()) ui->tab = i;
    }
}

struct ctl_device {
    char guid[33];
    std::string name;
};

/* Connected game controllers offered by the assignment chip.  Deduped by GUID
 * because the schema stores a GUID, not an instance: two identical pad models
 * share one GUID and therefore one selector entry. */
int enumerate_control_pads(ctl_device *out, int max)
{
    int n = SDL_NumJoysticks();
    int count = 0;
    if (!out || max <= 0) return 0;
    for (int i = 0; i < n && count < max; i++) {
        char guid[33];
        int dup = 0;
        const char *name;
        if (!SDL_IsGameController(i)) continue;
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i), guid, (int)sizeof(guid));
        for (int k = 0; k < count; k++) {
            if (SDL_strcasecmp(out[k].guid, guid) == 0) { dup = 1; break; }
        }
        if (dup) continue;
        snprintf(out[count].guid, sizeof(out[count].guid), "%s", guid);
        name = SDL_GameControllerNameForIndex(i);
        out[count].name = (name && *name) ? name : "Controller";
        count++;
    }
    return count;
}

std::string scancode_label(int code)
{
    const char *n;
    if (code < 0 || code >= SDL_NUM_SCANCODES) return "-";
    n = SDL_GetScancodeName((SDL_Scancode)code);
    return (n && *n) ? std::string(n) : std::string("-");
}

std::string pad_button_label(int code)
{
    const char *n;
    std::string out;
    if (code < 0 || code >= SDL_CONTROLLER_BUTTON_MAX) return "-";
    n = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)code);
    if (!n || !*n) return "-";
    for (const char *p = n; *p; ++p) {
        char c = *p;
        if (c == '_') c = ' ';
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out.push_back(c);
    }
    return out;
}

/* Binding chip: a compact click target that shows the current key/button, or
 * the "PRESS..." prompt while that row owns the capture. */
bool binding_chip(mw_ui *ui, const char *id, const char *label, ImVec2 pos,
                  ImVec2 sz, float s, bool waiting, bool bound)
{
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, sz, ImGuiButtonFlags_EnableNav);
    bool hovered = item_hot();
    bool clicked = item_pressed();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float fs = 13.0f * s;
    ImVec2 ts = text_size(ui->font_button, fs, label);

    if (ts.x > sz.x - 10.0f * s) {
        fs = 10.0f * s;
        ts = text_size(ui->font_button, fs, label);
    }
    dl->AddRectFilled(pos, ImVec2(pos.x + sz.x, pos.y + sz.y),
                      waiting ? IM_COL32(34, 6, 32, 245) : IM_COL32(8, 12, 22, 235),
                      6.0f * s);
    dl->AddRect(pos, ImVec2(pos.x + sz.x, pos.y + sz.y),
                waiting ? A(C_MAGENTA, 235) : A(C_CYAN, hovered ? 195 : (bound ? 78 : 42)),
                6.0f * s, 0, waiting ? 1.5f : 1.0f);
    text(dl, ui->font_button, fs,
         ImVec2(pos.x + (sz.x - ts.x) * 0.5f, pos.y + (sz.y - ts.y) * 0.5f - 1.0f * s),
         waiting ? IM_COL32(255, 196, 238, 255) : (bound ? C_TEXT : C_DIM), label);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return clicked;
}

/* One player's pad selection: steals a GUID already bound to another player so
 * the assignment is never ambiguous, mirrors the module maps into *settings and
 * marks the host's settings dirty. */
void apply_pad_assign(mw_settings *settings, int player, const char *guid, int *changed)
{
    const char *g = guid ? guid : "";
    if (*g) {
        for (int o = 0; o < MW_IN_PLAYERS; o++) {
            if (o == player) continue;
            if (settings->pad_guid[o][0] && SDL_strcasecmp(settings->pad_guid[o], g) == 0)
                mw_input_assign("", o);
        }
    }
    mw_input_assign(g, player);
    mw_input_save_maps(settings);
    if (changed) *changed = 1;
}

const char *const k_shader_names[3] = { "OFF", "SCANLINES", "CRT" };
const int k_res_w[4] = { 1280, 1600, 1920, 2560 };
const int k_res_h[4] = { 720, 900, 1080, 1440 };
const char *const k_res_names[4] = { "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440" };

/* Window-size presets write window_w/h; main_sdl.c applies them live. */
bool resolution_row(mw_ui *ui, mw_settings *settings, ImVec2 pos, float width, float s)
{
    int idx = -1;
    for (int i = 0; i < 4; i++) {
        if (settings->window_w == k_res_w[i] && settings->window_h == k_res_h[i]) {
            idx = i;
            break;
        }
    }
    char custom[32];
    snprintf(custom, sizeof(custom), "%d x %d", settings->window_w, settings->window_h);
    int next = cycle_row(ui, "##resolution", "RESOLUTION", k_res_names, 4, idx,
                         idx < 0 ? custom : NULL, pos, width, s);
    if (next < 0) return false;
    settings->window_w = k_res_w[next];
    settings->window_h = k_res_h[next];
    return true;
}

bool volume_row(mw_ui *ui, mw_settings *settings, ImVec2 pos, float width, float s, int *changed)
{
    return percent_row(ui, "##volume", "MASTER VOLUME",
                       settings ? &settings->volume : NULL, pos, width, s, changed);
}

void draw_corner(ImDrawList *dl, ImVec2 p, float len, ImU32 col, int dir_x, int dir_y, float s)
{
    dl->AddLine(p, ImVec2(p.x + dir_x * len, p.y), col, 2.0f * s);
    dl->AddLine(p, ImVec2(p.x, p.y + dir_y * len), col, 2.0f * s);
    dl->AddCircleFilled(ImVec2(p.x + dir_x * len, p.y), 2.5f * s, col);
    dl->AddCircleFilled(ImVec2(p.x, p.y + dir_y * len), 2.5f * s, col);
}

/* ---------------- in-UI archive browser ---------------- */

#define FB_MAX_ENTRIES 512

struct fb_entry {
    std::string name;
    int is_dir;
    int is_zip;
};

std::string fb_join(const std::string &dir, const char *name)
{
    std::string out = dir;
    if (!name || !*name) return out;
    if (!out.empty() && out.back() != '/' && out.back() != '\\') out += '/';
    out += name;
    return out;
}

std::string fb_parent(const std::string &dir)
{
    std::string d = dir;
    while (d.size() > 1 && (d.back() == '/' || d.back() == '\\')) d.pop_back();
    if (d.empty()) return d;
    if (d.size() == 2 && d[1] == ':') return d + '/';    /* Windows drive root */
    size_t at = d.find_last_of("/\\");
    if (at == std::string::npos) return d;
    if (at == 0) return d.substr(0, 1);                  /* POSIX root */
    std::string up = d.substr(0, at);
    if (up.size() == 2 && up[1] == ':') up += '/';
    return up;
}

int fb_zip_name(const std::string &name)
{
    return name.size() > 4 &&
           SDL_strcasecmp(name.c_str() + name.size() - 4, ".zip") == 0;
}

/* Plain-C directory scan (FindFirstFileA / opendir), sorted directories first
 * then case-insensitively by name.  ".." is added by the caller, not here. */
int fb_scan(const char *dir, fb_entry *out, int max)
{
    int n = 0;
    if (!dir || !*dir || !out || max <= 0) return 0;
#ifdef _WIN32
    std::string pattern = dir;
    if (pattern.back() != '/' && pattern.back() != '\\') pattern += '\\';
    pattern += '*';
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            if (n >= max) break;
            out[n].name = fd.cFileName;
            out[n].is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            n++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            struct stat st;
            std::string full;
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (n >= max) break;
            full = fb_join(dir, e->d_name);
            if (stat(full.c_str(), &st) != 0) continue;
            if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) continue;
            out[n].name = e->d_name;
            out[n].is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
            n++;
        }
        closedir(d);
    }
#endif
    for (int i = 0; i < n; i++)
        out[i].is_zip = out[i].is_dir ? 0 : fb_zip_name(out[i].name);
    std::sort(out, out + n, [](const fb_entry &a, const fb_entry &b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        return SDL_strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return n;
}

} // namespace

extern "C" mw_ui *mw_ui_create(SDL_Window *window, SDL_Renderer *renderer,
                               const char *base_path, const char *background_path)
{
    if (!window || !renderer) return nullptr;
    mw_ui *ui = new mw_ui();
    ui->window = window;
    ui->renderer = renderer;
    ui->background = nullptr;
    ui->bg_w = ui->bg_h = 0;
    ui->layout_scale = 1.0f;
    ui->dpi_scale = 1.0f;
    ui->tab = MW_UI_TAB_SETTINGS;
    ui->tab_count = 2;
    ui->cap_player = -1;
    ui->cap_action = -1;
    ui->cap_pad = 0;

    int ww = 0, wh = 0, ow = 0, oh = 0;
    SDL_GetWindowSize(window, &ww, &wh);
    SDL_GetRendererOutputSize(renderer, &ow, &oh);
    if (ww > 0 && wh > 0) ui->dpi_scale = (float)oh / (float)wh;
    if (ui->dpi_scale < 1.0f) ui->dpi_scale = 1.0f;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    /* Gamepad navigation only: the arrow keys and Space/Enter are game inputs
     * during play, so keyboard navigation must stay off. */
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.FontGlobalScale = 1.0f / ui->dpi_scale;

    ImGuiStyle &st = ImGui::GetStyle();
    st.WindowRounding = 18.0f;
    st.FrameRounding = 10.0f;
    st.GrabRounding = 8.0f;
    st.PopupRounding = 12.0f;
    st.WindowBorderSize = 0.0f;
    st.FrameBorderSize = 0.0f;
    st.ItemSpacing = ImVec2(12.0f, 10.0f);
    st.ItemInnerSpacing = ImVec2(8.0f, 8.0f);
    st.WindowPadding = ImVec2(0, 0);
    st.Colors[ImGuiCol_Text] = ImVec4(0.91f, 0.96f, 1.0f, 1.0f);
    st.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    /* Ring ImGui draws around the gamepad-focused item: the default white would
     * fight the neon palette, so it uses this file's cyan. */
    st.Colors[ImGuiCol_NavCursor] = ImVec4(0.20f, 0.91f, 1.0f, 0.75f);

    std::string base = base_path ? base_path : "";
    if (!base.empty() && base.back() != '/' && base.back() != '\\') base += '/';
    std::string fonts = base + "assets/fonts/";
    const float render_scale = 1.6f;
    const float d = ui->dpi_scale * render_scale;
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = true;
    cfg.RasterizerMultiply = 1.05f;
    auto load = [&](const char *file, float px) -> ImFont * {
        std::string p = fonts + file;
        ImFont *f = io.Fonts->AddFontFromFileTTF(p.c_str(), px * d, &cfg);
        return f;
    };
    ui->font_title  = load("Audiowide-Regular.ttf", 96.0f);
    ui->font_h1     = load("Rajdhani-Bold.ttf", 42.0f);
    ui->font_button = load("Rajdhani-SemiBold.ttf", 26.0f);
    ui->font_body   = load("Rajdhani-Regular.ttf", 22.0f);
    ui->font_small  = load("Rajdhani-SemiBold.ttf", 15.0f);
    if (!ui->font_title) ui->font_title = io.Fonts->AddFontDefault();
    if (!ui->font_h1) ui->font_h1 = io.Fonts->AddFontDefault();
    if (!ui->font_button) ui->font_button = io.Fonts->AddFontDefault();
    if (!ui->font_body) ui->font_body = io.Fonts->AddFontDefault();
    if (!ui->font_small) ui->font_small = io.Fonts->AddFontDefault();
    io.Fonts->Build();

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    {
        std::string icon_path = base + "assets/icon.png";
        int iw = 0, ih = 0, in = 0;
        unsigned char *pix = stbi_load(icon_path.c_str(), &iw, &ih, &in, 4);
        if (pix) {
            SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(pix, iw, ih, 32, iw * 4,
                0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u);
            if (surf) {
                SDL_SetWindowIcon(window, surf);
                SDL_FreeSurface(surf);
            }
            stbi_image_free(pix);
        }
    }

    if (background_path && *background_path) {
        int w = 0, h = 0, n = 0;
        stbi_set_flip_vertically_on_load(0);
        unsigned char *pixels = stbi_load(background_path, &w, &h, &n, 4);
        if (pixels) {
            ui->background = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                               SDL_TEXTUREACCESS_STATIC, w, h);
            if (ui->background) {
                SDL_UpdateTexture(ui->background, nullptr, pixels, w * 4);
                SDL_SetTextureBlendMode(ui->background, SDL_BLENDMODE_NONE);
                SDL_SetTextureScaleMode(ui->background, SDL_ScaleModeLinear);
                ui->bg_w = w;
                ui->bg_h = h;
            }
            stbi_image_free(pixels);
        } else {
            fprintf(stderr, "ui: could not load background %s\n", background_path);
        }
    }
    return ui;
}

extern "C" void mw_ui_destroy(mw_ui *ui)
{
    if (!ui) return;
    if (ui->background) SDL_DestroyTexture(ui->background);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    delete ui;
}

extern "C" void mw_ui_begin(mw_ui *ui)
{
    if (!ui) return;
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ui_scale(ui);
}

extern "C" void mw_ui_end(mw_ui *ui)
{
    if (!ui) return;
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ui->renderer);
}

extern "C" void mw_ui_process_event(mw_ui *ui, const SDL_Event *event)
{
    if (!ui || !event) return;
    ImGui_ImplSDL2_ProcessEvent(event);
}

extern "C" int mw_ui_wants_input(mw_ui *ui)
{
    if (!ui) return 0;
    ImGuiIO &io = ImGui::GetIO();
    return (io.WantCaptureMouse || io.WantCaptureKeyboard) ? 1 : 0;
}

extern "C" void mw_ui_draw_background(mw_ui *ui)
{
    if (!ui || !ui->background) return;
    int ww = 0, wh = 0;
    SDL_GetWindowSize(ui->window, &ww, &wh);
    if (ww <= 0 || wh <= 0) return;
    float src = (float)ui->bg_w / (float)ui->bg_h;
    float dst = (float)ww / (float)wh;
    SDL_Rect r;
    if (src > dst) {
        r.h = wh;
        r.w = (int)(wh * src + 0.5f);
        r.x = (ww - r.w) / 2;
        r.y = 0;
    } else {
        r.w = ww;
        r.h = (int)(ww / src + 0.5f);
        r.x = 0;
        r.y = (wh - r.h) / 2;
    }
    SDL_RenderCopy(ui->renderer, ui->background, nullptr, &r);
}

extern "C" void mw_ui_draw_launcher(mw_ui *ui, mw_settings *settings,
                                    const char *archive_path, const char *archive_status,
                                    int *browse, int *start, int *open_settings, int *quit,
                                    int *changed)
{
    if (!ui) return;
    ImGuiIO &io = ImGui::GetIO();
    float w = io.DisplaySize.x, h = io.DisplaySize.y;
    float s = ui_scale(ui);
    ImDrawList *bg = ImGui::GetBackgroundDrawList();

    bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(w * 0.68f, h),
                                IM_COL32(3, 5, 12, 250), IM_COL32(3, 5, 12, 188),
                                IM_COL32(3, 5, 12, 0), IM_COL32(3, 5, 12, 0));
    bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(w, h * 0.23f),
                                IM_COL32(2, 3, 8, 165), IM_COL32(2, 3, 8, 165),
                                IM_COL32(2, 3, 8, 0), IM_COL32(2, 3, 8, 0));
    bg->AddRectFilledMultiColor(ImVec2(0, h * 0.74f), ImVec2(w, h),
                                IM_COL32(2, 3, 8, 0), IM_COL32(2, 3, 8, 0),
                                IM_COL32(2, 3, 8, 210), IM_COL32(2, 3, 8, 210));

    for (float y = 0; y < h; y += 3.0f * s)
        bg->AddLine(ImVec2(0, y), ImVec2(w, y), IM_COL32(0, 0, 0, 18), 1.0f);
    float scan_y = fmodf((float)ImGui::GetTime() * 72.0f, h + 80.0f) - 40.0f;
    bg->AddRectFilled(ImVec2(0, scan_y), ImVec2(w, scan_y + 2.0f * s),
                      A(C_CYAN, 12));

    draw_corner(bg, ImVec2(30 * s, 30 * s), 86 * s, A(C_CYAN, 120), 1, 1, s);
    draw_corner(bg, ImVec2(w - 30 * s, h - 30 * s), 86 * s, A(C_MAGENTA, 100), -1, -1, s);

    {
        const char *overline = "MICHAEL JACKSON'S";
        text_spaced(bg, ui->font_small, 15.0f * s, ImVec2(58 * s + 1.5f, 40 * s + 1.5f),
                    IM_COL32(2, 4, 10, 220), overline, 3.2f * s);
        text_spaced(bg, ui->font_small, 15.0f * s, ImVec2(58 * s, 40 * s),
                    A(C_CYAN, 245), overline, 3.2f * s);
    }
    text_glow(bg, ui->font_title, 94.0f * s, ImVec2(56 * s, 64 * s),
              "MOON", IM_COL32(245, 252, 255, 255), C_CYAN, s);
    text_glow(bg, ui->font_title, 94.0f * s, ImVec2(56 * s, 152 * s),
              "WALKER", IM_COL32(245, 252, 255, 255), C_MAGENTA, s);

    ImVec2 line0(56 * s, 250 * s);
    bg->AddRectFilledMultiColor(line0, ImVec2(line0.x + 380 * s, line0.y + 2.5f * s),
                                C_MAGENTA, C_CYAN, C_CYAN, C_MAGENTA);
    {
        const char *sub = "SYSTEM 18  //  RECOMPILED";
        ImVec2 pill(54 * s, 258 * s);
        ImVec2 psz2(388 * s, 38 * s);
        bg->AddRectFilled(pill, ImVec2(pill.x + psz2.x, pill.y + psz2.y),
                          IM_COL32(4, 7, 14, 198), 7 * s);
        bg->AddRect(pill, ImVec2(pill.x + psz2.x, pill.y + psz2.y),
                    A(C_CYAN, 42), 7 * s, 0, 1.0f);
        bg->AddRectFilled(ImVec2(pill.x, pill.y + 8 * s),
                          ImVec2(pill.x + 2.5f * s, pill.y + psz2.y - 8 * s),
                          C_CYAN, 1 * s);
        text_spaced(bg, ui->font_body, 21.0f * s, ImVec2(pill.x + 14 * s, pill.y + 7 * s),
                    IM_COL32(205, 225, 238, 255), sub, 1.6f * s);
    }

    ImVec2 panel(56 * s, 330 * s);
    ImVec2 psz(500 * s, 326 * s);
    draw_panel(bg, panel, psz, 16 * s, false);

    float px = panel.x + 28 * s;
    float py = panel.y + 24 * s;
    text_spaced(bg, ui->font_small, 14.0f * s, ImVec2(px, py),
                A(C_CYAN, 220), "GAME ARCHIVE", 2.6f * s);

    ImVec2 field(px, py + 28 * s);
    float field_w = psz.x - 56 * s - 112 * s;
    float field_h = 46 * s;
    bg->AddRectFilled(field, ImVec2(field.x + field_w, field.y + field_h),
                      IM_COL32(4, 7, 14, 235), 9 * s);
    bg->AddRect(field, ImVec2(field.x + field_w, field.y + field_h),
                A(C_CYAN, 62), 9 * s, 0, 1.0f);

    std::string name = file_name(archive_path);
    std::string shown = name.empty() ? "NO ARCHIVE SELECTED" : ellipsize(ui->font_body, 19 * s, name, field_w - 28 * s);
    text(bg, ui->font_body, 19 * s, ImVec2(field.x + 14 * s, field.y + 13 * s),
         name.empty() ? C_DIM : C_TEXT, shown.c_str());
    if (!name.empty()) {
        float ext_w = text_size(ui->font_small, 13 * s, "ZIP").x + 14 * s;
        bg->AddRectFilled(ImVec2(field.x + field_w - ext_w - 8 * s, field.y + 13 * s),
                          ImVec2(field.x + field_w - 8 * s, field.y + 33 * s),
                          A(C_CYAN, 26), 6 * s);
        text(bg, ui->font_small, 13 * s, ImVec2(field.x + field_w - ext_w, field.y + 16 * s),
             A(C_CYAN, 220), "ZIP");
    }

    bool has = archive_path && *archive_path;
    bool ready = archive_status && !strncmp(archive_status, "READY", 5);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##launcher_ui", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoBackground);

    if (neon_button(ui, "##browse", "BROWSE", ImVec2(px + field_w + 12 * s, field.y),
                    ImVec2(100 * s, field_h), BUTTON_SECONDARY, true, s) && browse)
        *browse = 1;

    float sy = py + 88 * s;
    float pulse = 0.65f + 0.35f * sinf((float)ImGui::GetTime() * 2.6f);
    ImU32 dot = ready ? A(C_CYAN, (int)(170 + 70 * pulse)) : (has ? A(C_MAGENTA, 220) : A(C_DIM, 160));
    bg->AddCircleFilled(ImVec2(px + 6 * s, sy + 9 * s), 4.0f * s, dot);
    bg->AddCircle(ImVec2(px + 6 * s, sy + 9 * s), 8.0f * s, A(dot, 38), 0, 1.0f);
    text(bg, ui->font_small, 14.0f * s, ImVec2(px + 18 * s, sy),
         ready ? A(C_CYAN, 230) : (has ? A(C_MAGENTA, 230) : C_MUTED),
         archive_status && *archive_status ? archive_status : "SELECT YOUR mwalk.zip");

    if (neon_button(ui, "##start", "START GAME", ImVec2(px, py + 118 * s),
                    ImVec2(psz.x - 56 * s, 60 * s), BUTTON_PRIMARY, ready, s) && start)
        *start = 1;

    if (neon_button(ui, "##settings", "SETTINGS", ImVec2(px, py + 194 * s),
                    ImVec2(160 * s, 44 * s), BUTTON_SECONDARY, true, s) && open_settings)
        *open_settings = 1;
    {
        bool music_on = settings && settings->music_on;
        if (neon_button(ui, "##music", music_on ? "MUSIC ON" : "MUSIC OFF",
                        ImVec2(px + 176 * s, py + 194 * s), ImVec2(140 * s, 44 * s),
                        music_on ? BUTTON_SECONDARY : BUTTON_GHOST, true, s) && settings) {
            settings->music_on = music_on ? 0 : 1;
            if (changed) *changed = 1;
        }
    }
    if (neon_button(ui, "##quit", "QUIT", ImVec2(px + 332 * s, py + 194 * s),
                    ImVec2(112 * s, 44 * s), BUTTON_DANGER, true, s) && quit)
        *quit = 1;

    text_spaced(bg, ui->font_small, 14.0f * s, ImVec2(px, py + 252 * s),
                IM_COL32(158, 184, 203, 235), "F1  SETTINGS", 1.7f * s);
    text_spaced(bg, ui->font_small, 14.0f * s, ImVec2(px + 154 * s, py + 252 * s),
                IM_COL32(158, 184, 203, 235), "ESC  QUIT", 1.7f * s);

    const char *tag = "LAUNCHER v1.0  //  SYNTHWAVE EDITION";
    ImVec2 ts = text_size(ui->font_small, 13 * s, tag);
    text(bg, ui->font_small, 13 * s, ImVec2(w - 32 * s - ts.x, h - 30 * s),
         IM_COL32(165, 188, 205, 220), tag);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

extern "C" void mw_ui_draw_settings(mw_ui *ui, mw_settings *settings, int *open,
                                    int in_game, int *reset_game, int *quit_game,
                                    int *changed)
{
    if (!ui || !settings || !open) return;
    ImGuiIO &io = ImGui::GetIO();
    float w = io.DisplaySize.x, h = io.DisplaySize.y;
    float s = ui_scale(ui);
    ImDrawList *bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32(2, 4, 10, 214));
    for (float y = 0; y < h; y += 3.0f * s)
        bg->AddLine(ImVec2(0, y), ImVec2(w, y), IM_COL32(0, 0, 0, 16), 1.0f);

    float pw = 780 * s, ph = 700 * s;
    ImVec2 panel((w - pw) * 0.5f, (h - ph) * 0.5f);
    draw_panel(bg, panel, ImVec2(pw, ph), 18 * s, true);
    float px = panel.x + 38 * s;
    float py = panel.y + 28 * s;

    text_spaced(bg, ui->font_small, 14 * s, ImVec2(px, py),
                A(C_CYAN, 230), "SYSTEM CONFIGURATION", 2.6f * s);
    text_glow(bg, ui->font_title, 46 * s, ImVec2(px, py + 22 * s),
              "SETTINGS", IM_COL32(245, 252, 255, 255), C_CYAN, s);
    bg->AddRectFilledMultiColor(ImVec2(px, py + 86 * s), ImVec2(px + pw - 76 * s, py + 88 * s),
                                C_MAGENTA, C_CYAN, C_CYAN, C_MAGENTA);

    float label_size = 13 * s;
    float y_audio = py + 108 * s;
    text_spaced(bg, ui->font_small, label_size, ImVec2(px, y_audio),
                A(C_CYAN, 220), "AUDIO", 2.0f * s);

    float y_display = py + 240 * s;
    text_spaced(bg, ui->font_small, label_size, ImVec2(px, y_display),
                A(C_CYAN, 220), "DISPLAY", 2.0f * s);

    float y_video = py + 338 * s;
    text_spaced(bg, ui->font_small, label_size, ImVec2(px, y_video),
                A(C_CYAN, 220), "VIDEO", 2.0f * s);

    float y_ui = py + 526 * s;
    text_spaced(bg, ui->font_small, label_size, ImVec2(px, y_ui),
                A(C_CYAN, 220), "INTERFACE", 2.0f * s);

    bg->AddRectFilled(ImVec2(px, py + 600 * s), ImVec2(px + pw - 76 * s, py + 601 * s),
                      A(C_CYAN, 36));

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##settings_ui", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoBackground);

    /* SETTINGS / CONTROLLERS (plus CHEATS in game) are tabs of the one overlay;
     * the host draws the panel mw_ui_tab() names on the next frame. */
    ui->tab_count = in_game ? 3 : 2;
    overlay_tabs(ui, panel.x + pw - 54 * s, panel.y + 20 * s, s);

    volume_row(ui, settings, ImVec2(px, y_audio + 22 * s), pw - 76 * s, s, changed);
    bool snd = settings->sound_on != 0;
    if (toggle_row(ui, "##sound", "SOUND", "Music and sound effects",
                   &snd, ImVec2(px, y_audio + 78 * s), pw - 76 * s, s, changed))
        settings->sound_on = snd ? 1 : 0;
    display_mode_row(ui, settings, ImVec2(px, y_display + 22 * s),
                     pw - 76 * s, s, changed);
    text(bg, ui->font_small, 14 * s, ImVec2(px, y_display + 72 * s),
         A(C_MUTED, 215), settings->display_mode == MW_DISPLAY_ORIGINAL
             ? "Original 320x224 framing. No extra world is revealed."
             : "Expanded field of view. The scene is cropped, never stretched.");

    /* VIDEO is laid out as two compact columns so the five rows (fullscreen,
     * shader, intensity, resolution, integer scaling) fit between the header
     * and the INTERFACE section without growing the 700-px panel, which must
     * stay usable in the 640x448 minimum window. */
    const float col_w = (pw - 76 * s - 24 * s) * 0.5f;
    const float col2 = px + col_w + 24 * s;
    float vrow = y_video + 22 * s;

    bool fs = settings->fullscreen != 0;
    if (toggle_row(ui, "##fullscreen", "FULLSCREEN", "Borderless fullscreen display",
                   &fs, ImVec2(px, vrow), col_w, s, changed))
        settings->fullscreen = fs ? 1 : 0;

    int shader = cycle_row(ui, "##shader", "SHADER", k_shader_names, 3,
                           settings->shader_preset, "OFF", ImVec2(px, vrow + 50 * s), col_w, s);
    if (shader >= 0 && shader != settings->shader_preset) {
        settings->shader_preset = shader;
        if (changed) *changed = 1;
    }
    if (settings->shader_preset != 0) {
        percent_row(ui, "##intensity", "SCANLINE INTENSITY", &settings->scanline_intensity,
                    ImVec2(px, vrow + 96 * s), col_w, s, changed);
    } else {
        text(bg, ui->font_small, 14 * s, ImVec2(px, vrow + 106 * s), A(C_MUTED, 210),
             "Intensity applies to SCANLINES and CRT.");
    }

    if (resolution_row(ui, settings, ImVec2(col2, vrow), col_w, s) && changed)
        *changed = 1;

    bool integer = settings->integer_scale != 0;
    if (toggle_row(ui, "##integer", "INTEGER SCALING", "Pixel-perfect whole-number scaling",
                   &integer, ImVec2(col2, vrow + 50 * s), col_w, s, changed))
        settings->integer_scale = integer ? 1 : 0;

    bool fps = settings->show_fps != 0;
    if (toggle_row(ui, "##fps", "SHOW FPS", "Live frame-rate readout",
                   &fps, ImVec2(px, y_ui + 22 * s), pw - 76 * s, s, changed))
        settings->show_fps = fps ? 1 : 0;

    float btn_y = py + 618 * s;
    if (in_game) {
        if (neon_button(ui, "##quitgame", "QUIT GAME", ImVec2(px, btn_y),
                        ImVec2(150 * s, 48 * s), BUTTON_DANGER, true, s) && quit_game)
            *quit_game = 1;
        if (neon_button(ui, "##reset", "RESET GAME", ImVec2(px + 168 * s, btn_y),
                        ImVec2(170 * s, 48 * s), BUTTON_SECONDARY, true, s) && reset_game) {
            *reset_game = 1;
            *open = 0;
        }
        if (neon_button(ui, "##resume", "RESUME", ImVec2(panel.x + pw - 38 * s - 210 * s, btn_y),
                        ImVec2(210 * s, 48 * s), BUTTON_PRIMARY, true, s))
            *open = 0;
    } else {
        if (neon_button(ui, "##close", "CLOSE", ImVec2(panel.x + pw - 38 * s - 210 * s, btn_y),
                        ImVec2(210 * s, 48 * s), BUTTON_PRIMARY, true, s))
            *open = 0;
    }

    if (neon_button(ui, "##settingsx", "X", ImVec2(panel.x + pw - 54 * s, panel.y + 20 * s),
                    ImVec2(32 * s, 32 * s), BUTTON_GHOST, true, s))
        *open = 0;

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

extern "C" int mw_ui_tab(mw_ui *ui)
{
    return ui ? ui->tab : MW_UI_TAB_SETTINGS;
}

extern "C" void mw_ui_draw_controllers(mw_ui *ui, mw_settings *settings,
                                       int *open, int *changed)
{
    if (!ui || !settings || !open) return;
    ImGuiIO &io = ImGui::GetIO();
    float w = io.DisplaySize.x, h = io.DisplaySize.y;
    float s = ui_scale(ui);
    ImDrawList *bg = ImGui::GetBackgroundDrawList();

    /* A finished capture is consumed before drawing: the module already wrote
     * the binding into its maps, so mirroring them into *settings plus the
     * changed flag is all the host needs to persist it.  The check covers the
     * finished-but-unread state too (capture_active() is already 0 then), so
     * the answer produced between two frames is never dropped. */
    if (ui->cap_player >= 0) {
        int code = 0;
        int r = mw_input_capture_result(&code);
        if (r == 1) {
            mw_input_save_maps(settings);
            if (changed) *changed = 1;
            ui->cap_player = -1;
        } else if (r == -1 || !mw_input_capture_active()) {
            /* Cancelled with ESC, or disarmed from outside (overlay closed,
             * tab switched) - either way the row stops waiting. */
            ui->cap_player = -1;
        }
    }

    bg->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32(2, 4, 10, 214));
    for (float y = 0; y < h; y += 3.0f * s)
        bg->AddLine(ImVec2(0, y), ImVec2(w, y), IM_COL32(0, 0, 0, 16), 1.0f);

    float pw = 1080.0f * s, ph = 700.0f * s;
    ImVec2 panel((w - pw) * 0.5f, (h - ph) * 0.5f);
    draw_panel(bg, panel, ImVec2(pw, ph), 18 * s, true);
    float px = panel.x + 38 * s;
    float py = panel.y + 28 * s;

    text_spaced(bg, ui->font_small, 14 * s, ImVec2(px, py),
                A(C_CYAN, 230), "INPUT CONFIGURATION", 2.6f * s);
    text_glow(bg, ui->font_title, 46 * s, ImVec2(px, py + 22 * s),
              "CONTROLLERS", IM_COL32(245, 252, 255, 255), C_CYAN, s);
    bg->AddRectFilledMultiColor(ImVec2(px, py + 86 * s),
                                ImVec2(px + pw - 76 * s, py + 88 * s),
                                C_MAGENTA, C_CYAN, C_CYAN, C_MAGENTA);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##controllers_ui", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoBackground);

    overlay_tabs(ui, panel.x + pw - 54 * s, panel.y + 20 * s, s);

    ctl_device dev[MW_IN_PLAYERS];
    int devn = enumerate_control_pads(dev, MW_IN_PLAYERS);
    int capturing = (mw_input_capture_active() && ui->cap_player >= 0 &&
                     ui->cap_action >= 0 && ui->cap_action < MW_IN_ACTIONS) ? 1 : 0;

    const float inner_w = pw - 76 * s;
    const float gap = 18 * s;
    const float colw = (inner_w - gap * (MW_IN_PLAYERS - 1)) / (float)MW_IN_PLAYERS;
    const float top = py + 104 * s;
    const float chip_h = 24 * s;
    const float row_h = 30 * s;
    const float key_w = 76 * s;
    const float key_x = 108 * s;
    const float pad_x = 192 * s;

    for (int p = 0; p < MW_IN_PLAYERS; p++) {
        float cx = px + p * (colw + gap);
        char buf[128];
        /* Interior pointers from the input module shift on close_slot(); copy
         * them immediately, before anything else can open/close a pad. */
        const char *live_name_p = mw_input_pad_name(p);
        const char *live_guid_p = mw_input_pad_guid(p);
        std::string live_name(live_name_p ? live_name_p : "");
        std::string live_guid(live_guid_p ? live_guid_p : "");
        std::string stored(settings->pad_guid[p]);
        bool reserved = live_name.empty() && !stored.empty();

        snprintf(buf, sizeof(buf), "PLAYER %d", p + 1);
        text_spaced(bg, ui->font_small, 13 * s, ImVec2(cx, top), A(C_CYAN, 230), buf, 2.0f * s);
        bg->AddRectFilled(ImVec2(cx, top + 16 * s), ImVec2(cx + 18 * s, top + 17.6f * s),
                          A(C_CYAN, 70));

        /* Live connection state (device view, not a cached flag). */
        bg->AddCircleFilled(ImVec2(cx + 4 * s, top + 27 * s), 3.5f * s,
                            live_name.empty() ? A(C_DIM, 210) : A(C_CYAN, 235));
        if (!live_name.empty()) {
            std::string shown = ellipsize(ui->font_body, 17 * s, live_name, colw - 18 * s);
            text(bg, ui->font_body, 17 * s, ImVec2(cx + 14 * s, top + 18 * s), C_TEXT, shown.c_str());
            snprintf(buf, sizeof(buf), "GUID %.32s", live_guid.c_str());
            text(bg, ui->font_small, 11 * s, ImVec2(cx, top + 42 * s), A(C_MUTED, 215), buf);
        } else {
            text(bg, ui->font_body, 17 * s, ImVec2(cx + 14 * s, top + 18 * s),
                 A(C_MUTED, 235), "NO PAD // KEYBOARD");
            text(bg, ui->font_small, 11 * s, ImVec2(cx, top + 42 * s), A(C_DIM, 215),
                 "plug in a controller to assign it");
        }

        /* Ruling 1: a stored but disconnected pad is shown explicitly, with the
         * clear path spelled out, so the reservation policy is never silent. */
        if (reserved) {
            snprintf(buf, sizeof(buf), "P%d: RESERVED FOR", p + 1);
            text(bg, ui->font_small, 12 * s, ImVec2(cx, top + 56 * s),
                 A(C_MAGENTA, 245), buf);
            text(bg, ui->font_small, 11 * s, ImVec2(cx, top + 70 * s),
                 A(C_MAGENTA, 215), stored.c_str());
            text(bg, ui->font_small, 11 * s, ImVec2(cx, top + 84 * s),
                 A(C_MUTED, 225), "not connected - click PAD to reassign/clear");
        }

        /* Assignment chip: AUTO -> connected pads in order -> AUTO.  A reserved
         * binding clears on the first click. */
        {
            int cur = -1;
            std::string label = "AUTO";
            const char *nextg = NULL;
            if (!stored.empty()) {
                for (int k = 0; k < devn; k++) {
                    if (SDL_strcasecmp(dev[k].guid, stored.c_str()) == 0) { cur = k; break; }
                }
                label = (cur >= 0) ? ellipsize(ui->font_button, 13 * s, dev[cur].name, 198 * s)
                                   : std::string("RESERVED");
            }
            if (stored.empty()) {
                nextg = devn > 0 ? dev[0].guid : NULL;
            } else if (cur >= 0) {
                nextg = (cur + 1 < devn) ? dev[cur + 1].guid : NULL;
            } else {
                nextg = NULL; /* reserved -> clear (reassign on the next click) */
            }
            text(bg, ui->font_small, 12 * s, ImVec2(cx, top + 111 * s),
                 A(C_MUTED, 225), "PAD ASSIGN");
            {
                ImVec2 cp(cx + colw - 210 * s, top + 102 * s);
                char id[32];
                snprintf(id, sizeof(id), "##assign%d", p);
                if (binding_chip(ui, id, label.c_str(), cp, ImVec2(210 * s, 28 * s), s,
                                 false, !stored.empty())) {
                    const char *want = nextg ? nextg : "";
                    bool unchanged = (want[0] == 0 && stored.empty()) ||
                                     (want[0] && SDL_strcasecmp(want, stored.c_str()) == 0);
                    if (!unchanged)
                        apply_pad_assign(settings, p, want, changed);
                }
            }
        }

        /* 7-row rebind table: KEY writes key_map, PAD writes pad_map. */
        {
            float hdr = top + 140 * s;
            text(bg, ui->font_small, 11 * s, ImVec2(cx, hdr), A(C_MUTED, 230), "ACTION");
            text(bg, ui->font_small, 11 * s, ImVec2(cx + key_x + 8 * s, hdr),
                 A(C_MUTED, 230), "KEY");
            text(bg, ui->font_small, 11 * s, ImVec2(cx + pad_x + 8 * s, hdr),
                 A(C_MUTED, 230), "PAD");
        }

        for (int a = 0; a < MW_IN_ACTIONS; a++) {
            float ry = top + 158 * s + a * row_h;
            bool wait_key = capturing && ui->cap_player == p && ui->cap_action == a && ui->cap_pad == 0;
            bool wait_pad = capturing && ui->cap_player == p && ui->cap_action == a && ui->cap_pad == 1;
            std::string kl = wait_key ? std::string("PRESS...")
                                      : scancode_label(settings->key_map[p][a]);
            std::string pl = wait_pad ? std::string("PRESS...")
                                      : pad_button_label(settings->pad_map[p][a]);
            char id[40];
            ImVec2 kp(cx + key_x, ry);
            ImVec2 pp(cx + pad_x, ry);
            float pw_chip = colw - pad_x;

            text(bg, ui->font_body, 16 * s, ImVec2(cx, ry + 3 * s), C_TEXT, k_action_names[a]);
            snprintf(id, sizeof(id), "##key%d_%d", p, a);
            if (binding_chip(ui, id, kl.c_str(), kp, ImVec2(key_w, chip_h), s,
                             wait_key, settings->key_map[p][a] >= 0)) {
                mw_input_begin_capture(p, a, 0);
                ui->cap_player = p;
                ui->cap_action = a;
                ui->cap_pad = 0;
            }
            snprintf(id, sizeof(id), "##pad%d_%d", p, a);
            if (binding_chip(ui, id, pl.c_str(), pp, ImVec2(pw_chip, chip_h), s,
                             wait_pad, settings->pad_map[p][a] >= 0)) {
                mw_input_begin_capture(p, a, 1);
                ui->cap_player = p;
                ui->cap_action = a;
                ui->cap_pad = 1;
            }
        }
    }

    /* Capture banner.  Same strip is the permanent hint when idle. */
    {
        float by = py + 500 * s;
        ImVec2 bp(px, by);
        ImVec2 bsz(inner_w, 44 * s);
        float pulse = 0.62f + 0.38f * sinf((float)ImGui::GetTime() * 4.0f);
        bg->AddRectFilled(bp, ImVec2(bp.x + bsz.x, bp.y + bsz.y), IM_COL32(9, 6, 18, 225),
                          9 * s);
        bg->AddRect(bp, ImVec2(bp.x + bsz.x, bp.y + bsz.y),
                    capturing ? A(C_MAGENTA, (int)(150 + 100 * pulse)) : A(C_CYAN, 55),
                    9 * s, 0, capturing ? 1.6f : 1.0f);
        if (capturing) {
            char target[64];
            snprintf(target, sizeof(target), "P%d  //  %s  //  %s",
                     ui->cap_player + 1, k_action_names[ui->cap_action],
                     ui->cap_pad ? "PAD" : "KEY");
            /* The bundled fonts have no U+2026 glyph, so the prompt uses the
             * ASCII three-dot form the panel's own ellipsize() uses. */
            text(bg, ui->font_button, 18 * s, ImVec2(bp.x + 18 * s, by + 11 * s),
                 A(IM_COL32(255, 200, 240, 255), (int)(170 + 85 * pulse)),
                 "PRESS A KEY OR BUTTON...   (ESC CANCELS)");
            {
                ImVec2 ts = text_size(ui->font_button, 15 * s, target);
                text(bg, ui->font_button, 15 * s,
                     ImVec2(bp.x + bsz.x - 18 * s - ts.x, by + 13 * s), A(C_CYAN, 235), target);
            }
        } else {
            text(bg, ui->font_small, 14 * s, ImVec2(bp.x + 18 * s, by + 14 * s),
                 IM_COL32(174, 198, 216, 255),
                 "CLICK A KEY OR PAD CHIP, THEN PRESS THE NEW BINDING.   ESC CANCELS.");
        }
    }

    text(bg, ui->font_small, 13 * s, ImVec2(px, py + 560 * s),
         IM_COL32(158, 182, 202, 255),
         "P3 START shares the coin-3 bit (0x80); P3 BACK is unmapped by default.  "
         "Bindings save to settings.ini.");

    bg->AddRectFilled(ImVec2(px, py + 600 * s), ImVec2(px + inner_w, py + 601 * s),
                      A(C_CYAN, 36));

    if (neon_button(ui, "##ctlclose", "CLOSE",
                    ImVec2(panel.x + pw - 38 * s - 210 * s, py + 618 * s),
                    ImVec2(210 * s, 48 * s), BUTTON_PRIMARY, true, s))
        *open = 0;
    if (neon_button(ui, "##ctlsettings", "OPEN SETTINGS",
                    ImVec2(px, py + 618 * s), ImVec2(210 * s, 48 * s),
                    BUTTON_SECONDARY, true, s))
        ui->tab = MW_UI_TAB_SETTINGS;
    if (neon_button(ui, "##ctlx", "X", ImVec2(panel.x + pw - 54 * s, panel.y + 20 * s),
                    ImVec2(32 * s, 32 * s), BUTTON_GHOST, true, s))
        *open = 0;

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    /* No capture may outlive the panel: closing it here disarms immediately,
     * and the host repeats the disarm for closes it performs itself. */
    if (!*open) mw_input_cancel_capture();
}

extern "C" void mw_ui_draw_cheats(mw_ui *ui, mw_cheats *cheats, int *open)
{
    if (!ui || !cheats || !open) return;
    ImGuiIO &io = ImGui::GetIO();
    float w = io.DisplaySize.x, h = io.DisplaySize.y;
    float s = ui_scale(ui);
    ImDrawList *bg = ImGui::GetBackgroundDrawList();

    ui->tab_count = 3;

    bg->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32(2, 4, 10, 214));
    for (float y = 0; y < h; y += 3.0f * s)
        bg->AddLine(ImVec2(0, y), ImVec2(w, y), IM_COL32(0, 0, 0, 16), 1.0f);

    float pw = 780 * s, ph = 700 * s;
    ImVec2 panel((w - pw) * 0.5f, (h - ph) * 0.5f);
    draw_panel(bg, panel, ImVec2(pw, ph), 18 * s, true);
    float px = panel.x + 38 * s;
    float py = panel.y + 28 * s;
    const float inner_w = pw - 76 * s;

    text_spaced(bg, ui->font_small, 14 * s, ImVec2(px, py),
                A(C_MAGENTA, 235), "CHEAT MODULE", 2.6f * s);
    text_glow(bg, ui->font_title, 46 * s, ImVec2(px, py + 22 * s),
              "CHEATS", IM_COL32(245, 252, 255, 255), C_MAGENTA, s);
    bg->AddRectFilledMultiColor(ImVec2(px, py + 86 * s),
                                ImVec2(px + inner_w, py + 88 * s),
                                C_MAGENTA, C_CYAN, C_CYAN, C_MAGENTA);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##cheats_ui", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoBackground);

    overlay_tabs(ui, panel.x + pw - 54 * s, panel.y + 20 * s, s);

    text_spaced(bg, ui->font_small, 13 * s, ImVec2(px, py + 104 * s),
                A(C_MAGENTA, 225), "GAMEPLAY", 2.0f * s);
    text(bg, ui->font_small, 13 * s, ImVec2(px, py + 122 * s), A(C_MUTED, 215),
         "GLOBAL: one toggle covers 1P, 2P and 3P. Applied after every frame.");

    bool lives = cheats->infinite_lives != 0;
    toggle_row(ui, "##cheat_lives", "INFINITE LIVES",
               "Keeps a full spare-life stock (writes 9 lives every frame)",
               &lives, ImVec2(px, py + 146 * s), inner_w, s, NULL);
    cheats->infinite_lives = lives ? 1 : 0;

    bool invincible = cheats->invincibility != 0;
    toggle_row(ui, "##cheat_invincible", "INVINCIBILITY",
               "Vitality is refilled every frame, so hits cannot kill",
               &invincible, ImVec2(px, py + 202 * s), inner_w, s, NULL);
    cheats->invincibility = invincible ? 1 : 0;

    bool magic = cheats->infinite_magic != 0;
    toggle_row(ui, "##cheat_magic", "INFINITE MAGIC",
               "Dance charges are refilled every frame, so the dance is always ready",
               &magic, ImVec2(px, py + 258 * s), inner_w, s, NULL);
    cheats->infinite_magic = magic ? 1 : 0;

    text_spaced(bg, ui->font_small, 13 * s, ImVec2(px, py + 320 * s),
                A(C_CYAN, 225), "CREDITS", 2.0f * s);

    bool credits = cheats->free_credits != 0;
    toggle_row(ui, "##cheat_credits", "FREE CREDITS",
               "Holds the coin slot at 9 credits, including on the attract screen",
               &credits, ImVec2(px, py + 342 * s), inner_w, s, NULL);
    cheats->free_credits = credits ? 1 : 0;

    /* The session-only ruling has to be visible where the toggles live: a
     * player who expects settings.ini to remember them would be surprised. */
    {
        ImVec2 bp(px, py + 414 * s);
        ImVec2 bsz(inner_w, 84 * s);
        bg->AddRectFilled(bp, ImVec2(bp.x + bsz.x, bp.y + bsz.y),
                          IM_COL32(20, 8, 26, 210), 9 * s);
        bg->AddRect(bp, ImVec2(bp.x + bsz.x, bp.y + bsz.y), A(C_MAGENTA, 90),
                    9 * s, 0, 1.0f);
        bg->AddRectFilled(ImVec2(bp.x, bp.y + 12 * s),
                          ImVec2(bp.x + 2.5f * s, bp.y + bsz.y - 12 * s),
                          C_MAGENTA, 1 * s);
        text(bg, ui->font_small, 13 * s, ImVec2(bp.x + 16 * s, bp.y + 13 * s),
             A(IM_COL32(255, 205, 240, 255), 245), "SESSION ONLY");
        text(bg, ui->font_small, 13 * s, ImVec2(bp.x + 16 * s, bp.y + 33 * s),
             A(C_MUTED, 230),
             "Not saved to settings.ini. Toggles reset to off on reboot (F5)");
        text(bg, ui->font_small, 13 * s, ImVec2(bp.x + 16 * s, bp.y + 52 * s),
             A(C_MUTED, 230), "or when the emulator exits. Cheats never run headless.");
    }

    bg->AddRectFilled(ImVec2(px, py + 600 * s), ImVec2(px + inner_w, py + 601 * s),
                      A(C_CYAN, 36));

    if (neon_button(ui, "##cheats_settings", "OPEN SETTINGS",
                    ImVec2(px, py + 618 * s), ImVec2(210 * s, 48 * s),
                    BUTTON_SECONDARY, true, s))
        ui->tab = MW_UI_TAB_SETTINGS;
    if (neon_button(ui, "##cheats_close", "CLOSE",
                    ImVec2(panel.x + pw - 38 * s - 210 * s, py + 618 * s),
                    ImVec2(210 * s, 48 * s), BUTTON_PRIMARY, true, s))
        *open = 0;
    if (neon_button(ui, "##cheats_x", "X", ImVec2(panel.x + pw - 54 * s, panel.y + 20 * s),
                    ImVec2(32 * s, 32 * s), BUTTON_GHOST, true, s))
        *open = 0;

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

extern "C" void mw_ui_file_browser(mw_ui *ui, mw_file_browser *fb, const char *base)
{
    if (!ui || !fb || !fb->active) return;
    ImGuiIO &io = ImGui::GetIO();
    float w = io.DisplaySize.x, h = io.DisplaySize.y;
    float s = ui_scale(ui);
    ImDrawList *bg = ImGui::GetBackgroundDrawList();

    /* The host seeds cwd from the launcher base; an empty one still works. */
    fb->cwd[sizeof(fb->cwd) - 1] = 0;
    if (!fb->cwd[0]) {
        snprintf(fb->cwd, sizeof(fb->cwd), "%s", base ? base : "");
        fb->cwd[sizeof(fb->cwd) - 1] = 0;
    }

    /* ".." is always the first row, so the list reads like every file manager
     * and the gamepad's first Down never sits on a dim, inert entry. */
    fb_entry rows[FB_MAX_ENTRIES];
    int count = 0;
    rows[count].name = "..";
    rows[count].is_dir = 1;
    rows[count].is_zip = 0;
    count++;
    count += fb_scan(fb->cwd, rows + count, FB_MAX_ENTRIES - count);

    bg->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32(3, 5, 12, 226));
    for (float y = 0; y < h; y += 3.0f * s)
        bg->AddLine(ImVec2(0, y), ImVec2(w, y), IM_COL32(0, 0, 0, 16), 1.0f);

    const float pw = 840.0f * s, ph = 660.0f * s;
    ImVec2 panel((w - pw) * 0.5f, (h - ph) * 0.5f);
    draw_panel(bg, panel, ImVec2(pw, ph), 18.0f * s, true);
    float px = panel.x + 38.0f * s;
    float py = panel.y + 28.0f * s;
    float inner_w = pw - 76.0f * s;

    text_spaced(bg, ui->font_small, 14.0f * s, ImVec2(px, py),
                A(C_CYAN, 235), "ROM ARCHIVE", 2.6f * s);
    text_glow(bg, ui->font_title, 36.0f * s, ImVec2(px, py + 20.0f * s),
              "SELECT ARCHIVE", IM_COL32(245, 252, 255, 255), C_CYAN, s);
    bg->AddRectFilledMultiColor(ImVec2(px, py + 66.0f * s),
                                ImVec2(px + inner_w, py + 68.0f * s),
                                C_MAGENTA, C_CYAN, C_CYAN, C_MAGENTA);

    text_spaced(bg, ui->font_small, 11.0f * s, ImVec2(px, py + 78.0f * s),
                A(C_DIM, 230), "CURRENT FOLDER", 1.8f * s);
    {
        std::string where = ellipsize(ui->font_body, 16.0f * s, fb->cwd, inner_w);
        text(bg, ui->font_body, 16.0f * s, ImVec2(px, py + 94.0f * s),
             A(C_TEXT, 238), where.c_str());
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##file_browser_ui", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoBackground);

    const float row_h = 38.0f * s;
    const float list_h = 380.0f * s;
    ImGui::SetCursorScreenPos(ImVec2(px, py + 126.0f * s));
    /* NavFlattened keeps the list rows in the same nav order as the footer
     * buttons: the pad's first Down lands on the ".." row, never on a button
     * below the list. */
    ImGui::BeginChild("##fb_list", ImVec2(inner_w, list_h),
                      ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_NoSavedSettings);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float row_w = ImGui::GetContentRegionAvail().x;
    if (count == 1)
        text(dl, ui->font_small, 14.0f * s, ImVec2(0, 18.0f * s),
             A(C_DIM, 210), "EMPTY FOLDER");

    for (int i = 0; i < count; i++) {
        int is_up = i == 0;
        int is_dir = rows[i].is_dir;
        int is_zip = rows[i].is_zip;
        int selectable = is_dir || is_zip;
        ImGui::SetCursorPos(ImVec2(0.0f, (float)i * row_h));
        ImGui::PushID(i);
        /* The whole row is the target, exactly like the binding chips: the
         * gamepad focus ring and the activation both come from the shared
         * InvisibleButton + item_pressed() contract. */
        ImGui::InvisibleButton("##fbrow", ImVec2(row_w, row_h),
                               ImGuiButtonFlags_EnableNav);
        bool hovered = item_hot();
        bool pressed = item_pressed();
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        float rounding = 8.0f * s;

        if (hovered) {
            dl->AddRectFilled(rmin, rmax, A(C_CYAN, 26), rounding);
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        } else if (i & 1) {
            dl->AddRectFilled(rmin, rmax, IM_COL32(255, 255, 255, 6), rounding);
        }
        if (!is_up)
            dl->AddLine(ImVec2(rmin.x + 12.0f * s, rmax.y),
                        ImVec2(rmax.x - 12.0f * s, rmax.y), A(C_CYAN, 14), 1.0f);

        float isz = 16.0f * s;
        ImVec2 ip(rmin.x + 12.0f * s, rmin.y + (row_h - isz) * 0.5f);
        ImU32 icol = is_dir ? A(C_CYAN, hovered ? 245 : 165)
                            : (is_zip ? A(C_MAGENTA, hovered ? 245 : 165)
                                      : A(C_DIM, 150));
        dl->AddRectFilled(ip, ImVec2(ip.x + isz, ip.y + isz), A(icol, 30), 3.0f * s);
        dl->AddRect(ip, ImVec2(ip.x + isz, ip.y + isz), icol, 3.0f * s, 0, 1.0f);
        if (is_dir)
            dl->AddRectFilled(ImVec2(ip.x + 2.0f * s, ip.y - 2.5f * s),
                              ImVec2(ip.x + isz * 0.6f, ip.y + 1.5f * s),
                              icol, 1.5f * s);

        {
            float fs = 19.0f * s;
            std::string shown = ellipsize(ui->font_body, fs, rows[i].name, row_w - 150.0f * s);
            ImVec2 tsz = text_size(ui->font_body, fs, shown.c_str());
            ImU32 name_col = selectable ? (hovered ? C_TEXT : IM_COL32(226, 240, 250, 255))
                                        : C_DIM;
            text(dl, ui->font_body, fs,
                 ImVec2(rmin.x + 40.0f * s, rmin.y + (row_h - tsz.y) * 0.5f - 1.0f * s),
                 name_col, shown.c_str());
        }

        const char *tag = is_up ? "UP" : (is_dir ? "OPEN" : (is_zip ? "ZIP" : NULL));
        if (tag) {
            float fs = 11.0f * s;
            ImVec2 tsz = text_size(ui->font_small, fs, tag);
            float cw = tsz.x + 16.0f * s, ch = 20.0f * s;
            ImVec2 cp(rmax.x - 14.0f * s - cw, rmin.y + (row_h - ch) * 0.5f);
            ImU32 accent = is_zip ? C_MAGENTA : C_CYAN;
            dl->AddRectFilled(cp, ImVec2(cp.x + cw, cp.y + ch),
                              A(accent, hovered ? 40 : 18), 6.0f * s);
            dl->AddRect(cp, ImVec2(cp.x + cw, cp.y + ch),
                        A(accent, hovered ? 210 : 80), 6.0f * s, 0, 1.0f);
            text(dl, ui->font_small, fs,
                 ImVec2(cp.x + (cw - tsz.x) * 0.5f, cp.y + (ch - tsz.y) * 0.5f - 1.0f * s),
                 A(accent, 245), tag);
        }

        if (pressed) {
            if (is_up) {
                std::string up = fb_parent(fb->cwd);
                if (up != fb->cwd)
                    snprintf(fb->cwd, sizeof(fb->cwd), "%s", up.c_str());
            } else if (is_dir) {
                std::string next = fb_join(fb->cwd, rows[i].name.c_str());
                snprintf(fb->cwd, sizeof(fb->cwd), "%s", next.c_str());
            } else if (is_zip) {
                std::string full = fb_join(fb->cwd, rows[i].name.c_str());
                snprintf(fb->selection, sizeof(fb->selection), "%s", full.c_str());
                fb->done = 1;
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    float btn_y = panel.y + ph - 38.0f * s - 48.0f * s;
    if (neon_button(ui, "##fbselect", "SELECT THIS FOLDER", ImVec2(px, btn_y),
                    ImVec2(240.0f * s, 48.0f * s), BUTTON_SECONDARY, true, s)) {
        snprintf(fb->selection, sizeof(fb->selection), "%s", fb->cwd);
        fb->done = 1;
    }
    if (neon_button(ui, "##fbclose", "CLOSE",
                    ImVec2(panel.x + pw - 38.0f * s - 180.0f * s, btn_y),
                    ImVec2(180.0f * s, 48.0f * s), BUTTON_PRIMARY, true, s)) {
        fb->active = 0;
        fb->done = 0;
    }
    if (neon_button(ui, "##fbx", "X",
                    ImVec2(panel.x + pw - 54.0f * s, panel.y + 20.0f * s),
                    ImVec2(32.0f * s, 32.0f * s), BUTTON_GHOST, true, s)) {
        fb->active = 0;
        fb->done = 0;
    }
    text_spaced(bg, ui->font_small, 11.0f * s, ImVec2(px + 254.0f * s, btn_y + 17.0f * s),
                A(C_DIM, 215), "DPAD MOVE   A OPEN / SELECT", 1.5f * s);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

extern "C" void mw_ui_draw_fps(mw_ui *ui, float fps)
{
    if (!ui) return;
    ImGuiIO &io = ImGui::GetIO();
    float s = ui_scale(ui);
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    char buf[48];
    snprintf(buf, sizeof(buf), "%.0f FPS", fps);
    float sz = 13.0f * s;
    ImVec2 ts = text_size(ui->font_small, sz, buf);
    float x = io.DisplaySize.x - ts.x - 34 * s;
    float y = 18 * s;
    dl->AddRectFilled(ImVec2(x - 12 * s, y - 4 * s), ImVec2(x + ts.x + 12 * s, y + ts.y + 4 * s),
                      IM_COL32(2, 4, 10, 150), 8 * s);
    dl->AddCircleFilled(ImVec2(x - 18 * s, y + ts.y * 0.5f), 3.5f * s, A(C_CYAN, 220));
    text(dl, ui->font_small, sz, ImVec2(x, y), A(C_MUTED, 230), buf);
}
