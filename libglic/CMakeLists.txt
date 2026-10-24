add_library(glic STATIC
    src/bitio.cpp
    src/codec.cpp
    src/colorspaces.cpp
    src/effects.cpp
    src/encoding.cpp
    src/image_io.cpp
    src/planes.cpp
    src/prediction.cpp
    src/quantization.cpp
    src/segment.cpp
    src/wavelet.cpp
)
add_library(glic::glic ALIAS glic)

target_include_directories(glic
    PUBLIC include
    PRIVATE src external
)
target_compile_features(glic PUBLIC cxx_std_20)

# Output must be bit-exact across builds (golden tests):
#  - no FMA contraction or fast-math, so float results don't depend on -march;
#  - -fwrapv, because corrupt .glic data drives signed arithmetic past INT_MAX
#    and upstream relied on wrap-around.
target_compile_options(glic PRIVATE -ffp-contract=off -fwrapv -Wall -Wextra)

# Third-party header implementations; keep their warnings out of our output.
set_source_files_properties(src/image_io.cpp PROPERTIES COMPILE_OPTIONS "-w")
