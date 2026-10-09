/* 语义错误：使用了没有声明的名字，后面的输出不会执行。 */
int main(void) {
    missing = 1;
    printf("SHOULD_NOT_RUN\n");
    return 0;
}
