import std;
import cnetmod.core.time;
import cnetmod.database.datetime;
import cnetmod.database.sql_query_data;
import cnetmod.orm;

#include "test_framework.hpp"

TEST(unix_epoch_round_trips_through_database_datetime)
{
    const auto value = cnetmod::database::datetime_from_unix_seconds(0);
    ASSERT_TRUE(value.has_value());
    ASSERT_EQ(value->year, 1970);
    ASSERT_EQ(value->month, 1);
    ASSERT_EQ(value->day, 1);
    ASSERT_EQ(value->hour, 0);
    ASSERT_EQ(value->minute, 0);
    ASSERT_EQ(value->second, 0);

    const auto seconds = cnetmod::database::unix_seconds_from_datetime(*value);
    ASSERT_TRUE(seconds.has_value());
    ASSERT_EQ(*seconds, 0);
}

TEST(database_datetime_round_trip_is_timezone_independent)
{
    constexpr std::int64_t input = 1'789'633'917;
    const auto value =
        cnetmod::database::datetime_from_unix_seconds(input);
    ASSERT_TRUE(value.has_value());

    const auto output =
        cnetmod::database::unix_seconds_from_datetime(*value);
    ASSERT_TRUE(output.has_value());
    ASSERT_EQ(*output, input);
}

TEST(database_datetime_rejects_invalid_components)
{
    const cnetmod::database::calendar_datetime invalid{
        .year = 2026,
        .month = 2,
        .day = 30,
    };
    ASSERT_FALSE(
        cnetmod::database::unix_seconds_from_datetime(invalid).has_value());

    ASSERT_FALSE(cnetmod::database::datetime_from_unix_seconds(
        std::numeric_limits<std::int64_t>::max())
            .has_value());
}

TEST(optional_database_datetime_preserves_null)
{
    const std::optional<cnetmod::database::calendar_datetime> value;
    ASSERT_FALSE(
        cnetmod::database::unix_seconds_from_datetime(value).has_value());
}

TEST(unix_time_seconds_tracks_system_clock)
{
    const auto before = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
                            .count();
    const auto observed = cnetmod::unix_time_seconds();
    const auto after = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
                           .count();
    ASSERT_TRUE(observed >= before);
    ASSERT_TRUE(observed <= after);
}

TEST(orm_parameter_context_preserves_native_parameter_types)
{
    cnetmod::orm::param_context parameters;
    parameters.set("unsigned", std::uint64_t{42});
    parameters.set("datetime",
        cnetmod::database::calendar_datetime{2026, 9, 17, 8, 31, 57, 0});

    const auto unsigned_value = parameters.get_param("unsigned");
    ASSERT_TRUE(unsigned_value.kind ==
        cnetmod::database::query_parameter::kind_t::uint64_kind);
    ASSERT_EQ(unsigned_value.uint_val, 42U);

    const auto datetime_value = parameters.get_param("datetime");
    ASSERT_TRUE(datetime_value.kind ==
        cnetmod::database::query_parameter::kind_t::datetime_kind);
    ASSERT_EQ(datetime_value.datetime_val.to_string(),
        "2026-09-17 08:31:57");
}

RUN_TESTS()
