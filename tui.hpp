#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <termios.h>

typedef enum { DEFAULT } Color;

typedef enum { REGULAR, BOLD, UNDERLINE } Style;

typedef struct Cell {
  std::string symbol = " ";
  Color foreground_color = DEFAULT;
  Color background_color = DEFAULT;
  Style style = REGULAR;
  bool operator==(const Cell&) const = default;
} Cell;

typedef struct {
  uint32_t x, y, width, height;
} Rect; 

class App;

class Widget {
public:
  Widget(Rect r) : bounds{r} {}
  virtual ~Widget() = default;
  virtual void render(App& app) = 0;
  virtual void handle_key(uint8_t c) {}

  Rect bounds;
  bool focused = false;
};

class App {
  std::vector<std::unique_ptr<Widget>> widgets;
  Rect clip_rect;

public:
  App();  
  ~App();
  void init();
  void set_cell(uint32_t x, uint32_t y, std::string symbol, Style style = REGULAR);
  void render_text(uint32_t x, uint32_t y, std::string_view text, Style style = REGULAR); 

  void set_clip(Rect rect);
  Rect get_clip() const;
  Rect full_screen() const;
  
  void draw();
  void run();

  void term_enable_raw_mode();
  void term_disable_raw_mode();
  void handle_input();

  void render_cell(uint32_t x, uint32_t y);

  template<typename T, typename... Args>
  T& add_widget(Args&&... args) {
    widgets.push_back(std::make_unique<T>(std::forward<Args>(args)...));
    return static_cast<T&>(*widgets.back());
  }

private:
  uint32_t cols;
  uint32_t lines;
  std::vector<Cell> prev_term;
  std::vector<Cell> term;  
  termios original_term;
};

class Label : public Widget {
public:
  Label(uint32_t x, uint32_t y, std::string t) : Widget({x, y, static_cast<uint32_t>(t.length()), 1}), text(t) {}
  void render(App& app) override {
    app.render_text(bounds.x, bounds.y, text, REGULAR);
  }

  std::string text;
};

class Box : public Widget {
public:
  Box(Rect r, std::string t) : Widget(r), title(t) {}
  void render(App& app) override {
    app.set_clip(bounds);
    
    // Horizontal borders
    for (uint32_t i = 0; i < bounds.width; i++) {
        app.set_cell(bounds.x + i, bounds.y, "═");
        app.set_cell(bounds.x + i, bounds.y + bounds.height - 1, "═");
    }
    // Vertical borders
    for (uint32_t i = 0; i < bounds.height; i++) {
        app.set_cell(bounds.x, bounds.y + i, "║");
        app.set_cell(bounds.x + bounds.width - 1, bounds.y + i, "║");
    }
    // Corners
    app.set_cell(bounds.x, bounds.y, "╔");
    app.set_cell(bounds.x + bounds.width - 1, bounds.y, "╗");
    app.set_cell(bounds.x, bounds.y + bounds.height - 1, "╚");
    app.set_cell(bounds.x + bounds.width - 1, bounds.y + bounds.height - 1, "╝");
    
    app.render_text(bounds.x + 2, bounds.y + 1, title, BOLD);
  }
private:
  std::string title;
};

class LoginWidget : public Widget {
public:
  LoginWidget(Rect r) : Widget(r) {}

  void handle_key(uint8_t c) override {
    if (c == '\r' || c == '\n') {
      typing_password = !typing_password;
      return;
    }
    std::string& target = typing_password ? password : username;
    
    if (c == 127) {
      if (!target.empty()) target.pop_back();
      return;
    }
    if (target.length() < 24 && c >= 32 && c <= 126) {
      target += static_cast<char>(c);
    }
  }

  void render(App& app) override {
    // Draw the outer frame
    app.set_clip(bounds);
    
    // Horizontal borders
    for (uint32_t i = 0; i < bounds.width; i++) {
        app.set_cell(bounds.x + i, bounds.y, "═");
        app.set_cell(bounds.x + i, bounds.y + bounds.height - 1, "═");
    }
    // Vertical borders
    for (uint32_t i = 0; i < bounds.height; i++) {
        app.set_cell(bounds.x, bounds.y + i, "║");
        app.set_cell(bounds.x + bounds.width - 1, bounds.y + i, "║");
    }
    // Corners
    app.set_cell(bounds.x, bounds.y, "╔");
    app.set_cell(bounds.x + bounds.width - 1, bounds.y, "╗");
    app.set_cell(bounds.x, bounds.y + bounds.height - 1, "╚");
    app.set_cell(bounds.x + bounds.width - 1, bounds.y + bounds.height - 1, "╝");

    // Render internal text relative to the box
    app.render_text(bounds.x + (bounds.width / 2) - 3, bounds.y + 1, " LOGIN ", BOLD);
    
    app.render_text(bounds.x + 4, bounds.y + 4, "Username:");
    app.render_text(bounds.x + 4, bounds.y + 5, username, typing_password ? REGULAR : UNDERLINE);

    app.render_text(bounds.x + 4, bounds.y + 7, "Password:");
    std::string masked(password.length(), '*');
    app.render_text(bounds.x + 4, bounds.y + 8, masked, typing_password ? UNDERLINE : REGULAR);

    app.render_text(bounds.x + (bounds.width / 2) - 8, bounds.y + 10, "[ Enter to Login ]");
  }

private:
  std::string username = "";
  std::string password = "";
  bool typing_password = false;
};

typedef struct Layout {
  static std::vector<Rect> vertical(Rect area, const std::vector<int>& heights) {
      std::vector<Rect> result;
      uint32_t current_y = area.y;
    
      for (int h : heights) {
          // Handle percentages or fixed sizes here. 
          // For simplicity, 0 means "fill remaining"
          uint32_t actual_h = (h == 0) ? (area.height - (current_y - area.y)) : h;
        
          result.push_back({area.x, current_y, area.width, actual_h});
          current_y += actual_h;
      }
      return result;
    }
    
    static std::vector<Rect> horizontal(Rect area, const std::vector<int>& widths) {
        std::vector<Rect> result;
        uint32_t current_x = area.x;
        
        for (int w : widths) {
            uint32_t actual_w = (w == 0) ? (area.width - (current_x - area.x)) : w;
            result.push_back({current_x, area.y, actual_w, area.height});
            current_x += actual_w;
        }
        return result;
    }
} Layout;
