#include "ui_bindings.hpp"

#include <LCUI.h>

#include <iostream>

using namespace tei_mt_gui;

int main() {
    lcui_init();

    ui_widget_t* pack = ui_load_xml_file("ui_layout.xml");
    if (!pack) {
        std::cerr << "Failed to load ui_layout.xml\n";
        return 1;
    }

    ui_root_append(pack);
    ui_widget_unwrap(pack);
    ui_widget_set_title(ui_root(), L"HY-MT LCUI Translator");

    static UiContext ctx;
    bind_ui(ctx);

    return lcui_main();
}
