typedef int Count;
enum Color { RED = 2, GREEN, BLUE };
int main(void) {
    Count n;
    n = BLUE;
    goto done;
    n = 0;
done: printf("names = %d\n", n);
    return 0;
}
