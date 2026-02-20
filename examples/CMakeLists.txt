# ==============================================================================
# cnetmod examples
# ==============================================================================

# MSVC Debug 模式下，/RTC1（运行时检测）与 import std; 的模板实例化叠加，
# 单 TU 对象文件会轻易超4GB（C1605）。
# 在本子目录作用域内移除 /RTC1 并将 /Ob0 替换为 /Ob1：
#   - /Ob1：允许内联标记函数，大幅压缩 std 小工具函数的个别函数体
#   - 去 /RTC1：移除每个变量访问的检测截面代码
# 注：该变量修改仅影响本子目录创建的 target，不会回传到父目录
# 注：调试符号（/Zi）和调试运行库（/MDd）仍然保留
if(MSVC)
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Ob0"  "/Ob1" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

set(EXAMPLE_TARGETS
    echo_server
    async_file
    timer_demo
    channel_demo
    mutex_demo
    stdexec_bridge
    redis_client
    mysql_crud
    mysql_orm
    http_demo
    ws_demo
    hight_http
    hight_ws
    hight_plus_http
    serial_port
    multicore_http
    multicore_ws
    blocking_bridge_demo
        mqtt_demo
)

# SSL 示例（仅在 OpenSSL 可用时构建）
if(CNETMOD_HAS_SSL)
    list(APPEND EXAMPLE_TARGETS ssl_echo_server)
endif()

foreach(example ${EXAMPLE_TARGETS})
    add_executable(example_${example} ${example}.cpp)
    target_link_libraries(example_${example} PRIVATE ${CNETMOD_LINK_TARGET})
    set_property(TARGET example_${example} PROPERTY
        CXX_MODULE_GENERATION_MODE "SEPARATE"
    )
    if(MSVC)
        # 关闭调试信息 + 禁用 LTCG + 最小化代码，避免 MSVC C1605（对象文件超4GB）
        # /O1 追加在 AdditionalOptions 末尾，覆盖 MSBuild 生成的 /Od
        # /RTC1 已在本子目录顶部通过 string(REPLACE) 移除，故 /O1 不与 /RTC1 冲突
        set_property(TARGET example_${example} PROPERTY
            MSVC_DEBUG_INFORMATION_FORMAT "")
        target_compile_options(example_${example} PRIVATE /bigobj /Zm800 /EHsc /GL- /O1)
        target_link_options(example_${example} PRIVATE /LTCG:OFF /DEBUG:NONE)
    endif()
endforeach()