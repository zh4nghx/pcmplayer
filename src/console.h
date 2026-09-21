/*
 * console.h - thin Win32 console helpers (color, cursor, non blocking keys).
 */
#ifndef PCM_CONSOLE_H
#define PCM_CONSOLE_H

typedef enum {
    CON_DEFAULT = 0,
    CON_ACCENT,
    CON_GOOD,
    CON_WARN,
    CON_BAD,
    CON_DIM,
    CON_BOLD
} ConsoleColor;

/* Key codes returned by con_read_key(). Printable ASCII is returned as-is. */
#define CON_KEY_NONE       0
#define CON_KEY_UP        (-1)
#define CON_KEY_DOWN      (-2)
#define CON_KEY_LEFT      (-3)
#define CON_KEY_RIGHT     (-4)
#define CON_KEY_HOME      (-5)
#define CON_KEY_END       (-6)
#define CON_KEY_PGUP      (-7)
#define CON_KEY_PGDN      (-8)
#define CON_KEY_ENTER     (-9)
#define CON_KEY_ESC       (-10)
#define CON_KEY_SPACE     (-11)
#define CON_KEY_BACKSPACE (-12)
#define CON_KEY_CTRL_C    (-13)

int  con_init(void);
int  con_available(void);          /* 1 when a real console is attached */
int  con_width(void);
int  con_height(void);

void con_set_color(ConsoleColor color);
void con_reset_color(void);

int  con_gotoxy(int x, int y);
int  con_getxy(int *x, int *y);

void con_show_cursor(int show);
void con_flush(void);

int  con_read_key(void);           /* CON_KEY_* or ASCII, CON_KEY_NONE if idle */
void con_drain_keys(void);

#endif /* PCM_CONSOLE_H */
