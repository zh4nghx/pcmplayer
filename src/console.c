#include "console.h"

#include <conio.h>
#include <windows.h>

static HANDLE g_out = INVALID_HANDLE_VALUE;
static WORD   g_saved_attr = 0;
static int    g_have_console = 0;

int con_init(void)
{
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_out == NULL || g_out == INVALID_HANDLE_VALUE)
        return -1;

    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(g_out, &info)) {
        g_have_console = 1;
        g_saved_attr = info.wAttributes;
    }
    return 0;
}

int con_available(void)
{
    return g_have_console;
}

int con_width(void)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (g_have_console && GetConsoleScreenBufferInfo(g_out, &info))
        return info.srWindow.Right - info.srWindow.Left + 1;
    return 80;
}

int con_height(void)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (g_have_console && GetConsoleScreenBufferInfo(g_out, &info))
        return info.srWindow.Bottom - info.srWindow.Top + 1;
    return 25;
}

void con_set_color(ConsoleColor color)
{
    WORD attr;

    if (!g_have_console) return;

    switch (color) {
    case CON_ACCENT:
        attr = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        break;
    case CON_GOOD:
        attr = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        break;
    case CON_WARN:
        attr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        break;
    case CON_BAD:
        attr = FOREGROUND_RED | FOREGROUND_INTENSITY;
        break;
    case CON_DIM:
        attr = FOREGROUND_INTENSITY;
        break;
    case CON_BOLD:
        attr = g_saved_attr | FOREGROUND_INTENSITY;
        break;
    case CON_DEFAULT:
    default:
        attr = g_saved_attr;
        break;
    }
    SetConsoleTextAttribute(g_out, attr);
}

void con_reset_color(void)
{
    if (g_have_console)
        SetConsoleTextAttribute(g_out, g_saved_attr);
}

int con_gotoxy(int x, int y)
{
    COORD pos;
    if (!g_have_console) return -1;
    pos.X = (SHORT)x;
    pos.Y = (SHORT)y;
    return SetConsoleCursorPosition(g_out, pos) ? 0 : -1;
}

int con_getxy(int *x, int *y)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!g_have_console || !GetConsoleScreenBufferInfo(g_out, &info)) {
        if (x) *x = 0;
        if (y) *y = 0;
        return -1;
    }
    if (x) *x = info.dwCursorPosition.X;
    if (y) *y = info.dwCursorPosition.Y;
    return 0;
}

void con_show_cursor(int show)
{
    CONSOLE_CURSOR_INFO ci;
    if (!g_have_console) return;
    if (!GetConsoleCursorInfo(g_out, &ci)) return;
    ci.bVisible = show ? TRUE : FALSE;
    SetConsoleCursorInfo(g_out, &ci);
}

void con_flush(void)
{
    if (g_have_console)
        FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
}

int con_read_key(void)
{
    int ch;

    if (!_kbhit())
        return CON_KEY_NONE;

    ch = _getch();

    if (ch == 0 || ch == 0xE0) {
        int ext = _getch();
        switch (ext) {
        case 72: return CON_KEY_UP;
        case 80: return CON_KEY_DOWN;
        case 75: return CON_KEY_LEFT;
        case 77: return CON_KEY_RIGHT;
        case 71: return CON_KEY_HOME;
        case 79: return CON_KEY_END;
        case 73: return CON_KEY_PGUP;
        case 81: return CON_KEY_PGDN;
        default: return CON_KEY_NONE;
        }
    }

    switch (ch) {
    case 3:   return CON_KEY_CTRL_C;
    case 13:
    case 10:  return CON_KEY_ENTER;
    case 27:  return CON_KEY_ESC;
    case 32:  return CON_KEY_SPACE;
    case 8:
    case 127: return CON_KEY_BACKSPACE;
    default:  break;
    }

    if (ch >= 32 && ch < 127)
        return ch;
    return CON_KEY_NONE;
}

void con_drain_keys(void)
{
    while (_kbhit())
        _getch();
}
