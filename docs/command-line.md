# 鍛戒护琛屼娇鐢?

```powershell
build/cmake/minic.exe <tokens|parse|symbols|check|ir|run> <婧愭枃浠?
```

鍛戒护鐩存帴浠庢簮鐮佹墽琛岃瘝娉曘€佽娉曘€佽涔夊拰涓棿浠ｇ爜闃舵銆傜紪璇戝け璐ユ椂鍋滄鍚庣画闃舵锛岃瘖鏂啓鍏ユ爣鍑嗛敊璇€傝繍琛屾垚鍔熻繑鍥?main 鐨勮繑鍥炲€硷紝缂栬瘧鎴栨墽琛屽け璐ヨ繑鍥?1銆?

```c
#define N 5
int main(void) {
    printf("value = %d\n", N * 2);
    return 0;
}
```
