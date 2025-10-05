-- Create rotation enum type
CREATE TYPE rotation AS ENUM ('ROTATE_0', 'ROTATE_90', 'ROTATE_180', 'ROTATE_270');

-- Add rotation column with default value for existing devices
ALTER TABLE device_states ADD COLUMN rotation rotation NOT NULL DEFAULT 'ROTATE_0';
