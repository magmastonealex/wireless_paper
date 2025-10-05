use std::time::Duration;
use anyhow::{anyhow, Result};
use reqwest::Client;
use image::{GenericImageView, ImageReader};
use crate::types::{DisplayType, PixelFormat, Rotation};

pub struct ImageFetcher {
    client: Client,
}

impl ImageFetcher {
    pub fn new() -> Self {
        let client = Client::builder()
            .timeout(Duration::from_secs(30))
            .user_agent("epaper-server/0.1.0")
            .build()
            .expect("Failed to create HTTP client");

        Self { client }
    }

    async fn fetch_png(&self, url: &str) -> Result<Vec<u8>> {
        let response = self.client.get(url).send().await
            .map_err(|e| anyhow!("Failed to fetch URL {}: {}", url, e))?;

        if !response.status().is_success() {
            return Err(anyhow!("HTTP error {}: {}", response.status(), url));
        }

        let content_type = response.headers()
            .get("content-type")
            .and_then(|ct| ct.to_str().ok())
            .unwrap_or("");

        if !content_type.starts_with("image/png") && !content_type.starts_with("image/") {
            return Err(anyhow!("Invalid content type '{}' for URL {}", content_type, url));
        }

        let bytes = response.bytes().await
            .map_err(|e| anyhow!("Failed to read response body from {}: {}", url, e))?;

        if bytes.len() > 10 * 1024 * 1024 {
            return Err(anyhow!("Image too large: {} bytes from {}", bytes.len(), url));
        }

        if bytes.len() < 8 {
            return Err(anyhow!("Image too small: {} bytes from {}", bytes.len(), url));
        }

        let png_signature = &[0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A];
        if !bytes.starts_with(png_signature) {
            return Err(anyhow!("Not a valid PNG file from {}", url));
        }

        Ok(bytes.to_vec())
    }

    // 2 bit per pixel, white (01), black (00), yellow (10), red (11).
    fn png_to_2bpp_wryk(&self, png_data: &[u8], width: u32, height: u32, rotation: &Rotation) -> Result<Vec<u8>> {
        let img = ImageReader::new(std::io::Cursor::new(png_data))
            .with_guessed_format()
            .map_err(|e| anyhow!("Failed to read image format: {}", e))?
            .decode()
            .map_err(|e| anyhow!("Failed to decode PNG: {}", e))?;

        let img = match rotation {
            Rotation::ROTATE_0 => img,
            Rotation::ROTATE_90 => img.rotate90(),
            Rotation::ROTATE_180 => img.rotate180(),
            Rotation::ROTATE_270 => img.rotate270(),
        };

        let (real_width, real_height) = img.dimensions();
        if width != real_width || real_height != height {
            return Err(anyhow!("Image dimensions ({}x{}) do not match expected ({}x{})", real_width, real_height, width, height));
        }

        let resized_img = img.resize_exact(width, height, image::imageops::FilterType::Lanczos3);
        let rgb_img = resized_img.to_rgb8();

        // Output size: 2 bits per pixel = (width * height) / 4 bytes
        let mut raw_data = Vec::with_capacity(((width * height) / 4) as usize);

        // Process 4 pixels at a time to pack into one byte
        for row in 0..height {
            let mut col = 0;
            while col < width {
                let mut byte = 0u8;

                // Pack 4 pixels into one byte (2 bits each)
                for pixel_in_byte in 0..4 {
                    if col + pixel_in_byte >= width {
                        // If we don't have 4 pixels remaining, pad with white (01)
                        byte |= 0b01 << (6 - pixel_in_byte * 2);
                        continue;
                    }

                    let pixel = rgb_img.get_pixel(col + pixel_in_byte, row);
                    let r = pixel[0];
                    let g = pixel[1];
                    let b = pixel[2];

                    // Determine color based on RGB values
                    let color_bits = if r == 0xFF && g == 0x00 && b == 0x00 {
                        // Pure red (#ff0000) -> 11
                        0b11
                    } else if r == 0xFF && g == 0xFF && b == 0x00 {
                        // Pure yellow (#ffff00) -> 10
                        0b10
                    } else {
                        // Calculate luma: Y = 0.299*R + 0.587*G + 0.114*B
                        let luma = (0.299 * r as f32 + 0.587 * g as f32 + 0.114 * b as f32) as u8;

                        if luma > 127 {
                            // White -> 01
                            0b01
                        } else {
                            // Black -> 00
                            0b00
                        }
                    };

                    // Pack the 2-bit color value into the byte
                    // First pixel goes in bits 7-6, second in 5-4, third in 3-2, fourth in 1-0
                    byte |= color_bits << (6 - pixel_in_byte * 2);
                }

                raw_data.push(byte);
                col += 4;
            }
        }

        assert!(raw_data.len() == ((width * height) / 4) as usize);

        Ok(raw_data)
    }

    fn png_to_1bit(&self, png_data: &[u8], width: u32, height: u32, rotation: &Rotation) -> Result<Vec<u8>> {
        let img = ImageReader::new(std::io::Cursor::new(png_data))
            .with_guessed_format()
            .map_err(|e| anyhow!("Failed to read image format: {}", e))?
            .decode()
            .map_err(|e| anyhow!("Failed to decode PNG: {}", e))?;
        let img = match rotation {
            Rotation::ROTATE_0 => img,
            Rotation::ROTATE_90 => img.rotate90(),
            Rotation::ROTATE_180 => img.rotate180(),
            Rotation::ROTATE_270 => img.rotate270(),
        };
        
        let (real_width, real_height) = img.dimensions();
        if width != real_width || real_height != height {
            return Err(anyhow!("Image dimensions ({}x{}) do not match expected ({}x{})", real_width, real_height, width, height));
        }
        
        let resized_img = img.resize_exact(width, height, image::imageops::FilterType::Lanczos3);
        let gray_img = resized_img.to_luma8();

        let mut raw_data = Vec::with_capacity((width * height / 8) as usize);
        let pixels = gray_img.as_raw();

        for row in 0..height {
            let mut byte = 0u8;
            for col in 0..width {
                let pixel_idx = (row * width + col) as usize;
                let pixel_value = pixels[pixel_idx];

                let bit_value = if pixel_value > 127 { 0 } else { 1 };

                let bit_position = 7 - (col % 8);
                byte |= bit_value << bit_position;

                if col % 8 == 7 || col == width - 1 {
                    raw_data.push(byte);
                    byte = 0;
                }
            }
        }

        Ok(raw_data)
    }

    pub async fn fetch_and_convert_for_display(&self, url: &str, display_type: &DisplayType, rotation: &Rotation) -> Result<Vec<u8>> {
        let (width, height) = display_type.get_display_dimensions();
        let pixfmt = display_type.get_pixel_format();
        let png_data = self.fetch_png(url).await?;
        println!("Display: {:?}, {:?}, {:?}", width, height, pixfmt);
        match pixfmt {
            PixelFormat::Kw1Bit => {
                self.png_to_1bit(&png_data, width, height, rotation)
            },
            PixelFormat::Rykw2Bit => {
                self.png_to_2bpp_wryk(&png_data, width, height, rotation)
            }
        }
    }
}


impl Default for ImageFetcher {
    fn default() -> Self {
        Self::new()
    }
}
