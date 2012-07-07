PREFIX ?= /usr
TARGET ?= taskbar
BUILD ?= release

CC = clang
CC += -pipe -std=c++11 -fno-threadsafe-statics -fno-rtti -fno-exceptions -fno-omit-frame-pointer
CC += -Wall -Wextra -Wno-narrowing -Wno-missing-field-initializers -Wno-pmf-conversions
CC += $(FLAGS_$(BUILD))
FLAGS_debug := -g -DDEBUG
FLAGS_release := -O3 -ffast-math
FLAGS_profile := -g -O -finstrument-functions -finstrument-functions-exclude-file-list=core,array,map,profile

SRCS = $(SRCS_$(BUILD)) $(SRCS_$(TARGET))
SRCS_profile += profile
SRCS_memory += memory
SRCS_taskbar += png inflate
SRCS_desktop += png inflate jpeg ico
SRCS_player += png inflate
SRCS_music += png inflate

ICONS = $(ICONS_$(TARGET))
ICONS_taskbar := button
ICONS_desktop := shutdown network
ICONS_player := play pause next
ICONS_music := music music256
SRCS += $(ICONS:%=icons/%)

INSTALL = $(INSTALL_$(TARGET))
INSTALL_player = icons/$(TARGET).png $(TARGET).desktop
INSTALL_feeds = icons/$(TARGET).png $(TARGET).desktop
INSTALL_music = icons/$(TARGET).png $(TARGET).desktop

all: prepare $(BUILD)/$(TARGET)

%.l: %.d
		./dep.py $(BUILD)/$(TARGET) $@ $(BUILD) $< >$@

$(BUILD)/$(TARGET): $(SRCS:%=$(BUILD)/%.o)
		ld -o $(BUILD)/$(TARGET) $(filter %.o, $^)

$(BUILD)/%.d: %.cc
		@test -e $(dir $@) || mkdir -p $(dir $@)
		$(CC) -MM -MT $(BUILD)/$*.o -MT $(BUILD)/$*.d $< > $@

ifneq ($(MAKECMDGOALS),clean)
-include $(BUILD)/$(TARGET).l
endif

$(BUILD)/%.o : %.cc
		$(CC) -c -o $@ $<

$(BUILD)/%.o: %.png
		@test -e $(dir $@) || mkdir -p $(dir $@)
		ld -r -b binary -o $@ $<

prepare:
		@ln -sf $(TARGET).files serenity.files

clean:
		rm -f $(BUILD)/*.l
		rm -f $(BUILD)/*.d
		rm -f $(BUILD)/*.o
		rm -f $(BUILD)/$(TARGET)
		rm -fR $(BUILD)/icons
		rmdir $(BUILD)

install_icons/%.png: icons/%.png
		cp $< $(PREFIX)/share/icons/hicolor/32x32/apps

install_%.desktop: %.desktop
		cp $< $(PREFIX)/share/applications/

install: all $(INSTALL:%=install_%)
		cp $(BUILD)/$(TARGET) $(PREFIX)/bin/$(TARGET)
