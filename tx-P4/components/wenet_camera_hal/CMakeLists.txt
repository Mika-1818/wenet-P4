idf_component_register(SRCS "ov5647/wenet_camera_ov5647.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_cam esp_driver_jpeg)
