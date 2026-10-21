// stb is header-only: the implementation lives in this translation unit and in no other, so the
// rest of the core sees the declarations from the headers alone. The file is compiled with the
// warnings off (see native/CMakeLists.txt) because stb is C code that warns under -Wextra.
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
