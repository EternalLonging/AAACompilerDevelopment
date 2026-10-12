typedef int Count;

int main(void) {
    Count n;
    n = 4;
    goto done;
    n = 0;
done: printf("names = %d\n", n);
    return 0;
}
