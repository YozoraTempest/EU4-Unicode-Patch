#pragma once

namespace eu4unicode::icu_runtime {

// Resolve the ICU C API from the host's installed ICU major version. The
// Linux plugin deliberately avoids a link-time dependency on one ICU SONAME
// so the same ELF can run on distributions shipping different ICU majors.
void ensure_loaded();

}
