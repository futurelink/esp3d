library(camera)

set(COMPONENT_SRCS
        conversions/yuv.c conversions/to_jpg.cpp conversions/to_bmp.c conversions/jpge.cpp
        conversions/esp_jpg_decode.c)

if(IDF_TARGET STREQUAL "esp32" OR IDF_TARGET STREQUAL "esp32s2" OR IDF_TARGET STREQUAL "esp32s3")
    list(APPEND COMPONENT_SRCS
            driver/esp_camera.c driver/cam_hal.c driver/sccb.c driver/sensor.c
            sensors/ov2640.c sensors/ov3660.c sensors/ov5640.c sensors/ov7725.c sensors/ov7670.c sensors/nt99141.c
            sensors/gc0308.c sensors/gc2145.c sensors/gc032a.c sensors/bf3005.c sensors/bf20a6.c sensors/sc101iot.c
            sensors/sc030iot.c sensors/sc031gs.c)

    list(APPEND COMPONENT_PRIV_INCLUDEDIRS
            driver/private_include sensors/private_include target/private_include)

    if(IDF_TARGET STREQUAL "esp32")
        list(APPEND COMPONENT_SRCS
                target/xclk.c target/esp32/ll_cam.c)
    endif()

endif()
