#define TUI_IMPLEMENTATION
#include "tui.h"

int main() {
  Tui tui = tui_init();

  // Root is a STACK so we can layer the modal over the rest
  Widget* root = create_widget(WIDGET_STACK, FLEX(100), FLEX(100));

  // Base Layout (VRECT)
  Widget* base_layout = create_widget(WIDGET_VRECT, FLEX(100), FLEX(100));
  
  // Header: Full width, Fixed 3 height
  Widget* header = create_widget(WIDGET_BOX, FLEX(100), FIXED(3));
  
  // Body: Full width, Flex 100% of remaining height
  Widget* body = create_widget(WIDGET_HRECT, FLEX(100), FLEX(100));
  
  // Sidebar: 20% width, Full height. Content: 80% width, Full height.
  Widget* sidebar = create_widget(WIDGET_BOX, FLEX(20), FLEX(100));
  Widget* content = create_widget(WIDGET_BOX, FLEX(80), FLEX(100));

  // Modal: Absolute size 40x15, centered automatically by the STACK
  Widget* modal = create_widget(WIDGET_BOX, ABSOLUTE(40), ABSOLUTE(15));
  
  // Build the tree
  widget_add_child(body, sidebar);
  widget_add_child(body, content);

  widget_add_child(base_layout, header);
  widget_add_child(base_layout, body);

  widget_add_child(root, base_layout); // Layer 1
  widget_add_child(root, modal);       // Layer 2 (Modal)

  tui.root = root;

  run(&tui);

  free_widget_tree(root);
  tui_destroy(&tui);

  return 0;
}

