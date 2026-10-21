#include <cstdlib>

// Another plugin may export incompatible Pango functions. Resolve the
// rendering API from Pango's handle instead of the global lookup scope.
extern "C" int pango_layout_iter_get_baseline(void*) {std::abort();}
extern "C" int pango_layout_iter_get_run_baseline(void*) {std::abort();}
