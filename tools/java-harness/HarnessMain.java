// Headless driver for the original GLIC (third_party/GLIC), used to produce
// ground truth for libglic. Built by tools/java-harness/build.sh, which runs
// the GLIC .pde tabs through Processing 3.5.4's own preprocessor.
//
// Commands:
//   batch <cases.tsv> <images-dir> <out-dir>   encode+decode every case
//   wavelets <out.txt>                         dump JWave filter tables (hex)
//   presets <presets-dir> <out.json>           convert GLIC presets to JSON
//   random <seed> <n>                          Processing random() sequence

import java.awt.image.BufferedImage;
import java.io.*;
import java.lang.reflect.Field;
import java.util.*;
import javax.imageio.ImageIO;

import jwave.transforms.wavelets.Wavelet;

public class HarnessMain {

  static glic_harness h;

  public static void main(String[] args) throws Throwable {
    h = new glic_harness();
    // Silence GLIC's progress chatter; errors still go to stderr.
    PrintStream realOut = System.out;
    switch (args[0]) {
      case "batch":
        System.setOut(new PrintStream(new OutputStream() { public void write(int b) {} }));
        batch(new File(args[1]), new File(args[2]), new File(args[3]), realOut);
        break;
      case "wavelets":
        dumpWavelets(new File(args[1]));
        break;
      case "presets":
        System.setOut(new PrintStream(new OutputStream() { public void write(int b) {} }));
        presets(new File(args[1]), new File(args[2]));
        break;
      case "random": {
        h.randomSeed(Long.parseLong(args[1]));
        int n = Integer.parseInt(args[2]);
        for (int i = 0; i < n; i++) realOut.println(Float.floatToIntBits(h.random(37.5f)) + " " + Float.floatToIntBits(h.random(-3, 9)));
        break;
      }
      case "transform":
        transformProbe(Integer.parseInt(args[1]), Integer.parseInt(args[2]), Integer.parseInt(args[3]), realOut);
        break;
      default:
        throw new IllegalArgumentException(args[0]);
    }
  }

  static int[] loadArgb(File f, int[] wh) throws IOException {
    BufferedImage bi = ImageIO.read(f);
    wh[0] = bi.getWidth();
    wh[1] = bi.getHeight();
    return bi.getRGB(0, 0, wh[0], wh[1], null, 0, wh[0]);
  }

  static void savePng(int[] px, int w, int hgt, File f) throws IOException {
    BufferedImage bi = new BufferedImage(w, hgt, BufferedImage.TYPE_INT_ARGB);
    bi.setRGB(0, 0, w, hgt, px, 0, w);
    ImageIO.write(bi, "png", f);
  }

  // key=value settings; per-channel keys take one value (all channels) or three comma-separated.
  static void apply(glic_harness.CodecConfig c, String spec) {
    if (spec.equals("-")) return;
    for (String kv : spec.trim().split("\\s+")) {
      String[] p = kv.split("=", 2);
      String k = p[0], v = p[1];
      if (k.equals("colorspace")) { c.colorspace = Integer.parseInt(v); continue; }
      if (k.equals("border")) {
        String[] rgb = v.split(",");
        c.color_outside = 0xff000000 | (Integer.parseInt(rgb[0]) << 16) | (Integer.parseInt(rgb[1]) << 8) | Integer.parseInt(rgb[2]);
        continue;
      }
      String[] vals = v.split(",");
      for (int ch = 0; ch < 3; ch++) {
        String s = vals[vals.length == 3 ? ch : 0];
        switch (k) {
          case "min": c.min_block_size[ch] = Integer.parseInt(s); break;
          case "max": c.max_block_size[ch] = Integer.parseInt(s); break;
          case "thr": c.segmentation_precision[ch] = Float.parseFloat(s); break;
          case "enc": c.encoding_method[ch] = Integer.parseInt(s); break;
          case "pred": c.prediction_method[ch] = Integer.parseInt(s); break;
          case "quant": c.quantization_value[ch] = Integer.parseInt(s); break;
          case "clamp": c.clamp_method[ch] = Integer.parseInt(s); break;
          case "ttype": c.transform_type[ch] = Integer.parseInt(s); break;
          case "wavelet": c.transform_method[ch] = Integer.parseInt(s); break;
          case "compress": c.transform_compress[ch] = Float.parseFloat(s); break;
          case "scale": c.transform_scale[ch] = Integer.parseInt(s); break;
          default: throw new IllegalArgumentException("unknown key " + k);
        }
      }
    }
  }

  // cases.tsv: name  images  seed  encode-spec  decode-spec
  // decode-spec "-" decodes with the header; otherwise GLIC's do_skip_header
  // mode decodes with (encode-spec + decode-spec) instead of the header.
  static void batch(File tsv, File imgDir, File outDir, PrintStream log) throws Exception {
    BufferedReader r = new BufferedReader(new FileReader(tsv));
    String line;
    int n = 0;
    while ((line = r.readLine()) != null) {
      if (line.isEmpty() || line.startsWith("#")) continue;
      String[] f = line.split("\t");
      String name = f[0], images = f[1];
      long seed = Long.parseLong(f[2]);
      String spec = f[3];
      String decSpec = f.length > 4 ? f[4] : "-";
      File[] imgs = imgDir.listFiles((d, s) -> s.endsWith(".png"));
      Arrays.sort(imgs);
      for (File img : imgs) {
        String stem = img.getName().replace(".png", "");
        if (!images.equals("all") && !("," + images + ",").contains("," + stem + ",")) continue;
        File dir = new File(outDir, stem);
        dir.mkdirs();
        int[] wh = new int[2];
        int[] px = loadArgb(img, wh);

        h.do_skip_header = false;
        h.ccfg = h.new CodecConfig();
        apply(h.ccfg, spec);
        processing.core.PImage pi = h.createImage(wh[0], wh[1], processing.core.PConstants.ARGB);
        pi.pixels = px;
        h.img = pi;
        File glic = new File(dir, name + ".glic");
        h.randomSeed(seed);
        processing.core.PImage enc = h.encode(pi, glic.getAbsolutePath());
        if (enc == null) {
          glic.delete();
          writeText(new File(dir, name + ".encode-fails"), "GLIC encode() returned null\n");
          n++;
          continue;
        }

        // Decode with no source image loaded, so alpha comes out opaque.
        h.img = null;
        h.ccfg = h.new CodecConfig();
        if (!decSpec.equals("-")) {
          apply(h.ccfg, spec);
          apply(h.ccfg, decSpec);
          h.do_skip_header = true;
        }
        h.randomSeed(seed);
        processing.core.PImage dec = h.decode(glic.getAbsolutePath());
        h.do_skip_header = false;
        if (dec == null) {
          writeText(new File(dir, name + ".decode-fails"), "GLIC decode() returned null\n");
        } else {
          savePng(dec.pixels, dec.width, dec.height, new File(dir, name + ".png"));
        }
        n++;
      }
    }
    log.println("java harness: " + n + " outputs");
  }

  static void writeText(File f, String s) throws IOException {
    FileWriter w = new FileWriter(f);
    w.write(s);
    w.close();
  }

  // transform <wavelet> <ttype> <size>: forward and reverse of a fixed matrix, as hex doubles.
  static void transformProbe(int wavelet, int ttype, int size, PrintStream out) throws Throwable {
    jwave.transforms.WaveletTransform t = h.createTransform(ttype, h.createWavelet(wavelet));
    double[][] m = new double[size][size];
    for (int x = 0; x < size; x++)
      for (int y = 0; y < size; y++)
        m[x][y] = (((x * 7919 + y * 104729) % 511) - 255) / 255.0;
    double[][] f = t.forward(m);
    double[][] r = t.reverse(f);
    StringBuilder sb = new StringBuilder();
    for (double[] row : f) for (double d : row) sb.append(Double.toHexString(d)).append(' ');
    out.println(sb.toString().trim());
    sb = new StringBuilder();
    for (double[] row : r) for (double d : row) sb.append(Double.toHexString(d)).append(' ');
    out.println(sb.toString().trim());
  }

  static final String[] WAVELET_CLASSES = new String[68];

  static void dumpWavelets(File out) throws Exception {
    PrintWriter w = new PrintWriter(out);
    for (int id = 1; id < 68; id++) {
      Wavelet wv = h.createWavelet(id);
      w.println("wavelet " + id + " " + wv.getClass().getName() + " mother=" + wv.getMotherWavelength()
          + " transform=" + wv.getTransformWavelength() + " name=" + wv.getName());
      w.println("  sd " + hex(wv.getScalingDeComposition()));
      w.println("  wd " + hex(wv.getWaveletDeComposition()));
      w.println("  sr " + hex(wv.getScalingReConstruction()));
      w.println("  wr " + hex(wv.getWaveletReConstruction()));
    }
    w.close();
  }

  static String hex(double[] a) {
    StringBuilder sb = new StringBuilder();
    for (double d : a) sb.append(Double.toHexString(d)).append(' ');
    return sb.toString().trim();
  }

  @SuppressWarnings("unchecked")
  static void presets(File dir, File out) throws Exception {
    File[] files = dir.listFiles();
    Arrays.sort(files);
    PrintWriter w = new PrintWriter(out, "UTF-8");
    w.println("{");
    boolean first = true;
    for (File f : files) {
      ObjectInputStream ois = new ObjectInputStream(new FileInputStream(f));
      HashMap<String, Object> m = (HashMap<String, Object>) ois.readObject();
      ois.close();
      TreeMap<String, Object> sorted = new TreeMap<>(m);
      if (!first) w.println(",");
      first = false;
      w.print("  " + quote(f.getName()) + ": {");
      boolean fk = true;
      for (Map.Entry<String, Object> e : sorted.entrySet()) {
        if (!fk) w.print(", ");
        fk = false;
        Object v = e.getValue();
        String vs = v instanceof Number || v instanceof Boolean ? v.toString() : quote(String.valueOf(v));
        w.print(quote(e.getKey()) + ": " + vs);
      }
      w.print("}");
    }
    w.println();
    w.println("}");
    w.close();
  }

  static String quote(String s) {
    return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
  }
}
