use std::time::Duration;
use anyhow::{anyhow, Result};
use reqwest::Client;
use image::{GenericImageView, ImageReader};
use crate::types::DisplayType;

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

    fn png_to_1bit(&self, png_data: &[u8], width: u32, height: u32) -> Result<Vec<u8>> {
        let img = ImageReader::new(std::io::Cursor::new(png_data))
            .with_guessed_format()
            .map_err(|e| anyhow!("Failed to read image format: {}", e))?
            .decode()
            .map_err(|e| anyhow!("Failed to decode PNG: {}", e))?;

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

    pub async fn fetch_and_convert(&self, url: &str, width: u32, height: u32) -> Result<Vec<u8>> {
        let png_data = self.fetch_png(url).await?;
        self.png_to_1bit(&png_data, width, height)
    }

    pub async fn fetch_and_convert_for_display(&self, url: &str, display_type: &DisplayType) -> Result<Vec<u8>> {
        let (width, height) = get_display_dimensions(display_type);
        self.fetch_and_convert(url, width, height).await
    }
}

fn get_display_dimensions(display_type: &DisplayType) -> (u32, u32) {
    match display_type {
        DisplayType::EPD_TYPE_GDEY029T71H => (296, 128),  // 2.9" B/W
        DisplayType::EPD_TYPE_GDEM035F51 => (240, 416),   // 3.5" 4-color
        DisplayType::EPD_TYPE_GDEY029F51 => (296, 128),   // 2.9" 4-color
        DisplayType::EPD_TYPE_GDEM075F52 => (800, 480),   // 7.5" 4-color
        DisplayType::EPD_TYPE_WS_75_V2B => (800, 480),    // 7.5" 2-color + red
    }
}

impl Default for ImageFetcher {
    fn default() -> Self {
        Self::new()
    }
}
