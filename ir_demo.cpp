// ir_demo.cpp —— 四元式生成器最小可运行示范（1 组 ir.cpp 的骨架版）
// 编译： g++ -std=c++17 -O2 -o ir_demo.exe ir_demo.cpp
//
// 演示的唯一重点：gen_expr 是"返回结果放在哪个名字里"（Place），
// 父节点拿孩子返回的名字当自己的操作数 —— 这就是后序遍历能直接生成四元式的原因。
#include <bits/stdc++.h>
using namespace std;

/* ===================== 0. 类型 ===================== */
enum Ty { T_INT, T_FLOAT, T_VOID, T_STRING, T_ERR };
static const char *ty_name(Ty t) {
    switch (t) {
        case T_INT: return "int";
        case T_FLOAT: return "float";
        case T_VOID: return "void";
        case T_STRING: return "string";
        default: return "?";
    }
}

/* ===================== 1. AST（对应项目冻结接口 ASTNode） ===================== */
enum NT { N_BLOCK, N_DECL, N_ASSIGN, N_BINARY, N_ID,
          N_INT_LIT, N_FLOAT_LIT, N_STRING_LIT, N_CALL, N_RETURN, N_IF, N_WHILE };

struct Node {
    NT kind;
    Ty type = T_ERR;            // ← 语义分析阶段标注好的结果类型
    string name;                // 变量名 / 运算符 / 函数名 / 字面量原文
    double value = 0;
    int line = 0, col = 0;      // 源码位置（报错定位用）
    vector<unique_ptr<Node>> kids;
    explicit Node(NT k) : kind(k) {}
};
using NP = unique_ptr<Node>;

static NP at(NP p, int l, int c) { p->line = l; p->col = c; return p; }

static NP mk(NT k) { return make_unique<Node>(k); }
static NP mkId(const string &n, Ty t)      { auto p = mk(N_ID);        p->name = n; p->type = t; return p; }
static NP mkInt(const string &lex)         { auto p = mk(N_INT_LIT);   p->name = lex; p->value = stod(lex); p->type = T_INT; return p; }
static NP mkFloat(const string &lex)       { auto p = mk(N_FLOAT_LIT); p->name = lex; p->value = stod(lex); p->type = T_FLOAT; return p; }
static NP mkStr(const string &lex)         { auto p = mk(N_STRING_LIT);p->name = lex; p->type = T_STRING; return p; }
static NP mkBin(const string &op, Ty t, NP l, NP r) {
    auto p = mk(N_BINARY); p->name = op; p->type = t;
    p->kids.push_back(move(l)); p->kids.push_back(move(r)); return p;
}
static NP mkAssign(const string &var, Ty t, NP rhs) {
    auto p = mk(N_ASSIGN); p->name = var; p->type = t; p->kids.push_back(move(rhs)); return p;
}
static NP mkDecl(const string &var, Ty t) { auto p = mk(N_DECL); p->name = var; p->type = t; return p; }

/* ===================== 2. 符号表（本演示只用一张平表） ===================== */
static map<string, Ty> symtab;                     // 项目里由 symbol_table.cpp 用作用域栈实现
static void clear_symtab() { symtab.clear(); }

/* ===================== 3. 四元式 ===================== */
struct Quad { string op, a1, a2, res; };

/* ===================== 4. 中间代码生成器 ===================== */
class IRGen {
    vector<Quad> q_;
    int temp_no_ = 0, label_no_ = 0;

    // 实现里用保留前缀（%t1）避免与用户变量重名；这里展示时省掉 % 便于阅读
    string new_temp()  { return "t" + to_string(++temp_no_); }
    string new_label() { return "L" + to_string(++label_no_); } // 标号先取名、后定位

    void emit(const string &op, const string &a1, const string &a2, const string &res) {
        q_.push_back({op, a1, a2, res});
    }

    // ★ 核心数据结构：一个"值放在哪里"的描述
    //   常量 → 字面量原文；变量 → 变量名；中间结果 → 临时变量名
    struct Place { string name; Ty ty; bool is_const = false; };

    // 类型落位：要把 p 当成 to 类型用时，需不需要插转换
    Place promote(Place p, Ty to) {
        if (p.ty == to) return p;
        if (p.ty == T_INT && to == T_FLOAT) {
            if (p.is_const) return {p.name + ".0", T_FLOAT, true};   // 常量：编译期直接改写字面量
            string t = new_temp();                                   // 变量：生成显式转换指令
            emit("cvt_i2f", p.name, "-", t);
            return {t, T_FLOAT, false};
        }
        return p;                                                    // 其他情况语义阶段已保证合法
    }

    /* ---------- 表达式：返回"结果在哪个名字里" ---------- */
    Place gen_expr(const Node *n) {
        switch (n->kind) {
            case N_INT_LIT:   return {n->name, T_INT,   true};
            case N_FLOAT_LIT: return {n->name, T_FLOAT, true};
            case N_STRING_LIT:return {n->name, T_STRING,true};
            case N_ID: {
                auto it = symtab.find(n->name);
                if (it == symtab.end()) return {n->name, n->type, false};
                return {n->name, it->second, false};
            }
            case N_BINARY: {
                Place a = gen_expr(n->kids[0].get());     // 先算左（后序：先孩子）
                Place b = gen_expr(n->kids[1].get());     // 再算右
                a = promote(a, n->type);                  // 结果类型是语义阶段算好的 float
                b = promote(b, n->type);
                string t = new_temp();                    // 给中间结果起名字
                emit(n->name, a.name, b.name, t);         // 生成本运算
                return {t, n->type, false};               // ★ 把名字交回给父节点
            }
            case N_CALL: {
                for (auto &a : n->kids) {                 // 实参从左到右
                    Place p = gen_expr(a.get());
                    emit("arg", p.name, "-", "-");
                }
                string t = new_temp();
                emit("call", n->name, to_string(n->kids.size()), t);
                return {t, n->type, false};
            }
            default: return {"?", T_ERR, false};
        }
    }

    /* ---------- 语句/结构：只生成、不返回值 ---------- */
    void gen_stmt(const Node *n) {
        switch (n->kind) {
            case N_BLOCK:
                for (auto &k : n->kids) gen_stmt(k.get());
                break;

            case N_DECL: {
                symtab[n->name] = n->type;                       // 登记（项目中由符号表模块完成）
                if (!n->kids.empty()) {                          // 带初始化：int r = 1;
                    Place v = promote(gen_expr(n->kids[0].get()), n->type);
                    emit("=", v.name, "-", n->name);
                }
                break;
            }
            case N_ASSIGN: {
                Ty target = symtab.count(n->name) ? symtab[n->name] : n->type;
                Place v = promote(gen_expr(n->kids[0].get()), target);
                emit("=", v.name, "-", n->name);
                break;
            }
            case N_CALL: {
                for (auto &a : n->kids) { Place p = gen_expr(a.get()); emit("arg", p.name, "-", "-"); }
                emit("call", n->name, to_string(n->kids.size()), "-");
                break;
            }
            case N_RETURN:
                if (n->kids.empty()) emit("ret", "-", "-", "-");
                else { Place p = gen_expr(n->kids[0].get()); emit("ret", p.name, "-", "-"); }
                break;

            /* ---- 控制流：标号 + 跳转（先取标号名，所以不需要回填） ---- */
            case N_IF: {
                string L2 = new_label(), L3 = new_label();
                Place c = gen_expr(n->kids[0].get());
                emit("jz", c.name, "-", L2);                     // 条件假 → 跳 else
                gen_stmt(n->kids[1].get());
                if (n->kids.size() > 2) {
                    emit("jmp", "-", "-", L3);                   // 真分支执行完 → 跳过 else
                    emit("label", "-", "-", L2);
                    gen_stmt(n->kids[2].get());
                    emit("label", "-", "-", L3);
                } else {
                    emit("label", "-", "-", L2);
                }
                break;
            }
            case N_WHILE: {
                string L1 = new_label(), L2 = new_label();
                emit("label", "-", "-", L1);                     // 循环头
                Place c = gen_expr(n->kids[0].get());
                emit("jz", c.name, "-", L2);
                gen_stmt(n->kids[1].get());
                emit("jmp", "-", "-", L1);                       // ★ 回边
                emit("label", "-", "-", L2);
                break;
            }
            default: break;
        }
    }

public:
    vector<Quad> generate(const Node *program) { gen_stmt(program); return q_; }
    static void print(const vector<Quad> &q) {
        printf("%-3s %-9s %-14s %-14s %-8s\n", "#", "op", "arg1", "arg2", "result");
        printf("---------------------------------------------------------------\n");
        for (size_t i = 0; i < q.size(); ++i)
            printf("%-3zu %-9s %-14s %-14s %-8s\n", i + 1, q[i].op.c_str(),
                   q[i].a1.c_str(), q[i].a2.c_str(), q[i].res.c_str());
    }
};

/* ===================== 5. 四元式解释器（对照：四元式怎么被执行） ===================== */
struct Interp {
    map<string, double> vars;
    static bool is_num(const string &s) {
        if (s.empty() || s == "-") return false;
        return isdigit((unsigned char)s[0]) ||
               ((s[0] == '-' || s[0] == '+') && s.size() > 1 && (isdigit((unsigned char)s[1]) || s[1] == '.'));
    }
    double val(const string &s) { return is_num(s) ? stod(s) : vars[s]; }

    static string unescape(const string &lex) {              // 去掉字面量两侧的引号并还原 \n \t
        string s = lex;
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
        string o;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char c = s[++i];
                o += (c == 'n' ? '\n' : c == 't' ? '\t' : c);
            } else o += s[i];
        }
        return o;
    }

    void run(const vector<Quad> &q) {
        vector<string> args;
        for (const auto &x : q) {
            const string &op = x.op;
            if (op == "=")             vars[x.res] = val(x.a1);
            else if (op == "+")        vars[x.res] = val(x.a1) + val(x.a2);
            else if (op == "-")        vars[x.res] = val(x.a1) - val(x.a2);
            else if (op == "*")        vars[x.res] = val(x.a1) * val(x.a2);
            else if (op == "/")        vars[x.res] = val(x.a1) / val(x.a2);
            else if (op == "cvt_i2f")  vars[x.res] = (double)val(x.a1);
            else if (op == "arg")      args.push_back(x.a1);
            else if (op == "call") {
                if (x.a1 == "printf") {
                    string fmt = unescape(args.empty() ? "" : args[0]);
                    size_t ai = 1; string out;
                    for (size_t i = 0; i < fmt.size(); ++i) {
                        if (fmt[i] == '%' && i + 1 < fmt.size()) {
                            char sp = fmt[++i];
                            double v = (ai < args.size()) ? val(args[ai++]) : 0;
                            char buf[64];
                            if (sp == 'f') { snprintf(buf, sizeof buf, "%.6f", v); out += buf; }
                            else if (sp == 'd') { snprintf(buf, sizeof buf, "%d", (int)v); out += buf; }
                            else out += sp;
                        } else if (fmt[i] == '\\' && i + 1 < fmt.size() && fmt[i + 1] == 'n') {
                            out += '\n'; ++i;
                        } else out += fmt[i];
                    }
                    printf("%s", out.c_str());
                    fflush(stdout);
                }
                args.clear();
            } else if (op == "ret") {
                printf("[program exit, ret = %g]\n", val(x.a1));
            }
        }
    }
};

/* ===================== 6. 演示一：你那段程序（cout 改成 printf） ===================== */
static NP build_user_program() {
    auto blk = mk(N_BLOCK);
    blk->kids.push_back(mkDecl("r", T_INT));
    blk->kids.push_back(mkDecl("c", T_FLOAT));
    blk->kids.push_back(mkAssign("r", T_INT, mkInt("1")));
    blk->kids.push_back(mkAssign("r", T_INT, mkInt("1")));

    // c = r * 3.1415926 * r   语义阶段已标注：整棵乘法结果类型 float
    NP m1 = mkBin("*", T_FLOAT, mkId("r", T_INT), mkFloat("3.1415926"));
    NP m2 = mkBin("*", T_FLOAT, move(m1), mkId("r", T_INT));
    blk->kids.push_back(mkAssign("c", T_FLOAT, move(m2)));

    auto call = mk(N_CALL); call->name = "printf"; call->type = T_INT;
    call->kids.push_back(mkStr("\"c = %f\\n\""));
    call->kids.push_back(mkId("c", T_FLOAT));
    blk->kids.push_back(move(call));

    auto ret = mk(N_RETURN); ret->type = T_INT;
    ret->kids.push_back(mkInt("0"));
    blk->kids.push_back(move(ret));
    return blk;
}

/* ===================== 7. 演示二：控制流 ===================== */
static NP build_control_flow_block() {
    auto blk = mk(N_BLOCK);
    for (const char *v : {"a", "b", "x", "i", "n"}) blk->kids.push_back(mkDecl(v, T_INT));

    // if (a < b) { x = 1; } else { x = 2; }
    auto iff = mk(N_IF);
    iff->kids.push_back(mkBin("<", T_INT, mkId("a", T_INT), mkId("b", T_INT)));
    iff->kids.push_back(mkAssign("x", T_INT, mkInt("1")));
    iff->kids.push_back(mkAssign("x", T_INT, mkInt("2")));
    blk->kids.push_back(move(iff));

    // while (i < n) { i = i + 1; }
    auto wh = mk(N_WHILE);
    wh->kids.push_back(mkBin("<", T_INT, mkId("i", T_INT), mkId("n", T_INT)));
    wh->kids.push_back(mkAssign("i", T_INT, mkBin("+", T_INT, mkId("i", T_INT), mkInt("1"))));
    blk->kids.push_back(move(wh));
    return blk;
}

/* ===================== 8. AST 打印（看 printf("...") 长什么样） ===================== */
static const char *kind_name(NT k) {
    switch (k) {
        case N_BLOCK:      return "Block";
        case N_DECL:       return "Decl";
        case N_ASSIGN:     return "Assign";
        case N_BINARY:     return "BinaryOp";
        case N_ID:         return "Id";
        case N_INT_LIT:    return "IntLit";
        case N_FLOAT_LIT:  return "FloatLit";
        case N_STRING_LIT: return "StringLit";
        case N_CALL:       return "Call";
        case N_RETURN:     return "Return";
        case N_IF:         return "If";
        case N_WHILE:      return "While";
    }
    return "?";
}

static string child_label(const Node *p, size_t i) {
    switch (p->kind) {
        case N_CALL:   return "arg[" + to_string(i) + "]";
        case N_BINARY: return i == 0 ? "left" : "right";
        case N_IF:     return i == 0 ? "cond" : i == 1 ? "then" : "else";
        case N_WHILE:  return i == 0 ? "cond" : "body";
        case N_ASSIGN: return "rhs";
        case N_DECL:   return "init";
        case N_RETURN: return "value";
        default:       return "child[" + to_string(i) + "]";
    }
}

static void dump(const Node *n, const string &prefix, bool last, const string &label = "") {
    printf("%s%s", prefix.c_str(), last ? "`-- " : "|-- ");
    if (!label.empty()) printf("%s -> ", label.c_str());
    printf("%s", kind_name(n->kind));
    if (n->kind == N_STRING_LIT)      printf("  %s", n->name.c_str());
    else if (!n->name.empty())        printf("  name=%s", n->name.c_str());
    if (n->type != T_ERR)             printf("  :%s", ty_name(n->type));
    if (n->line)                      printf("  (line %d, col %d)", n->line, n->col);
    printf("\n");
    string np = prefix + (last ? "    " : "|   ");
    for (size_t i = 0; i < n->kids.size(); ++i)
        dump(n->kids[i].get(), np, i + 1 == n->kids.size(), child_label(n, i));
}

static NP build_printf_demo() {
    auto blk = mk(N_BLOCK);
    blk->kids.push_back(at(mkDecl("c", T_FLOAT), 7, 5));

    // printf("c = %f\n", c);     第 8 行
    auto call = mk(N_CALL); call->name = "printf"; call->type = T_INT;
    call->line = 8; call->col = 5;
    call->kids.push_back(at(mkStr("\"c = %f\\n\""), 8, 12));
    call->kids.push_back(at(mkId("c", T_FLOAT), 8, 25));
    blk->kids.push_back(move(call));

    // printf("%d %f\n", f(x) + 1, c * 2);     第 9 行：看实参是任意表达式 + 调用嵌套
    auto c2 = mk(N_CALL); c2->name = "printf"; c2->type = T_INT;
    c2->line = 9; c2->col = 5;
    c2->kids.push_back(at(mkStr("\"%d %f\\n\""), 9, 12));
    NP fx = mk(N_CALL); fx->name = "f"; fx->type = T_INT;
    fx->line = 9; fx->col = 21;
    fx->kids.push_back(at(mkId("x", T_INT), 9, 23));
    c2->kids.push_back(at(mkBin("+", T_INT, move(fx), mkInt("1")), 9, 20));
    c2->kids.push_back(at(mkBin("*", T_FLOAT, mkId("c", T_FLOAT), mkFloat("2")), 9, 33));
    blk->kids.push_back(move(c2));
    return blk;
}

static NP build_printf_int_demo() {
    // int main() { int s; s = 5; printf("%d", s); }
    auto blk = mk(N_BLOCK);
    blk->kids.push_back(at(mkDecl("s", T_INT), 7, 5));
    blk->kids.push_back(at(mkAssign("s", T_INT, mkInt("5")), 8, 5));
    auto call = mk(N_CALL); call->name = "printf"; call->type = T_INT;
    call->line = 9; call->col = 5;
    call->kids.push_back(at(mkStr("\"%d\""), 9, 12));
    call->kids.push_back(at(mkId("s", T_INT), 9, 18));
    blk->kids.push_back(move(call));
    return blk;
}

/* ===================== 9. main ===================== */int main() {
    /* ---- 演示一 ---- */
    printf("=== Demo 1: the user's program (cout -> printf) ===\n\n");
    clear_symtab();
    NP prog = build_user_program();
    IRGen gen;
    vector<Quad> q1 = gen.generate(prog.get());
    IRGen::print(q1);

    printf("\n-- interpreter executes the quadruples --\n");
    Interp ip; ip.run(q1);

    /* ---- 演示二 ---- */
    printf("\n=== Demo 2: control flow (if-else / while) ===\n\n");
    clear_symtab();
    NP blk = build_control_flow_block();
    IRGen gen2;
    vector<Quad> q2 = gen2.generate(blk.get());
    IRGen::print(q2);

    /* ---- 演示三 ---- */
    printf("\n=== Demo 3: AST shape of printf(\"...\") ===\n\n");
    clear_symtab();
    NP pf = build_printf_demo();
    dump(pf.get(), "", true);
    printf("\n-- quadruples generated from that AST --\n\n");
    IRGen gen3;
    vector<Quad> q3 = gen3.generate(pf.get());
    IRGen::print(q3);

    /* ---- 演示四：printf("%d", s); ---- */
    printf("\n=== Demo 4: printf(\"%%d\", s) end to end ===\n\n");
    clear_symtab();
    NP p4 = build_printf_int_demo();
    dump(p4.get(), "", true);
    printf("\n-- quadruples --\n\n");
    IRGen gen4;
    vector<Quad> q4 = gen4.generate(p4.get());
    IRGen::print(q4);
    printf("\n-- interpreter executes --\n\n");
    Interp ip4; ip4.run(q4);
    return 0;
}
