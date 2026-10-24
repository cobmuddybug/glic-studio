#!/usr/bin/env bash
# Build the headless Java GLIC harness.
#
# Downloads Processing 3.5.4 (bundles a Java 8 runtime and the sketch
# preprocessor) into build/processing, assembles a sketch from the codec tabs of
# third_party/GLIC (GUI and main tabs replaced by harness.pde), preprocesses and
# compiles it, then compiles HarnessMain.java with the bundled Eclipse compiler.
#
# Output: build/java-harness/run.sh <command> ...
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
build="$root/build"
proc="$build/processing/processing-3.5.4"
url="https://github.com/processing/processing/releases/download/processing-0270-3.5.4/processing-3.5.4-linux64.tgz"
sha="ded445069db3c6fc384fe4da89ca7aa7d0a4bd2536c5aa8de3fa4e115de3025b"

if [[ ! -x "$proc/processing-java" ]]; then
    mkdir -p "$build/processing"
    curl -sSL -o "$build/processing/p.tgz" "$url"
    echo "$sha  $build/processing/p.tgz" | sha256sum -c --quiet
    tar -xzf "$build/processing/p.tgz" -C "$build/processing"
    rm "$build/processing/p.tgz"
fi

out="$build/java-harness"
sketch="$out/glic_harness"
rm -rf "$out"
mkdir -p "$sketch/code" "$out/classes"

glic="$root/third_party/GLIC"
for tab in codec colorspaces encoding planes predictions quantization segmentation transformation; do
    cp "$glic/$tab.pde" "$sketch/"
done
cp "$glic"/code/*.jar "$sketch/code/"
cp "$root/tools/java-harness/harness.pde" "$sketch/glic_harness.pde"

"$proc/processing-java" --sketch="$sketch" --output="$out/sketch-build" --force --build >/dev/null

java="$proc/java/bin/java"
cp="$proc/core/library/core.jar:$sketch/code/JWave.jar:$sketch/code/bit-io-1.3.7.jar:$out/sketch-build"
"$java" -cp "$proc/modes/java/mode/org.eclipse.jdt.core.jar" org.eclipse.jdt.internal.compiler.batch.Main \
    -1.8 -nowarn -cp "$cp" -d "$out/classes" "$root/tools/java-harness/HarnessMain.java"

cat > "$out/run.sh" <<EOF
#!/usr/bin/env bash
exec "$java" -Djava.awt.headless=true -cp "$cp:$out/classes" HarnessMain "\$@"
EOF
chmod +x "$out/run.sh"
echo "built $out/run.sh"
