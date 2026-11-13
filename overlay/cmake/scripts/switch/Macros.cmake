# Switch-specific CMake macros.
#
# Horizon OS has no dynamic loader, so Kodi's DVD wrapper (.so) cannot exist.
# The DVD navigation libraries are still built/available at configure time, but
# the shared wrapper is skipped. DVD playback is a later, static-link task.
function(core_link_library lib wraplib)
  message(STATUS "Switch: skipping shared wrapper ${wraplib} (${lib})")
endfunction()
