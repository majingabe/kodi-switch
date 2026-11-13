# Horizon OS / Nintendo Switch: GLES rendering through EGL on the default
# libnx NWindow. There is no X11, Wayland or GBM on the console.
list(APPEND PLATFORM_REQUIRED_DEPS OpenGLES EGL)
set(APP_RENDER_SYSTEM gles)

list(APPEND PLATFORM_OPTIONAL_DEPS_EXCLUDE CEC VAAPI VDPAU)
