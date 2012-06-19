PREFIX ?= /usr
TARGET ?= taskbar
BUILD ?= release

CCX = clang++
#CCX = g++ -fno-implicit-templates
#-mapcs

FLAGS ?= -pipe -std=c++11 -Wall -Wextra -Wno-narrowing -Wno-missing-field-initializers -Wno-pmf-conversions -fno-rtti -fno-exceptions
FLAGS += $(FLAGS__$(BUILD))
FLAGS__debug := -g -DDEBUG -fno-omit-frame-pointer
FLAGS__release := -O3 -ffast-math
FLAGS__profile := -g -mapcs -O -finstrument-functions -finstrument-functions-exclude-file-list=core,array,map,profile
FLAGS_font = -I/usr/include/freetype2

FLAGS += -march=armv7-a -mtune=cortex-a8 -mfpu=neon

SRCS = $(SRCS__$(BUILD)) $(SRCS_$(TARGET))
SRCS__profile += profile
SRCS__memory += memory
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
		$(CCX) $(FLAGS) $(FLAGS_$*) -MM -MT $(BUILD)/$*.o -MT $(BUILD)/$*.d $< > $@

ifneq ($(MAKECMDGOALS),clean)
-include $(BUILD)/$(TARGET).l
endif

$(BUILD)/%.o : %.cc
		$(CCX) $(FLAGS) $(FLAGS_$*) -c -o $@ $<

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
