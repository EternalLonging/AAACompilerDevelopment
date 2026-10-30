struct Box {
    int values[2]; /* 返回对象中的两个整数。 */
};
struct Box make(void) {
    struct Box box;
    box.values[0] = 4;
    box.values[1] = 8;
    return box;
}
int main(void) {
    printf("temporary = %d\n", make().values[1]);
    return 0;
}
