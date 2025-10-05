-- Remove the columns
ALTER TABLE device_states DROP COLUMN display_type;
ALTER TABLE device_states DROP COLUMN image_url;

-- Drop the enum type
DROP TYPE display_type;
