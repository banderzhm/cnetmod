# Database Examples

MySQL database operations with ORM and transaction support.

## Examples

### mysql_crud.cpp
Basic CRUD operations:
- Create (INSERT)
- Read (SELECT)
- Update (UPDATE)
- Delete (DELETE)

### mysql_orm.cpp
Object-Relational Mapping:
- Entity mapping
- Type-safe queries
- Automatic serialization

### mysql_transaction.cpp
Transaction management:
- BEGIN/COMMIT/ROLLBACK
- ACID properties
- Error handling

### mysql_xml_mapper.cpp
MyBatis-style XML mappers:
- SQL in XML files
- Parameter binding
- Result mapping

### mysql_xml_complex.cpp
Complex XML mapper scenarios:
- Joins
- Nested queries
- Dynamic SQL

### mybatis_plus_demo.cpp
MyBatis Plus features:
- Code generation
- CRUD without SQL
- Pagination
- Conditional queries

### transaction_simple_test.cpp
Simple transaction testing:
- Basic transaction flow
- Rollback scenarios
- Isolation levels

## Prerequisites

- MySQL server running
- Database created
- Connection credentials configured

## Building

```bash
cd build
cmake --build . --target <example_name>
```

## Running

```bash
./database/<example_name>
```

## Configuration

Update connection parameters in the example files:
```cpp
auto conn = co_await pool.acquire();
// host: localhost
// port: 3306
// user: root
// password: your_password
// database: test_db
```
