# GNTS - 观澜 · Nexus Trading System
# Makefile

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE -I./include
LDFLAGS = -lpthread -lm

# 源文件目录
SRC_DIRS = src/common src/data_system src/strategy src/backtest src/oms src/risk

# 查找所有.c文件
SRCS = $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
OBJS = $(SRCS:.c=.o)

# 目标文件
TARGET = libgnts.a

# 默认目标
all: $(TARGET)

# 编译静态库
$(TARGET): $(OBJS)
	ar rcs $@ $^

# 编译目标文件
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 创建源文件目录
dirs:
	mkdir -p $(SRC_DIRS)

# 清理
clean:
	rm -f $(OBJS) $(TARGET)
	rm -rf tests/*.o tests/test_main

# 测试
test: $(TARGET)
	$(CC) $(CFLAGS) tests/test_main.c -L. -lgnts $(LDFLAGS) -o tests/test_main
	./tests/test_main

# 安装
install: $(TARGET)
	mkdir -p /usr/local/lib
	mkdir -p /usr/local/include/gnts
	cp $(TARGET) /usr/local/lib/
	cp -r include/* /usr/local/include/gnts/

# 卸载
uninstall:
	rm -f /usr/local/lib/$(TARGET)
	rm -rf /usr/local/include/gnts

# ============ Sanitizer 构建 ============

# AddressSanitizer (内存错误检测)
asan: CFLAGS = -Wall -Wextra -O1 -g -std=c11 -D_GNU_SOURCE -I./include \
              -fsanitize=address -fno-omit-frame-pointer
asan: LDFLAGS = -fsanitize=address -lpthread -lm
asan: clean $(TARGET)

# UndefinedBehaviorSanitizer (未定义行为检测)
ubsan: CFLAGS = -Wall -Wextra -O1 -g -std=c11 -D_GNU_SOURCE -I./include \
               -fsanitize=undefined -fno-omit-frame-pointer
ubsan: LDFLAGS = -fsanitize=undefined -lpthread -lm
ubsan: clean $(TARGET)

# ThreadSanitizer (线程竞争检测)
tsan: CFLAGS = -Wall -Wextra -O1 -g -std=c11 -D_GNU_SOURCE -I./include \
              -fsanitize=thread -fno-omit-frame-pointer
tsan: LDFLAGS = -fsanitize=thread -lpthread -lm
tsan: clean $(TARGET)

# Sanitizer 测试
test-asan: asan
	$(CC) $(CFLAGS) tests/test_main.c -L. -lgnts $(LDFLAGS) -o tests/test_main
	ASAN_OPTIONS=detect_leaks=1 ./tests/test_main

test-ubsan: ubsan
	$(CC) $(CFLAGS) tests/test_main.c -L. -lgnts $(LDFLAGS) -o tests/test_main
	./tests/test_main

test-tsan: tsan
	$(CC) $(CFLAGS) tests/test_main.c -L. -lgnts $(LDFLAGS) -o tests/test_main
	./tests/test_main

# ============ 代码覆盖率 ============

coverage: CFLAGS = -Wall -Wextra -O0 -g -std=c11 -D_GNU_SOURCE -I./include \
                   --coverage -fprofile-arcs -ftest-coverage
coverage: LDFLAGS = --coverage -lpthread -lm
coverage: clean $(TARGET)

test-coverage: coverage
	$(CC) $(CFLAGS) tests/test_main.c -L. -lgnts $(LDFLAGS) -o tests/test_main
	./tests/test_main

coverage-report:
	@mkdir -p coverage
	lcov --capture --directory . --output-file coverage/coverage.info \
	     --rc lcov_branch_coverage=1
	lcov --remove coverage/coverage.info '/usr/*' 'tests/*' \
	     --output-file coverage/coverage_filtered.info \
	     --rc lcov_branch_coverage=1
	genhtml coverage/coverage_filtered.info --output-directory coverage/html \
	        --branch-coverage --legend
	@echo "Coverage report generated in coverage/html/index.html"

# ============ 静态分析 ============

cppcheck:
	cppcheck --enable=all --inconclusive --std=c11 --language=c \
	         -I./include src/ 2>&1 | tee cppcheck-report.txt

# ============ 代码格式化 ============

format:
	@if [ -f .clang-format ]; then \
		find src include tests -name '*.c' -o -name '*.h' | xargs clang-format -i; \
		echo "Code formatted with clang-format"; \
	else \
		echo "No .clang-format file found"; \
	fi

format-check:
	@if [ -f .clang-format ]; then \
		find src include tests -name '*.c' -o -name '*.h' | xargs clang-format --dry-run -Werror; \
	else \
		echo "No .clang-format file found"; \
	fi

# ============ 全部检查 ============

check: cppcheck format-check test

# ============ Doxygen 文档 ============

docs:
	doxygen Doxyfile
	@echo "API documentation generated in docs/html/index.html"

.PHONY: all clean test install uninstall dirs
.PHONY: asan ubsan tsan test-asan test-ubsan test-tsan
.PHONY: coverage test-coverage coverage-report
.PHONY: cppcheck format format-check check docs