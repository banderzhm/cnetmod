export module cnetmod.protocol.mysql:orm_generator;

import std;
import :types;
import :client;
import cnetmod.coro.task;

namespace cnetmod::mysql::orm {

// =============================================================================
// table_column — Database column information
// =============================================================================

export struct table_column {
    std::string name;
    std::string type;
    bool is_nullable = false;
    bool is_primary_key = false;
    bool is_auto_increment = false;
    std::string default_value;
    std::string comment;
};

// =============================================================================
// table_info — Database table information
// =============================================================================

export struct table_info {
    std::string name;
    std::string comment;
    std::vector<table_column> columns;
};

// =============================================================================
// generator_config — Code generator configuration
// =============================================================================

export struct generator_config {
    std::string output_dir = "./generated";
    std::string namespace_name = "generated";
    bool generate_mapper = true;
    bool generate_xml = true;
    bool generate_service = false;
    std::string table_prefix = "";  // Remove prefix from class names
};

// =============================================================================
// code_generator — Generate C++ code from database tables
// =============================================================================

export class code_generator {
public:
    explicit code_generator(generator_config config = {})
        : config_(std::move(config)) {}

    /// Fetch table information from database
    static auto fetch_table_info(client& cli, std::string_view table_name) -> task<std::expected<table_info, std::string>> {
        table_info info;
        info.name = table_name;

        // Query table comment
        std::string comment_sql = std::format(
            "SELECT TABLE_COMMENT FROM information_schema.TABLES "
            "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '{}'", table_name);

        auto comment_rs = co_await cli.execute(comment_sql);
        if (!comment_rs.is_err() && !comment_rs.rows.empty()) {
            auto& comment_field = comment_rs.rows[0][0];
            if (!comment_field.is_null() && comment_field.is_string()) {
                info.comment = std::string(comment_field.get_string());
            }
        }

        // Query columns
        std::string columns_sql = std::format(
            "SELECT COLUMN_NAME, COLUMN_TYPE, IS_NULLABLE, COLUMN_KEY, EXTRA, COLUMN_DEFAULT, COLUMN_COMMENT "
            "FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '{}' "
            "ORDER BY ORDINAL_POSITION", table_name);

        auto columns_rs = co_await cli.execute(columns_sql);
        if (columns_rs.is_err()) {
            co_return std::unexpected(columns_rs.error_msg);
        }

        for (auto& row : columns_rs.rows) {
            table_column col;

            // COLUMN_NAME (always string)
            if (row[0].is_string()) {
                col.name = std::string(row[0].get_string());
            }

            // COLUMN_TYPE (always string)
            if (row[1].is_string()) {
                col.type = std::string(row[1].get_string());
            }

            // IS_NULLABLE (YES/NO)
            if (row[2].is_string()) {
                auto nullable_str = row[2].get_string();
                col.is_nullable = (nullable_str == "YES");
            }

            // COLUMN_KEY (PRI/UNI/MUL or empty)
            if (row[3].is_string()) {
                auto key_str = row[3].get_string();
                col.is_primary_key = (key_str == "PRI");
            }

            // EXTRA (auto_increment, etc.)
            if (row[4].is_string()) {
                auto extra_str = row[4].get_string();
                col.is_auto_increment = (extra_str.find("auto_increment") != std::string::npos);
            }

            // COLUMN_DEFAULT (can be NULL)
            if (!row[5].is_null() && row[5].is_string()) {
                col.default_value = std::string(row[5].get_string());
            }

            // COLUMN_COMMENT (can be NULL)
            if (!row[6].is_null() && row[6].is_string()) {
                col.comment = std::string(row[6].get_string());
            }

            info.columns.push_back(std::move(col));
        }

        co_return info;
    }

    /// Generate model class code
    auto generate_model(const table_info& table) const -> std::string {
        std::string class_name = to_pascal_case(remove_prefix(table.name));
        std::string code;

        // Header comment
        code += std::format("// Generated from table: {}\n", table.name);
        if (!table.comment.empty()) {
            code += std::format("// {}\n", table.comment);
        }
        code += "\n";

        // Includes
        code += "#include <cnetmod/orm.hpp>\n";
        code += "import std;\n";
        code += "import cnetmod.protocol.mysql;\n\n";

        // Namespace
        code += std::format("namespace {} {{\n\n", config_.namespace_name);

        // Struct definition
        code += std::format("struct {} {{\n", class_name);

        // Fields
        for (auto& col : table.columns) {
            std::string cpp_type = mysql_type_to_cpp(col.type, col.is_nullable);
            std::string field_name = to_camel_case(col.name);

            if (!col.comment.empty()) {
                code += std::format("    /// {}\n", col.comment);
            }

            code += std::format("    {} {};\n", cpp_type, field_name);
        }

        code += "};\n\n";

        // CNETMOD_MODEL macro
        code += std::format("CNETMOD_MODEL({}, \"{}\",\n", class_name, table.name);

        for (std::size_t i = 0; i < table.columns.size(); ++i) {
            auto& col = table.columns[i];
            std::string field_name = to_camel_case(col.name);
            std::string sql_type = mysql_type_to_sql_type(col.type);

            std::string flags;
            if (col.is_primary_key) flags += "PK";
            if (col.is_auto_increment) {
                if (!flags.empty()) flags += " | ";
                flags += "AUTO_INC";
            }
            if (col.is_nullable) {
                if (!flags.empty()) flags += " | ";
                flags += "NULLABLE";
            }
            if (flags.empty()) flags = "NONE";

            code += std::format("    CNETMOD_FIELD({}, \"{}\", {}, {})",
                field_name, col.name, sql_type, flags);

            if (i < table.columns.size() - 1) {
                code += ",\n";
            } else {
                code += "\n";
            }
        }

        code += ")\n\n";

        // Close namespace
        code += std::format("}} // namespace {}\n", config_.namespace_name);

        return code;
    }

    /// Generate XML mapper
    auto generate_xml_mapper(const table_info& table) const -> std::string {
        std::string class_name = to_pascal_case(remove_prefix(table.name));
        std::string xml;

        xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        xml += std::format("<mapper namespace=\"{}Mapper\">\n\n", class_name);

        // SQL fragment for columns
        xml += "    <sql id=\"columns\">\n        ";
        for (std::size_t i = 0; i < table.columns.size(); ++i) {
            if (i > 0) xml += ", ";
            xml += std::format("`{}`", table.columns[i].name);
        }
        xml += "\n    </sql>\n\n";

        // SELECT by ID
        xml += "    <select id=\"findById\">\n";
        xml += "        SELECT <include refid=\"columns\"/>\n";
        xml += std::format("        FROM `{}`\n", table.name);

        // Find primary key
        std::string pk_column;
        for (auto& col : table.columns) {
            if (col.is_primary_key) {
                pk_column = col.name;
                break;
            }
        }

        if (!pk_column.empty()) {
            xml += std::format("        WHERE `{}` = #{{id}}\n", pk_column);
        }
        xml += "    </select>\n\n";

        // SELECT all
        xml += "    <select id=\"findAll\">\n";
        xml += "        SELECT <include refid=\"columns\"/>\n";
        xml += std::format("        FROM `{}`\n", table.name);
        xml += "    </select>\n\n";

        // INSERT
        xml += "    <insert id=\"insert\">\n";
        xml += std::format("        INSERT INTO `{}` (\n", table.name);

        std::vector<std::string> insert_cols;
        for (auto& col : table.columns) {
            if (!col.is_auto_increment) {
                insert_cols.push_back(col.name);
            }
        }

        for (std::size_t i = 0; i < insert_cols.size(); ++i) {
            xml += std::format("            `{}`", insert_cols[i]);
            if (i < insert_cols.size() - 1) xml += ",";
            xml += "\n";
        }

        xml += "        ) VALUES (\n";
        for (std::size_t i = 0; i < insert_cols.size(); ++i) {
            xml += std::format("            #{{{}}}",to_camel_case(insert_cols[i]));
            if (i < insert_cols.size() - 1) xml += ",";
            xml += "\n";
        }
        xml += "        )\n";
        xml += "    </insert>\n\n";

        // UPDATE
        if (!pk_column.empty()) {
            xml += "    <update id=\"update\">\n";
            xml += std::format("        UPDATE `{}`\n", table.name);
            xml += "        <set>\n";

            for (auto& col : table.columns) {
                if (col.is_primary_key || col.is_auto_increment) continue;

                std::string field_name = to_camel_case(col.name);
                xml += std::format("            <if test=\"{} != null\">\n", field_name);
                xml += std::format("                `{}` = #{{{}}},\n", col.name, field_name);
                xml += "            </if>\n";
            }

            xml += "        </set>\n";
            xml += std::format("        WHERE `{}` = #{{id}}\n", pk_column);
            xml += "    </update>\n\n";
        }

        // DELETE
        if (!pk_column.empty()) {
            xml += "    <delete id=\"delete\">\n";
            xml += std::format("        DELETE FROM `{}`\n", table.name);
            xml += std::format("        WHERE `{}` = #{{id}}\n", pk_column);
            xml += "    </delete>\n\n";
        }

        xml += "</mapper>\n";

        return xml;
    }

    /// Generate all files for a table
    auto generate_all(client& cli, std::string_view table_name) -> task<std::expected<void, std::string>> {
        // Fetch table info
        auto table_result = co_await fetch_table_info(cli, table_name);
        if (!table_result) {
            co_return std::unexpected(table_result.error());
        }

        auto& table = *table_result;

        // Create output directory
        std::filesystem::create_directories(config_.output_dir);

        // Generate model
        if (config_.generate_mapper) {
            std::string model_code = generate_model(table);
            std::string class_name = to_pascal_case(remove_prefix(table.name));
            std::string model_file = std::format("{}/{}.hpp", config_.output_dir, to_snake_case(class_name));

            std::ofstream out(model_file);
            if (!out) {
                co_return std::unexpected("Failed to write model file");
            }
            out << model_code;
        }

        // Generate XML mapper
        if (config_.generate_xml) {
            std::string xml_code = generate_xml_mapper(table);
            std::string class_name = to_pascal_case(remove_prefix(table.name));
            std::string xml_file = std::format("{}/{}Mapper.xml", config_.output_dir, class_name);

            std::ofstream out(xml_file);
            if (!out) {
                co_return std::unexpected("Failed to write XML file");
            }
            out << xml_code;
        }

        co_return {};
    }

private:
    generator_config config_;

    auto remove_prefix(std::string_view name) const -> std::string {
        if (config_.table_prefix.empty()) return std::string(name);
        if (name.starts_with(config_.table_prefix)) {
            return std::string(name.substr(config_.table_prefix.size()));
        }
        return std::string(name);
    }

    static auto to_pascal_case(std::string_view snake) -> std::string {
        std::string pascal;
        bool next_upper = true;

        for (char c : snake) {
            if (c == '_') {
                next_upper = true;
            } else if (next_upper) {
                pascal.push_back(std::toupper(static_cast<unsigned char>(c)));
                next_upper = false;
            } else {
                pascal.push_back(c);
            }
        }

        return pascal;
    }

    static auto to_camel_case(std::string_view snake) -> std::string {
        std::string camel;
        bool next_upper = false;

        for (char c : snake) {
            if (c == '_') {
                next_upper = true;
            } else if (next_upper) {
                camel.push_back(std::toupper(static_cast<unsigned char>(c)));
                next_upper = false;
            } else {
                camel.push_back(c);
            }
        }

        return camel;
    }

    static auto to_snake_case(std::string_view pascal) -> std::string {
        std::string snake;

        for (std::size_t i = 0; i < pascal.size(); ++i) {
            char c = pascal[i];
            if (std::isupper(static_cast<unsigned char>(c))) {
                if (i > 0) snake.push_back('_');
                snake.push_back(std::tolower(static_cast<unsigned char>(c)));
            } else {
                snake.push_back(c);
            }
        }

        return snake;
    }

    static auto mysql_type_to_cpp(std::string_view mysql_type, bool is_nullable) -> std::string {
        std::string base_type;

        if (mysql_type.starts_with("int") || mysql_type.starts_with("tinyint(1)")) {
            base_type = "std::int32_t";
        } else if (mysql_type.starts_with("bigint")) {
            base_type = "std::int64_t";
        } else if (mysql_type.starts_with("varchar") || mysql_type.starts_with("text") ||
                   mysql_type.starts_with("char")) {
            base_type = "std::string";
        } else if (mysql_type.starts_with("datetime") || mysql_type.starts_with("timestamp")) {
            base_type = "std::time_t";
        } else if (mysql_type.starts_with("date")) {
            base_type = "std::time_t";
        } else if (mysql_type.starts_with("double") || mysql_type.starts_with("float")) {
            base_type = "double";
        } else if (mysql_type.starts_with("decimal")) {
            base_type = "double";
        } else {
            base_type = "std::string";
        }

        if (is_nullable && base_type != "std::string") {
            return std::format("std::optional<{}>", base_type);
        }

        return base_type;
    }

    static auto mysql_type_to_sql_type(std::string_view mysql_type) -> std::string {
        if (mysql_type.starts_with("int")) return "int_";
        if (mysql_type.starts_with("bigint")) return "bigint";
        if (mysql_type.starts_with("varchar")) return "varchar";
        if (mysql_type.starts_with("text")) return "text";
        if (mysql_type.starts_with("datetime")) return "datetime";
        if (mysql_type.starts_with("timestamp")) return "timestamp";
        if (mysql_type.starts_with("date")) return "date";
        if (mysql_type.starts_with("double")) return "double_";
        if (mysql_type.starts_with("float")) return "float_";
        if (mysql_type.starts_with("decimal")) return "decimal";
        if (mysql_type.starts_with("tinyint(1)")) return "tinyint";
        return "varchar";
    }
};

} // namespace cnetmod::mysql::orm
