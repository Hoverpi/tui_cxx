#define TUI_IMPLEMENTATION
#include "tui.h"

int main() {
    Tui tui = tui_init();

    // Root is a STACK to layer modals over the base UI
    Widget* root = create_widget(WIDGET_STACK, FLEX(100), FLEX(100));

    // Base Layout (Vertical)
    Widget* base_layout = create_widget(WIDGET_VRECT, FLEX(100), FLEX(100));
    Widget* header = create_widget(WIDGET_BOX, FLEX(100), FIXED(3));
    Widget* body = create_widget(WIDGET_HRECT, FLEX(100), FLEX(100));
    
    // Sidebar & Content
    Widget* sidebar = create_widget(WIDGET_BOX, FLEX(20), FLEX(100));
    Widget* content = create_widget(WIDGET_BOX, FLEX(80), FLEX(100));

    // Assembly
    widget_add_child(body, sidebar);
    widget_add_child(body, content);
    widget_add_child(base_layout, header);
    widget_add_child(base_layout, body);
    
    widget_add_child(root, base_layout); // Layer 0
    tui.root = root;

    run(&tui);

    free_widget_tree(root);
    tui_destroy(&tui);
    return 0;
}

