set(DOZER_AUDIO_DEPENDENCY_CACHE "$ENV{HOME}/.xmake/packages" CACHE PATH
  "Optional preinstalled SDL3 dependency cache; xmake is not invoked")
file(GLOB dozer_audio_sdl3_prefixes
  "${DOZER_AUDIO_DEPENDENCY_CACHE}/l/libsdl3/*/*")
find_package(SDL3 CONFIG REQUIRED HINTS ${dozer_audio_sdl3_prefixes})