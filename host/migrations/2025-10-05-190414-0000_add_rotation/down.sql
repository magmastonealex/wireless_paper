-- Remove rotation column
ALTER TABLE device_states DROP COLUMN rotation;

-- Drop rotation enum type
DROP TYPE rotation;
