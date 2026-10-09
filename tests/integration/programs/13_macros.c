#include "../headers/config.h"
#include "../headers/config.h"
#define STR(x) #x
#define XSTR(x) STR(x)
#define CAT(a,b) a ## b
#if defined(BASE) && BASE == 3
int main(void) {
    int value3 = BASE + 4;
    printf("macro = %d %s\n", CAT(value,3), XSTR(BASE));
    return 0;
}
#else
#error configuration missing
#endif
