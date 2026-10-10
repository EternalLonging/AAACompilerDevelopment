# 数组、记录与控制流

当前保留功能及语法限制见 [language-scope.md](language-scope.md)，公共 AST 合同见 [interface.md](interface.md)。

## 数据与存储

数组支持多维下标、初始化、长度推导、传参和 sizeof。形参数组内部保存首元素地址，调用期间引用调用者数组；因此修改形参数组元素会改变原数组。struct/union 支持成员、值拷贝、按值传参和返回；聚合传值不会共享容器。enum、typedef、const、static、extern 保留。

布局固定为 char=1/1、short=2/2、int/long/float=4/4、double/long double=8/8（大小/对齐，字节），不取宿主 C++ 的 sizeof。struct 逐成员对齐并补齐末尾；union 成员偏移为 0。内部地址占 8 字节，仅用于数组参数和运行时寻址。单对象最多 16 MiB，类型嵌套最多 128 层。

const 向数组元素和记录成员传播。数值与聚合读取检查初始化、对象存活、对齐和边界。数组下标要求 `0 <= i < length`，不提供源码中的指针偏移或尾后地址。

## 控制与输入输出

保留 if/else、while、for、do-while、switch/case/default、break、continue、goto 和 return。控制体需要花括号；同块先声明再执行，for 变量提前声明。

printf/scanf 格式必须是字面量，支持已实现的数值格式和长度修饰、字符数组 `%s`；scanf 字符串可指定宽度。数值输入用 `&x`、`&a[i]` 或 `&s.member`，只允许可写对象。普通表达式取地址、指针声明、解引用、`->` 和间接函数调用已删除。

## 内部寻址四元式

| 指令 | 作用 |
|---|---|
| addr | 获取变量的内部地址 |
| indexaddr | 按下标定位普通数组元素 |
| offsetaddr | 按下标定位数组形参的元素，检查数组边界 |
| memberaddr | 按成员偏移定位记录成员 |
| tempaddr | 定位临时聚合的成员 |
| decay | 将数组转换为传参或字符串读写所需的内部首元素地址 |
| load / store | 读取或写入地址对应的值 |
| zero | 初始化聚合存储 |

只保留直接 `call`；旧 `faddr`、`callind`、`ptradd`、`ptrsub`、`ptrdiff` 不再生成或执行。实参从左到右求值，调用前保存快照，递归调用使用独立调用帧。

回归涵盖数组传参、多维下标、输入数组元素和成员、字符串输入、记录传值、const、静态存储、运行错误和删除功能的拒绝行为。
