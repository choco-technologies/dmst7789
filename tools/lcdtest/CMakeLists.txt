# lcdtest - a manual test of a dmst7789 display; added from the top-level CMakeLists.txt (shares its dmod setup)
set(DMOD_DMR_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lcdtest.dmr)

dmod_add_executable(lcdtest ${DMOD_MODULE_VERSION}
    main.c
)

# dmdrvi: headers only, for the graphics commands of the node (DMDRVI_IOCTL_GFX_*)
dmod_link_modules(lcdtest
    dmdrvi
)

target_include_directories(lcdtest PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
