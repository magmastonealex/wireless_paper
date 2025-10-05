-- Add display_type enum
CREATE TYPE display_type AS ENUM (
    'EPD_TYPE_GDEY029T71H',
    'EPD_TYPE_GDEM035F51',
    'EPD_TYPE_GDEY029F51',
    'EPD_TYPE_GDEM075F52',
    'EPD_TYPE_WS_75_V2B'
);

-- Add image_url column (nullable to allow existing devices)
ALTER TABLE device_states ADD COLUMN image_url VARCHAR;

-- Add display_type column (nullable to allow existing devices)
ALTER TABLE device_states ADD COLUMN display_type display_type;
