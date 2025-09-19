-- add the vbat column to track battery voltage of the device over time.
ALTER TABLE device_states ADD COLUMN "vbat_mv" INTEGER NOT NULL DEFAULT 1500;