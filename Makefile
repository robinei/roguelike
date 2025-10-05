CC = clang
CFLAGS = -std=c99 -Wall -Wextra -g -MMD -MP
LDFLAGS = -lm
BUILDDIR = build
TARGET = $(BUILDDIR)/roguelike

SRCS = $(shell find . -name '*.c')
OBJS = $(SRCS:./%.c=$(BUILDDIR)/%.o)
DEPS = $(SRCS:./%.c=$(BUILDDIR)/%.d)

$(TARGET): $(OBJS) | $(BUILDDIR)
	$(CC) $(LDFLAGS) $(OBJS) -o $(TARGET)

$(BUILDDIR)/%.o: ./%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

-include $(DEPS)

clean:
	rm -rf $(BUILDDIR)

.PHONY: clean