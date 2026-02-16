#ifdef TUI_IMPLEMENTATION

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <wchar.h>
#include <locale.h>

typedef struct {
  char **data;
  size_t count;
  size_t capacity;
} Cmd;

#define append(xs, ...)                                                     \
  do {                                                                      \
   __typeof__(*(xs).data) _items[] = {__VA_ARGS__};                         \
    size_t _n = sizeof(_items) / sizeof(_items[0]);                         \
                                                                            \
    if ((xs).count + _n >= (xs).capacity) {                                 \
      if ((xs).capacity == 0) (xs).capacity = 8;                            \
      while ((xs).count + _n > (xs).capacity) (xs).capacity <<= 1;          \
                                                                            \
      void *_tmp = realloc((xs).data, (xs).capacity * sizeof(*(xs).data));  \
      if (_tmp == NULL) {                                                   \
        fprintf(stderr, "Out of Memory\n");                                 \
        exit(EXIT_FAILURE);                                                 \
      }                                                                     \
      (xs).data = _tmp;                                                     \
    }                                                                       \
                                                                            \
    memcpy((xs).data + (xs).count, _items, sizeof(_items));                 \
    (xs).count += _n;                                                       \
  } while(0)                                                                \


void get_terminal_size(uint32_t *cols, uint32_t *rows) {
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  *cols = w.ws_col;
  *rows = w.ws_row;
}

void run_cmd(Cmd *cmd) {
  if (cmd->count <= 0) return;

  // calc cmd len
  size_t cmd_len = 0;

  for (size_t i = 0; i < cmd->count; ++i) {
    if (cmd->data[i] == NULL) continue;
    for (size_t j = 0; cmd->data[i][j] != '\0'; ++j) {
      if (!isspace((unsigned char)cmd->data[i][j])) {
        cmd_len++;
      }
    }
  }
  cmd_len += (cmd->count - 1) + 1;

  char *cmd_str = calloc(cmd_len, sizeof(char));
  if (cmd_str == NULL) return;
  char *writer = cmd_str;

  for (size_t i = 0; i < cmd->count; ++i) {
    if (cmd->data[i] == NULL) continue;
    const char *src = cmd->data[i];
    while (*src) {
      if (!isspace((unsigned char)*src)) *writer++ = *src;
      src++;
    }
    if (i < cmd->count - 1) *writer++ = ' ';
  }
  *writer = '\0';

  printf("cmd: [%s] cmd size: %zu\n", cmd_str, cmd_len);

  system(cmd_str);
  
  free(cmd_str);
}

char* run_and_get_output_cmd(Cmd* cmd) {
  if (cmd->count <= 0) return "";

  // calc cmd len
  size_t cmd_len = 0;

  for (size_t i = 0; i < cmd->count; ++i) {
    if (cmd->data[i] == NULL) continue;
    for (size_t j = 0; cmd->data[i][j] != '\0'; ++j) {
      if (!isspace((unsigned char)cmd->data[i][j])) {
        cmd_len++;
      }
    }
  }
  cmd_len += (cmd->count - 1) + 1;

  char *cmd_str = calloc(cmd_len, sizeof(char));
  if (cmd_str == NULL) {
    fprintf(stderr, "Failed calloc\n");
    exit(EXIT_FAILURE);
  }
  char *writer = cmd_str;

  for (size_t i = 0; i < cmd->count; ++i) {
    if (cmd->data[i] == NULL) continue;
    const char *src = cmd->data[i];
    while (*src) {
      if (!isspace((unsigned char)*src)) *writer++ = *src;
      src++;
    }
    if (i < cmd->count - 1) *writer++ = ' ';
  }
  *writer = '\0';

  printf("cmd: [%s]\n", cmd_str);

  FILE* f_cmd;
  char lines[256];
  char* output = NULL;
  size_t output_len = 0;
  size_t chunk_len;
  
  f_cmd = popen(cmd_str, "r");
  if (f_cmd == NULL) return "";

  while (fgets(lines, sizeof(lines), f_cmd) != NULL) {
    chunk_len = strlen(lines);
    output_len += chunk_len;

    output = realloc(output, output_len + 1);
    if (output == NULL) {
      fprintf(stderr, "Failed calloc\n");
      exit(EXIT_FAILURE);
    }

    strcat(output, lines);
  }
  printf("string: %s\n", output);

  free(cmd_str);

  return output;
}

typedef enum { DEFAULT } Color;

typedef enum { REGULAR, BOLD, UNDERLINE } Style;

typedef struct Cell {
  wchar_t symbol;
  Color foreground_color;
  Color background_color;
  Style style;
} Cell;

typedef struct {
    wchar_t top_left, top_right;
    wchar_t bottom_left, bottom_right;
    wchar_t horizontal, vertical;
} BorderSet;

// Default border style
const BorderSet DOUBLE_LINE = { L'╔', L'╗', L'╚', L'╝', L'═', L'║' };
const BorderSet EMPTY_BORDER = { L' ', L' ', L' ', L' ', L' ', L' ' };

typedef enum {
  SIZE_FIXED,
  SIZE_FLEX,
  SIZE_ABSOLUTE,
} SizeType;

typedef struct {
  SizeType type;
  uint32_t value;
} Constraint;

#define FIXED(val) (Constraint){ .type = SIZE_FIXED, .value = (val) }
#define FLEX(val)  (Constraint){ .type = SIZE_FLEX, .value = (val) }
#define ABSOLUTE(val) (Constraint){ .type = SIZE_ABSOLUTE, .value = (val) }

typedef enum {
  WIDGET_VRECT,
  WIDGET_HRECT,
  WIDGET_STACK,
  WIDGET_BOX,
} WidgetType;

typedef struct Widget Widget;
struct Widget {
  WidgetType type;
  Constraint width_constraint;
  Constraint height_constraint;
  BorderSet borders;

  uint32_t x, y, w, h;

  Widget** children;
  size_t child_count;
};

typedef struct {
  uint32_t width;
  uint32_t height;
  Cell* terminal;
  Cell* front_buffer;
  size_t terminal_len;
  uint8_t is_dirty;
  struct termios original_terminal;
  Widget* root;
} Tui;


void map_cell_at(Tui* tui, Cell* cell, uint32_t x, uint32_t y) {
  if (x >= tui->width || y >= tui->height) return;

  // map 2D in 1D
  size_t index = (y * tui->width) + x;
  tui->terminal[index] = *cell;
  tui->is_dirty = 1;
}

void render_cell_at(Tui* tui, uint32_t x, uint32_t y) {
  if (x >= tui->width || y >= tui->height) return;

  Cell cell = tui->terminal[y * tui->width + x];

  // move cursor
  printf("\033[%d;%dH", y + 1, x + 1);

  // apply style
  if (cell.style == BOLD) printf("\033[1m");

  // print symbol
  wprintf(L"%ls", cell.symbol);
}

void enable_terminal_raw_mode(Tui* tui) {
  if (tcgetattr(STDIN_FILENO, &tui->original_terminal) != 0) {
    fprintf(stderr, "Failed tcgetattr\n");
    exit(EXIT_FAILURE);
  }

  struct termios new_terminal = tui->original_terminal;

  new_terminal.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                           | INLCR | IGNCR | ICRNL | IXON);
  new_terminal.c_oflag &= ~OPOST;
  new_terminal.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
  new_terminal.c_cflag &= ~(CSIZE | PARENB);
  new_terminal.c_cflag |= CS8;
  new_terminal.c_cc[VMIN] = 0;
  new_terminal.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_terminal) != 0) {
    fprintf(stderr, "Failed tcsetattr\n");
    exit(EXIT_FAILURE);
  }
}

void handle_key_input() {
  uint8_t read_char;
  read(STDIN_FILENO, &read_char, 1);
  if (read_char == 3) { // Ctrl + c
    exit(EXIT_SUCCESS);
  }
}

void disable_terminal_raw_mode(Tui* tui) {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &tui->original_terminal) != 0) {
    fprintf(stderr, "Failed tcsetattr\n");
    exit(EXIT_FAILURE);
  }
  
}

Tui tui_init() {
  Tui tui = {0};
  setlocale(LC_ALL, "");
  get_terminal_size(&tui.width, &tui.height);

  tui.terminal_len = tui.width * tui.height;
  tui.terminal = calloc(tui.terminal_len, sizeof(Cell));;
  tui.front_buffer = calloc(tui.terminal_len, sizeof(Cell));

  for (uint32_t i = 0; i < tui.terminal_len; ++i) {
    tui.terminal[i] = (Cell){ .symbol = ' ', .style = DEFAULT };
    tui.front_buffer[i] = (Cell){ .symbol = L'\0', .style = DEFAULT }; // initial mismatch
  }

  enable_terminal_raw_mode(&tui);
  wprintf(L"\033[?25l\033[2J"); // Hide cursor and clear screen
  fflush(stdout);

  return tui;
}

  WidgetType type;
  Constraint width_constraint;
  Constraint height_constraint;
  BorderSet borders;

  uint32_t x, y, w, h;

  Widget** children;
  size_t child_count;

Widget* create_widget(WidgetType type, Constraint width_constraint, Constraint height_constraint) {
  Widget* widget = calloc(1, sizeof(Widget));
  if (!widget) exit(EXIT_FAILURE);

  widget->type = type;
  widget->width_constraint = width_constraint;
  widget->height_constraint = height_constraint;
  widget->borders = DOUBLE_LINE;

  return widget;
}

void widget_add_child(Widget* parent, Widget* child) {
  parent->child_count++;
  parent->children = realloc(parent->children, parent->child_count * sizeof(Widget*));
  if (!parent->children) exit(EXIT_FAILURE);
  parent->children[parent->child_count - 1] = child;
}

void free_widget_tree(Widget* widget) {
  if (!widget) return;
  for (size_t i = 0; i < widget->child_count; ++i) {
    free_widget_tree(widget->children[i]);
  }

  free(widget->children);
  free(widget);
}

void widget_compute_layout(Widget* widget, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
  widget->x = x;
  widget->y = y;
  widget->w = width;
  widget->h = height;

  if (widget->child_count == 0) return;

  if (widget->type == WIDGET_VRECT) {
    uint32_t remaining_h = height;
    uint32_t current_y = y;

    // Deduct FIXED heights
    for (size_t i = 0; i < widget->child_count; ++i) {
      if (widget->children[i]->height_constraint.type == SIZE_FIXED) {
        remaining_h -= widget->children[i]->height_constraint.value;
      }
    }

    // Distribute remaining space to FLEX and calculate positions
    for (size_t i = 0; i < widget->child_count; ++i) {
      Widget* child = widget->children[i];
      uint32_t child_h = 0;

      if (child->height_constraint.type == SIZE_FIXED) {
        child_h = child->height_constraint.value;
      } else if (child->height_constraint.type == SIZE_FLEX) {
        if (i == widget->child_count - 1) {
          child_h = remaining_h;
        } else {
          child_h = (remaining_h * child->height_constraint.value) / 100;
          remaining_h -= child_h;
        }
      }

      // A VRECT child always takes full width of parent
      widget_compute_layout(child, x, current_y, width, child_h);
      current_y += child_h;
    }
  } 
  else if (widget->type == WIDGET_HRECT) {
    uint32_t remaining_w = width;
    uint32_t current_x = x;

    // Deduct FIXED widths
    for (size_t i = 0; i < widget->child_count; ++i) {
      if (widget->children[i]->width_constraint.type == SIZE_FIXED) {
        remaining_w -= widget->children[i]->width_constraint.value;
      }
    }

    // Distribute remaining space to FLEX and calculate positions
    for (size_t i = 0; i < widget->child_count; ++i) {
      Widget* child = widget->children[i];
      uint32_t child_w = 0;

      if (child->width_constraint.type == SIZE_FIXED) {
          child_w = child->width_constraint.value;
      } else if (child->width_constraint.type == SIZE_FLEX) {
        if (i == widget->child_count - 1) {
          child_w = remaining_w;
        } else {
          child_w = (remaining_w * child->width_constraint.value) / 100;
          remaining_w -= child_w;
        }
      }

      // An HRECT child always takes full height of parent
      widget_compute_layout(child, current_x, y, child_w, height);
      current_x += child_w;
    }
  }
  else if (widget->type == WIDGET_STACK) {
    for (size_t i = 0; i < widget->child_count; ++i) {
      Widget* child = widget->children[i];
      
      // If absolute, center it based on exact values
      if (child->width_constraint.type == SIZE_ABSOLUTE && child->height_constraint.type == SIZE_ABSOLUTE) {
        uint32_t child_w = child->width_constraint.value;
        uint32_t child_h = child->height_constraint.value;
        uint32_t center_x = x + (width - child_w) / 2;
        uint32_t center_y = y + (height - child_h) / 2;
        widget_compute_layout(child, center_x, center_y, child_w, child_h);
      } else {
        // Otherwise, it layers exactly over the parent
        widget_compute_layout(child, x, y, width, height);
      }
    }
  }
}

void draw_widget_border(Tui* tui, Widget* widget) {
    if (widget->w < 2 || widget->h < 2) return;

    Cell cell = { .style = DEFAULT };

    // Draw Corners
    uint32_t x2 = widget->x + widget->w - 1;
    uint32_t y2 = widget->y + widget->h - 1;

    cell.symbol = widget->borders.top_left;
    map_cell_at(tui, &cell, widget->x, widget->y);
    
    cell.symbol = widget->borders.top_right;
    map_cell_at(tui, &cell, x2, widget->y);
    
    cell.symbol = widget->borders.bottom_left;
    map_cell_at(tui, &cell, widget->x, y2);
    
    cell.symbol = widget->borders.bottom_right;
    map_cell_at(tui, &cell, x2, y2);

    // Draw vertical sides
    cell.symbol = widget->borders.vertical;
    for (uint32_t i = 1; i < widget->h - 1; ++i) {
        map_cell_at(tui, &cell, widget->x, widget->y + i);
        map_cell_at(tui, &cell, x2, widget->y + i);
    }

    // Draw horizontal sides
    cell.symbol = widget->borders.horizontal;
    for (uint32_t i = 1; i < widget->w - 1; ++i) {
        map_cell_at(tui, &cell, widget->x + i, widget->y);
        map_cell_at(tui, &cell, widget->x + i, y2);
    }
}

void widget_render_to_buffer(Tui* tui, Widget* widget) {
  if (widget->width_constraint.type == SIZE_ABSOLUTE) {
    Cell blank = { .symbol = L' ', .style = DEFAULT };
    for (uint32_t y = widget->y; y < widget->y + widget->h; ++y) {
      for (uint32_t x = widget->x; x < widget->x + widget->w; ++x) {
        map_cell_at(tui, &blank, x, y);
      }
    }
  }

  if (widget->type == WIDGET_BOX) {
    draw_widget_border(tui, widget);
  }

  for (size_t i = 0; i < widget->child_count; ++i) {
    widget_render_to_buffer(tui, widget->children[i]);
  }
}

void tui_render(Tui* tui) {
  Style current_style = DEFAULT;
  int cursor_y = -1, cursor_x = -1;

  for (uint32_t y = 0; y < tui->height; ++y) {
    for (uint32_t x = 0; x < tui->width; ++x) {
      size_t idx = y * tui->width + x;
      Cell* back = &tui->terminal[idx];
      Cell* front = &tui->front_buffer[idx];

      // Only act if the cell has changed
      if (back->symbol != front->symbol || back->style != front->style) {
        // Move cursor only if necessary
        if (cursor_y != (int)y || cursor_x != (int)x) {
          wprintf(L"\033[%d;%dH", y + 1, x + 1);
        }

        // Update Style
        if (back->style != current_style) {
          if (back->style == DEFAULT) wprintf(L"\033[0m");
          else if (back->style == BOLD) wprintf(L"\033[1m");
          else if (back->style == UNDERLINE) wprintf(L"\033[4m");
          current_style = back->style;
        }

        putwchar(back->symbol);
        *front = *back; // Sync buffers
      
        cursor_y = y;
        cursor_x = x + 1; // Anticipate terminal moving cursor forward
      }
    }
  }
  fflush(stdout);
}

void run(Tui* tui) {
  for (;;) {
    clock_t start = clock();

    handle_key_input();

    // Clear previous frame
    for (uint32_t i = 0; i < tui->terminal_len; ++i) {
      tui->terminal[i] = (Cell){ .symbol = L' ', .style = DEFAULT };
    }

    // Run layout and push to buffer
    if (tui->root) {
      widget_compute_layout(tui->root, 0, 0, tui->width, tui->height);
      widget_render_to_buffer(tui, tui->root);
    }
    
    if (tui->is_dirty) {
      tui_render(tui);
      tui->is_dirty = 0; 
    }

    clock_t end = clock();
    uint32_t work_time = (uint32_t)((end - start) * 1000000 / CLOCKS_PER_SEC);

    uint32_t target_time = 16666; 
    if (work_time < target_time) {
        usleep(target_time - work_time);
    }
  }
}

void tui_destroy(Tui* tui) {
  disable_terminal_raw_mode(tui);
}

#endif // TUI_IMPLEMENTATION
