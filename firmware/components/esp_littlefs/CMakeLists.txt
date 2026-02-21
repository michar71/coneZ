idf_component_register(
    SRCS
        "src/esp_littlefs.c"
        "src/lfs_config.c"
        "src/littlefs_esp_part.c"
        "src/littlefs/lfs.c"
        "src/littlefs/lfs_util.c"
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "src"
    REQUIRES
        esp_partition spi_flash vfs
    PRIV_REQUIRES
        esptool_py
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE -DLFS_CONFIG=lfs_config.h)
