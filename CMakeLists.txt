if(CONFIG_SHIELD_SKREECUSTOM_LEFT OR CONFIG_SHIELD_SKREECUSTOM_RIGHT)
  if(NOT CONFIG_ZMK_KSCAN_MATRIX_POLLING)
    message(FATAL_ERROR
      "Skree's SPI shift register requires matrix polling. Check that config/skreecustom.conf was loaded.")
  endif()
endif()

if(CONFIG_SKREE_DIAGNOSTICS)
  zephyr_library()
  zephyr_library_sources(src/skree_diagnostics.c)
endif()
