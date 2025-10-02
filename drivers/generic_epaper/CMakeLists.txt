#This seems strange. No one else needs to define a named library and manually link it in CMakeLists.txt.
# But without this, we will never seem to link this library. Not sure why. 
zephyr_library_named("generic_epaper")
zephyr_library_sources_ifdef(CONFIG_GENERIC_EPAPER generic_epaper.c)

zephyr_library_add_dependencies(offsets_h)