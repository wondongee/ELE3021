#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "stat.h"

extern void list_process(void);

void panic(char *s)
{
  printf(2, "%s\n", s);
  exit();
}

int getcmd(char *buf, int nbuf)
{
  printf(2, "$$ ");
  memset(buf, 0, nbuf);
  gets(buf, nbuf);
  if(buf[0] == 0) // EOF
    return -1;
  return 0;
}

int fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

char* getnext(char *buf, char *newbuf, int num)
{
  int i=0, j=0;
  memset(newbuf, 0, 100);
  for(int k=0; k<num; k++) {
    while(buf[i++] != ' ') { }
  }
  while(buf[i] != ' ' && buf[i] != '\n') {
    newbuf[j++] = buf[i++];
  }
  newbuf[j] = '\0';
  return newbuf;
}

int strcmp2(char *buf)
{
    if (buf[0]=='l' && buf[1]=='i' && buf[2]=='s' && buf[3]=='t') {
        return 1;
    } else if (buf[0]=='k' && buf[1]=='i' && buf[2]=='l' && buf[3]=='l') {
        return 2;
    } else if (buf[0]=='e' && buf[1]=='x' && buf[2]=='e' && buf[3]=='c' && buf[4]=='u' && buf[5]=='t' && buf[6]=='e') {
        return 3;
    } else if (buf[0]=='m' && buf[1]=='e' && buf[2]=='m' && buf[3]=='l' && buf[4]=='i' && buf[5]=='m') {
        return 4;
    } else if (buf[0]=='e' && buf[1]=='x' && buf[2]=='i' && buf[3]=='t') {
        return 5;
    } else {
        return -1;
    }
}
int neg_atoi(const char *s) {
    int n = 0;
    int sign = 1;

    if (*s == '-') {
        sign = -1;
        s++;  // '-' 문자는 건너뛰고 숫자로 진행
    }

    while ('0' <= *s && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }

    return sign * n;
}

int main() {
    static char buf[100];
    static char newbuf[100];
    static char newbuf2[100];
    static char* arg1;
    static char* arg2;

    while(getcmd(buf, sizeof(buf)) >= 0) {
        int num = strcmp2(buf);
        switch (num) {
            case 1:
                list_process();
                break;
            case 2:
                arg1 = getnext(buf, newbuf, 1);
                kill(atoi(arg1));
                break;
            case 3:
                arg1 = getnext(buf, newbuf, 1);                
                arg2 = getnext(buf, newbuf2, 2);
                char *argv[] = { arg1 };
                if(fork1() == 0) {
                    if(exec2(arg1, argv, atoi(arg2)) == -1) {
                        printf(2, "프로그램 실행에 실패하였습니다.\n");   
                    }                 
                }                                                                        
                break;
            case 4:
                arg1 = getnext(buf, newbuf, 1);               
                arg2 = getnext(buf, newbuf2, 2);
                if(setmemorylimit(atoi(arg1), neg_atoi(arg2)) == 0) {
                    printf(2, "memory limit 설정에 성공하였습니다.\n");
                }
                break;
            case 5:
                exit();
                break;
            default:
                printf(2, "올바른 명령어를 입력해주세요.\n");        
                break;
        }
    }
}