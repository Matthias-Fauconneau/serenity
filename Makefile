PREFIX ?= /usr
TARGET ?= taskbar
BUILD ?= fast

CC = clang -pipe -std=c++11 -fno-threadsafe-statics -fno-rtti -fno-exceptions -fno-omit-frame-pointer -Wall -Wextra -Wno-missing-field-initializers
CC += $(FLAGS_$(BUILD))
FLAGS_debug := -g -DDEBUG
FLAGS_fast := -g -DDEBUG -O
FLAGS_profile := -g -O -finstrument-functions

SRCS = $(SRCS_$(BUILD)) $(SRCS_$(TARGET))
SRCS_profile += profile
SRCS_browser += png inflate jpeg ico
SRCS_desktop += png inflate jpeg ico
SRCS_taskbar += png inflate
SRCS_player += png inflate
SRCS_music += png inflate

ICONS = $(ICONS_$(TARGET))
ICONS_browser := cursor
ICONS_taskbar := cursor button
ICONS_desktop := cursor shutdown
ICONS_player := cursor play pause next
ICONS_music := cursor music
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
