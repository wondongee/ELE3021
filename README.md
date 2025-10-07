# 🖥️ ELE3021 - Operating System Projects

**한양대학교 컴퓨터소프트웨어학부 운영체제 과목 프로젝트 모음**

[![C](https://img.shields.io/badge/C-Programming-blue.svg)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Operating System](https://img.shields.io/badge/OS-xv6-green.svg)](https://pdos.csail.mit.edu/6.828/2020/xv6.html)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

## 📋 목차

- [프로젝트 개요](#-프로젝트-개요)
- [프로젝트 목록](#-프로젝트-목록)
- [기술 스택](#-기술-스택)
- [설치 및 실행](#-설치-및-실행)
- [프로젝트 구조](#-프로젝트-구조)
- [상세 설명](#-상세-설명)
- [학습 성과](#-학습-성과)
- [기여하기](#-기여하기)

## 🎯 프로젝트 개요

본 저장소는 **한양대학교 컴퓨터소프트웨어학부 ELE3021 운영체제** 과목에서 수행한 3개의 프로젝트를 포함합니다. 각 프로젝트는 xv6 운영체제를 기반으로 하여 운영체제의 핵심 개념들을 실습을 통해 학습합니다.

### 학습 목표

- 🧠 **운영체제 핵심 개념 이해**: 프로세스, 스레드, 파일시스템 등
- 💻 **시스템 프로그래밍**: C 언어를 활용한 저수준 프로그래밍
- 🔧 **커널 수정**: xv6 커널의 핵심 기능 수정 및 확장
- 📚 **이론과 실습의 결합**: 교과서 지식을 실제 코드로 구현

## 📚 프로젝트 목록

### Project 1: Multi-Level Feedback Queue (MLFQ) Scheduler
- **기간**: 2021년 1학기
- **주제**: MLFQ 스케줄러 구현
- **핵심 기술**: 프로세스 스케줄링, 우선순위 큐, 시간 할당량

### Project 2: Thread Management System
- **기간**: 2021년 1학기  
- **주제**: 사용자 레벨 스레드 시스템 구현
- **핵심 기술**: 스레드 생성/종료/킬, 컨텍스트 스위칭, 동기화

### Project 3: File System Operations
- **기간**: 2021년 1학기
- **주제**: 파일시스템 연산 구현
- **핵심 기술**: 파일 생성/삭제, 심볼릭 링크, 디렉토리 구조

## 🛠️ 기술 스택

- **C Programming Language**
- **xv6 Operating System**
- **GCC Compiler**
- **QEMU Emulator**
- **GDB Debugger**
- **Make Build System**

## 🚀 설치 및 실행

### 1. 저장소 클론

```bash
git clone https://github.com/wondongee/ELE3021.git
cd ELE3021
```

### 2. 환경 설정

```bash
# xv6 환경 설정 (Linux/macOS)
sudo apt-get install qemu-system-x86  # Ubuntu/Debian
brew install qemu                     # macOS

# 또는 MIT 6.828 환경 사용
# https://pdos.csail.mit.edu/6.828/2020/tools.html
```

### 3. 프로젝트 실행

```bash
# Project 1 실행
cd Project1
make qemu

# Project 2 실행  
cd Project2
make qemu

# Project 3 실행
cd Project3
make qemu
```

## 📁 프로젝트 구조

```
ELE3021/
├── Project1/                        # MLFQ 스케줄러
│   ├── bootxv6.sh                   # 부팅 스크립트
│   ├── main.c                       # 메인 함수
│   ├── proc.c                       # 프로세스 관리
│   ├── proc.h                       # 프로세스 헤더
│   ├── syscall.c                    # 시스템 콜
│   ├── syscall.h                    # 시스템 콜 헤더
│   ├── trap.c                       # 트랩 처리
│   ├── mlfq_test.c                  # MLFQ 테스트
│   └── ELE3021_project01_*.pdf      # 프로젝트 보고서
├── Project2/                        # 스레드 관리
│   ├── pmanager.c                   # 프로세스 매니저
│   ├── thread_*.c                   # 스레드 관련 함수들
│   ├── thread_test.c                # 스레드 테스트
│   └── ELE3021_project02_*.pdf      # 프로젝트 보고서
├── Project3/                        # 파일시스템
│   ├── exec.c                       # 실행 함수
│   ├── fs.c                         # 파일시스템
│   ├── fs.h                         # 파일시스템 헤더
│   ├── ln.c                         # 링크 생성
│   └── ELE3021_project03_*.pdf      # 프로젝트 보고서
└── README.md                        # 프로젝트 문서
```

## 📖 상세 설명

### Project 1: Multi-Level Feedback Queue Scheduler

#### 목표
기존의 Round Robin 스케줄러를 Multi-Level Feedback Queue (MLFQ) 스케줄러로 교체

#### 구현 내용

1. **우선순위 큐 구조**
   ```c
   struct mlfq_queue {
       struct proc *head;
       int priority;
       int time_slice;
   };
   ```

2. **스케줄링 알고리즘**
   - 높은 우선순위 큐부터 처리
   - 시간 할당량 초과 시 낮은 우선순위로 이동
   - I/O 대기 시 우선순위 유지

3. **주요 함수**
   - `mlfq_scheduler()`: MLFQ 스케줄링 로직
   - `update_priority()`: 우선순위 업데이트
   - `mlfq_test()`: 테스트 함수

#### 테스트 결과
- **CPU 집약적 작업**: 낮은 우선순위로 이동
- **I/O 집약적 작업**: 높은 우선순위 유지
- **응답 시간**: 개선된 사용자 경험

### Project 2: Thread Management System

#### 목표
사용자 레벨에서 스레드를 생성, 관리, 종료할 수 있는 시스템 구현

#### 구현 내용

1. **스레드 구조체**
   ```c
   struct thread {
       int tid;                    // 스레드 ID
       void *stack;                // 스택 포인터
       int state;                  // 스레드 상태
       struct proc *parent;        // 부모 프로세스
   };
   ```

2. **시스템 콜 추가**
   - `thread_create()`: 스레드 생성
   - `thread_exit()`: 스레드 종료
   - `thread_kill()`: 스레드 강제 종료

3. **컨텍스트 스위칭**
   - 스레드 간 컨텍스트 저장/복원
   - 스택 포인터 관리
   - 레지스터 상태 보존

#### 테스트 결과
- **동시성**: 여러 스레드가 동시에 실행
- **동기화**: 스레드 간 안전한 데이터 공유
- **성능**: 프로세스 생성보다 빠른 스레드 생성

### Project 3: File System Operations

#### 목표
파일시스템의 기본 연산들을 구현하고 심볼릭 링크 기능 추가

#### 구현 내용

1. **파일 연산**
   - 파일 생성/삭제
   - 디렉토리 생성/삭제
   - 파일 복사/이동

2. **심볼릭 링크**
   ```c
   int symlink(char *target, char *linkpath) {
       // 심볼릭 링크 생성 로직
   }
   ```

3. **디렉토리 구조**
   - 계층적 디렉토리 지원
   - 경로 해석 알고리즘
   - inode 관리

#### 테스트 결과
- **파일 관리**: 정상적인 파일 생성/삭제
- **링크 기능**: 심볼릭 링크 정상 동작
- **디렉토리**: 계층적 구조 지원

## 🎓 학습 성과

### 기술적 역량

1. **시스템 프로그래밍**
   - C 언어를 활용한 저수준 프로그래밍
   - 메모리 관리 및 포인터 활용
   - 시스템 콜 구현 및 활용

2. **운영체제 이해**
   - 프로세스와 스레드의 차이점
   - 스케줄링 알고리즘의 동작 원리
   - 파일시스템의 구조와 동작

3. **디버깅 및 테스트**
   - GDB를 활용한 커널 디버깅
   - 체계적인 테스트 케이스 작성
   - 성능 분석 및 최적화

### 프로젝트별 성과

| 프로젝트 | 성과 | 기술적 도전 |
|----------|------|-------------|
| Project 1 | MLFQ 스케줄러 완전 구현 | 우선순위 큐 관리 |
| Project 2 | 사용자 스레드 시스템 구현 | 컨텍스트 스위칭 |
| Project 3 | 파일시스템 연산 구현 | inode 관리 |

## 🔧 커스터마이징

### 새로운 스케줄러 추가

```c
// Project1/proc.c에 추가
void custom_scheduler(void) {
    // 커스텀 스케줄링 로직
}
```

### 추가 시스템 콜 구현

```c
// Project2/syscall.c에 추가
int sys_thread_create(void) {
    // 새로운 시스템 콜 구현
}
```

### 파일시스템 확장

```c
// Project3/fs.c에 추가
int custom_file_operation(void) {
    // 새로운 파일 연산 구현
}
```

## 🐛 문제 해결

### 자주 발생하는 문제

1. **컴파일 오류**
   ```bash
   # Makefile 확인
   make clean
   make
   ```

2. **QEMU 실행 오류**
   ```bash
   # QEMU 설치 확인
   qemu-system-i386 --version
   ```

3. **디버깅 문제**
   ```bash
   # GDB로 디버깅
   make qemu-gdb
   gdb kernel
   ```

## 📚 참고 자료

- [MIT 6.828 Operating System Engineering](https://pdos.csail.mit.edu/6.828/2020/)
- [xv6: a simple, Unix-like teaching operating system](https://pdos.csail.mit.edu/6.828/2020/xv6.html)
- [Operating System Concepts (Silberschatz)](https://www.os-book.com/)

## 🤝 기여하기

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## 📄 라이선스

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 📞 연락처

- **GitHub**: [@wondongee](https://github.com/wondongee)
- **이메일**: wondongee@example.com
- **학교**: 한양대학교 컴퓨터소프트웨어학부

## 🙏 감사의 말

- 한양대학교 컴퓨터소프트웨어학부 교수님들께 감사드립니다
- MIT 6.828 강의 자료에 감사드립니다
- xv6 개발팀에게 감사드립니다

---

**⭐ 이 프로젝트가 도움이 되었다면 Star를 눌러주세요!**
