# Chromium for Ozone Wayland.
The goal of this project is to enable Chromium browser to run on Wayland and X11 using
Chromium Ozone platform abstraction layer.
# Building Linux/Ozone for Wayland
To build chrome, certain steps should be performed:
1) Configure gn args by 

   > gn args out/Ozone
   
   The arguments should be the following:
   ```
   use_ozone = true

   ozone_platform_x11 = true
   ozone_platform_gbm = false
   ozone_platform_wayland = true

   enable_package_mash_services = true
   use_xkbcommon = true
   use_sysroot = false
   treat_warnings_as_errors = false
   enable_nacl = false

   is_clang=false

   remove_webcore_debug_symbols = true
   ```
2) Build Chromium

   > ninja -C out/Ozone chrome
  
   You can specify amount of threads to be used for compiling. For example:
   > -j8
