use std::fs::File;
use std::io::BufReader;
use std::path::Path;

/// Crop rectangle in source image coordinates.
#[derive(Debug, Clone, Copy)]
pub struct CropBox {
    pub x: u32,
    pub y: u32,
    pub w: u32,
    pub h: u32,
}

/// Decoded raw RGBA8 image buffer.
#[derive(Debug, Clone)]
pub struct RgbaImage {
    pub width: u32,
    pub height: u32,
    pub data: Vec<u8>,
}

/// Premultiplied Cairo ARGB32 frame (little-endian BGRA bytes in memory).
#[derive(Debug, Clone)]
pub struct RenderedFrame {
    pub width: i32,
    pub height: i32,
    pub stride: i32,
    pub data: Vec<u8>,
}

impl RgbaImage {
    pub fn new(width: u32, height: u32) -> Self {
        Self {
            width,
            height,
            data: vec![0u8; (width as usize) * (height as usize) * 4],
        }
    }

    /// Loads any PNG (RGBA, RGB, Gray, GrayAlpha, Indexed+tRNS) into straight RGBA8.
    pub fn load_png(path: &Path) -> Result<Self, String> {
        let file = File::open(path).map_err(|e| format!("open {}: {e}", path.display()))?;
        let mut decoder = png::Decoder::new(BufReader::new(file));
        decoder.set_transformations(png::Transformations::EXPAND | png::Transformations::STRIP_16);
        let mut reader = decoder
            .read_info()
            .map_err(|e| format!("png header {}: {e}", path.display()))?;
        let mut buf = vec![0u8; reader.output_buffer_size()];
        let info = reader
            .next_frame(&mut buf)
            .map_err(|e| format!("png decode {}: {e}", path.display()))?;
        let raw = &buf[..info.buffer_size()];
        let w = info.width;
        let h = info.height;
        let pixels = (w as usize) * (h as usize);
        let mut rgba = Vec::with_capacity(pixels * 4);

        match info.color_type {
            png::ColorType::Rgba => rgba.extend_from_slice(&raw[..pixels * 4]),
            png::ColorType::Rgb => {
                for px in raw.chunks_exact(3).take(pixels) {
                    rgba.extend_from_slice(&[px[0], px[1], px[2], 255]);
                }
            }
            png::ColorType::GrayscaleAlpha => {
                for px in raw.chunks_exact(2).take(pixels) {
                    rgba.extend_from_slice(&[px[0], px[0], px[0], px[1]]);
                }
            }
            png::ColorType::Grayscale => {
                for &g in raw.iter().take(pixels) {
                    rgba.extend_from_slice(&[g, g, g, 255]);
                }
            }
            png::ColorType::Indexed => {
                return Err(format!("unexpected unexpanded indexed png: {}", path.display()));
            }
        }
        Ok(Self {
            width: w,
            height: h,
            data: rgba,
        })
    }

    /// Porter-Duff straight-alpha "source over" onto self at (0, 0).
    /// Skips tiny placeholder assets (e.g. 1x1 blank layers in Just Natsuki).
    pub fn composite_over(&mut self, overlay: &RgbaImage) {
        if overlay.width <= 1 || overlay.height <= 1 {
            return;
        }
        let copy_w = self.width.min(overlay.width) as usize;
        let copy_h = self.height.min(overlay.height) as usize;
        let dst_w = self.width as usize;
        let src_w = overlay.width as usize;

        for y in 0..copy_h {
            let dst_row = &mut self.data[y * dst_w * 4..(y * dst_w + copy_w) * 4];
            let src_row = &overlay.data[y * src_w * 4..(y * src_w + copy_w) * 4];
            for (d, s) in dst_row.chunks_exact_mut(4).zip(src_row.chunks_exact(4)) {
                let sa = s[3] as u32;
                if sa == 0 {
                    continue;
                }
                if sa == 255 || d[3] == 0 {
                    d.copy_from_slice(s);
                    continue;
                }
                let da = d[3] as u32;
                let inv_sa = 255 - sa;
                let out_a = sa * 255 + da * inv_sa; // scaled by 255
                if out_a == 0 {
                    d.copy_from_slice(&[0, 0, 0, 0]);
                    continue;
                }
                for c in 0..3 {
                    let premul = (s[c] as u32) * sa * 255 + (d[c] as u32) * da * inv_sa;
                    d[c] = ((premul + (out_a >> 1)) / out_a).min(255) as u8;
                }
                d[3] = ((out_a + 127) / 255).min(255) as u8;
            }
        }
    }

    /// Crops `crop` region (clamped safely) and bilinear/area-downsamples to `target_height`,
    /// emitting premultiplied Cairo ARGB32 bytes (`[B, G, R, A]` on little-endian).
    pub fn crop_resize_to_cairo_argb32(&self, crop: CropBox, target_height: i32) -> RenderedFrame {
        let th = target_height.max(64) as u32;
        let cx0 = crop.x.min(self.width.saturating_sub(1));
        let cy0 = crop.y.min(self.height.saturating_sub(1));
        let cw = crop.w.min(self.width - cx0).max(1);
        let ch = crop.h.min(self.height - cy0).max(1);
        let tw = (((cw as f64) * (th as f64) / (ch as f64)).round() as u32).max(1);

        let stride = (tw * 4) as i32;
        let mut out = vec![0u8; (tw * th * 4) as usize];

        let scale_x = (cw as f64) / (tw as f64);
        let scale_y = (ch as f64) / (th as f64);
        let sw = self.width as usize;

        for dy in 0..th {
            // Area-box average over source pixel footprint + bilinear tap keeps fine anime lineart sharp
            let sy0_f = (cy0 as f64) + (dy as f64) * scale_y;
            let sy1_f = (cy0 as f64) + ((dy + 1) as f64) * scale_y;
            let sy_start = (sy0_f.floor() as u32).min(self.height - 1);
            let sy_end = (sy1_f.ceil() as u32).clamp(sy_start + 1, self.height);

            for dx in 0..tw {
                let sx0_f = (cx0 as f64) + (dx as f64) * scale_x;
                let sx1_f = (cx0 as f64) + ((dx + 1) as f64) * scale_x;
                let sx_start = (sx0_f.floor() as u32).min(self.width - 1);
                let sx_end = (sx1_f.ceil() as u32).clamp(sx_start + 1, self.width);

                let mut acc_r = 0.0f64;
                let mut acc_g = 0.0f64;
                let mut acc_b = 0.0f64;
                let mut acc_a = 0.0f64;
                let mut weight_sum = 0.0f64;

                for sy in sy_start..sy_end {
                    let wy = ((sy as f64 + 1.0).min(sy1_f) - (sy as f64).max(sy0_f)).max(0.0);
                    if wy <= 0.0 {
                        continue;
                    }
                    let row_off = (sy as usize) * sw;
                    for sx in sx_start..sx_end {
                        let wx = ((sx as f64 + 1.0).min(sx1_f) - (sx as f64).max(sx0_f)).max(0.0);
                        let w = wx * wy;
                        if w <= 0.0 {
                            continue;
                        }
                        let idx = (row_off + sx as usize) * 4;
                        let r = self.data[idx] as f64;
                        let g = self.data[idx + 1] as f64;
                        let b = self.data[idx + 2] as f64;
                        let a = (self.data[idx + 3] as f64) / 255.0;
                        // Accumulate in premultiplied light so transparent fringes never bleed dark halos!
                        acc_r += r * a * w;
                        acc_g += g * a * w;
                        acc_b += b * a * w;
                        acc_a += self.data[idx + 3] as f64 * w;
                        weight_sum += w;
                    }
                }

                if weight_sum > 0.0 {
                    let pr = (acc_r / weight_sum).round().clamp(0.0, 255.0) as u8;
                    let pg = (acc_g / weight_sum).round().clamp(0.0, 255.0) as u8;
                    let pb = (acc_b / weight_sum).round().clamp(0.0, 255.0) as u8;
                    let pa = (acc_a / weight_sum).round().clamp(0.0, 255.0) as u8;
                    let o = ((dy * tw + dx) * 4) as usize;
                    // Cairo ARGB32 on little-endian stores [B, G, R, A]
                    out[o] = pb.min(pa);
                    out[o + 1] = pg.min(pa);
                    out[o + 2] = pr.min(pa);
                    out[o + 3] = pa;
                }
            }
        }

        RenderedFrame {
            width: tw as i32,
            height: th as i32,
            stride,
            data: out,
        }
    }
}

