#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <vector>
#include <print>
#include <termios.h>

#include "tui.hpp"

std::string exec_cmd(const uint8_t* cmd) {
  std::array<uint8_t, 128> buffer;
  std::string result;

  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(reinterpret_cast<const char*>(cmd), "r"), pclose);
  if (!pipe) {
    throw std::runtime_error("popen() failed");
  }

  while (fgets(reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
    result += reinterpret_cast<char*>(buffer.data());
  }
  return result;
}

App::App() : original_term(0) {
  term_enable_raw_mode();
  init();
}

App::~App() {
  term_disable_raw_mode();
}

void App::set_clip(Rect rect) {
  clip_rect = rect;
}

Rect App::get_clip() const {
  return clip_rect;
}
Rect App::full_screen() const {
  return {0, 0, cols, lines}; 
}

void App::set_cell(uint32_t x, uint32_t y, std::string symbol, Style style) {
  if (x >= cols || y >= lines) return;

  // If the coordinate is outside the current widget's area, ignore it.
  if (x < clip_rect.x || x >= clip_rect.x + clip_rect.width ||
    y < clip_rect.y || y >= clip_rect.y + clip_rect.height) {
    return;
  }

  uint32_t index = y * cols + x;
  term[index].symbol = symbol;
  term[index].style = style;
}

void App::render_cell(uint32_t x, uint32_t y) {
  const Cell& cell = term[y * cols + x];

  // move cursor
  std::print("\033[{};{}H", y + 1, x + 1);

  // apply style
  if (cell.style == BOLD) std::print("\033[1m");
  if (cell.style == UNDERLINE) std::print("\033[4m");

  // print symbol
  std::print("{}", cell.symbol);

  // reset
  std::print("\033[0m");
}

void App::draw() {
  // clear the buffer to prevent ghosting
  std::fill(term.begin(), term.end(), Cell{ .symbol = " ", .style = REGULAR });
  
  for (const auto& w : widgets) {
      // We set the clip to the widget's bounds so it's trapped in its box
      this->set_clip(w->bounds);
      w->render(*this);
  }
  bool changed = false;

  for (size_t i = 0; i < term.size(); ++i) {
    if (term[i] != prev_term[i]) { 
      uint32_t x = i % cols;
      uint32_t y = i / cols;
      render_cell(x, y);
      changed = true;
    }
  }
  // Only sync and flush if something actually changed
  if (changed) {
    prev_term = term; // Backup the current frame
    std::fflush(stdout);
  }
}

void App::handle_input() {
  uint8_t read_char;

  if (read(STDIN_FILENO, &read_char, 1) > 0) {
    switch (read_char) {
      case 3: // Ctrl + c
        std::exit(EXIT_FAILURE);
        break;
    }

    for (auto& w : widgets) {
      w->handle_key(read_char);
    }
  }
}

void App::term_enable_raw_mode() {
  if (tcgetattr(STDIN_FILENO, &original_term) != 0) {
    throw new std::runtime_error("ERROR: failed to get terminal attributes");
  }

  termios new_term = original_term;
  // define new_term parameters
  // input modes
  new_term.c_iflag &= ~(IGNBRK | BRKINT | ICRNL | INLCR | PARMRK | INPCK | ISTRIP | IXON);
  // output modes
  new_term.c_oflag &= ~OPOST;
  // local modes
  new_term.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
  // control modes
  new_term.c_cflag &= ~(CSIZE | PARENB);
  new_term.c_cflag |= CS8;
  // control chars
  // I need read(2) to return every single char without timeout
  new_term.c_cc[VMIN] = 0;
  new_term.c_cc[VTIME] = 0;

  // set new_term parameters to the user terminal
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_term) != 0) {
    throw new std::runtime_error("ERROR: failed to set new terminal attributes");
  } 
}

void App::term_disable_raw_mode() {
  // set original_term parameters to the user terminal
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_term) != 0) {
    throw new std::runtime_error("ERROR: failed to set original terminal attributes");
  } 
}

void App::init() {
  try {
    

    cols = std::stoi(exec_cmd(reinterpret_cast<const uint8_t*>("tput cols")));
    lines = std::stoi(exec_cmd(reinterpret_cast<const uint8_t*>("tput lines")));
    // index = y * cols + x
    term.assign(cols * lines, Cell{ .symbol = " ", .style = REGULAR });
    prev_term.assign(cols * lines, Cell{ .symbol = " ", .style = REGULAR });

    std::print("\033[?1049h"); // Switch to Alternate Buffer (don't mess up user's bash history)
    std::print("\033[2J\033[H"); // Clear screen and home cursor
    std::print("\033[?25l");     // Hide cursor
    
    std::fflush(stdout);

    clip_rect = {0, 0, cols, lines};
    
    draw();

  } catch (const std::exception& e) {
    std::println("Initialization failed: {}", e.what());
  }
}

void App::render_text(uint32_t x, uint32_t y, std::string_view text, Style style) {
  for (size_t i = 0; i < text.length(); ++i) {
    set_cell(x + i, y, std::string(1, text[i]), style);
  }
}

void App::run() {
  bool is_running = true;
  while (is_running) {
    handle_input();
    draw();
    // 60 FPS aprox
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
}
