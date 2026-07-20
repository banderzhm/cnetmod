module cnetmod.protocol.mysql;

import std;
import :orm_auto_fill;

namespace cnetmod::mysql::orm {

auto auto_fill_interceptor::param_to_field_value(const param_value &pv)
    -> field_value {
  switch (pv.kind) {
  case param_value::kind_t::null_kind:
    return field_value::null();
  case param_value::kind_t::int64_kind:
    return field_value::from_int64(pv.int_val);
  case param_value::kind_t::uint64_kind:
    return field_value::from_uint64(pv.uint_val);
  case param_value::kind_t::double_kind:
    return field_value::from_double(pv.double_val);
  case param_value::kind_t::string_kind:
    return field_value::from_string(pv.str_val);
  case param_value::kind_t::blob_kind:
    return field_value::from_blob(pv.str_val);
  case param_value::kind_t::date_kind:
    return field_value::from_date(pv.date_val);
  case param_value::kind_t::datetime_kind:
    return field_value::from_datetime(pv.datetime_val);
  case param_value::kind_t::time_kind:
    return field_value::from_time(pv.time_val);
  default:
    return field_value::null();
  }
}

auto auto_fill_interceptor::generate_value(const auto_fill_config &config)
    -> param_value {
  using namespace std::chrono;
  switch (config.strategy) {
  case fill_strategy::current_timestamp: {
    const auto now = system_clock::now();
    const auto dp = floor<days>(now);
    const auto ymd = year_month_day{dp};
    const auto time = hh_mm_ss{now - dp};
    mysql_datetime dt{
        .year = static_cast<std::uint16_t>(static_cast<int>(ymd.year())),
        .month = static_cast<std::uint8_t>(static_cast<unsigned>(ymd.month())),
        .day = static_cast<std::uint8_t>(static_cast<unsigned>(ymd.day())),
        .hour = static_cast<std::uint8_t>(time.hours().count()),
        .minute = static_cast<std::uint8_t>(time.minutes().count()),
        .second = static_cast<std::uint8_t>(time.seconds().count()),
        .microsecond = 0};
    param_value result;
    result.kind = param_value::kind_t::datetime_kind;
    result.datetime_val = dt;
    return result;
  }
  case fill_strategy::current_date: {
    const auto ymd = year_month_day{floor<days>(system_clock::now())};
    mysql_date date{
        .year = static_cast<std::uint16_t>(static_cast<int>(ymd.year())),
        .month = static_cast<std::uint8_t>(static_cast<unsigned>(ymd.month())),
        .day = static_cast<std::uint8_t>(static_cast<unsigned>(ymd.day()))};
    param_value result;
    result.kind = param_value::kind_t::date_kind;
    result.date_val = date;
    return result;
  }
  case fill_strategy::current_time: {
    const auto now = system_clock::now();
    const auto time = hh_mm_ss{now - floor<days>(now)};
    mysql_time value{
        .negative = false,
        .hours = static_cast<std::uint32_t>(time.hours().count()),
        .minutes = static_cast<std::uint8_t>(time.minutes().count()),
        .seconds = static_cast<std::uint8_t>(time.seconds().count()),
        .microsecond = 0};
    param_value result;
    result.kind = param_value::kind_t::time_kind;
    result.time_val = value;
    return result;
  }
  case fill_strategy::uuid: {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::uint64_t> dis;
    return param_value::from_string(std::format(
        "{:08x}-{:04x}-4{:03x}-{:04x}-{:012x}", dis(gen) & 0xFFFFFFFF,
        (dis(gen) >> 32) & 0xFFFF, dis(gen) & 0xFFF,
        (dis(gen) & 0x3FFF) | 0x8000, dis(gen) & 0xFFFFFFFFFFFF));
  }
  case fill_strategy::custom:
    return config.custom_handler ? config.custom_handler()
                                 : param_value::null();
  }
  return param_value::null();
}

auto global_auto_fill_interceptor() -> auto_fill_interceptor & {
  static auto_fill_interceptor instance;
  return instance;
}

} // namespace cnetmod::mysql::orm
