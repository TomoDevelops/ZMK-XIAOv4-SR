if(CONFIG_SKREE_DIAGNOSTICS)
  zephyr_library()
  zephyr_library_sources(src/skree_diagnostics.c)
endif()
