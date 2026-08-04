include_guard(GLOBAL)

# Optional protocol modules.  Keep this registry in one place so adding a
# protocol does not spread source-selection logic throughout the root project.
# src/protocol/impl is deliberately absent: it provides the shared TCP/UDP
# transports used by several protocols and is part of cnetmod's core runtime.
set(CNETMOD_PROTOCOLS
    AMQP091
    AMQP10
    COAP
    DNS
    GRPC
    HTTP
    KAFKA
    MAIL
    MODBUS
    MONGODB
    MQTT
    MYSQL
    OPENAI
    POSTGRESQL
    QUIC
    RAFT
    REDIS
    SOCKS5
    WEBSOCKET
)

set(CNETMOD_PROTOCOL_AMQP091_DIRECTORY "amqp091")
set(CNETMOD_PROTOCOL_AMQP10_DIRECTORY "amqp10")
set(CNETMOD_PROTOCOL_COAP_DIRECTORY "coap")
set(CNETMOD_PROTOCOL_DNS_DIRECTORY "dns")
set(CNETMOD_PROTOCOL_GRPC_DIRECTORY "grpc")
set(CNETMOD_PROTOCOL_HTTP_DIRECTORY "http")
set(CNETMOD_PROTOCOL_KAFKA_DIRECTORY "kafka")
set(CNETMOD_PROTOCOL_MAIL_DIRECTORY "mail")
set(CNETMOD_PROTOCOL_MODBUS_DIRECTORY "modbus")
set(CNETMOD_PROTOCOL_MONGODB_DIRECTORY "mongodb")
set(CNETMOD_PROTOCOL_MQTT_DIRECTORY "mqtt")
set(CNETMOD_PROTOCOL_MYSQL_DIRECTORY "mysql")
set(CNETMOD_PROTOCOL_OPENAI_DIRECTORY "openai")
set(CNETMOD_PROTOCOL_POSTGRESQL_DIRECTORY "postgresql")
set(CNETMOD_PROTOCOL_RAFT_DIRECTORY "raft")
set(CNETMOD_PROTOCOL_REDIS_DIRECTORY "redis")
set(CNETMOD_PROTOCOL_SOCKS5_DIRECTORY "socks5")
set(CNETMOD_PROTOCOL_WEBSOCKET_DIRECTORY "websocket")
set(CNETMOD_PROTOCOL_QUIC_DIRECTORY "quic")

# Direct C++ module dependencies.  PostgreSQL intentionally has no MySQL
# dependency: database-neutral ORM contracts live under src/orm.
set(CNETMOD_PROTOCOL_GRPC_DEPENDS HTTP)
set(CNETMOD_PROTOCOL_MQTT_DEPENDS HTTP WEBSOCKET)
set(CNETMOD_PROTOCOL_OPENAI_DEPENDS HTTP)
set(CNETMOD_PROTOCOL_WEBSOCKET_DEPENDS HTTP)
set(CNETMOD_PROTOCOL_DNS_DEPENDS HTTP)
set(CNETMOD_PROTOCOL_QUIC_DEPENDS HTTP)

option(CNETMOD_ENABLE_ORM
    "Build the protocol-neutral SQL ORM and XML mapper support"
    ON)

option(CNETMOD_ENABLE_ALL_PROTOCOLS
    "Default value for individual CNETMOD_ENABLE_<PROTOCOL> options"
    ON)

foreach(protocol IN LISTS CNETMOD_PROTOCOLS)
    option(CNETMOD_ENABLE_${protocol}
        "Build the ${protocol} protocol module"
        ${CNETMOD_ENABLE_ALL_PROTOCOLS})
endforeach()

function(cnetmod_validate_protocol_selection)
    foreach(protocol IN LISTS CNETMOD_PROTOCOLS)
        if(NOT CNETMOD_ENABLE_${protocol})
            continue()
        endif()

        foreach(dependency IN LISTS CNETMOD_PROTOCOL_${protocol}_DEPENDS)
            if(NOT CNETMOD_ENABLE_${dependency})
                message(FATAL_ERROR
                    "CNETMOD_ENABLE_${protocol}=ON requires "
                    "CNETMOD_ENABLE_${dependency}=ON. Disable ${protocol} too, "
                    "or enable its ${dependency} module dependency.")
            endif()
        endforeach()
    endforeach()
endfunction()

# Remove every interface, module implementation, and regular implementation
# belonging to a disabled protocol.  Matching relative paths also covers root
# umbrella modules such as src/protocol/mongodb.cppm.
function(cnetmod_filter_disabled_protocol_sources)
    set(multi_value_args VARIABLES)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "" "${multi_value_args}")

    foreach(source_list_variable IN LISTS ARG_VARIABLES)
        set(filtered_sources)
        foreach(source IN LISTS ${source_list_variable})
            file(RELATIVE_PATH relative_source
                "${CMAKE_CURRENT_SOURCE_DIR}"
                "${source}")
            cmake_path(CONVERT "${relative_source}" TO_CMAKE_PATH_LIST relative_source)

            set(protocol_source_enabled ON)
            foreach(protocol IN LISTS CNETMOD_PROTOCOLS)
                if(CNETMOD_ENABLE_${protocol})
                    continue()
                endif()

                set(protocol_directory "${CNETMOD_PROTOCOL_${protocol}_DIRECTORY}")
                if(relative_source MATCHES
                    "^src/protocol/${protocol_directory}(/|\\.(cppm|ixx|cpp)$)")
                    set(protocol_source_enabled OFF)
                    break()
                endif()
            endforeach()

            if(protocol_source_enabled)
                list(APPEND filtered_sources "${source}")
            endif()
        endforeach()
        set(${source_list_variable} "${filtered_sources}" PARENT_SCOPE)
    endforeach()
endfunction()

function(cnetmod_filter_disabled_component_sources)
    set(multi_value_args VARIABLES)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "" "${multi_value_args}")
    if(CNETMOD_ENABLE_ORM)
        return()
    endif()
    foreach(source_list_variable IN LISTS ARG_VARIABLES)
        set(filtered_sources "${${source_list_variable}}")
        list(FILTER filtered_sources EXCLUDE REGEX "[/\\\\]src[/\\\\]orm([/\\\\]|\\.(cppm|ixx|cpp)$)")
        list(FILTER filtered_sources EXCLUDE REGEX "[/\\\\]src[/\\\\]protocol[/\\\\](mysql[/\\\\]orm|postgresql[/\\\\]postgresql_orm)($|[/\\\\]|\\.(cppm|ixx|cpp)$)")
        set(${source_list_variable} "${filtered_sources}" PARENT_SCOPE)
    endforeach()
endfunction()

cnetmod_validate_protocol_selection()
