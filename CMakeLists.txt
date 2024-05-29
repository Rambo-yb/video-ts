cmake_minimum_required(VERSION 2.8)
project(video_ts)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/rk_media)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/rk_lib)

add_executable(test_udp_public_media video_transmit_by_udp.c)
target_link_libraries(test_udp_public_media pthread)
target_link_libraries(test_udp_public_media easymedia drm rockchip_mpp liveMedia groupsock BasicUsageEnvironment UsageEnvironment asound v4l2 rga sqlite3 od_share OpenVX)
