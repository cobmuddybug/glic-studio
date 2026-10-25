add_library(glic STATIC
    src/bitio.cpp
    src/codec.cpp
    src/colorspaces.cpp
    src/corrupt.cpp
    src/image_io.cpp
    src/names.cpp
    src/planes.cpp
    src/predict.cpp
    src/wavelets.cpp
)
add_library(glic::glic ALIAS glic)

target_include_directories(glic
    PUBLIC include
    PRIVATE src external
)
target_compile_features(glic PUBLIC cxx_std_20)

# libglic must compute exactly what GLIC computes on the JVM:
#  - no FMA contraction or fast-math: float expressions round like Java's;
#  - -fwrapv: Java int arithmetic wraps on overflow.
target_compile_options(glic PRIVATE -ffp-contract=off -fwrapv -fno-fast-math -Wall -Wextra)

set_source_files_properties(src/image_io.cpp PROPERTIES COMPILE_OPTIONS "-w")
