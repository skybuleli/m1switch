#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

int main() {
    // Test 1: spirv-val on invalid file
    int r1 = system("spirv-val /nonexistent.spv 2>/dev/null");
    printf("system() returned: %d\n", r1);
    printf("WEXITSTATUS: %d\n", WEXITSTATUS(r1));
    
    // Test 2: popen
    FILE* pipe = popen("spirv-val /nonexistent.spv 2>/dev/null", "r");
    if (pipe) {
        char buf[256];
        while (fgets(buf, sizeof(buf), pipe)) { }
        int rc = pclose(pipe);
        printf("pclose() returned: %d (0x%x)\n", rc, rc);
        printf("WEXITSTATUS(pclose): %d\n", WEXITSTATUS(rc));
    }
    
    // Test 3: popen with valid command
    pipe = popen("echo hello", "r");
    if (pipe) {
        char buf[256];
        while (fgets(buf, sizeof(buf), pipe)) { }
        int rc = pclose(pipe);
        printf("pclose(echo) returned: %d (0x%x)\n", rc, rc);
        printf("WEXITSTATUS(echo): %d\n", WEXITSTATUS(rc));
    }
    
    // Test 4: system exit 1
    int r4 = system("exit 1");
    printf("system(exit 1) returned: %d\n", r4);
    printf("WEXITSTATUS: %d\n", WEXITSTATUS(r4));
    
    return 0;
}
