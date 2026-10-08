#pragma once
// One GL header for the whole app.
// macOS exports every core GL 4.1 entry point directly, so no loader is needed
// there (and skipping GLEW keeps the bundle free of extra dylibs).
#ifdef __APPLE__
  #define GL_SILENCE_DEPRECATION
  #include <OpenGL/gl3.h>
#else
  #include <GL/glew.h>
#endif
