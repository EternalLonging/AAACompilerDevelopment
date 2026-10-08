#include "minic/constant_pool.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

using namespace minic;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static TypePtr number_type(TypeKind kind, bool is_unsigned = false) {
    auto type = std::make_shared<TypeInfo>();
    type->kind = kind;
    type->is_unsigned = is_unsigned;
    return type;
}

static TypePtr string_type(std::size_t size) {
    auto type = std::make_shared<TypeInfo>();
    type->kind = TypeKind::Array;
    type->base = number_type(TypeKind::Char);
    type->array_length = size + 1;
    return type;
}

template <typename F>
static void rejects(ConstantPool& pool, F call) {
    const auto before = pool.size();
    bool rejected = false;
    try { call(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "非法常量必须明确拒绝");
    require(pool.size() == before, "失败不能改变常量池");
}

int main() {
    try {
        ConstantPool pool;
        const auto integer = number_type(TypeKind::Int);
        const auto ten = pool.intern(integer, std::int64_t{10}, "10",
                                    {"first.c", {2, 3, 5}, {2, 5, 7}});
        // 用另一份结构相同的 TypeInfo、不同进制和位置，仍应复用原编号。
        require(pool.intern(number_type(TypeKind::Int), std::int64_t{10}, "012") == ten,
                "去重应依据类型和值，不是指针或拼写");
        require(pool.get(ten)->spelling == "10" && pool.get(ten)->range.file == "first.c",
                "去重后应保留首次出现信息");
        const auto unsigned_ten = pool.intern(number_type(TypeKind::Int, true),
                                              std::uint64_t{10}, "10U");
        const auto long_ten = pool.intern(number_type(TypeKind::Long), std::int64_t{10}, "10L");
        const auto float_ten = pool.intern(number_type(TypeKind::Float), 10.0, "10.0f");
        const auto double_ten = pool.intern(number_type(TypeKind::Double), 10.0, "10.0");
        require(unsigned_ten != ten && long_ten != ten && float_ten != ten &&
                double_ten != float_ten, "不同 C 类型不能合并");
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        const auto big = pool.intern(number_type(TypeKind::Long, true), maximum, "ULONG_MAX");
        require(std::get<std::uint64_t>(pool.get(big)->value) == maximum,
                "大整数不可经过 double 丢失精度");

        const auto positive_zero = pool.intern(number_type(TypeKind::Double), 0.0, "0.0");
        const auto negative_zero = pool.intern(number_type(TypeKind::Double), -0.0, "-0.0");
        require(positive_zero != negative_zero &&
                std::signbit(std::get<double>(pool.get(negative_zero)->value)),
                "浮点正零和负零必须保留区别");
        const auto rounded = pool.intern(number_type(TypeKind::Float), 1.0 + 1e-9, "rounded");
        require(rounded == pool.intern(number_type(TypeKind::Float), 1.0, "1.0f"),
                "float 应按自身精度舍入后去重");
        require(pool.intern(number_type(TypeKind::Double), 1.0 + 1e-9, "distinct") !=
                pool.intern(number_type(TypeKind::Double), 1.0, "1.0"),
                "double 不使用 float 精度或近似比较");

        const std::string embedded_zero("a\0b", 3);
        const auto text = pool.intern(string_type(3), embedded_zero, "\"a\\0b\"");
        require(text == pool.intern(string_type(3), embedded_zero, "\"a\\000b\""),
                "字符串按解码内容去重");
        const auto prefix = pool.intern(string_type(1), std::string("a"), "\"a\"");
        require(prefix != text && std::get<std::string>(pool.get(text)->value).size() == 3,
                "零字节不能截断字符串或制造错误合并");
        pool.intern(string_type(0), std::string{}, "\"\"");
        require(pool.get(invalid_id) == nullptr && pool.get(100000) == nullptr,
                "非法编号不能越界访问");
        require(constant_operand(ten) == "%c0", "常量操作数应带唯一前缀");

        rejects(pool, [&] { pool.intern({}, std::int64_t{1}, "1"); });
        rejects(pool, [&] { pool.intern(integer, std::monostate{}, "?"); });
        rejects(pool, [&] { pool.intern(integer, std::uint64_t{1}, "1"); });
        rejects(pool, [&] { pool.intern(number_type(TypeKind::Double),
                                       std::numeric_limits<double>::infinity(), "inf"); });
        rejects(pool, [&] { pool.intern(number_type(TypeKind::Double),
                                       std::numeric_limits<double>::quiet_NaN(), "nan"); });
        rejects(pool, [&] { pool.intern(number_type(TypeKind::Float),
                                       std::numeric_limits<double>::max(), "overflow"); });
        rejects(pool, [&] { pool.intern(string_type(2), std::string("a"), "bad length"); });
        rejects(pool, [&] { pool.intern(number_type(TypeKind::LongDouble), 1.0, "1.0L"); });
        rejects(pool, [&] { constant_operand(invalid_id); });

        // 修改调用方仍持有的可写类型，不得改变已入池的类型和索引身份。
        auto mutable_type = std::make_shared<TypeInfo>();
        mutable_type->kind = TypeKind::Int;
        const auto snapshot = pool.intern(mutable_type, std::int64_t{99}, "99");
        mutable_type->kind = TypeKind::Float;
        require(pool.get(snapshot)->type->kind == TypeKind::Int &&
                snapshot == pool.intern(integer, std::int64_t{99}, "99"),
                "入池类型应为只读快照");
        for (std::int64_t i = 1000; i < 2000; ++i) pool.intern(integer, i, std::to_string(i));
        require(pool.get(ten)->id == ten && pool.intern(integer, std::int64_t{10}, "10") == ten,
                "扩容后编号和去重索引应保持稳定");

        IRProgram program;
        const auto count = pool.size();
        program.constants = std::move(pool).release();
        require(program.constants.entries.size() == count && pool.size() == 0,
                "release 应转移数据并清空登记器");
        require(pool.intern(integer, std::int64_t{10}, "new compilation") == 0,
                "不同编译的池应独立重新编号");
        std::cout << "constant pool: deduplication, type identity, precision, strings, "
                     "invalid input, stable IDs and ownership checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
