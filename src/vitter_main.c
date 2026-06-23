#include "../../src/adapters/parsers.h"
#include "../../src/renderer.h"
#include "../../src/net/tcp_debugger.h"
#include "../../src/net/net.h"
#include "js.h"

int main(int argc, const char *argv[]) {
    lxb_html_document_t *document;
    lxb_css_stylesheet_t *css;
    net_init();
    debug_init();
    //
    document = html_to_element("html/index.html");
    css = apply_css(document, "html/style.css");
    debug("Document parsed.", NULL);
    graph_init();
    debug("Graph init done, rendering.", NULL);
    render_document(document, css, add_duk_functions);
    (void) lxb_html_document_destroy(document);
    (void) lxb_css_stylesheet_destroy(css, true);
    return EXIT_SUCCESS;
}
